#include "EnginePCH.h"
#include "PhysicsAssetUtils.h"

#include <cfloat>
#include <algorithm>

#include "PhysicsAsset.h"
#include "BodySetup.h"
#include "Render/SkeletalMesh.h"
#include "Math/EngineMath.h"

namespace
{
    // 정점 -> 본 배정
    void CalcBoneVertInfos(const USkeletalMesh& Mesh, EPhysAssetFitVertWeight Mode,
        TArray<TArray<FVector>>& OutVerts)
    {
        const FSkeletalMeshLODRenderData& LOD = Mesh.GetRenderData().LODRenderData[0];
        OutVerts.SetNum(Mesh.GetNumBones());

        for (int32 V = 0; V < LOD.BindVertices.Num(); ++V)
        {
            const FVector& Pos = LOD.BindVertices[V].Position;
            const FSkinWeight& W = LOD.SkinWeights[V];
                
            if (Mode == EPhysAssetFitVertWeight::DominantWeight)
            {
                int32 BestBone = -1;
                float BestWeight = 0.0f;

                for (int32 K = 0; K < MaxBoneInfluences; ++K)
                {
                    if (W.Weight[K] > BestWeight)
                    {
                        BestWeight = W.Weight[K];
                        BestBone = W.BoneIndex[K];
                    }
                }

                if (BestBone >= 0)
                {
                    OutVerts[BestBone].Add(Pos);
                }
                else
                {
                    for (int32 K = 0; K < MaxBoneInfluences; ++K)
                    {
                        if (W.Weight[K] > 0.0f)
                        {
                            OutVerts[W.BoneIndex[K]].Add(Pos);
                        }
                    }
                }
            }
        }
    }

    // 컴포넌트 공간 정점들을 뼈 공간으로 변환
    void ToBoneLocal(const FBoneInfo& Bone, const TArray<FVector>& CompVerts, TArray<FVector>& OutLocal)
    {
        OutLocal.Empty();
        OutLocal.Reserve(CompVerts.Num());
        for (const FVector& P : CompVerts)
        {
            OutLocal.Add(Bone.InverseBindPose.TransformPosition(P));
        }
    }

    // 정점들의 AABB 계산
    bool CalcBox(const TArray<FVector>& Verts, FVector& OutMin, FVector& OutMax)
    {
        if (Verts.IsEmpty())
        {
            return false;
        }

        OutMin = FVector(FLT_MAX, FLT_MAX, FLT_MAX);
        OutMax = FVector(-FLT_MAX, -FLT_MAX, -FLT_MAX);

        for (const FVector& P : Verts)
        {
            for (int32 Axis = 0; Axis < 3; ++Axis)
            {
                OutMin[Axis] = FMath::Min(OutMin[Axis], P[Axis]);
                OutMax[Axis] = FMath::Max(OutMax[Axis], P[Axis]);
            }
        }
        return true;
    }

    // 본 크기 = 본 로컬 aabb의 가장 긴 변
    float CalcBoneSize(const FBoneInfo& Bone, const TArray<FVector>& CompVerts)
    {
        TArray<FVector> Local;
        ToBoneLocal(Bone, CompVerts, Local);
        FVector Min, Max;

        if (!CalcBox(Local, Min, Max))
        {
            return 0.0f;
        }

        const FVector Size = Max - Min;
        return FMath::Max(FMath::Max(Size.X, Size.Y), Size.Z);
    }


    bool FitSphyl(const TArray<FVector>& LocalVerts, FKSphylElem& Out)
    {
        FVector Min, Max;
        if (!CalcBox(LocalVerts, Min, Max))
        {
            return false;
        }

        const FVector Center = (Min + Max) * 0.5f;
        const FVector Half = (Max - Min) * 0.5f;

        // 가장 긴 축 ==> 캡슐이 뻗는 방향임.
        int32 LongAxis = 0;
        if (Half[1] > Half[LongAxis])
        {
            LongAxis = 1;
        }
        
        if (Half[2] > Half[LongAxis])
        {
            LongAxis = 2;
        }

        const int32 A = (LongAxis + 1) % 3;
        const int32 B = (LongAxis + 2) % 3;

        const float Radius = FMath::Max(Half[A], Half[B]);
        if (Radius <= KINDA_SMALL_NUMBER)
        {
            return false;
        }

        Out.Radius = Radius;
        Out.Length = FMath::Max(0.0f, 2.0f * (Half[LongAxis] - Radius));
        Out.Center = Center;

        switch (LongAxis)
        {
        case 0:
        {
            Out.Rotation = FQuat::MakeFromAxisAngle(FVector(0, 1, 0), PI * 0.5f);
            break;
        }
        case 1:
        {
            Out.Rotation = FQuat::MakeFromAxisAngle(FVector(1, 0, 0), PI * 0.5f);
            break;
        }
        default:
        {
            Out.Rotation = FQuat::Identity();
            break;
        }
        }

        return true;
    }
}

bool FPhysicsAssetUtils::CreateFromSkeletalMesh(UPhysicsAsset& Asset, const USkeletalMesh& Mesh, const FPhysAssetCreateParams& Params, FString& OutError)
{
    OutError.clear();
    const TArray<FBoneInfo>& Bones = Mesh.GetRefSkeleton();
    if (Bones.IsEmpty() || Mesh.GetRenderData().LODRenderData.IsEmpty())
    {
        OutError = "PhysicsAsset: Mesh has no skeleton or render data";
        return false;
    }

    Asset.ClearAllBodies();

    // 정점 배정
    TArray<TArray<FVector>> BoneVerts;
    CalcBoneVertInfos(Mesh, Params.VertWeight, BoneVerts);

    // 작은 본 병합
    const FBox& MeshBox = Mesh.GetRenderData().LODRenderData[0].BindAABB;
    const FVector MeshSize = MeshBox.Max - MeshBox.Min;
    const float MinBoneSize = FMath::Max(FMath::Max(MeshSize.X, MeshSize.Y), MeshSize.Z) * Params.MinBoneSizeRatio;

    // 자식이 부모보다 먼저 처리되게 함.
    for (int32 B = Bones.Num() - 1; B > 0; --B)
    {
        if (BoneVerts[B].IsEmpty())
        {
            continue;
        }

        const int32 Parent = Bones[B].ParentIndex;
        if (Parent < 0)
        {
            continue;
        }

        if (CalcBoneSize(Bones[B], BoneVerts[B]) < MinBoneSize)
        {
            BoneVerts[Parent].Append(BoneVerts[B]);
            BoneVerts[B].Empty();
        }
    }

    TArray<FVector> Local;
    for (int32 B = 0; B < Bones.Num(); ++B)
    {
        if (BoneVerts[B].Num() < Params.MinVertsPreBody)
        {
            continue;
        }

        ToBoneLocal(Bones[B], BoneVerts[B], Local);

        FKSphylElem Sphyl;
        if (!FitSphyl(Local, Sphyl))
        {
            continue;
        }

        UBodySetup* Setup = Asset.AddBodySetup(FName(Bones[B].Name));
        Setup->AggGeom = FKAggregateGeom();
        Setup->AggGeom.SphylElems.Add(Sphyl);
    }

    if (Asset.BodySetup.IsEmpty())
    {
        OutError = "PhysicsAsset : no bone had enough vertices to make a body";
        return false;
    }

    KRS_LOG(Info, "[Physics] PhysicsAsset generated: {} bodies from {} bones", Asset.BodySetup.Num(), Bones.Num());
    return true;
}

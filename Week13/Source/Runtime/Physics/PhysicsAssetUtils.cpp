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

    FTransform ComposeTransform(const FTransform& Local, const FTransform& Parent)
    {
        FTransform Out;
        Out.Rotation = (Parent.Rotation * Local.Rotation).Normalized();
        Out.Scale = FVector(Local.Scale.X * Parent.Scale.X, Local.Scale.Y * Parent.Scale.Y, Local.Scale.Z * Parent.Scale.Z);
        const FVector Scaled(Local.Location.X * Parent.Scale.X, Local.Location.Y * Parent.Scale.Y, Local.Location.Z * Parent.Scale.Z);
        Out.Location = Parent.Location + Parent.Rotation.RotateVector(Scaled);
        return Out;
    }

    // 바인드 포즈의 본별 컴포넌트 공간 트랜스폼. 부모가 항상 앞 인덱스라 앞에서부터 한 번에 된다.
    void CalcRefPoseComponentSpace(const TArray<FBoneInfo>& Bones, TArray<FTransform>& Out)
    {
        Out.SetNum(Bones.Num());
        for (int32 B = 0; B < Bones.Num(); ++B)
        {
            const int32 P = Bones[B].ParentIndex;
            Out[B] = (P >= 0) ? ComposeTransform(Bones[B].LocalBindPose, Out[P]) : Bones[B].LocalBindPose;
        }
    }

    // B의 조상 중 바디가 있는 가장 가까운 본. 없으면 -1.
    // 루트·IK 본·병합된 본은 바디가 없으므로 건너뛴다 (작업 3에서 생긴 구멍을 여기서 메운다)
    int32 FindParentBoneWithBody(const UPhysicsAsset& Asset, const TArray<FBoneInfo>& Bones, int32 B)
    {
        for (int32 P = Bones[B].ParentIndex; P >= 0; P = Bones[P].ParentIndex)
        {
            if (Asset.FindBodyIndex(FName(Bones[P].Name)) >= 0)
                return P;
        }
        return -1;
    }

    void CreateConstraints(UPhysicsAsset& Asset, const TArray<FBoneInfo>& Bones)
    {
        TArray<FTransform> RefComp;
        CalcRefPoseComponentSpace(Bones, RefComp);

        for (UBodySetup* ChildSetup : Asset.BodySetup)
        {
            const int32 Child = -1 + 1 + [&] {   // 이름 → 본 인덱스
                for (int32 i = 0; i < Bones.Num(); ++i)
                    if (FName(Bones[i].Name) == ChildSetup->BoneName) return i;
                return -1; }();
                if (Child < 0)
                    continue;

                const int32 Parent = FindParentBoneWithBody(Asset, Bones, Child);
                if (Parent < 0)
                    continue;   // 최상위 바디(보통 pelvis)는 조인트가 없다

                UPhysicsConstraintTemplate* Template = Asset.AddConstraint(ChildSetup->BoneName, FName(Bones[Parent].Name));
                FConstraintInstance& CI = Template->DefaultInstance;

                // ── Frame1: 자식 로컬. 위치는 자식 본 원점, X(Twist)축은 캡슐의 긴 축 ──
                // FKSphylElem은 로컬 Z로 서므로 Rotation×Z가 긴 축, Rotation×X는 그에 수직인 축
                FQuat CapsuleRot = FQuat::Identity();
                if (!ChildSetup->AggGeom.SphylElems.IsEmpty())
                    CapsuleRot = ChildSetup->AggGeom.SphylElems[0].Rotation;

                CI.Pos1 = FVector(0, 0, 0);
                CI.PriAxis1 = CapsuleRot.RotateVector(FVector(0, 0, 1));
                CI.SecAxis1 = CapsuleRot.RotateVector(FVector(1, 0, 0));

                // ── Frame2: 같은 점·같은 축을 부모 본 공간에서 본 값 ──
                // 바인드 포즈에서 Frame1과 정확히 겹친다 → 시뮬 시작 때 튀지 않는다
                const FTransform ChildInParent = RefComp[Child].GetRelativeTransform(RefComp[Parent]);
                CI.Pos2 = ChildInParent.Location;   // Pos1이 원점이라 회전 항이 사라진다
                CI.PriAxis2 = ChildInParent.Rotation.RotateVector(CI.PriAxis1);
                CI.SecAxis2 = ChildInParent.Rotation.RotateVector(CI.SecAxis1);
        }
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
    Asset.ClearAllConstraints();

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

    CreateConstraints(Asset, Bones);

    KRS_LOG(Info, "[Physics] PhysicsAsset generated: {} bodies from {} bones", Asset.BodySetup.Num(), Bones.Num());
    return true;
}

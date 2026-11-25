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

    bool HasBody(const UPhysicsAsset& Asset, const TArray<FBoneInfo>& Bones, int32 Bone)
    {
        return Asset.FindBodyIndex(FName(Bones[Bone].Name)) >= 0;
    }

    // A가 B 자신이거나 B의 조상인가
    bool IsSelfOrAncestor(const TArray<FBoneInfo>& Bones, int32 A, int32 B)
    {
        for (int32 P = B; P >= 0; P = Bones[P].ParentIndex)
        {
            if (P == A)
                return true;
        }
        return false;
    }

    float SegmentSegmentDistSq(const FVector& P1, const FVector& Q1, const FVector& P2, const FVector& Q2);
    bool GetCapsuleSegment(const UBodySetup& Setup, const FTransform& BoneComp, FVector& OutA, FVector& OutB, float& OutRadius);

    // 바인드 포즈에서 점 P와 본 Bone 바디의 캡슐 표면 사이 거리 (안쪽이면 음수). 캡슐이 없으면 FLT_MAX
    float DistanceToBody(const UPhysicsAsset& Asset, const USkeletalMesh& Mesh, const TArray<FTransform>& RefComp, int32 Bone, const FVector& P)
    {
        const int32 BodyIndex = Asset.FindBodyIndex(FName(Mesh.GetRefSkeleton()[Bone].Name));
        FVector A, B;
        float Radius = 0.0f;
        if (BodyIndex < 0 || !GetCapsuleSegment(*Asset.BodySetup[BodyIndex], RefComp[Bone], A, B, Radius))
            return FLT_MAX;
        return std::sqrt(SegmentSegmentDistSq(P, P, A, B)) - Radius * RefComp[Bone].Scale.X;
    }

    // 자식 바디 Child를 조인트로 묶을 부모 본. 없으면 -1 (최상위 바디).
    // 기본은 바디가 있는 가장 가까운 조상이다. 루트·IK 본·병합된 본은 바디가 없으므로 건너뛴다.
    // 단, 스킨이 보조 본(예: Thigh 아래 원점이 같은 ThighAim, Calf 아래 Knee)에 붙어 주 사슬 본에 바디가 없는 리그에서는
    // 조상을 그냥 건너뛰면 무릎·발목·발이 모두 골반에 직접 묶인 별 모양이 된다.
    // 그래서 바디 없는 조상 P와 원점이 같은 보조 본(P의 자식, 또는 "_EX"처럼 P와 나란한 형제)에 바디가 있으면
    // 그 바디를 P 대신 쓴다 (보조 본 = P의 대리).
    int32 FindHierarchyParentBody(const UPhysicsAsset& Asset, const TArray<FBoneInfo>& Bones, const TArray<FTransform>& RefComp,
        int32 Child, float ProxyTolerance)
    {
        for (int32 P = Bones[Child].ParentIndex; P >= 0; P = Bones[P].ParentIndex)
        {
            if (HasBody(Asset, Bones, P))
                return P;

            const int32 GrandParent = Bones[P].ParentIndex;
            for (int32 C = 0; C < Bones.Num(); ++C)
            {
                const int32 CParent = Bones[C].ParentIndex;
                const bool bChildOfP = CParent == P;
                const bool bTwinOfP = C != P && GrandParent >= 0 && CParent == GrandParent;
                if ((!bChildOfP && !bTwinOfP) || !HasBody(Asset, Bones, C) || IsSelfOrAncestor(Bones, C, Child))
                    continue;
                // 원점이 같은 형제 둘이 서로를 대리로 잡아 고리가 생기지 않게, 그 경우만 앞 인덱스를 허용한다
                const bool bChildAtP = (RefComp[Child].Location - RefComp[P].Location).Size() <= ProxyTolerance;
                if (P == Bones[Child].ParentIndex && bChildAtP && C > Child)
                    continue;
                if ((RefComp[C].Location - RefComp[P].Location).Size() <= ProxyTolerance)
                    return C;
            }
        }
        return -1;
    }

    // 조인트 부모를 정한다. 계층상 부모 바디가 조인트 위치(자식 본 원점)에서 멀리 떨어져 있으면
    // (예: 루트 아래에 붙은 모자 본 → 골반 근처 바디) 긴 레버로 위치가 고정되어 떨리므로,
    // 조인트 위치를 품고 있는 가장 가까운 바디로 바꾼다. 자식 자신과 그 후손은 후보에서 뺀다
    TArray<int32> ChooseConstraintParents(const UPhysicsAsset& Asset, const USkeletalMesh& Mesh, const TArray<FTransform>& RefComp,
        float ProxyTolerance)
    {
        const TArray<FBoneInfo>& Bones = Mesh.GetRefSkeleton();
        TArray<int32> ParentOf;
        ParentOf.Init(-1, Bones.Num());

        for (UBodySetup* Setup : Asset.BodySetup)
        {
            const int32 Child = Mesh.FindBoneIndex(Setup->BoneName.ToString());
            if (Child < 0)
                continue;
            int32 Parent = FindHierarchyParentBody(Asset, Bones, RefComp, Child, ProxyTolerance);
            if (Parent < 0)
                continue;

            const FVector Anchor = RefComp[Child].Location;
            const float ParentDist = DistanceToBody(Asset, Mesh, RefComp, Parent, Anchor);
            float ParentRadius = 0.0f;
            {
                const UBodySetup* ParentSetup = Asset.BodySetup[Asset.FindBodyIndex(FName(Bones[Parent].Name))];
                if (!ParentSetup->AggGeom.SphylElems.IsEmpty())
                    ParentRadius = ParentSetup->AggGeom.SphylElems[0].Radius * RefComp[Parent].Scale.X;
            }
            if (ParentDist != FLT_MAX && ParentDist > std::max(2.0f * ParentRadius, ProxyTolerance * 10.0f))
            {
                int32 Best = -1;
                float BestDist = FLT_MAX;
                for (UBodySetup* Other : Asset.BodySetup)
                {
                    const int32 Candidate = Mesh.FindBoneIndex(Other->BoneName.ToString());
                    if (Candidate < 0 || IsSelfOrAncestor(Bones, Child, Candidate))
                        continue;
                    const float Dist = DistanceToBody(Asset, Mesh, RefComp, Candidate, Anchor);
                    if (Dist < BestDist)
                    {
                        BestDist = Dist;
                        Best = Candidate;
                    }
                }
                if (Best >= 0 && BestDist <= ParentDist * 0.25f)
                    Parent = Best;
            }
            ParentOf[Child] = Parent;
        }

        // 공간 기준으로 바꾼 부모 때문에 고리가 생겼으면 그 바디는 계층상 부모로 되돌린다
        for (int32 Child = 0; Child < Bones.Num(); ++Child)
        {
            if (ParentOf[Child] < 0)
                continue;
            int32 Steps = 0;
            for (int32 P = ParentOf[Child]; P >= 0 && Steps <= Bones.Num(); P = ParentOf[P], ++Steps)
            {
                if (P == Child)
                {
                    ParentOf[Child] = FindHierarchyParentBody(Asset, Bones, RefComp, Child, ProxyTolerance);
                    break;
                }
            }
        }
        return ParentOf;
    }

    void CreateConstraints(UPhysicsAsset& Asset, const USkeletalMesh& Mesh, const TArray<FTransform>& RefComp, float ProxyTolerance)
    {
        const TArray<FBoneInfo>& Bones = Mesh.GetRefSkeleton();
        const TArray<int32> ParentOf = ChooseConstraintParents(Asset, Mesh, RefComp, ProxyTolerance);
        for (UBodySetup* ChildSetup : Asset.BodySetup)
        {
            const int32 Child = Mesh.FindBoneIndex(ChildSetup->BoneName.ToString());
            if (Child < 0)
                continue;

                const int32 Parent = ParentOf[Child];
                if (Parent < 0)
                    continue;   // 최상위 바디(보통 pelvis)는 조인트가 없다

                UPhysicsConstraintTemplate* Template = Asset.AddConstraint(ChildSetup->BoneName, FName(Bones[Parent].Name));
                FConstraintInstance& CI = Template->DefaultInstance;

                // Frame1: 자식 로컬. 위치는 자식 본 원점, X(Twist)축은 캡슐의 긴 축 ──
                // FKSphylElem은 로컬 Z로 서므로 Rotation×Z가 긴 축, Rotation×X는 그에 수직인 축
                FQuat CapsuleRot = FQuat::Identity();
                if (!ChildSetup->AggGeom.SphylElems.IsEmpty())
                    CapsuleRot = ChildSetup->AggGeom.SphylElems[0].Rotation;

                CI.Pos1 = FVector(0, 0, 0);
                CI.PriAxis1 = CapsuleRot.RotateVector(FVector(0, 0, 1));
                CI.SecAxis1 = CapsuleRot.RotateVector(FVector(1, 0, 0));

                // Frame2: 같은 점·같은 축을 부모 본 공간에서 본 값 ──
                // 바인드 포즈에서 Frame1과 정확히 겹친다 → 시뮬 시작 때 튀지 않는다
                const FTransform ChildInParent = RefComp[Child].GetRelativeTransform(RefComp[Parent]);
                CI.Pos2 = ChildInParent.Location;   // Pos1이 원점이라 회전 항이 사라진다
                CI.PriAxis2 = ChildInParent.Rotation.RotateVector(CI.PriAxis1);
                CI.SecAxis2 = ChildInParent.Rotation.RotateVector(CI.SecAxis1);
        }
    }

    FVector TransformPoint(const FTransform& T, const FVector& P)
    {
        const FVector Scaled(P.X * T.Scale.X, P.Y * T.Scale.Y, P.Z * T.Scale.Z);
        return T.Location + T.Rotation.RotateVector(Scaled);
    }

    float SegmentSegmentDistSq(const FVector& P1, const FVector& Q1, const FVector& P2, const FVector& Q2)
    {
        constexpr float Eps = 1e-8f;
        const FVector D1 = Q1 - P1;
        const FVector D2 = Q2 - P2;
        const FVector R = P1 - P2;
        const float A = D1.Dot(D1);
        const float E = D2.Dot(D2);
        const float F = D2.Dot(R);

        float S = 0.0f, T = 0.0f;
        if (A <= Eps && E <= Eps)
            return R.Dot(R);                                   // 둘 다 점
        if (A <= Eps)
        {
            T = std::clamp(F / E, 0.0f, 1.0f);                 // 첫 번째가 점
        }
        else
        {
            const float C = D1.Dot(R);
            if (E <= Eps)
            {
                S = std::clamp(-C / A, 0.0f, 1.0f);            // 두 번째가 점
            }
            else
            {
                const float B = D1.Dot(D2);
                const float Denom = A * E - B * B;             // 0이면 평행
                S = (Denom > Eps) ? std::clamp((B * F - C * E) / Denom, 0.0f, 1.0f) : 0.0f;
                T = (B * S + F) / E;
                // T가 선분 밖이면 끝점으로 자르고 S를 다시 구한다
                if (T < 0.0f) { T = 0.0f; S = std::clamp(-C / A, 0.0f, 1.0f); }
                else if (T > 1.0f) { T = 1.0f; S = std::clamp((B - C) / A, 0.0f, 1.0f); }
            }
        }
        const FVector C1 = P1 + D1 * S;
        const FVector C2 = P2 + D2 * T;
        return (C1 - C2).SizeSquared();
    }

    // 바디의 첫 캡슐을 컴포넌트 공간 선분으로. 캡슐이 없으면 false
    bool GetCapsuleSegment(const UBodySetup& Setup, const FTransform& BoneComp, FVector& OutA, FVector& OutB, float& OutRadius)
    {
        if (Setup.AggGeom.SphylElems.IsEmpty())
            return false;
        const FKSphylElem& S = Setup.AggGeom.SphylElems[0];
        const FVector HalfAxis = S.Rotation.RotateVector(FVector(0, 0, 1)) * (S.Length * 0.5f);
        OutA = TransformPoint(BoneComp, S.Center + HalfAxis);
        OutB = TransformPoint(BoneComp, S.Center - HalfAxis);
        OutRadius = S.Radius;   // 바인드 포즈 스케일은 1로 본다
        return true;
    }

    void DisableAdjacentAndOverlappingCollisions(UPhysicsAsset& Asset, const USkeletalMesh& Mesh, const TArray<FTransform>& RefComp)
    {
        // 인접: 조인트로 이어진 쌍. PhysX 조인트도 기본으로 꺼 주지만,
        //    테이블에도 넣어 두면 조인트 설정과 무관하게 의도가 에셋에 남는다
        for (UPhysicsConstraintTemplate* Template : Asset.ConstraintSetup)
            Asset.DisableCollision(Template->DefaultInstance.ConstraintBone1, Template->DefaultInstance.ConstraintBone2);

        // 조인트 두 단계 안의 이웃 (같은 부모에 붙은 형제, 조부모-손자). 이 쌍이 바인드 포즈에서 거의 닿아 있으면
        // (예: 같은 발에 붙은 큰 발목 캡슐과 발끝) 관절이 조금만 꺾여도 서로 밀어내고, 둘 다 조인트로 묶여 있어 비켜날 곳이 없어 떤다
        auto ParentOf = [&Asset](const FName& Bone)
        {
            for (const UPhysicsConstraintTemplate* Template : Asset.ConstraintSetup)
            {
                if (Template->DefaultInstance.ConstraintBone1 == Bone)
                    return Template->DefaultInstance.ConstraintBone2;
            }
            return FName();
        };
        auto AreNear = [&](const FName& A, const FName& B)
        {
            const FName PA = ParentOf(A), PB = ParentOf(B);
            return (!PA.IsNone() && PA == PB)
                || (!PA.IsNone() && ParentOf(PA) == B)
                || (!PB.IsNone() && ParentOf(PB) == A);
        };

        // 바인드 포즈에서 이미 겹치는 쌍 (양쪽 허벅지, 쇄골-가슴 등). 그대로 두면 첫 스텝에 서로 밀어낸다
        const int32 N = Asset.BodySetup.Num();
        for (int32 I = 0; I < N; ++I)
        {
            const UBodySetup* A = Asset.BodySetup[I];
            const int32 BoneA = Mesh.FindBoneIndex(A->BoneName.ToString());
            FVector A0, A1; float RA;
            if (BoneA < 0 || !GetCapsuleSegment(*A, RefComp[BoneA], A0, A1, RA))
                continue;

            for (int32 J = I + 1; J < N; ++J)   // 쌍마다 한 번
            {
                const UBodySetup* B = Asset.BodySetup[J];
                const int32 BoneB = Mesh.FindBoneIndex(B->BoneName.ToString());
                FVector B0, B1; float RB;
                if (BoneB < 0 || !GetCapsuleSegment(*B, RefComp[BoneB], B0, B1, RB))
                    continue;

                // 이웃이면 작은 쪽 반지름만큼 떨어져 있어도 닿은 것으로 본다
                const float Margin = AreNear(A->BoneName, B->BoneName) ? FMath::Min(RA, RB) : 0.0f;
                const float RSum = RA + RB + Margin;
                if (SegmentSegmentDistSq(A0, A1, B0, B1) < RSum * RSum)   // sqrt 없이 제곱끼리 비교
                    Asset.DisableCollision(A->BoneName, B->BoneName);
            }
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

    // 본마다 캡슐을 맞춘다. 맞출 수 없으면 Valid=false
    struct FFit { FKSphylElem Sphyl; bool bValid = false; };
    auto FitAll = [&]()
    {
        TArray<FFit> Fits;
        Fits.SetNum(Bones.Num());
        TArray<FVector> Local;
        for (int32 B = 0; B < Bones.Num(); ++B)
        {
            if (BoneVerts[B].Num() < Params.MinVertsPreBody)
                continue;
            ToBoneLocal(Bones[B], BoneVerts[B], Local);
            Fits[B].bValid = FitSphyl(Local, Fits[B].Sphyl);
        }
        return Fits;
    };
    TArray<FFit> Fits = FitAll();

    // 부피가 너무 작은 바디는 부모 본에 합친다. 길이로 거르는 위 단계는 가늘고 긴 손가락·보조 본을 남기는데,
    // 이런 바디는 질량이 몸통의 1/100도 안 되어 조인트 솔버가 떨린다. 자식부터 처리해 합친 결과가 위로 이어진다
    if (Params.MinBodyVolumeRatio > 0.0f)
    {
        auto Volume = [](const FKSphylElem& S) { return PI * S.Radius * S.Radius * (S.Length + 4.0f / 3.0f * S.Radius); };
        TArray<float> Volumes;
        for (const FFit& Fit : Fits)
        {
            if (Fit.bValid)
                Volumes.Add(Volume(Fit.Sphyl));
        }
        if (Volumes.Num() >= 3)
        {
            std::sort(Volumes.begin(), Volumes.end());
            const float MinVolume = Volumes[Volumes.Num() / 2] * Params.MinBodyVolumeRatio;
            bool bMerged = false;
            for (int32 B = Bones.Num() - 1; B > 0; --B)
            {
                const int32 Parent = Bones[B].ParentIndex;
                if (Parent < 0 || !Fits[B].bValid || Volume(Fits[B].Sphyl) >= MinVolume)
                    continue;
                BoneVerts[Parent].Append(BoneVerts[B]);
                BoneVerts[B].Empty();
                bMerged = true;
            }
            if (bMerged)
                Fits = FitAll();
        }
    }

    for (int32 B = 0; B < Bones.Num(); ++B)
    {
        if (!Fits[B].bValid)
        {
            continue;
        }
        const FKSphylElem& Sphyl = Fits[B].Sphyl;

        UBodySetup* Setup = Asset.AddBodySetup(FName(Bones[B].Name));
        Setup->AggGeom = FKAggregateGeom();
        Setup->AggGeom.SphylElems.Add(Sphyl);
    }

    if (Asset.BodySetup.IsEmpty())
    {
        OutError = "PhysicsAsset : no bone had enough vertices to make a body";
        return false;
    }

    TArray<FTransform> RefComp;
    CalcRefPoseComponentSpace(Bones, RefComp);

    // 원점이 "같다"고 볼 거리. 보조 본은 보통 정확히 0이지만 FBX 변환 오차를 허용한다
    const float ProxyTolerance = FMath::Max(MinBoneSize * 0.1f, KINDA_SMALL_NUMBER);
    CreateConstraints(Asset, Mesh, RefComp, ProxyTolerance);
    DisableAdjacentAndOverlappingCollisions(Asset, Mesh, RefComp);
    KRS_LOG(Info, "[Physics] PhysicsAsset generated: {} bodies from {} bones", Asset.BodySetup.Num(), Bones.Num());
    return true;
}

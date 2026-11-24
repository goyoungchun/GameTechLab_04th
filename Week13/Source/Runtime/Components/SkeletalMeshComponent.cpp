#include "EnginePCH.h"
#include "SkeletalMeshComponent.h"
#include "Serialization/MathSerialization.h"
#include "Animation/BonePose.h"
#include "Animation/AnimNodeBase.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/AnimGraphInstance.h"
#include "Animation/AnimBlueprintCompiler.h"
#include "Animation/AnimBlueprintFile.h"
#include "Asset/AssetUtils.h"
#include "Core/Misc/Paths.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Casts.h"
#include "UObject/UObjectIterator.h"
#include "Render/LineBatcher.h"
#include "GameFramework/Actor.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace1D.h"
#include "Animation/BlendSpaceFile.h"
#include "Asset/AssetManager.h"
#include "Clothing/ClothingAsset.h"
#include "Clothing/ClothingSimulationNv.h"
#include "Render/SkeletalMeshObject.h"
#include "Physics/PhysicsAsset.h"
#include "Physics/BodyInstance.h"
#include "Physics/ConstraintInstance.h"
#include "Physics/PhysScene.h"
#include "Physics/PhysXConversions.h"
#include "Physics/PhysXIncludes.h"
#include "Engine/World.h"

namespace
{
	FTransform MatrixToTransform(const FMatrix& M)
	{
		FVector X(M[0][0], M[0][1], M[0][2]);
		FVector Y(M[1][0], M[1][1], M[1][2]);
		FVector Z(M[2][0], M[2][1], M[2][2]);
		const FVector Scale(X.Size(), Y.Size(), Z.Size());
		X = X.GetSafeNormal(); Y = Y.GetSafeNormal(); Z = Z.GetSafeNormal();
		const physx::PxQuat Q(physx::PxMat33(K2PVector(X), K2PVector(Y), K2PVector(Z)));
		return FTransform(P2KQuat(Q.getNormalized()), FVector(M[3][0], M[3][1], M[3][2]), Scale);
	}
}

void USkeletalMeshComponent::SetAnimInstance(UAnimInstance* InInstance)
{
	if (AnimInstance == InInstance)
	{
		return;
	}
	if (AnimInstance)
	{
		AnimInstance->UninitializeAnimation();
	}
    AnimInstance = InInstance;

    if (ManagedAnimGraph && ManagedAnimGraph.get() != InInstance)
    {
        ManagedAnimGraph.reset();
    }

    AnimBlueprintPath.clear();
    bBlueprintReloadRequested = false;

	if (AnimInstance)
	{
		AnimInstance->InitializeAnimation();
	}
}

void USkeletalMeshComponent::BeginPlay()
{
	Super::BeginPlay();

    if (!AnimBlueprintPath.empty() && (bBlueprintReloadRequested || !ManagedAnimGraph))
    {
        ReloadAnimBlueprint();
    }

	if (AnimInstance)
	{
		AnimInstance->InitializeAnimation();

		if (auto* Instance = GetSingleNodeInstance())
		{
			if (Instance->GetCurrentAsset())
			{
				Instance->SetPlaying(true);
			}
		}
	}

	CreateClothingSimulation();
}

void USkeletalMeshComponent::TickComponent(float DeltaTime)
{
    if (bBlueprintReloadRequested)
    {
        bBlueprintReloadRequested = false;
        ReloadAnimBlueprint();
    }

	if (AnimInstance && GetSkeletalMesh())
	{
        bUpdatingAnimation = true;
		if (auto* Instance = Cast<UAnimSingleNodeInstance>(AnimInstance))
		{
			Instance->SetBlendSpaceInput(BlendSpaceInput);
		}

		AnimInstance->UpdateAnimation(DeltaTime);
		FPoseContext Pose(AnimInstance, GetBones());
		AnimInstance->EvaluateAnimation(Pose);
		SetBoneSpaceTransforms(Pose.Pose);
		AnimInstance->TriggerAnimNotifies(DeltaTime);

		// 루트 모션: AnimInstance에 모인 delta를 월드 공간으로 바꿔 CharacterMovement가 가져갈 때까지 쌓아 둔다
		const FRootMotionMovementParams RootMotion = AnimInstance->ConsumeExtractedRootMotion();
		if (RootMotion.bHasRootMotion)
		{
			const FTransform WorldDelta = ConvertRootMotionToWorld(RootMotion.GetRootMotionTransform());
			PendingRootMotion.Location = (bHasPendingRootMotion ? PendingRootMotion.Location : FVector::ZeroVector) + WorldDelta.Location;
			PendingRootMotion.Rotation = bHasPendingRootMotion ? (WorldDelta.Rotation * PendingRootMotion.Rotation).Normalized() : WorldDelta.Rotation;
			bHasPendingRootMotion = true;
		}
        bUpdatingAnimation = false;
	}
	Super::TickComponent(DeltaTime);
	UpdateClothingSimulation(DeltaTime);
}

const TArray<FBoneInfo>& USkeletalMeshComponent::GetBones() const
{
	static const TArray<FBoneInfo> Empty{};
	return GetSkeletalMesh() ? GetSkeletalMesh()->GetRefSkeleton() : Empty;
}

int32 USkeletalMeshComponent::FindBoneIndex(const FString& Name) const
{
	return GetSkeletalMesh() ? GetSkeletalMesh()->FindBoneIndex(Name) : -1;
}

FTransform USkeletalMeshComponent::GetBoneLocalTransform(int32 BoneIndex) const
{
	if (BoneIndex < 0 || BoneIndex >= BoneSpaceTransforms.Num()) return FTransform();
	return BoneSpaceTransforms[BoneIndex];
}

void USkeletalMeshComponent::SetBoneLocalTransform(int32 BoneIndex, const FTransform& Local)
{
	if (BoneIndex < 0 || BoneIndex >= BoneSpaceTransforms.Num()) return;
	BoneSpaceTransforms[BoneIndex] = Local;
	MarkBoneTransformsDirty();
}

void USkeletalMeshComponent::SetBoneSpaceTransforms(const FCompactPose& Pose)
{
	if (Pose.GetNumBones() != GetBones().Num())
	{
		KRS_LOG(Warning, "SetBoneSpaceTransforms: pose has {} bones, mesh has {}.", Pose.GetNumBones(), GetBones().Num());
		return;
	}
#if defined(_DEBUG)
	for (int32 i = 0; i < Pose.GetNumBones(); ++i)
	{
		const FQuat& Q = Pose[i].Rotation;
		const float LenSq = Q.X * Q.X + Q.Y * Q.Y + Q.Z * Q.Z + Q.W * Q.W;
		if (std::abs(LenSq - 1.0f) > 1e-3f)
			KRS_LOG(Warning, "Pose bone {} rotation not normalized (|q|^2 = {})", i, LenSq);
	}
#endif
	BoneSpaceTransforms = Pose.GetBones();
	MarkBoneTransformsDirty();
}

FMatrix USkeletalMeshComponent::GetBoneComponentMatrix(int32 BoneIndex) const
{
	if (BoneIndex < 0 || BoneIndex >= ComponentSpaceTransforms.Num()) return FMatrix::Identity;
	return ComponentSpaceTransforms[BoneIndex];
}

FMatrix USkeletalMeshComponent::GetBoneWorldMatrix(int32 BoneIndex) const
{
	return GetBoneComponentMatrix(BoneIndex) * GetWorldMatrix();
}

void USkeletalMeshComponent::ResetPose()
{
	OnSkeletalMeshChanged();
	MarkBoneTransformsDirty();
}

void USkeletalMeshComponent::RefreshBoneTransformsNow()
{
	UpdateBoneTransformsNow();
}

void USkeletalMeshComponent::RefreshBoneTransforms()
{
	const TArray<FBoneInfo>& Bones = GetBones();
	if (BoneSpaceTransforms.Num() != Bones.Num())
		OnSkeletalMeshChanged();

	ComponentSpaceTransforms.SetNum(Bones.Num());
	for (int i = 0; i < Bones.Num(); ++i)
	{
		const FMatrix Local = BoneSpaceTransforms[i].GetLocalMatrix();
		const int32 Parent = Bones[i].ParentIndex;
		ComponentSpaceTransforms[i] = Parent < 0 ? Local : Local * ComponentSpaceTransforms[Parent];
	}
}

void USkeletalMeshComponent::Serialize(FStructuredArchive::FRecord Record)
{
    FArchive& Ar = Record.GetUnderlyingArchive();
    UAnimInstance* SavedInstance = AnimInstance;

    if (Ar.IsSaving() && (!AnimBlueprintPath.empty() || ManagedAnimGraph))
    {
        AnimInstance = nullptr;
    }

    Super::Serialize(Record);

    if (Ar.IsSaving())
    {
        AnimInstance = SavedInstance;
    }

	int32 Num = Ar.IsSaving() ? BoneSpaceTransforms.Num() : 0;
	FStructuredArchive::FArray PoseArray = Record.EnterArray("BonePose", Num);

	for (int32 I = 0; I < Num; ++I)
	{
		FStructuredArchive::FRecord BoneRecord = PoseArray.EnterElement().EnterRecord();
		FString Name = Ar.IsSaving() ? GetBones()[I].Name : FString();
		FTransform Local = Ar.IsSaving() ? BoneSpaceTransforms[I] : FTransform();
		BoneRecord << SA_VALUE("Name", Name) << SA_VALUE("Local", Local);

		if (Ar.IsLoading())
		{
			// 메시의 본 순서가 바뀌어도 맞도록 이름으로 찾는다
			const int32 Index = FindBoneIndex(Name);
			if (Index >= 0)
				BoneSpaceTransforms[Index] = Local;
		}
	}

	SerializeSingleAnimation(Record);

    if (Ar.IsLoading())
    {
        if (!AnimBlueprintPath.empty())
        {
            ReloadAnimBlueprint();
        }

        RefreshBoneTransformsNow();
    }
}

bool USkeletalMeshComponent::CanBodiesCollide(const FBodyInstance& A, const FBodyInstance& B) const
{
	UPhysicsAsset* PhysAsset = GetPhysicsAsset();   // 바디가 있으면 이미 로드돼 있다 (지연 로드 안 일어남)
	if (!PhysAsset)
		return false;

	const int32 IA = A.InstanceBodyIndex;
	const int32 IB = B.InstanceBodyIndex;
	const int32 Num = PhysAsset->BodySetup.Num();
	if (IA < 0 || IB < 0 || IA >= Num || IB >= Num)
		return false;

	return PhysAsset->IsCollisionEnabled(PhysAsset->BodySetup[IA]->BoneName, PhysAsset->BodySetup[IB]->BoneName);
}

void USkeletalMeshComponent::SerializeSingleAnimation(FStructuredArchive::FRecord& Record)
{
	// 디테일 패널에서 고른 단일 애니메이션(SingleNodeInstance의 에셋)은 AnimInstance 객체가 아니라
	// 에셋 경로로 저장한다. 없으면 필드를 쓰지 않으므로 예전 씬도 그대로 읽힌다.
	FArchive& Ar = Record.GetUnderlyingArchive();
	UAnimSingleNodeInstance* Instance = GetSingleNodeInstance();
	UAnimationAsset* Asset = Instance ? Instance->GetCurrentAsset() : nullptr;
	const bool bHasAnimation = Ar.IsSaving() && Asset && !Asset->GetAssetPath().empty();

	TOptional<FStructuredArchive::FSlot> AnimSlot = Record.TryEnterField("SingleAnimation", bHasAnimation);
	if (!AnimSlot.IsSet())
		return;

	FStructuredArchive::FRecord AnimRecord = AnimSlot.GetValue().EnterRecord();
	FString Path = Ar.IsSaving() ? Asset->GetAssetPath() : FString();
	bool bLoop = Ar.IsSaving() ? Instance->IsLooping() : true;
	float Rate = Ar.IsSaving() ? Instance->GetPlayRate() : 1.0f;
	AnimRecord << SA_VALUE("Asset", Path) << SA_VALUE("Looping", bLoop) << SA_VALUE("PlayRate", Rate);

	if (!Ar.IsLoading())
		return;

	// AssetManager 캐시로 불러온다 (디테일 패널이 고를 때와 같은 객체)
	UAnimationAsset* Loaded = BlendSpaceFile::IsBlendSpacePath(Path)
		? static_cast<UAnimationAsset*>(UAssetManager::LoadBlendSpace(Path))
		: static_cast<UAnimationAsset*>(UAssetManager::LoadAnimSequence(Path));
	if (!Loaded)
	{
		KRS_LOG(Warning, "SkeletalMeshComponent: single animation not found: {}", Path);
		return;
	}

	// 재생 시작은 BeginPlay가 한다 (에디터 월드에서는 첫 프레임 포즈만 보인다)
	SetAnimation(Loaded);
	if (UAnimSingleNodeInstance* NewInstance = GetSingleNodeInstance())
	{
		NewInstance->SetLooping(bLoop);
		NewInstance->SetPlayRate(Rate);
	}
}

void USkeletalMeshComponent::OnSkeletalMeshChanged()
{
    bBlueprintReloadRequested = !AnimBlueprintPath.empty();
	BoneSpaceTransforms.Reset();
	for (const FBoneInfo& Bone : GetBones())
		BoneSpaceTransforms.Add(Bone.LocalBindPose);
	CreateClothingSimulation();
}
bool USkeletalMeshComponent::ShouldCreatePhysicsState() const
{
	UWorld* World = GetWorld();
	FPhysScene* Scene = World ? World->GetPhysicsScene() : nullptr;
	if (BodyInstance.CollisionEnabled == ECollisionEnabled::NoCollision)
		return false;
	return Scene && Scene->IsValid() && GetPhysicsAsset() != nullptr;
}
void USkeletalMeshComponent::OnCreatePhysicsState()
{
	UActorComponent::OnCreatePhysicsState();
	InstantiatePhysicsAssetBodies_Internal();
}
void USkeletalMeshComponent::OnDestroyPhysicsState()
{
	TermArticulated();
	ClearComponentOverlaps();
	UActorComponent::OnDestroyPhysicsState();
}
UAnimSingleNodeInstance* USkeletalMeshComponent::GetSingleNodeInstance() const
{
	return Cast<UAnimSingleNodeInstance>(AnimInstance);
}

void USkeletalMeshComponent::SetAnimation(UAnimationAsset* NewAnimToPlay)
{
	if (NewAnimToPlay && !Cast<UAnimSequence>(NewAnimToPlay) && !Cast<UBlendSpace1D>(NewAnimToPlay))
	{
		KRS_LOG(Warning, "SetAnimation: unsupported animation asset type");
		return;
	}
    AnimBlueprintPath.clear();
    AnimBlueprintError.clear();
    bBlueprintReloadRequested = false;
	auto* SingleNodeInstance = Cast<UAnimSingleNodeInstance>(AnimInstance);

	if (!SingleNodeInstance)
	{
		SingleNodeInstance = NewObject<UAnimSingleNodeInstance>(this);
		SetAnimInstance(SingleNodeInstance);
	}

	SingleNodeInstance->SetAnimationAsset(NewAnimToPlay);
	SingleNodeInstance->SetBlendSpaceInput(BlendSpaceInput);

	RefreshAnimationPose();
}

void USkeletalMeshComponent::Play(bool bLooping)
{
	if (auto* Instance = GetSingleNodeInstance())
	{
		Instance->SetLooping(bLooping);
		Instance->SetPlaying(true);
	}
}

void USkeletalMeshComponent::Stop()
{
	if (auto* Instance = GetSingleNodeInstance())
	{
		Instance->SetPlaying(false);
	}
}

bool USkeletalMeshComponent::IsPlaying() const
{
	const auto* Instance = GetSingleNodeInstance();
	return Instance && Instance->IsPlaying();
}

void USkeletalMeshComponent::PlayAnimation(UAnimationAsset* NewAnimToPlay, bool bLooping)
{
	if (NewAnimToPlay && !Cast<UAnimSequence>(NewAnimToPlay) && !Cast<UBlendSpace1D>(NewAnimToPlay))
	{
		return;
	}

	SetAnimation(NewAnimToPlay);
	Play(bLooping);
}

void USkeletalMeshComponent::RefreshAnimationPose()
{
    if (!AnimInstance)
    {
        ResetPose();
        RefreshBoneTransformsNow();
        return;
    }

	if (auto* Instance = GetSingleNodeInstance())
	{
		Instance->SetBlendSpaceInput(BlendSpaceInput);
	}

	FPoseContext Pose(AnimInstance, GetBones());
	AnimInstance->EvaluateAnimation(Pose);
	SetBoneSpaceTransforms(Pose.Pose);
	RefreshBoneTransformsNow();
}


FTransform USkeletalMeshComponent::ConsumeRootMotion()
{
	const FTransform Result = PendingRootMotion;
	PendingRootMotion = FTransform();
	bHasPendingRootMotion = false;
	return Result;
}

FTransform USkeletalMeshComponent::ConvertRootMotionToWorld(const FTransform& ParentSpaceDelta) const
{
	// delta는 root 본의 부모 공간 값이다. UAL은 Armature → root 구조라 부모(Armature)를 거쳐야 컴포넌트 공간이 된다.
	const TArray<FBoneInfo>& Bones = GetBones();
	const int32 RootIndex = GetRootMotionBoneIndex();
	const int32 ParentIndex = RootIndex >= 0 ? Bones[RootIndex].ParentIndex : -1;

	// 이동: 부모(바인드 포즈) → 컴포넌트 → 월드 행렬로 방향·스케일만 적용 (Armature 스케일, 메시 0.01, yaw −180 모두 여기서)
	const FMatrix ComponentToWorld = GetWorldMatrix();
	const FMatrix ParentToWorld = ParentIndex >= 0
		? Bones[ParentIndex].InverseBindPose.Inverse() * ComponentToWorld
		: ComponentToWorld;

	FTransform Result;
	Result.Location = ParentToWorld.TransformVector(ParentSpaceDelta.Location);

	// 회전: 부모 공간 회전을 월드 공간 회전으로 바꾼다 (P × Δ × P⁻¹, P = 컴포넌트 월드 회전 × 부모 본 회전)
	FQuat ParentRotation = FQuat(GetWorldRotation());
	FQuat BoneChainRotation;
	for (int32 Index = ParentIndex; Index >= 0; Index = Bones[Index].ParentIndex)
	{
		BoneChainRotation = Bones[Index].LocalBindPose.Rotation * BoneChainRotation;
	}
	ParentRotation = (ParentRotation * BoneChainRotation).Normalized();
	Result.Rotation = (ParentRotation * ParentSpaceDelta.Rotation * ParentRotation.Inverse()).Normalized();
	return Result;
}

int32 USkeletalMeshComponent::GetRootMotionBoneIndex() const
{
	const int32 RootIndex = FindBoneIndex("root");
	if (RootIndex >= 0)
	{
		return RootIndex;
	}
	return GetBones().IsEmpty() ? -1 : 0;
}

void USkeletalMeshComponent::DrawRootDebug(FLineBatcher* LineBatcher) const
{
	const int32 RootIndex = GetRootMotionBoneIndex();
	if (!LineBatcher || RootIndex < 0 || RootIndex >= ComponentSpaceTransforms.Num())
	{
		return;
	}

	// root 본 위치에 구체
	const FVector Root = GetBoneWorldMatrix(RootIndex).TransformPosition(FVector::ZeroVector);
	const FVector4 Color(1.0f, 0.3f, 0.8f, 1.0f);
	LineBatcher->AddSphere(Root, 0.08f, Color);

	// 액터 위치를 root 높이로 내린 점 → root. 선 길이 = root가 액터(캡슐)에서 벗어난 거리
	if (const AActor* Owner = GetOwner())
	{
		const FVector ActorLocation = Owner->GetActorLocation();
		const FVector Base(ActorLocation.X, ActorLocation.Y, Root.Z);
		LineBatcher->AddLine(Base, Root, Color);
		LineBatcher->AddSphere(Base, 0.03f, Color);
	}
}
void DrawSkeletalRootDebug(UWorld* World, FLineBatcher* LineBatcher)
{
	if (!World || !LineBatcher) return;
	for (TObjectIterator<USkeletalMeshComponent> It; It; ++It)
	{
		const USkeletalMeshComponent* Mesh = *It;
		if (!Mesh || Mesh->GetWorld() != World || !Mesh->IsRegistered()) continue;
		Mesh->DrawRootDebug(LineBatcher);
	}
}

USkeletalMeshComponent::USkeletalMeshComponent()
{

}

USkeletalMeshComponent::~USkeletalMeshComponent()
{
	TermArticulated();
	DestroyClothingSimulation();
    if (ManagedAnimGraph)
    {
        if (AnimInstance == ManagedAnimGraph.get())
        {
            SetAnimInstance(nullptr);
        }
        else
        {
            ManagedAnimGraph->UninitializeAnimation();
        }
    }
}

void USkeletalMeshComponent::CreateClothingSimulation()
{
	DestroyClothingSimulation();

	USkeletalMesh* Mesh = GetSkeletalMesh();
	if (!Mesh || !bEnableClothingSimulation)
		return;

	UClothingAssetCommon* Asset = Mesh->GetClothingAsset();
	if (!Asset || !Asset->IsValid())
		return;

	TUniquePtr<FClothingSimulationNv> NewSim = MakeUnique<FClothingSimulationNv>();
	if (!NewSim->CreateClothFromAsset(*Asset))
		return;

	ClothingSimulation = std::move(NewSim);
	bClothTeleportPending = true;
}

void USkeletalMeshComponent::DestroyClothingSimulation()
{
	if (ClothingSimulation)
	{
		ClothingSimulation->Shutdown();
		ClothingSimulation.reset();
	}
	ClothSimData.Positions.Empty();
	ClothSimData.Normals.Empty();
	if (FSkeletalMeshObject* MeshObj = GetMeshObject())
	{
		MeshObj->UpdateClothData(nullptr);
	}
}

void USkeletalMeshComponent::SetClothSimulationSuspended(bool bSuspend)
{
	bSuspendClothingSimulation = bSuspend;
	if (bSuspend)
	{
		if (FSkeletalMeshObject* MeshObj = GetMeshObject())
			MeshObj->UpdateClothData(nullptr);
	}
	else
	{
		bClothTeleportPending = true;
	}
}

void USkeletalMeshComponent::RefreshClothingOnAllComponents(USkeletalMesh* TargetMesh)
{
	if (!TargetMesh)
		return;

	for (TObjectIterator<USkeletalMeshComponent> It; It; ++It)
	{
		USkeletalMeshComponent* Comp = *It;
		if (Comp && Comp->GetSkeletalMesh() == TargetMesh)
		{
			Comp->CreateClothingSimulation();
		}
	}
}

UPhysicsAsset* USkeletalMeshComponent::GetPhysicsAsset() const
{
	USkeletalMesh* Mesh = GetSkeletalMesh();
	return Mesh ? Mesh->GetPhysicsAsset() : nullptr;
}

int32 USkeletalMeshComponent::FindBodyIndexByBone(const FString& BoneName) const
{
	UPhysicsAsset* PhysAsset = GetPhysicsAsset();
	if (!PhysAsset)
		return -1;
	const int32 Index = PhysAsset->FindBodyIndex(FName(BoneName));
	return (Index >= 0 && Index < Bodies.Num() && Bodies[Index]) ? Index : -1;
}

FBodyInstance* USkeletalMeshComponent::GetBodyInstanceByBone(const FString& BoneName) const
{
	const int32 Index = FindBodyIndexByBone(BoneName);
	return Index >= 0 ? Bodies[Index] : nullptr;
}

void USkeletalMeshComponent::UpdateClothingSimulation(float DeltaTime)
{
	FSkeletalMeshObject* MeshObj = GetMeshObject();
	if (!MeshObj)
		return;

	if (!bEnableClothingSimulation || bSuspendClothingSimulation)
	{
		if (ClothingSimulation)
			MeshObj->UpdateClothData(nullptr);
		return;
	}

	// 에셋에 ClothingAsset이 나중에 바인딩된 경우 자동 생성
	if (!ClothingSimulation)
	{
		USkeletalMesh* Mesh = GetSkeletalMesh();
		if (Mesh && Mesh->GetClothingAsset() && Mesh->GetClothingAsset()->IsValid())
			CreateClothingSimulation();
		if (!ClothingSimulation)
			return;
	}

	const FTransform ComponentToWorld(GetWorldRotation().Quaternion(), GetWorldLocation(), GetWorldScale3D());
	ClothingSimulation->UpdateSkeletalPose(MeshObj->GetRefToLocal(), ComponentToWorld, bClothTeleportPending);
	bClothTeleportPending = false;

	ClothingSimulation->Simulate(DeltaTime);
	ClothingSimulation->GetSimulationData(ClothSimData);
	MeshObj->UpdateClothData(&ClothSimData);
}

void USkeletalMeshComponent::InstantiatePhysicsAssetBodies_Internal()
{
	TermArticulated();

	UPhysicsAsset* PhysAsset = GetPhysicsAsset();
	USkeletalMesh* Mesh = GetSkeletalMesh();
	FPhysScene* Scene = GetWorld()->GetPhysicsScene();
	if (!PhysAsset || !Mesh || !Scene)
		return;

	// 바디를 지금 포즈 위치에 만들려면 본 행렬이 최신이어야 한다
	UpdateBoneTransformsNow();

	// 바디
	Bodies.SetNum(PhysAsset->BodySetup.Num());
	for (int32 i = 0; i < PhysAsset->BodySetup.Num(); ++i)
	{
		Bodies[i] = nullptr;
		UBodySetup* Setup = PhysAsset->BodySetup[i];
		if (!Setup)
			continue;

		// 에셋과 메시의 본 이름이 안 맞으면(다른 스켈레톤용 에셋) 건너뛴다
		const int32 BoneIndex = Mesh->FindBoneIndex(Setup->BoneName.ToString());
		if (BoneIndex < 0)
		{
			KRS_LOG(Warning, "[Physics] PhysicsAsset bone '{}' not found in mesh", Setup->BoneName.ToString());
			continue;
		}

		FBodyInstance* Body = new FBodyInstance();
		Body->CopyBodyInstancePropertiesFrom(BodyInstance);   // 컴포넌트의 BodyInstance = 템플릿
		Body->InstanceBoneIndex = BoneIndex;
		Body->InstanceBodyIndex = i;
		Body->OwnerId = GetUUID();

		// 형상(캡슐)이 본 로컬 기준이므로 바디는 본의 월드 트랜스폼에 놓는다
		const FTransform BoneWorld = MatrixToTransform(GetBoneWorldMatrix(BoneIndex));
		if (!Body->InitBody(Setup, BoneWorld, this, Scene))
		{
			delete Body;
			continue;
		}
		Bodies[i] = Body;
	}

	// 조인트 (두 바디가 모두 있어야 하므로 바디를 전부 만든 뒤에)
	Constraints.SetNum(PhysAsset->ConstraintSetup.Num());
	for (int32 i = 0; i < PhysAsset->ConstraintSetup.Num(); ++i)
	{
		Constraints[i] = nullptr;
		UPhysicsConstraintTemplate* Template = PhysAsset->ConstraintSetup[i];
		if (!Template)
			continue;

		const FConstraintInstance& Def = Template->DefaultInstance;
		const int32 ChildIdx = PhysAsset->FindBodyIndex(Def.ConstraintBone1);
		const int32 ParentIdx = PhysAsset->FindBodyIndex(Def.ConstraintBone2);
		FBodyInstance* Child = ChildIdx >= 0 ? Bodies[ChildIdx] : nullptr;
		FBodyInstance* Parent = ParentIdx >= 0 ? Bodies[ParentIdx] : nullptr;
		if (!Child || !Parent)
			continue;

		FConstraintInstance* CI = new FConstraintInstance();
		CI->CopyConstraintParamsFrom(Def);
		if (!CI->InitConstraint(Child, Parent))
		{
			delete CI;
			continue;
		}
		Constraints[i] = CI;
	}
}

void USkeletalMeshComponent::TermArticulated()
{
	// 조인트 먼저: 액터가 먼저 사라지면 조인트가 해제된 액터를 가리킨다
	for (FConstraintInstance* CI : Constraints)
		delete CI;   // 소멸자가 TermConstraint
	Constraints.Empty();

	for (FBodyInstance* Body : Bodies)
		delete Body; // 소멸자가 TermBody
	Bodies.Empty();
}

const FString& USkeletalMeshComponent::GetAnimBlueprintPath() const
{
    return AnimBlueprintPath;
}

const FString& USkeletalMeshComponent::GetAnimBlueprintError() const
{
    return AnimBlueprintError;
}

bool USkeletalMeshComponent::SetAnimBlueprint(const FString& Path)
{
    if (bUpdatingAnimation)
    {
        AnimBlueprintError = "Load or reload Animation Blueprints from actor Tick, outside animation callbacks.";
        return false;
    }

    if (Path.empty())
    {
        const bool bHadBlueprint = !AnimBlueprintPath.empty();
        AnimBlueprintPath.clear();
        AnimBlueprintError.clear();
        bBlueprintReloadRequested = false;

        if (ManagedAnimGraph && bHadBlueprint)
        {
            SetAnimInstance(nullptr);
            ResetPose();
            RefreshBoneTransformsNow();
        }

        return true;
    }

    AnimBlueprintError.clear();
    const FString Key = FAssetUtils::GetAssetKey(Path);
    const FString FilePath = FAssetUtils::ResolveAssetFilePath(Key);
    FAnimBlueprintData Data;

    if (!AnimBlueprintFile::IsAnimBlueprintPath(Key) || !AnimBlueprintFile::Read(FPaths::FromUtf8(FilePath.empty() ? Key : FilePath), Data, AnimBlueprintError))
    {
        if (AnimBlueprintError.empty())
        {
            AnimBlueprintError = "Use an .animblueprint asset.";
        }

        return false;
    }

    TUniquePtr<UAnimGraphInstance> Instance;
    AnimBlueprintCompiler::FNodeMap NodeIndices;

    if (!AnimBlueprintCompiler::Build(Data, *this, Instance, NodeIndices, AnimBlueprintError))
    {
        return false;
    }

    SetAnimInstance(Instance.get());
    ManagedAnimGraph = std::move(Instance);
    AnimBlueprintPath = Key;
    bBlueprintReloadRequested = false;
    AnimBlueprintError.clear();
    RefreshAnimationPose();
    return true;
}

bool USkeletalMeshComponent::ReloadAnimBlueprint()
{
    const FString Path = AnimBlueprintPath;
    bBlueprintReloadRequested = false;

    if (Path.empty())
    {
        return false;
    }

    if (!SetAnimBlueprint(Path))
    {
        KRS_LOG(Warning, "AnimBlueprint load failed: {} ({})", Path, AnimBlueprintError);
        return false;
    }

    return true;
}

UAnimGraphInstance* USkeletalMeshComponent::CreateAnimGraph()
{
    if (bUpdatingAnimation)
    {
        AnimBlueprintError = "Create animation graphs outside animation callbacks.";
        return nullptr;
    }

    if (!AnimBlueprintPath.empty())
    {
        AnimBlueprintError = "An Animation Blueprint is assigned. Use GetAnimGraph to update its variables.";
        return nullptr;
    }

    AnimBlueprintError.clear();

    if (UAnimGraphInstance* Existing = Cast<UAnimGraphInstance>(AnimInstance))
    {
        Existing->ResetGraph();
        return Existing;
    }

    TUniquePtr<UAnimGraphInstance> Instance(NewObject<UAnimGraphInstance>(this));
    SetAnimInstance(Instance.get());
    ManagedAnimGraph = std::move(Instance);
    return ManagedAnimGraph.get();
}

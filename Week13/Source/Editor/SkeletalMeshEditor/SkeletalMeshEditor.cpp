#include "EnginePCH.h"
#include "Editor/SkeletalMeshEditor/SkeletalMeshEditor.h"
#include "Editor/AssetEditor/EditorManager.h"
#include "SkeletalMeshEditorTab.h"
#include "Components/SkeletalMeshComponent.h"
#include "Render/SkeletalMesh.h"
#include "Physics/PhysicsAsset.h"
#include "Physics/PhysicsAssetUtils.h"
#include "Physics/PhysicsAssetFile.h"
#include "Asset/AssetUtils.h"
#include "Asset/AssetManager.h"

#include <filesystem>

namespace
{
	FString MakePhysicsAssetPath(const USkeletalMesh& Mesh)
	{
		std::filesystem::path Path(FAssetUtils::ResolveAssetFilePath(Mesh.GetPath()));
		Path.replace_extension(".phys");
		return Path.generic_string();
	}
}

bool FSkeletalMeshEditor::InitializeResources()
{
	if (!InitializePreview()) return false;
	BoneGizmoTarget = Cast<USceneComponent>(PreviewScene.AddComponent(USceneComponent::StaticClass()));
	if (!BoneGizmoTarget) return false;
	BoneGizmoTarget->DetachFromParent();
	ResetBonePoseHistory();
	LoadPhysicsAsset();   // 이전에 Generate해 둔 .phys가 있으면 읽어 온다
	return true;
}

void FSkeletalMeshEditor::ShutdownResources()
{
	PoseUndoStack.Empty();
	PoseRedoStack.Empty();
	CommittedBonePose.Empty();
	bPoseUndoPending = false;
	BoneGizmo.EndDrag();
	BoneGizmo.SetTarget(nullptr);
	BoneGizmoTarget = nullptr;
	FSkeletalPreviewEditor::ShutdownResources();
}

void FSkeletalMeshEditor::TickAfterWorld(float DeltaTime)
{
	// 기즈모가 입력을 쓰지 않았을 때만 본을 피킹하고, 마지막에 기즈모를 선택한 본 위치로 맞춘다
	if (!UpdateBoneGizmo())
		UpdateBonePicking();
	SyncBoneGizmo();
}

TUniquePtr<IEditorTab> FSkeletalMeshEditor::CreateTab()
{
	return MakeUnique<FSkeletalMeshEditorTab>(*this);
}

void FSkeletalMeshEditor::SetGizmoMode(EGizmoMode Mode)
{
	BoneGizmo.EndDrag();
	BoneGizmo.SetMode(Mode);
	SyncBoneGizmo();
}

void FSkeletalMeshEditor::SetGizmoSpace(EGizmoSpace Space)
{
	BoneGizmo.EndDrag();
	BoneGizmo.SetSpace(Space);
	SyncBoneGizmo();
}

void FSkeletalMeshEditor::OpenAnimationAsset(const FString& InAssetPath)
{
	if (!InAssetPath.empty())
	{
		EditorManager.OpenEditor(InAssetPath);
	}
}

void FSkeletalMeshEditor::GeneratePhysicsAsset()
{
	if (!Mesh)
		return;

	// 로드된 에셋이 있으면 그 객체를 그대로 덮어쓴다.
	// 새로 NewObject하면 AssetManager 캐시에 옛 객체가 남아, 다른 곳에서 Load하면 예전 결과가 나온다.
	if (!PhysicsAsset)
		PhysicsAsset = NewObject<UPhysicsAsset>();

	FPhysAssetCreateParams Params;   // 기본값으로 시작. 결과 보고 조정
	FString Error;
	if (!FPhysicsAssetUtils::CreateFromSkeletalMesh(*PhysicsAsset, *Mesh, Params, Error))
	{
		KRS_LOG(Warning, "[Physics] Generate failed: {}", Error);
		return;
	}

	// 메시 파일 옆에 같은 이름으로 저장 (Character.fbx → Character.phys). 로드와 같은 경로 함수를 쓴다
	const FString Path = MakePhysicsAssetPath(*Mesh);
	if (!PhysicsAssetFile::Write(std::filesystem::path(Path), *PhysicsAsset, Error))
		KRS_LOG(Warning, "[Physics] Save failed: {}", Error);
	else
		KRS_LOG(Info, "[Physics] Saved {}", Path);
}

void FSkeletalMeshEditor::LoadPhysicsAsset()
{
	PhysicsAsset = nullptr;
	if (!Mesh)
		return;

	const FString Path = MakePhysicsAssetPath(*Mesh);
	if (!std::filesystem::exists(Path))
		return;   // 아직 Generate한 적 없음 → 조용히 넘어간다 (경고 X)

	PhysicsAsset = UAssetManager::LoadPhysicsAsset(Path);
}

void FSkeletalMeshEditor::OnRenderLine(FLineBatcher& LineBatcher)
{
	FSkeletalPreviewEditor::OnRenderLine(LineBatcher);   // 본 시각화는 그대로

	if (!bShowPhysicsBodies || !PhysicsAsset || !Mesh || !MeshComponent)
		return;

	const FVector4 Color(0.2f, 1.0f, 0.3f, 1.0f);
	for (UBodySetup* Setup : PhysicsAsset->BodySetup)
	{
		const int32 Bone = Mesh->FindBoneIndex(Setup->BoneName.ToString());
		if (Bone < 0)
			continue;

		// 본 로컬 → 컴포넌트 공간 (바인드 포즈). 프리뷰 컴포넌트가 원점에 있다고 가정
		const FMatrix BoneToWorld = MeshComponent->GetBoneWorldMatrix(Bone);

		for (const FKSphylElem& S : Setup->AggGeom.SphylElems)
		{
			const FVector Up = S.Rotation.RotateVector(FVector(0, 0, 1));
			const FVector Side1 = S.Rotation.RotateVector(FVector(1, 0, 0));
			const FVector Side2 = S.Rotation.RotateVector(FVector(0, 1, 0));
			const FVector HalfAxis = Up * (S.Length * 0.5f);

			auto ToWorld = [&](const FVector& Local) { return BoneToWorld.TransformPosition(Local); };

			const FVector TopLocal = S.Center + HalfAxis;
			const FVector BottomLocal = S.Center - HalfAxis;

			// 반지름 길이의 벡터를 본 행렬로 변환한 뒤 길이를 잰다 → 행렬 스케일이 반영된 월드 반지름
			const float WorldRadius = BoneToWorld.TransformVector(Side1 * S.Radius).Size();

			LineBatcher.AddSphere(ToWorld(TopLocal), WorldRadius, Color, 12);
			LineBatcher.AddSphere(ToWorld(BottomLocal), WorldRadius, Color, 12);

			const FVector Offsets[4] = { Side1 * S.Radius, -Side1 * S.Radius, Side2 * S.Radius, -Side2 * S.Radius };
			for (const FVector& O : Offsets)
				LineBatcher.AddLine(ToWorld(TopLocal + O), ToWorld(BottomLocal + O), Color);
		}
	}


}

void FSkeletalMeshEditor::SyncBoneGizmo()
{
	if (!MeshComponent || !BoneGizmoTarget || SelectedBoneIndex < 0 || SelectedBoneIndex >= BoneItems.Num() ||
		BoneDrawMode == EBoneDrawMode::None)
	{
		BoneGizmo.SetTarget(nullptr);
		return;
	}

	if (BoneGizmo.IsUsing())
	{
		return;
	}

	const FMatrix BoneWorld = MeshComponent->GetBoneWorldMatrix(SelectedBoneIndex);
	const FVector Location = BoneWorld.TransformPosition(FVector::ZeroVector);
	const FRotator Rotation = GetBoneWorldRotation(SelectedBoneIndex).ToFRotator();

	// 스케일은 조작 배율로만 사용합니다. 부모의 비균일 스케일과 전단을 축 회전에 섞지 않습니다.
	BoneGizmoTarget->SetTransform(FTransform(Rotation, Location));
	BoneGizmo.SetTarget(BoneGizmoTarget);
}

FQuat FSkeletalMeshEditor::GetBoneWorldRotation(int32 BoneIndex) const
{
	FQuat Rotation = FQuat::Identity();
	const TArray<FBoneInfo>& Bones = Mesh->GetRefSkeleton();
	while (BoneIndex >= 0)
	{
		Rotation = MeshComponent->GetBoneLocalTransform(BoneIndex).Rotation * Rotation;
		BoneIndex = Bones[BoneIndex].ParentIndex;
	}

	return (MeshComponent->GetWorldRotation().Quaternion() * Rotation).Normalized();
}

bool FSkeletalMeshEditor::UpdateBoneGizmo()
{
	if
		(
			// 공통 뷰포트 입력 조건을 만족하지 않으면 입력을 무시
			!CanProcessViewportInput() ||

			// BoneGizmo의 대상이 없으면 입력을 무시
			!BoneGizmo.GetTarget()
			)
	{
		BoneGizmo.EndDrag();
		return false;
	}

	if (PointerInput.bCancel)
	{
		if (BoneGizmo.IsUsing())
		{
			MeshComponent->SetBoneLocalTransform(SelectedBoneIndex, DragStartBoneTransform);
			MeshComponent->RefreshBoneTransformsNow();
		}
		BoneGizmo.EndDrag();
		return false;
	}

	const bool bWasUsing = BoneGizmo.IsUsing();
	if ((!PointerInput.bHovered && !bWasUsing) || CameraInput.bCaptured)
	{
		BoneGizmo.EndDrag();
		return false;
	}

	SyncBoneGizmo();
	const int32 Width = static_cast<int32>(ViewportClient->GetWidth());
	const int32 Height = static_cast<int32>(ViewportClient->GetHeight());
	const FVector2 MousePosition(PointerInput.MouseUV.X * Width, PointerInput.MouseUV.Y * Height);
	const FRay MouseRay = ViewportClient->DeprojectScreenToWorld(MousePosition);

	// 클릭한 프레임에만 시작하고, 버튼을 놓는 프레임의 마지막 마우스 위치도 반영합니다.
	const bool bUpdateDrag = bWasUsing || (PointerInput.bMousePressed && PointerInput.bMouseDown);
	BoneGizmo.Update(MouseRay, MousePosition, ViewportClient->GetViewProjectionMatrix(),
		Width, Height, bUpdateDrag, ViewportClient->GetViewLocation(), !ViewportClient->IsPerspective());

	if (!bWasUsing && BoneGizmo.IsUsing())
	{
		const int32 ParentIndex = Mesh->GetRefSkeleton()[SelectedBoneIndex].ParentIndex;
		const FMatrix ParentWorld = ParentIndex >= 0
			? MeshComponent->GetBoneWorldMatrix(ParentIndex) : MeshComponent->GetWorldMatrix();

		// 스케일이 0인 부모의 역행렬로 이동을 계산할 수 없습니다.
		if (BoneGizmo.GetMode() == EGizmoMode::Location && fabsf(ParentWorld.Determinant()) < 1e-12f)
		{
			BoneGizmo.EndDrag();
			return true;
		}

		DragStartBoneTransform = MeshComponent->GetBoneLocalTransform(SelectedBoneIndex);
		DragParentRotation = GetBoneWorldRotation(ParentIndex);
		if (BoneGizmo.GetMode() == EGizmoMode::Location)
		{
			DragParentInverse = ParentWorld.Inverse();
		}
	}

	if (BoneGizmo.IsUsing())
	{
		ApplyBoneGizmoTransform();
		if (!PointerInput.bMouseDown)
		{
			BoneGizmo.EndDrag();
		}
	}

	return bWasUsing || BoneGizmo.IsUsing() || BoneGizmo.GetHoveredAxis() >= 0;
}

void FSkeletalMeshEditor::ApplyBoneGizmoTransform()
{
	FTransform Local = DragStartBoneTransform;
	switch (BoneGizmo.GetMode())
	{
	case EGizmoMode::Location:
		Local.Location = DragParentInverse.TransformPosition(BoneGizmoTarget->GetWorldLocation());
		break;
	case EGizmoMode::Rotation:
		Local.Rotation = (DragParentRotation.Inverse() *
			BoneGizmoTarget->GetRelativeRotation().Quaternion()).Normalized();
		break;
	case EGizmoMode::Scale:
		Local.Scale = DragStartBoneTransform.Scale * BoneGizmoTarget->GetRelativeScale3D();
		break;
	}

	// 조작한 성분만 변경하고 나머지는 드래그 시작 시점의 로컬 값을 유지합니다.
	MeshComponent->SetBoneLocalTransform(SelectedBoneIndex, Local);
	MeshComponent->RefreshBoneTransformsNow();
	bPoseUndoPending = true;
}

bool FSkeletalMeshEditor::GetSelectedBoneLocalTransform(FTransform& OutTransform) const
{
	if (!MeshComponent || SelectedBoneIndex < 0 || SelectedBoneIndex >= BoneItems.Num())
	{
		return false;
	}

	OutTransform = MeshComponent->GetBoneLocalTransform(SelectedBoneIndex);
	return true;
}

void FSkeletalMeshEditor::SetSelectedBoneLocalTransform(const FTransform& Transform)
{
	if (!MeshComponent || SelectedBoneIndex < 0 || SelectedBoneIndex >= BoneItems.Num())
	{
		return;
	}

	// 원본 애셋 대신 프리뷰 컴포넌트의 포즈만 수정합니다. 본 갱신은 다음 World Tick에서 수행합니다.
	BoneGizmo.EndDrag();
	MeshComponent->SetBoneLocalTransform(SelectedBoneIndex, Transform);
	bPoseUndoPending = true;
}

void FSkeletalMeshEditor::ResetPose()
{
	if (!MeshComponent)
	{
		return;
	}

	BoneGizmo.EndDrag();
	MeshComponent->ResetPose();
	bPoseUndoPending = true;
}

void FSkeletalMeshEditor::ResetBone(int32 BoneIndex)
{
	if (!Mesh)
	{
		return;
	}

	if (!MeshComponent)
	{
		return;
	}

	if (BoneIndex < 0 || BoneIndex >= Mesh->GetRefSkeleton().Num())
	{
		return;
	}

	BoneGizmo.EndDrag();
	FTransform Transform = Mesh->GetRefSkeleton()[BoneIndex].LocalBindPose;
	MeshComponent->SetBoneLocalTransform(BoneIndex, Transform);
	bPoseUndoPending = true;
}

namespace
{
	bool AreBonePosesEqual(const TArray<FTransform>& A, const TArray<FTransform>& B)
	{
		if (A.Num() != B.Num())
		{
			return false;
		}
		for (int32 i = 0; i < A.Num(); ++i)
		{
			if (A[i].Location.X != B[i].Location.X || A[i].Location.Y != B[i].Location.Y || A[i].Location.Z != B[i].Location.Z ||
				A[i].Rotation.X != B[i].Rotation.X || A[i].Rotation.Y != B[i].Rotation.Y || A[i].Rotation.Z != B[i].Rotation.Z || A[i].Rotation.W != B[i].Rotation.W ||
				A[i].Scale.X != B[i].Scale.X || A[i].Scale.Y != B[i].Scale.Y || A[i].Scale.Z != B[i].Scale.Z)
			{
				return false;
			}
		}
		return true;
	}
}

TArray<FTransform> FSkeletalMeshEditor::CaptureBonePose() const
{
	TArray<FTransform> Pose;
	if (!Mesh || !MeshComponent)
	{
		return Pose;
	}

	const int32 NumBones = Mesh->GetRefSkeleton().Num();
	Pose.Reserve(NumBones);
	for (int32 i = 0; i < NumBones; ++i)
	{
		Pose.Add(MeshComponent->GetBoneLocalTransform(i));
	}
	return Pose;
}

void FSkeletalMeshEditor::ApplyBonePose(const TArray<FTransform>& Pose)
{
	if (!Mesh || !MeshComponent)
	{
		return;
	}

	BoneGizmo.EndDrag();
	const int32 NumBones = std::min(static_cast<int32>(Pose.Num()), static_cast<int32>(Mesh->GetRefSkeleton().Num()));
	for (int32 i = 0; i < NumBones; ++i)
	{
		MeshComponent->SetBoneLocalTransform(i, Pose[i]);
	}
	MeshComponent->RefreshBoneTransformsNow();
	SyncBoneGizmo();
}

void FSkeletalMeshEditor::ResetBonePoseHistory()
{
	PoseUndoStack.Empty();
	PoseRedoStack.Empty();
	CommittedBonePose = CaptureBonePose();
	bPoseUndoPending = false;
}

void FSkeletalMeshEditor::CommitBonePoseUndo(bool bInteractionActive)
{
	if (!bPoseUndoPending || bInteractionActive || !MeshComponent)
	{
		return;
	}
	bPoseUndoPending = false;

	TArray<FTransform> Current = CaptureBonePose();
	if (AreBonePosesEqual(Current, CommittedBonePose))
	{
		return;
	}

	PoseUndoStack.Add(std::move(CommittedBonePose));
	if (PoseUndoStack.Num() > MaxUndoSteps)
	{
		PoseUndoStack.RemoveAt(0);
	}
	PoseRedoStack.Empty();
	CommittedBonePose = std::move(Current);
}

void FSkeletalMeshEditor::UndoPoseEdit()
{
	CommitBonePoseUndo(false);
	if (PoseUndoStack.IsEmpty() || !MeshComponent)
	{
		return;
	}

	PoseRedoStack.Add(std::move(CommittedBonePose));
	CommittedBonePose = std::move(PoseUndoStack[PoseUndoStack.Num() - 1]);
	PoseUndoStack.RemoveAt(PoseUndoStack.Num() - 1);
	ApplyBonePose(CommittedBonePose);
}

void FSkeletalMeshEditor::RedoPoseEdit()
{
	CommitBonePoseUndo(false);
	if (PoseRedoStack.IsEmpty() || !MeshComponent)
	{
		return;
	}

	PoseUndoStack.Add(std::move(CommittedBonePose));
	CommittedBonePose = std::move(PoseRedoStack[PoseRedoStack.Num() - 1]);
	PoseRedoStack.RemoveAt(PoseRedoStack.Num() - 1);
	ApplyBonePose(CommittedBonePose);
}

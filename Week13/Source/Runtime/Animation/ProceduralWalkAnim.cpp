#include "EnginePCH.h"
#include "Animation/ProceduralWalkAnim.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSequenceFile.h"
#include "Asset/AssetManager.h"
#include "Core/Misc/Paths.h"
#include "Render/SkeletalMesh.h"
#include "Render/SkeletalMeshData.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <unordered_map>

namespace
{
	constexpr float DegToRad = PI / 180.0f;

	FQuat AxisAngle(const FVector& Axis, float Degrees)
	{
		return FQuat::MakeFromAxisAngle(Axis, Degrees * DegToRad);
	}

	int32 FindBone(const TArray<FBoneInfo>& Bones, const FString& Name)
	{
		if (Name.empty()) return -1;
		for (int32 Index = 0; Index < Bones.Num(); ++Index)
		{
			if (Bones[Index].Name == Name) return Index;
		}
		return -1;
	}

	FVector Horizontal(const FVector& V)
	{
		return FVector(V.X, V.Y, 0.0f);
	}

	// 레퍼런스 포즈의 컴포넌트 공간 회전/위치/스케일 (스케일은 균일하다고 본다)
	struct FRefPose
	{
		TArray<FQuat> Rotation;
		TArray<FVector> Position;
		TArray<float> Scale;
	};

	FRefPose ComputeRefPose(const TArray<FBoneInfo>& Bones)
	{
		FRefPose Pose;
		Pose.Rotation.SetNum(Bones.Num());
		Pose.Position.SetNum(Bones.Num());
		Pose.Scale.SetNum(Bones.Num());
		for (int32 Index = 0; Index < Bones.Num(); ++Index)
		{
			const FTransform& Local = Bones[Index].LocalBindPose;
			const int32 Parent = Bones[Index].ParentIndex;
			if (Parent < 0)
			{
				Pose.Rotation[Index] = Local.Rotation.Normalized();
				Pose.Position[Index] = Local.Location;
				Pose.Scale[Index] = Local.Scale.X;
				continue;
			}
			Pose.Rotation[Index] = (Pose.Rotation[Parent] * Local.Rotation).Normalized();
			Pose.Position[Index] = Pose.Position[Parent] + Pose.Rotation[Parent].RotateVector(Local.Location * Pose.Scale[Parent]);
			Pose.Scale[Index] = Pose.Scale[Parent] * Local.Scale.X;
		}
		return Pose;
	}

	struct FResolvedLimb
	{
		int32 Upper = -1;
		int32 Lower = -1;
		int32 End = -1;
	};

	struct FResolvedRig
	{
		int32 Root = -1;
		int32 Pelvis = -1;
		TArray<int32> Spine;
		int32 Head = -1;
		FResolvedLimb LegLeft, LegRight, ArmLeft, ArmRight;
		int32 ToeLeft = -1;
		int32 ToeRight = -1;
	};

	bool ResolveLimb(const TArray<FBoneInfo>& Bones, const FProceduralLimbBones& Names, FResolvedLimb& Out, FString& OutError)
	{
		Out.Upper = FindBone(Bones, Names.Upper);
		Out.Lower = FindBone(Bones, Names.Lower);
		Out.End = FindBone(Bones, Names.End);
		if (Out.Upper < 0 || Out.Lower < 0 || Out.End < 0)
		{
			OutError = "Missing limb bone: " + Names.Upper + " / " + Names.Lower + " / " + Names.End;
			return false;
		}
		return true;
	}

	bool ResolveRig(const TArray<FBoneInfo>& Bones, const FProceduralWalkRig& Rig, FResolvedRig& Out, FString& OutError)
	{
		Out.Root = FindBone(Bones, Rig.Root);
		Out.Pelvis = FindBone(Bones, Rig.Pelvis);
		Out.Head = FindBone(Bones, Rig.Head);
		Out.ToeLeft = FindBone(Bones, Rig.ToeLeft);
		Out.ToeRight = FindBone(Bones, Rig.ToeRight);
		if (Out.Root < 0 || Out.Pelvis < 0)
		{
			OutError = "Missing root/pelvis bone: " + Rig.Root + " / " + Rig.Pelvis;
			return false;
		}
		for (const FString& Name : Rig.Spine)
		{
			const int32 Index = FindBone(Bones, Name);
			if (Index < 0)
			{
				OutError = "Missing spine bone: " + Name;
				return false;
			}
			Out.Spine.Add(Index);
		}
		return ResolveLimb(Bones, Rig.LegLeft, Out.LegLeft, OutError)
			&& ResolveLimb(Bones, Rig.LegRight, Out.LegRight, OutError)
			&& ResolveLimb(Bones, Rig.ArmLeft, Out.ArmLeft, OutError)
			&& ResolveLimb(Bones, Rig.ArmRight, Out.ArmRight, OutError);
	}

	// 컴포넌트 공간의 기준 축. 모두 레퍼런스 포즈에서 구한다
	struct FRigFrame
	{
		FVector Up = FVector(0, 0, 1);
		FVector Down = FVector(0, 0, -1);
		FVector Forward = FVector(1, 0, 0);
		FVector Left = FVector(0, -1, 0);
		FVector Swing;       // +각도: 아래를 향한 팔다리를 앞으로 든다
		FVector Twist;       // +각도: 몸의 왼쪽을 앞으로 돌린다
		FVector Lean;        // +각도: 위를 향한 척추를 앞으로 숙인다
		FVector Roll;        // +각도: 몸의 왼쪽을 위로 든다
		float LegLength = 1.0f;
	};

	FRigFrame ComputeFrame(const FRefPose& Ref, const FResolvedRig& Rig)
	{
		FRigFrame Frame;

		// 앞: 발목 → 발끝 (양발 평균). 발끝 본이 없으면 +X
		FVector Forward = FVector::ZeroVector;
		if (Rig.ToeLeft >= 0) Forward += Horizontal(Ref.Position[Rig.ToeLeft] - Ref.Position[Rig.LegLeft.End]);
		if (Rig.ToeRight >= 0) Forward += Horizontal(Ref.Position[Rig.ToeRight] - Ref.Position[Rig.LegRight.End]);
		Frame.Forward = Forward.GetSafeNormal(SMALL_NUMBER, FVector(1, 0, 0));

		// 왼쪽: 오른 허벅지 → 왼 허벅지. 앞과 수직이 되게 다시 맞춘다
		FVector Left = Horizontal(Ref.Position[Rig.LegLeft.Upper] - Ref.Position[Rig.LegRight.Upper]);
		Left = (Left - Frame.Forward * Left.Dot(Frame.Forward)).GetSafeNormal(SMALL_NUMBER, Frame.Up.Cross(Frame.Forward));
		Frame.Left = Left;

		// a × b 축으로 +각도 회전하면 a가 b 쪽으로 돈다
		Frame.Swing = Frame.Down.Cross(Frame.Forward).GetSafeNormal();
		Frame.Twist = Frame.Left.Cross(Frame.Forward).GetSafeNormal();
		Frame.Lean = Frame.Up.Cross(Frame.Forward).GetSafeNormal();
		Frame.Roll = Frame.Left.Cross(Frame.Up).GetSafeNormal();

		auto LegLength = [&Ref](const FResolvedLimb& Leg)
		{
			return (Ref.Position[Leg.Lower] - Ref.Position[Leg.Upper]).Size() + (Ref.Position[Leg.End] - Ref.Position[Leg.Lower]).Size();
		};
		Frame.LegLength = 0.5f * (LegLength(Rig.LegLeft) + LegLength(Rig.LegRight));
		return Frame;
	}

	// 레퍼런스 포즈의 T/A 포즈 팔을 거의 수직으로 내리는 회전 (컴포넌트 공간)
	FQuat ComputeArmLower(const FRefPose& Ref, const FResolvedLimb& Arm, const FRigFrame& Frame, float HangDeg)
	{
		const FVector Dir = (Ref.Position[Arm.Lower] - Ref.Position[Arm.Upper]).GetSafeNormal();
		const float Angle = std::acos(std::clamp(Dir.Dot(Frame.Down), -1.0f, 1.0f)) / DegToRad - HangDeg;
		const FVector Axis = Dir.Cross(Frame.Down);
		if (Angle <= 0.0f || Axis.SizeSquared() < 1.0e-8f)
		{
			return FQuat::Identity();
		}
		return AxisAngle(Axis.GetSafeNormal(), Angle);
	}

	float StrideLengthFor(const FRigFrame& Frame, const FProceduralWalkParams& Params)
	{
		if (!Params.bWalk) return 0.0f;
		// 반 주기(한 걸음) 동안 디딤발이 +A에서 −A로 지나간다. 디딤 무릎이 조금 굽은 만큼 줄인다
		const float Step = 2.0f * Frame.LegLength * std::sin(Params.ThighSwingDeg * DegToRad);
		return 2.0f * Step * std::cos(Params.KneeBaseDeg * DegToRad);
	}
}

bool ProceduralWalkAnim::AnalyzeRig(const TArray<FBoneInfo>& Bones, const FProceduralWalkRig& Rig, const FProceduralWalkParams& Params,
	FProceduralWalkInfo& OutInfo, FString& OutError)
{
	FResolvedRig Resolved;
	if (!ResolveRig(Bones, Rig, Resolved, OutError))
	{
		return false;
	}
	const FRefPose Ref = ComputeRefPose(Bones);
	const FRigFrame Frame = ComputeFrame(Ref, Resolved);
	OutInfo.Forward = Frame.Forward;
	OutInfo.ForwardYawDeg = std::atan2(Frame.Forward.Y, Frame.Forward.X) / DegToRad;
	OutInfo.LegLength = Frame.LegLength;
	OutInfo.StrideLength = StrideLengthFor(Frame, Params);
	OutInfo.CycleSeconds = Params.CycleSeconds;
	return true;
}

bool ProceduralWalkAnim::Build(const TArray<FBoneInfo>& Bones, const FProceduralWalkRig& Rig, const FProceduralWalkParams& Params,
	const FString& ClipName, UAnimSequence& OutSequence, FString& OutError)
{
	FResolvedRig R;
	if (!ResolveRig(Bones, Rig, R, OutError))
	{
		return false;
	}

	const int32 NumBones = Bones.Num();
	const FRefPose Ref = ComputeRefPose(Bones);
	const FRigFrame Frame = ComputeFrame(Ref, R);
	const FQuat LowerLeft = ComputeArmLower(Ref, R.ArmLeft, Frame, Params.ArmHangDeg);
	const FQuat LowerRight = ComputeArmLower(Ref, R.ArmRight, Frame, Params.ArmHangDeg);

	// 같은 부모 아래에 "<이름>_EX" 보조 본이 있으면 (스킨이 그쪽에 붙은 리그) 같은 회전을 준다
	TArray<int32> Twin;
	Twin.Init(-1, NumBones);
	for (int32 Index = 0; Index < NumBones; ++Index)
	{
		const int32 Found = FindBone(Bones, Bones[Index].Name + "_EX");
		if (Found >= 0 && Found != Index && Bones[Found].ParentIndex == Bones[Index].ParentIndex)
		{
			Twin[Index] = Found;
		}
	}

	const int32 FPS = std::max(1, Params.FramesPerSecond);
	const int32 NumFrames = std::max(2, static_cast<int32>(std::lround(std::max(0.1f, Params.CycleSeconds) * FPS)));
	const int32 NumKeys = NumFrames + 1;   // 마지막 키 = 첫 키 → 이음매 없이 반복
	const float PlayLength = static_cast<float>(NumFrames) / static_cast<float>(FPS);

	TArray<FRawAnimSequenceTrack> Keys;
	Keys.SetNum(NumBones);
	TArray<uint8> bTracked;
	bTracked.Init(0, NumBones);
	bTracked[R.Root] = 1;

	TArray<FQuat> Rel;          // 부모 기준 추가 회전 (레퍼런스 컴포넌트 축)
	TArray<uint8> bHasRel;
	TArray<FQuat> Delta;        // 컴포넌트 공간 누적 회전 D
	Rel.SetNum(NumBones);
	bHasRel.SetNum(NumBones);
	Delta.SetNum(NumBones);

	const float Pi2 = 2.0f * PI;
	for (int32 Key = 0; Key < NumKeys; ++Key)
	{
		const float Phase = Pi2 * static_cast<float>(Key % NumFrames) / static_cast<float>(NumFrames);
		const float S = std::sin(Phase);
		const float C = std::cos(Phase);

		for (int32 Index = 0; Index < NumBones; ++Index)
		{
			Rel[Index] = FQuat::Identity();
			bHasRel[Index] = 0;
		}
		auto SetRel = [&](int32 Index, const FQuat& Q)
		{
			if (Index < 0) return;
			Rel[Index] = Q;
			bHasRel[Index] = 1;
			bTracked[Index] = 1;
			if (Twin[Index] >= 0)
			{
				Rel[Twin[Index]] = Q;
				bHasRel[Twin[Index]] = 1;
				bTracked[Twin[Index]] = 1;
			}
		};

		FVector RootOffset = FVector::ZeroVector;
		const int32 NumSpine = std::max(1, R.Spine.Num());

		if (Params.bWalk)
		{
			// 왼다리 위상 Phase, 오른다리 Phase + π. 앞으로 옮기는 다리(cos > 0)는 무릎을 접는다
			const float ThighL = Params.ThighSwingDeg * S;
			const float ThighR = -ThighL;
			const float KneeL = Params.KneeBaseDeg + Params.KneeBendDeg * std::max(0.0f, C);
			const float KneeR = Params.KneeBaseDeg + Params.KneeBendDeg * std::max(0.0f, -C);

			SetRel(R.Pelvis, AxisAngle(Frame.Twist, Params.PelvisTwistDeg * S) * AxisAngle(Frame.Roll, -Params.PelvisRollDeg * C));

			auto Leg = [&](const FResolvedLimb& Limb, float Thigh, float Knee)
			{
				SetRel(Limb.Upper, AxisAngle(Frame.Swing, Thigh));
				SetRel(Limb.Lower, AxisAngle(Frame.Swing, -Knee));
				SetRel(Limb.End, AxisAngle(Frame.Swing, Params.FootFollow * Thigh - Thigh + Knee));
			};
			Leg(R.LegLeft, ThighL, KneeL);
			Leg(R.LegRight, ThighR, KneeR);

			for (int32 Index : R.Spine)
			{
				SetRel(Index, AxisAngle(Frame.Twist, -Params.SpineCounterTwistDeg * S / NumSpine)
					* AxisAngle(Frame.Lean, Params.SpineLeanDeg / NumSpine));
			}
			SetRel(R.Head, AxisAngle(Frame.Twist, Params.SpineCounterTwistDeg * S)
				* AxisAngle(Frame.Lean, -Params.SpineLeanDeg + Params.HeadNodDeg * std::cos(2.0f * Phase)));

			auto Arm = [&](const FResolvedLimb& Limb, const FQuat& Lower, float Swing)
			{
				const float Forwardness = Params.ArmSwingDeg > 0.0f ? Swing / Params.ArmSwingDeg : 0.0f;
				const float Elbow = Params.ElbowBaseDeg + Params.ElbowSwingDeg * std::max(0.0f, Forwardness);
				SetRel(Limb.Upper, AxisAngle(Frame.Swing, Swing) * Lower);
				SetRel(Limb.Lower, Lower.Inverse() * AxisAngle(Frame.Swing, Elbow) * Lower);
			};
			Arm(R.ArmLeft, LowerLeft, -Params.ArmSwingDeg * S);
			Arm(R.ArmRight, LowerRight, Params.ArmSwingDeg * S);

			// 디딤 다리가 기울어진 만큼 골반을 내려 발이 바닥에 붙어 있게 한다 (두 발이 가장 벌어질 때 가장 낮다)
			const float StanceAngle = Params.ThighSwingDeg * std::fabs(S) * DegToRad;
			RootOffset = Frame.Down * (Frame.LegLength * (1.0f - std::cos(StanceAngle)) * Params.BobScale);
		}
		else
		{
			// 대기: 숨쉬기. 상체가 살짝 펴졌다 숙여지고, 팔이 따라 흔들린다
			const float Breath = Params.BreathDeg * S;
			for (int32 Index : R.Spine)
			{
				SetRel(Index, AxisAngle(Frame.Lean, (Params.SpineLeanDeg * 0.5f - Breath) / NumSpine));
			}
			SetRel(R.Head, AxisAngle(Frame.Lean, -Params.SpineLeanDeg * 0.5f + Breath * 0.5f));
			SetRel(R.LegLeft.Lower, AxisAngle(Frame.Swing, -Params.KneeBaseDeg));
			SetRel(R.LegRight.Lower, AxisAngle(Frame.Swing, -Params.KneeBaseDeg));
			auto Arm = [&](const FResolvedLimb& Limb, const FQuat& Lower, float Sway)
			{
				SetRel(Limb.Upper, AxisAngle(Frame.Swing, Sway) * Lower);
				SetRel(Limb.Lower, Lower.Inverse() * AxisAngle(Frame.Swing, Params.ElbowBaseDeg + Sway) * Lower);
			};
			Arm(R.ArmLeft, LowerLeft, Params.IdleArmSwayDeg * C);
			Arm(R.ArmRight, LowerRight, Params.IdleArmSwayDeg * C);
			RootOffset = Frame.Down * (Frame.LegLength * 0.004f * (1.0f - C) * Params.BobScale);
		}

		// 부모가 먼저 오므로 앞에서부터 누적한다: D = Dparent · Rel
		for (int32 Index = 0; Index < NumBones; ++Index)
		{
			const int32 Parent = Bones[Index].ParentIndex;
			const FQuat ParentDelta = Parent >= 0 ? Delta[Parent] : FQuat::Identity();
			Delta[Index] = bHasRel[Index] ? (ParentDelta * Rel[Index]).Normalized() : ParentDelta;
		}

		// 컴포넌트 공간 D를 본 로컬로: Lnew = Qp⁻¹ · Dp⁻¹ · D · Qp · Lref
		for (int32 Index = 0; Index < NumBones; ++Index)
		{
			if (!bTracked[Index]) continue;
			const FTransform& Local = Bones[Index].LocalBindPose;
			const int32 Parent = Bones[Index].ParentIndex;
			const FQuat ParentRef = Parent >= 0 ? Ref.Rotation[Parent] : FQuat::Identity();
			const FQuat ParentDelta = Parent >= 0 ? Delta[Parent] : FQuat::Identity();

			FRawAnimSequenceTrack& Track = Keys[Index];
			Track.RotKeys.Add((ParentRef.Inverse() * ParentDelta.Inverse() * Delta[Index] * ParentRef * Local.Rotation).Normalized());
			FVector Location = Local.Location;
			if (Index == R.Root)
			{
				const float ParentScale = Parent >= 0 ? Ref.Scale[Parent] : 1.0f;
				Location += ParentRef.Inverse().RotateVector(RootOffset) / (ParentScale != 0.0f ? ParentScale : 1.0f);
			}
			Track.PosKeys.Add(Location);
			Track.ScaleKeys.Add(Local.Scale);
		}
	}

	// 모든 키에서 같은 본 집합을 움직이므로 트랙마다 키 개수가 같다 (.anim.bin은 트랙끼리 키 수가 같아야 한다)
	TArray<FBoneAnimationTrack> Tracks;
	for (int32 Index = 0; Index < NumBones; ++Index)
	{
		if (!bTracked[Index]) continue;
		FRawAnimSequenceTrack& Track = Keys[Index];
		if (Track.RotKeys.Num() != NumKeys)
		{
			OutError = "Inconsistent key count on bone " + Bones[Index].Name;
			return false;
		}
		FBoneAnimationTrack Out;
		Out.Name = Bones[Index].Name;
		Out.InternalTrack = std::move(Track);
		Tracks.Add(std::move(Out));
	}
	if (Tracks.IsEmpty())
	{
		OutError = "No tracks generated";
		return false;
	}

	OutSequence.Initialize(ClipName, std::move(Tracks), FFrameRate(FPS, 1), PlayLength);
	return true;
}

UAnimSequence* ProceduralWalkAnim::LoadOrGenerate(USkeletalMesh* Mesh, const FString& MeshAssetPath, const FString& AssetPath,
	const FProceduralWalkRig& Rig, const FProceduralWalkParams& Params, bool bForceRegenerate)
{
	if (!Mesh)
	{
		return nullptr;
	}

	// 이번 실행에서 이미 만든 시퀀스 (다시 만들라고 해도 실행마다 한 번만 만든다)
	static std::unordered_map<FString, UAnimSequence*> Generated;
	if (auto It = Generated.find(AssetPath); It != Generated.end())
	{
		return It->second;
	}

	namespace fs = std::filesystem;
	const fs::path File = FPaths::ProjectContentDir() / FPaths::FromUtf8(AssetPath);
	std::error_code Ec;
	if (!bForceRegenerate && fs::exists(File, Ec))
	{
		if (UAnimSequence* Loaded = UAssetManager::LoadAnimSequence(AssetPath))
		{
			return Loaded;
		}
	}

	UAnimSequence* Sequence = NewObject<UAnimSequence>();
	FString Error;
	const FString ClipName = FPaths::ToUtf8(File.filename()).substr(0, FPaths::ToUtf8(File.filename()).find('.'));
	if (!Build(Mesh->GetRefSkeleton(), Rig, Params, ClipName, *Sequence, Error))
	{
		KRS_LOG(Warning, "Procedural animation failed: {} ({})", AssetPath, Error);
		delete Sequence;
		return nullptr;
	}
	Sequence->SetPreviewMeshPath(MeshAssetPath);

	fs::create_directories(File.parent_path(), Ec);
	if (AnimSequenceFile::Write(File, *Sequence, Error))
	{
		KRS_LOG(Info, "Generated procedural animation: {}", AssetPath);
	}
	else
	{
		KRS_LOG(Warning, "Could not save procedural animation {} ({}); using it in memory only", AssetPath, Error);
	}

	Generated[AssetPath] = Sequence;
	return Sequence;
}

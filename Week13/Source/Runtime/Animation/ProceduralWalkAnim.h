#pragma once

#include "Containers/Array.h"
#include "Core/EngineString.h"
#include "Math/Vector.h"

struct FBoneInfo;
class UAnimSequence;
class USkeletalMesh;

// 스키닝된 스켈레톤만 있고 애니메이션 클립이 없는 캐릭터에 걷기/대기 클립을 코드로 만들어 준다.
// 본 축이 메시마다 달라도 되도록 모든 회전을 "레퍼런스 포즈의 컴포넌트 공간" 축(앞/위/옆)으로 정의하고,
// 본마다 로컬 회전으로 바꿔 .anim.bin 트랙으로 굽는다. (Lnew = Qp⁻¹ · Dp⁻¹ · D · Qp · Lref)
struct FProceduralLimbBones
{
	FString Upper;   // 허벅지 / 위팔
	FString Lower;   // 종아리 / 아래팔
	FString End;     // 발목 / 손목
};

struct FProceduralWalkRig
{
	FString Root;              // 몸 전체 bob(상하 이동)을 줄 본. 이 본의 조상은 움직이지 않는다
	FString Pelvis;            // 골반 비틀기. 다리의 부모 쪽
	TArray<FString> Spine;     // 아래에서 위 순서. 골반과 반대로 비튼다
	FString Head;
	FProceduralLimbBones LegLeft, LegRight;
	FProceduralLimbBones ArmLeft, ArmRight;
	FString ToeLeft, ToeRight; // 메시의 앞 방향을 발끝으로 알아낸다
};

struct FProceduralWalkParams
{
	bool bWalk = true;            // false면 제자리 대기(숨쉬기)
	float CycleSeconds = 1.0f;    // 왼발→오른발→왼발 한 주기
	int32 FramesPerSecond = 30;

	float ThighSwingDeg = 26.0f;  // 허벅지 앞뒤 진폭
	float KneeBaseDeg = 4.0f;     // 디딤 다리도 살짝 굽힌다
	float KneeBendDeg = 48.0f;    // 다리를 앞으로 옮길 때 무릎을 접는 최대 각
	float FootFollow = 0.35f;     // 발끝이 허벅지 각도를 따라가는 비율 (뒤꿈치 착지 / 발끝 떼기)

	float ArmHangDeg = 10.0f;     // 팔을 내렸을 때 수직에서 벌어진 각 (T포즈 → 차렷)
	float ArmSwingDeg = 22.0f;
	float ElbowBaseDeg = 12.0f;
	float ElbowSwingDeg = 18.0f;

	float PelvisTwistDeg = 7.0f;
	float SpineCounterTwistDeg = 9.0f;
	float SpineLeanDeg = 4.0f;    // 앞으로 숙임
	float PelvisRollDeg = 3.0f;   // 디딤발 쪽으로 골반이 기운다
	float HeadNodDeg = 2.0f;
	float BobScale = 1.0f;        // 다리 길이로 계산한 상하 이동의 배율

	float BreathDeg = 1.5f;       // 대기: 상체 들숨/날숨
	float IdleArmSwayDeg = 2.0f;  // 대기: 팔 흔들림
};

// 리그를 분석한 결과. 이동 속도를 걸음 폭에 맞출 때 쓴다 (메시 단위)
struct FProceduralWalkInfo
{
	FVector Forward = FVector(1, 0, 0);   // 컴포넌트 공간에서 메시가 보는 방향 (수평)
	float ForwardYawDeg = 0.0f;           // atan2(Forward.Y, Forward.X)
	float LegLength = 0.0f;               // 허벅지 관절 → 발목
	float StrideLength = 0.0f;            // 한 주기(두 걸음) 동안 몸이 나아가는 거리
	float CycleSeconds = 1.0f;
};

namespace ProceduralWalkAnim
{
	// 본 이름 확인 + 앞 방향/다리 길이 계산
	bool AnalyzeRig(const TArray<FBoneInfo>& Bones, const FProceduralWalkRig& Rig, const FProceduralWalkParams& Params,
		FProceduralWalkInfo& OutInfo, FString& OutError);

	// 메모리에 시퀀스를 만든다
	bool Build(const TArray<FBoneInfo>& Bones, const FProceduralWalkRig& Rig, const FProceduralWalkParams& Params,
		const FString& ClipName, UAnimSequence& OutSequence, FString& OutError);

	// AssetPath(Content 기준, 예: "FBX/woody_high/Woody_Walk.anim.bin")가 있으면 불러오고,
	// 없거나 bForceRegenerate면 만들어 파일로 저장한 뒤 에셋 매니저로 불러온다.
	// 저장이 안 되는 환경(패키지 등)이면 메모리에 만든 시퀀스를 돌려준다.
	UAnimSequence* LoadOrGenerate(USkeletalMesh* Mesh, const FString& MeshAssetPath, const FString& AssetPath,
		const FProceduralWalkRig& Rig, const FProceduralWalkParams& Params, bool bForceRegenerate);
}

#pragma once

#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "Animation/ProceduralWalkAnim.h"
#include "ToyRoomActors.generated.h"

class USkeletalMeshComponent;
class UCameraComponent;
class UAnimSequence;
class AToyRoomHumanActor;
class AWoodyToyActor;

// 장난감 방(ToyRoom.scene) 연출용 액터들.
// - AWoodyToyActor: 우디가 방 안을 돌아다니다 사람이 방에 들어오면 그 자리에서 랙돌로 쓰러진다 (장난감은 사람 앞에서 움직이지 않는다)
// - AToyRoomHumanActor: 정해진 시간이 지나면 문을 열고 들어와 우디 앞까지 걸어온 뒤 멈춰 선다
// - AToyRoomDirector: 장면을 따라가는 시네마틱 카메라 (씬의 첫 Pawn이라 PlayerController가 빙의한다)
// 두 캐릭터의 걷기/대기 클립은 ProceduralWalkAnim이 스켈레톤으로 만들어 .anim.bin으로 저장한다.

UCLASS()
class AWoodyToyActor : public AActor
{
	GENERATED_BODY()

public:
	AWoodyToyActor();

	static constexpr const char* DefaultMeshPath = "FBX/woody_high/woody_high.fbx.bin";
	static constexpr const char* WalkAnimPath = "FBX/woody_high/Woody_Walk.anim.bin";
	static constexpr const char* IdleAnimPath = "FBX/woody_high/Woody_Idle.anim.bin";

	void BeginPlay() override;
	void Tick(float DeltaTime) override;

	USkeletalMeshComponent* GetMesh() const { return MeshComponent; }

	// 사람을 보면 부른다. 애니메이션을 끄고 랙돌로 바꾼다. 걷던 속도를 바디에 넘겨 앞으로 고꾸라진다
	void GoLimp();
	bool IsLimp() const { return bLimp; }
	// 몸통(랙돌이면 물리 결과) 월드 위치. 카메라와 사람이 바라볼 곳
	FVector GetBodyLocation() const;

	// 메시 배율. 우디 메시는 키가 약 208 단위라 0.003이면 약 62cm 장난감이 된다
	UPROPERTY(EditAnywhere)
	float MeshScale = 0.003f;
	// 시작 위치를 중심으로 이 반지름(m)의 타원 경로를 돈다
	UPROPERTY(EditAnywhere)
	float WanderRadius = 1.0f;
	// 걸음 폭으로 계산한 속도에 곱한다 (1이면 발이 미끄러지지 않는다)
	UPROPERTY(EditAnywhere)
	float SpeedScale = 1.0f;
	UPROPERTY(EditAnywhere)
	float TurnRate = 110.0f;
	// 방에 들어온 사람이 이 거리(m) 안이면 쓰러진다
	UPROPERTY(EditAnywhere)
	float NoticeDistance = 8.0f;
	// 쓰러질 때 걷던 속도를 바디에 얼마나 남길지 (0이면 제자리에서 무너진다)
	UPROPERTY(EditAnywhere)
	float LimpMomentum = 0.8f;
	// 랙돌 안정화: 바디 질량을 중앙값의 [Min, Max]배로 묶는다 (질량비가 크면 조인트가 떤다), 솔버 위치 반복 횟수, 회전 감쇠
	UPROPERTY(EditAnywhere)
	float RagdollMassRangeMin = 0.35f;
	UPROPERTY(EditAnywhere)
	float RagdollMassRangeMax = 2.5f;
	UPROPERTY(EditAnywhere)
	int32 RagdollSolverIterations = 16;
	UPROPERTY(EditAnywhere)
	float RagdollAngularDamping = 0.6f;
	// 켜면 실행할 때마다 걷기/대기 클립을 다시 만들어 저장한다 (끄면 저장된 .anim.bin을 쓴다)
	UPROPERTY(EditAnywhere)
	bool bRegenerateAnimations = false;

	static FProceduralWalkRig MakeRig();
	static FProceduralWalkParams MakeWalkParams();

private:
	void ApplyMeshSetup();
	void StabilizeRagdollBodies();
	AToyRoomHumanActor* FindHumanInRoom() const;

	USkeletalMeshComponent* MeshComponent = nullptr;
	FProceduralWalkInfo WalkInfo;
	TArray<FVector> Waypoints;
	int32 WaypointIndex = 0;
	float Yaw = 0.0f;
	float WalkSpeed = 0.0f;   // m/s
	bool bLimp = false;
};

UCLASS()
class AToyRoomHumanActor : public AActor
{
	GENERATED_BODY()

public:
	AToyRoomHumanActor();

	static constexpr const char* DefaultMeshPath = "FBX/All_Might/All_Might.fbx.bin";
	static constexpr const char* WalkAnimPath = "FBX/All_Might/AllMight_Walk.anim.bin";
	static constexpr const char* IdleAnimPath = "FBX/All_Might/AllMight_Idle.anim.bin";

	enum class EPhase : uint8 { Waiting, OpeningDoor, Entering, Approaching, Standing };

	void BeginPlay() override;
	void Tick(float DeltaTime) override;

	USkeletalMeshComponent* GetMesh() const { return MeshComponent; }
	EPhase GetPhase() const { return Phase; }
	float GetPhaseTime() const { return PhaseTime; }
	// 문이 반쯤 열린 뒤부터 장난감이 사람을 "본다"
	bool IsInRoom() const;
	FVector GetHeadLocation() const;
	float GetDoorOpenAlpha() const { return DoorAlpha; }
	AActor* GetDoorActor() const { return Door; }

	UPROPERTY(EditAnywhere)
	float MeshScale = 0.003f;
	// 플레이 시작 후 문을 열기까지 (초)
	UPROPERTY(EditAnywhere)
	float EnterDelay = 9.0f;
	UPROPERTY(EditAnywhere)
	float DoorOpenSeconds = 1.4f;
	UPROPERTY(EditAnywhere)
	float DoorOpenAngle = 95.0f;
	// 이 이름의 액터를 문으로 쓴다. 루트가 경첩 위치여야 한다
	UPROPERTY(EditAnywhere)
	FString DoorActorName = "DoorHinge";
	// 문을 지나 들어설 지점 (월드, m)
	UPROPERTY(EditAnywhere)
	FVector EntryPoint = FVector(0.0f, 0.0f, 0.0f);
	// 우디 앞 이 거리(m)에서 멈춘다
	UPROPERTY(EditAnywhere)
	float StopDistance = 0.9f;
	UPROPERTY(EditAnywhere)
	float SpeedScale = 1.0f;
	UPROPERTY(EditAnywhere)
	bool bRegenerateAnimations = false;

	static FProceduralWalkRig MakeRig();
	static FProceduralWalkParams MakeWalkParams();

private:
	void ApplyMeshSetup();
	void SetPhase(EPhase NewPhase);
	bool WalkToward(const FVector& Target, float DeltaTime, float ArriveDistance);
	void UpdateDoor();

	USkeletalMeshComponent* MeshComponent = nullptr;
	UAnimSequence* WalkAnim = nullptr;
	UAnimSequence* IdleAnim = nullptr;
	AActor* Door = nullptr;
	AWoodyToyActor* Woody = nullptr;
	FRotator DoorClosedRotation;
	FProceduralWalkInfo WalkInfo;
	EPhase Phase = EPhase::Waiting;
	float PhaseTime = 0.0f;
	float DoorAlpha = 0.0f;
	float Yaw = 0.0f;
	float WalkSpeed = 0.0f;
};

UCLASS()
class AToyRoomDirector : public APawn
{
	GENERATED_BODY()

public:
	AToyRoomDirector();

	void BeginPlay() override;
	void Tick(float DeltaTime) override;

	// 카메라가 목표 위치/시선을 따라가는 빠르기 (클수록 즉각적)
	UPROPERTY(EditAnywhere)
	float CameraSmoothing = 2.2f;
	// 우디를 도는 궤도 반지름(m)과 높이(m)
	UPROPERTY(EditAnywhere)
	float OrbitRadius = 1.5f;
	UPROPERTY(EditAnywhere)
	float OrbitHeight = 0.45f;
	UPROPERTY(EditAnywhere)
	float OrbitSpeed = 12.0f;   // deg/s
	UPROPERTY(EditAnywhere)
	float FieldOfView = 55.0f;

private:
	void FindActors();
	void ComputeShot(float DeltaTime, FVector& OutLocation, FVector& OutTarget) const;

	UCameraComponent* CameraComponent = nullptr;
	AWoodyToyActor* Woody = nullptr;
	AToyRoomHumanActor* Human = nullptr;
	FVector CameraLocation = FVector::ZeroVector;
	FVector CameraTarget = FVector::ZeroVector;
	float OrbitAngle = 200.0f;
	float Time = 0.0f;
	bool bHasCamera = false;
};

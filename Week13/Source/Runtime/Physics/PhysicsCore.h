#pragma once

#include "Core/Types.h"
#include "Core/EngineString.h"
#include "Core/EngineLog.h"
#include "Containers/Containers.h"
#include <mutex>
#include <thread>

// PhysX 헤더를 끌어오지 않도록 전방 선언만 둔다.
namespace physx
{
	class PxFoundation;
	class PxPhysics;
	class PxPvd;
	class PxPvdTransport;
	class PxMaterial;
	class PxDefaultCpuDispatcher;
}

class FPhysicsCore
{
public:
	static FPhysicsCore& Get() { static FPhysicsCore Instance; return Instance; }

	bool Init();
	void Shutdown();
	bool IsInitialized() const { return Physics != nullptr; }

	physx::PxPhysics* GetPhysics() const { return Physics; }
	physx::PxMaterial* GetDefaultMaterial() const { return DefaultMaterial; }
	physx::PxDefaultCpuDispatcher* GetDispatcher() const { return Dispatcher; }
	physx::PxPvd* GetPvd() const { return Pvd; }

	// 종료 순서 검증용. FPhysScene 생성자·소멸자가 올리고 내린다.
	void OnSceneCreated() { ++LiveSceneCount; }
	void OnSceneDestroyed() { --LiveSceneCount; }

	// PhysX 에러 콜백이 넘긴 메시지를 기록한다. 메인 스레드면 바로 로그로 보내고,
	// 워커 스레드(simulate 중)면 대기열에 쌓는다. FLog의 sink 호출은 스레드 안전하지 않다.
	void EnqueueLog(ELogVerbosity Verbosity, FString Message);
	// 대기열의 메시지를 메인 스레드에서 로그로 보낸다. fetchResults 직후에 부른다.
	void FlushPendingLogs();

	FPhysicsCore(const FPhysicsCore&) = delete;
	FPhysicsCore& operator=(const FPhysicsCore&) = delete;

private:
	FPhysicsCore() = default;
	~FPhysicsCore() = default;   // 해제는 Shutdown이 한다. static 소멸 순서에 맡기지 않는다.

	bool InitPvd();              // 연결 실패는 false를 돌려주지만 Init을 실패시키지 않는다.

	physx::PxFoundation* Foundation = nullptr;
	physx::PxPvd* Pvd = nullptr;
	physx::PxPvdTransport* PvdTransport = nullptr;
	physx::PxPhysics* Physics = nullptr;
	physx::PxMaterial* DefaultMaterial = nullptr;
	physx::PxDefaultCpuDispatcher* Dispatcher = nullptr;

	bool bExtensionsInitialized = false;   // PxInitExtensions 성공 여부 (Shutdown에서 PxCloseExtensions 짝)
	bool bVehicleSDKInitialized = false;   // PxInitVehicleSDK 성공 여부 (PxCloseVehicleSDK 짝)
	int32 LiveSceneCount = 0;

	struct FPendingLog
	{
		ELogVerbosity Verbosity;
		FString Message;
	};
	std::thread::id MainThreadId;          // Init에서 기록한다.
	std::mutex PendingLogMutex;
	TArray<FPendingLog> PendingLogs;       // PendingLogMutex로 보호한다.
};
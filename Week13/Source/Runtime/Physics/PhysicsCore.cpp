#include "EnginePCH.h"
#include "PhysicsCore.h"
#include "PhysXIncludes.h"

// PhysX가 Foundation 생성에 요구하는 어댑터만 둔다. 상태(대기열 등)는 FPhysicsCore가 갖는다.
// 둘 다 Foundation보다 오래 살아야 하므로 파일 범위 객체로 둔다.
namespace
{
	physx::PxDefaultAllocator Allocator;

	class FPhysXErrorCallback : public physx::PxErrorCallback
	{
	public:
		void reportError(physx::PxErrorCode::Enum Code, const char* Message, const char* File, int Line) override
		{
			ELogVerbosity Verbosity = ELogVerbosity::Error;
			switch (Code)
			{
			case physx::PxErrorCode::eDEBUG_INFO:
				Verbosity = ELogVerbosity::Info;
				break;
			case physx::PxErrorCode::eDEBUG_WARNING:
			case physx::PxErrorCode::ePERF_WARNING:
				Verbosity = ELogVerbosity::Warning;
				break;
			default:
				break;
			}
			FPhysicsCore::Get().EnqueueLog(Verbosity, std::format("[PhysX] {} ({}:{})", Message, File, Line));
		}
	};

	FPhysXErrorCallback ErrorCallback;
}

bool FPhysicsCore::Init()
{
	MainThreadId = std::this_thread::get_id();
	if (Foundation) 
		return true;
	Foundation = PxCreateFoundation(PX_PHYSICS_VERSION, Allocator, ErrorCallback);

	if (!Foundation)
	{
		KRS_LOG(Error, "PxCreateFoundation failed!");
		return false;
	}

	KRS_LOG(Info, "PhysX Foundation created.");
	return true;
}

void FPhysicsCore::EnqueueLog(ELogVerbosity Verbosity, FString Message)
{
	if (std::this_thread::get_id() == MainThreadId)
	{
		FLog::Emit(Verbosity, Message);
		return;
	}

	std::lock_guard<std::mutex> Lock(PendingLogMutex);
	PendingLogs.Add(FPendingLog{ Verbosity, std::move(Message) });
}

void FPhysicsCore::FlushPendingLogs()
{
	// 락을 잡은 채 로그를 보내지 않도록 먼저 꺼내 온다.
	TArray<FPendingLog> Logs;
	{
		std::lock_guard<std::mutex> Lock(PendingLogMutex);
		Logs = std::move(PendingLogs);
		PendingLogs.Empty();
	}
	for (const FPendingLog& Log : Logs)
	{
		FLog::Emit(Log.Verbosity, Log.Message);
	}
}

void FPhysicsCore::Shutdown()
{
	if (LiveSceneCount > 0)
	{
		KRS_LOG(Warning, "FPhysicsCore::Shutdown called while {} PhysX scenes are still alive!", LiveSceneCount);
	}
	if (Dispatcher)
	{
		Dispatcher->release();
		Dispatcher = nullptr;
	}
	if (DefaultMaterial)
	{
		DefaultMaterial->release();
		DefaultMaterial = nullptr;
	}
	if (Physics)
	{
		Physics->release();
		Physics = nullptr;
	}
	// Pvd가 release 중에 Transport로 연결을 끊으므로 Pvd를 먼저 해제한다.
	if (Pvd)
	{
		Pvd->release();
		Pvd = nullptr;
	}
	if (PvdTransport)
	{
		PvdTransport->release();
		PvdTransport = nullptr;
	}
	if (Foundation)
	{
		Foundation->release();
		Foundation = nullptr;
	}
	// 해제 도중 생긴 메시지(누수 보고 등)까지 내보낸다.
	FlushPendingLogs();
	KRS_LOG(Info, "PhysX shutdown complete.");
}

bool FPhysicsCore::InitPvd()
{ 
	return false;
}
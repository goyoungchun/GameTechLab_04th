#include "EnginePCH.h"
#include "GameFramework/MyPlayerController.h"
#include "GameFramework/Actor/MyCharacter.h"
#include "GameFramework/Actor/KurisuCharacter.h"
#include "Camera/PlayerCameraManager.h"
#include "Camera/CameraModifier_Photo.h"
#include "UI/ViewfinderWidget.h"

namespace
{
	constexpr float PhotoZoomSpeedMm = 40.0f; // Z/X를 누르고 있을 때 초당 렌즈 길이 변화
	constexpr float WheelNotch = 120.0f;      // WM_MOUSEWHEEL 한 칸
}

void AMyPlayerController::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!IsPhotoModeActive()) return;
	const float Zoom = static_cast<float>(IsKeyDown(static_cast<int32>(EKeyCode::X))) -
		static_cast<float>(IsKeyDown(static_cast<int32>(EKeyCode::Z)));
	if (Zoom != 0.0f)
		GetPhotoModifier(false)->AdjustZoom(Zoom * PhotoZoomSpeedMm * DeltaTime);
}

void AMyPlayerController::InputAxis(EGameInputAxis Axis, float Delta)
{
	Super::InputAxis(Axis, Delta);

	if (Axis == EGameInputAxis::MouseWheel && Delta != 0.0f && IsPhotoModeActive())
		GetPhotoModifier(false)->AdjustFocus(Delta / WheelNotch);
}

UCameraModifier_Photo* AMyPlayerController::GetPhotoModifier(bool bCreate)
{
	APlayerCameraManager* PCM = GetCameraManager();
	if (!PCM) return nullptr;

	auto* Modifier = static_cast<UCameraModifier_Photo*>(
		PCM->FindCameraModifierByClass(UCameraModifier_Photo::StaticClass()));
	if (!Modifier && bCreate)
	{
		Modifier = NewObject<UCameraModifier_Photo>(PCM);
		PCM->AddCameraModifier(Modifier);

		// 뷰파인더는 모디파이어 상태를 매 프레임 직접 읽으므로 여기서 포인터를 들고 있지 않는다.
		// PIE가 끝나면 뷰포트가 위젯을 지운다
		UViewfinderWidget* Viewfinder = NewObject<UViewfinderWidget>();
		Viewfinder->SetOwningWorld(GetWorld());
		Viewfinder->AddToViewport(100);
	}
	return Modifier;
}

bool AMyPlayerController::IsPhotoModeActive()
{
	UCameraModifier_Photo* Photo = GetPhotoModifier(false);
	return Photo && Photo->IsPhotoModeActive();
}

bool AMyPlayerController::HandlePhotoKey(int32 Key)
{
	if (Key == static_cast<int32>(EKeyCode::C))
	{
		if (UCameraModifier_Photo* Photo = GetPhotoModifier(true))
			Photo->TogglePhotoMode();
		return true;
	}

	if (!IsPhotoModeActive()) return false;
	UCameraModifier_Photo* Photo = GetPhotoModifier(false);

	// 위/아래: 조리개를 조이고 연다. 오른쪽/왼쪽: 셔터를 빠르게/느리게
	if (Key == static_cast<int32>(EKeyCode::Up))    { Photo->StepAperture(+1); return true; }
	if (Key == static_cast<int32>(EKeyCode::Down))  { Photo->StepAperture(-1); return true; }
	if (Key == static_cast<int32>(EKeyCode::Right)) { Photo->StepShutter(+1);  return true; }
	if (Key == static_cast<int32>(EKeyCode::Left))  { Photo->StepShutter(-1);  return true; }

	// 카메라를 들고 있는 동안 좌클릭은 셔터(6단계)라서 공격으로 넘기지 않는다
	if (Key == static_cast<int32>(EKeyCode::LButton) || Key == static_cast<int32>(EKeyCode::RButton))
		return true;
	return false;
}

void AMyPlayerController::InputKey(int32 Key, bool bDown)
{
	// Super가 bKeyDown을 갱신하므로, 눌림 순간 판정은 호출 전에 이전 상태를 잡아 둔다
	const bool bJustPressed = bDown && !IsKeyDown(Key);
	Super::InputKey(Key, bDown);

	if (!bJustPressed) return;
	if (HandlePhotoKey(Key)) return;

	if (Key == static_cast<int32>(EKeyCode::LButton))
	{
		// Kurisu는 좌클릭 잽, 다른 캐릭터는 기존 공격
		if (AKurisuCharacter* Kurisu = Cast<AKurisuCharacter>(GetPawn()))
		{
			Kurisu->Jab();
		}
		else if (AMyCharacter* MyCharacter = Cast<AMyCharacter>(GetPawn()))
		{
			MyCharacter->Attack();
		}
	}

	if (Key == static_cast<int32>(EKeyCode::RButton))
	{
		if (AKurisuCharacter* Kurisu = Cast<AKurisuCharacter>(GetPawn()))
		{
			Kurisu->Punch();
		}
	}

	if (Key == static_cast<int32>(EKeyCode::Space))
	{
		if (AMyCharacter* MyCharacter = Cast<AMyCharacter>(GetPawn()))
		{
			MyCharacter->Jump();
		}
	}

	// Kurisu 슬라이드: E / 구르기: R. Shift(Run)는 캐릭터 Tick이 키 상태를 읽는다
	if (Key == static_cast<int32>(EKeyCode::E))
	{
		if (AKurisuCharacter* Kurisu = Cast<AKurisuCharacter>(GetPawn()))
			Kurisu->Slide();
	}

	if (Key == static_cast<int32>(EKeyCode::R))
	{
		if (AKurisuCharacter* Kurisu = Cast<AKurisuCharacter>(GetPawn()))
			Kurisu->Roll();
	}
}

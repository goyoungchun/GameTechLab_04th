#include "EnginePCH.h"
#include "UI/ViewfinderWidget.h"

#include "Asset/AssetManager.h"
#include "Camera/CameraModifier_Photo.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Text/Font.h"

#include <algorithm>
#include <cmath>

namespace
{
	const FLinearColor MaskColor(0.01f, 0.01f, 0.01f, 1.0f);
	const FLinearColor PlateColor(0.08f, 0.08f, 0.08f, 1.0f);
	const FLinearColor LabelColor(0.82f, 0.82f, 0.80f, 1.0f);
	const FLinearColor FrameColor(0.95f, 0.93f, 0.88f, 0.85f);
	const FLinearColor NeedleColor(1.0f, 0.55f, 0.15f, 1.0f);
	const FLinearColor FlagColor(0.85f, 0.10f, 0.08f, 1.0f);
	const FLinearColor PatchColor(0.80f, 0.80f, 0.80f, 0.30f);

	const char* ShutterLabels[] = { "15", "30", "60", "125", "250", "500" };
	const char* ApertureLabels[] = { "2.8", "4", "5.6", "8", "11", "16", "22" };
	static_assert(sizeof(ShutterLabels) / sizeof(ShutterLabels[0]) == FPhotoCameraState::NumShutterStops);
	static_assert(sizeof(ApertureLabels) / sizeof(ApertureLabels[0]) == FPhotoCameraState::NumApertureStops);

	// 피벗이 눈금판에서 멀수록 바늘이 덜 기운다 (디자인 해상도 픽셀)
	constexpr float NeedlePivotDistance = 700.0f;

	FLinearColor WithOpacity(FLinearColor Color, float Opacity)
	{
		Color.A *= Opacity;
		return Color;
	}

	float LerpF(float A, float B, float T) { return A + (B - A) * T; }

	// 화면 좌표는 Y가 아래로 커지므로 90°가 아래, 270°가 위다
	void AddArc(FUIDrawList& DrawList, const FVector2& Center, float Radius, float StartDeg, float EndDeg,
		float Thickness, const FLinearColor& Color)
	{
		constexpr int32 Segments = 8;
		const float ToRad = 3.14159265f / 180.0f;
		FVector2 Prev = Center + FVector2(cosf(StartDeg * ToRad), sinf(StartDeg * ToRad)) * Radius;
		for (int32 i = 1; i <= Segments; ++i)
		{
			const float Deg = LerpF(StartDeg, EndDeg, static_cast<float>(i) / Segments);
			const FVector2 Next = Center + FVector2(cosf(Deg * ToRad), sinf(Deg * ToRad)) * Radius;
			DrawList.AddLine(Prev, Next, Thickness, Color);
			Prev = Next;
		}
	}

}

void FViewfinderNeedle::Update(float Target, float DeltaTime)
{
	if (!bInitialized)
	{
		Position = Target;
		Velocity = 0.0f;
		bInitialized = true;
		return;
	}

	// 감쇠비를 1보다 작게 두어 실제 계기 바늘처럼 살짝 넘쳤다가 돌아오게 한다
	constexpr float Stiffness = 180.0f;
	constexpr float DampingRatio = 0.55f;
	const float Damping = 2.0f * DampingRatio * std::sqrt(Stiffness);
	// 프레임이 크게 튀면 스프링이 발산하므로 한 번에 진행하는 시간을 제한한다
	const float Dt = std::min(DeltaTime, 1.0f / 30.0f);

	Velocity += (Stiffness * (Target - Position) - Damping * Velocity) * Dt;
	Position += Velocity * Dt;
}

UViewfinderWidget::UViewfinderWidget()
{
	// HUD라서 클릭을 가로채지 않는다
	Visibility = static_cast<int32>(EUIVisibility::HitTestInvisible);
}

void UViewfinderWidget::NativeConstruct()
{
	Font = UAssetManager::GetAssetByPath<UFont>("Engine/Fonts/Pretendard.json");
}

UCameraModifier_Photo* UViewfinderWidget::FindPhotoModifier() const
{
	UWorld* World = GetOwningWorld();
	APlayerController* PC = World ? World->GetPlayerController() : nullptr;
	APlayerCameraManager* PCM = PC ? PC->GetCameraManager() : nullptr;
	if (!PCM) return nullptr;
	return static_cast<UCameraModifier_Photo*>(PCM->FindCameraModifierByClass(UCameraModifier_Photo::StaticClass()));
}

void UViewfinderWidget::NativeTick(float DeltaTime)
{
	const UCameraModifier_Photo* Photo = FindPhotoModifier();
	if (!Photo) return;

	const FPhotoCameraState& State = Photo->GetState();
	ShutterNeedle.Update(static_cast<float>(State.ShutterIndex) / (FPhotoCameraState::NumShutterStops - 1), DeltaTime);
	ApertureNeedle.Update(static_cast<float>(State.ApertureIndex) / (FPhotoCameraState::NumApertureStops - 1), DeltaTime);
}

void UViewfinderWidget::PaintNeedle(FUIDrawList& DrawList, const FVector2& Pivot, const FVector2& Tip,
	float HalfLength, float Thickness, const FLinearColor& Color) const
{
	const FVector2 D = Tip - Pivot;
	const float Len = std::sqrt(D.X * D.X + D.Y * D.Y);
	if (Len <= 0.0f) return;
	const FVector2 Dir = D * (1.0f / Len);
	DrawList.AddLine(Tip - Dir * HalfLength, Tip + Dir * HalfLength, Thickness, Color);
}

void UViewfinderWidget::Paint(FUIDrawList& DrawList, float ParentOpacity) const
{
	if (!IsVisible()) return;
	const UCameraModifier_Photo* Photo = FindPhotoModifier();
	if (!Photo || Photo->GetAlpha() <= 0.0f) return;

	const float Opacity = ParentOpacity * RenderOpacity * Photo->GetAlpha();
	const FPhotoCameraState& State = Photo->GetState();
	const FUIRect& R = CachedRect;
	const float S = CachedScale;

	// ---------- 프레임과 창 ----------
	// 프레임 크기는 CameraModifier_Photo가 화각을 계산할 때와 같은 비율이어야 찍히는 범위가 맞는다
	const float FrameH = R.H * FPhotoCameraState::FrameHeightOnScreen;
	const float FrameW = FrameH * FPhotoCameraState::FilmAspect;
	const float CX = R.X + R.W * 0.5f;
	const float CY = R.Y + R.H * 0.5f;
	const float FL = CX - FrameW * 0.5f, FR = CX + FrameW * 0.5f;
	const float FT = CY - FrameH * 0.5f, FB = CY + FrameH * 0.5f;

	// 실제 거리계 카메라처럼 프레임선 바깥도 조금 보이게 창을 넓힌다
	const float Pad = 26.0f * S;
	const float WL = FL - Pad, WR = FR + Pad, WT = FT - Pad, WB = FB + Pad;

	const FLinearColor Mask = WithOpacity(MaskColor, Opacity);
	DrawList.AddRect({ R.X, R.Y, R.W, WT - R.Y }, Mask);
	DrawList.AddRect({ R.X, WB, R.W, R.Y + R.H - WB }, Mask);
	DrawList.AddRect({ R.X, WT, WL - R.X, WB - WT }, Mask);
	DrawList.AddRect({ WR, WT, R.X + R.W - WR, WB - WT }, Mask);

	// 모서리가 둥근 프레임선. 변 가운데는 비워 두어 피사체를 가리지 않는다 (LC-A·Elektra 스타일)
	{
		const FLinearColor Line = WithOpacity(FrameColor, Opacity);
		const float Th = 3.0f * S, Rad = 22.0f * S;
		const float SegW = FrameW * 0.3f, SegH = FrameH * 0.3f;

		AddArc(DrawList, FVector2(FL + Rad, FT + Rad), Rad, 180.0f, 270.0f, Th, Line);
		AddArc(DrawList, FVector2(FR - Rad, FT + Rad), Rad, 270.0f, 360.0f, Th, Line);
		AddArc(DrawList, FVector2(FR - Rad, FB - Rad), Rad, 0.0f, 90.0f, Th, Line);
		AddArc(DrawList, FVector2(FL + Rad, FB - Rad), Rad, 90.0f, 180.0f, Th, Line);

		DrawList.AddLine(FVector2(FL + Rad, FT), FVector2(FL + SegW, FT), Th, Line);
		DrawList.AddLine(FVector2(FR - Rad, FT), FVector2(FR - SegW, FT), Th, Line);
		DrawList.AddLine(FVector2(FL + Rad, FB), FVector2(FL + SegW, FB), Th, Line);
		DrawList.AddLine(FVector2(FR - Rad, FB), FVector2(FR - SegW, FB), Th, Line);
		DrawList.AddLine(FVector2(FL, FT + Rad), FVector2(FL, FT + SegH), Th, Line);
		DrawList.AddLine(FVector2(FL, FB - Rad), FVector2(FL, FB - SegH), Th, Line);
		DrawList.AddLine(FVector2(FR, FT + Rad), FVector2(FR, FT + SegH), Th, Line);
		DrawList.AddLine(FVector2(FR, FB - Rad), FVector2(FR, FB - SegH), Th, Line);
	}

	// 가운데 거리계 패치
	{
		const float PW = 84.0f * S, PH = 58.0f * S;
		DrawList.AddRect({ CX - PW * 0.5f, CY - PH * 0.5f, PW, PH }, WithOpacity(PatchColor, Opacity));
	}

	const FLinearColor Label = WithOpacity(LabelColor, Opacity);
	const FLinearColor Needle = WithOpacity(NeedleColor, Opacity);

	// ---------- 위: 셔터 눈금 (35RC) ----------
	{
		const float PW = 560.0f * S, PH = 40.0f * S;
		const float PL = CX - PW * 0.5f;
		const float PCY = R.Y + (WT - R.Y) * 0.5f;
		const float PT = PCY - PH * 0.5f;
		DrawList.AddRect({ PL, PT, PW, PH }, WithOpacity(PlateColor, Opacity));

		const float Inset = 44.0f * S;
		const float X0 = PL + Inset, X1 = PL + PW - Inset;
		for (int32 i = 0; i < FPhotoCameraState::NumShutterStops; ++i)
		{
			const float X = LerpF(X0, X1, static_cast<float>(i) / (FPhotoCameraState::NumShutterStops - 1));
			if (Font)
				DrawList.AddText({ X - 40.0f * S, PT, 80.0f * S, PH }, ShutterLabels[i], *Font, 22.0f * S, Label, 1);
		}

		// 피벗이 아래에 있어서 바늘이 양 끝으로 갈수록 기운다
		const FVector2 Tip(LerpF(X0, X1, ShutterNeedle.Position), PCY);
		const FVector2 Pivot(CX, PCY + NeedlePivotDistance * S);
		PaintNeedle(DrawList, Pivot, Tip, PH * 0.5f + 6.0f * S, 2.5f * S, Needle);
	}

	// ---------- 오른쪽: 조리개 눈금 (QL17) ----------
	{
		const float PW = 64.0f * S, PH = FrameH * 0.72f;
		const float PCX = WR + 60.0f * S;
		const float PL = PCX - PW * 0.5f, PT = CY - PH * 0.5f;
		DrawList.AddRect({ PL, PT, PW, PH }, WithOpacity(PlateColor, Opacity));

		// 위아래 빨간 칸은 노출계(5단계)가 범위를 벗어났을 때 바늘이 가리키는 자리다
		const float FlagH = 26.0f * S;
		const FLinearColor Flag = WithOpacity(FlagColor, Opacity);
		DrawList.AddRect({ PL, PT, PW, FlagH }, Flag);
		DrawList.AddRect({ PL, PT + PH - FlagH, PW, FlagH }, Flag);

		const float Y0 = PT + FlagH + 24.0f * S, Y1 = PT + PH - FlagH - 24.0f * S;
		for (int32 i = 0; i < FPhotoCameraState::NumApertureStops; ++i)
		{
			const float Y = LerpF(Y0, Y1, static_cast<float>(i) / (FPhotoCameraState::NumApertureStops - 1));
			if (Font)
				DrawList.AddText({ PL, Y - 15.0f * S, PW, 30.0f * S }, ApertureLabels[i], *Font, 22.0f * S, Label, 1);
		}

		const FVector2 Tip(PCX, LerpF(Y0, Y1, ApertureNeedle.Position));
		const FVector2 Pivot(PCX - NeedlePivotDistance * S, CY);
		PaintNeedle(DrawList, Pivot, Tip, PW * 0.5f + 6.0f * S, 2.5f * S, Needle);
	}

	// ---------- 아래 왼쪽: 필름·렌즈 정보 ----------
	// 아래 가운데는 4단계 LC-A 거리계 자리라 비워 둔다
	if (Font)
	{
		const FString Info = "ISO " + std::to_string(static_cast<int32>(State.ISO)) + "   " +
			std::to_string(static_cast<int32>(std::lround(State.FocalLength))) + "mm";
		const float BandH = R.Y + R.H - WB;
		DrawList.AddText({ WL, WB, 300.0f * S, BandH }, Info, *Font, 20.0f * S, Label, 0);
	}
}

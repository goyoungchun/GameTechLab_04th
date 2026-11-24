#include "EnginePCH.h"
#include "ConstraintInstance.h"

void FConstraintInstance::CopyConstraintParamsFrom(const FConstraintInstance& From)
{
	ConstraintBone1 = From.ConstraintBone1;
	ConstraintBone2 = From.ConstraintBone2;
	Pos1 = From.Pos1; PriAxis1 = From.PriAxis1; SecAxis1 = From.SecAxis1;
	Pos2 = From.Pos2; PriAxis2 = From.PriAxis2; SecAxis2 = From.SecAxis2;
	Swing1Motion = From.Swing1Motion;
	Swing2Motion = From.Swing2Motion;
	TwistMotion = From.TwistMotion;
	Swing1LimitDegrees = From.Swing1LimitDegrees;
	Swing2LimitDegrees = From.Swing2LimitDegrees;
	TwistLimitDegrees = From.TwistLimitDegrees;
	// ConstraintData는 복사하지 않는다 — 같은 조인트를 두 번 release하게 된다
}
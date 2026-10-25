#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_ACLIENT_INPUTS_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_ACLIENT_INPUTS_H

#include <base/vmath.h>

namespace AcInputs
{
	float EffectiveOffsetTicks(bool ForOthers = false);

	int PredictionTicks(float OffsetTicks);
	int PredictionTicksOthers(float OffsetTicks);

	void ApplyOffset(float OffsetTicks, int &Tick, float &Intra);

	float BestInterpolationAmount(float Fraction, float DeltaLength, bool Enable);
	vec2 BestInterpolate(vec2 PrevPos, vec2 CurPos, float Fraction, bool Enable);
	float BestInterpolate(float PrevPos, float CurPos, float Fraction, bool Enable);

	bool FastOthers();
	bool BestOthers();
	bool SaikoOthers();
	bool DeltaOthers();
	bool FOthers();
	bool CloudOthers();
	bool AnyOthers();
	bool ImmediateOthers();

	bool IsActive();
} // namespace AcInputs

#endif

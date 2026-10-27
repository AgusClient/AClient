#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_ACLIENT_INPUTS_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_ACLIENT_INPUTS_H

#include <base/vmath.h>

class CCollision;

namespace AcInputs
{
	float EffectiveOffsetTicks(bool ForOthers = false);

	int PredictionTicks(float OffsetTicks);
	int PredictionTicksOthers(float OffsetTicks);

	void ApplyOffset(float OffsetTicks, int &Tick, float &Intra);

	float BestInterpolationAmount(float Fraction, float DeltaLength, bool Enable);
	vec2 BestInterpolate(vec2 PrevPos, vec2 CurPos, float Fraction, bool Enable);
	float BestInterpolate(float PrevPos, float CurPos, float Fraction, bool Enable);

	// aX30 Advanced Features
	vec2 AX30_EvaluateTrajectory(vec2 P_prev, vec2 P0, vec2 P1, vec2 P_next, float Fraction);
	void UpdatePingSample(int PingMs);
	float GetAdaptiveJitterTicks();
	vec2 ClampCollision(vec2 P0, vec2 TargetPos, const ::CCollision *pCollision);

	bool FastOthers();
	bool BestOthers();
	bool SaikoOthers();
	bool DeltaOthers();
	bool FOthers();
	bool CloudOthers();
	bool AClientOthers();
	bool AX30Others();
	bool AnyOthers();
	bool ImmediateOthers();

	bool IsActive();
} // namespace AcInputs

#endif

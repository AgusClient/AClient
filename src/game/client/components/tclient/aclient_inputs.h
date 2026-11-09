#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_ACLIENT_INPUTS_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_ACLIENT_INPUTS_H

#include <base/vmath.h>
#include <cstdint>

class CCollision;

namespace AcInputs
{
	void SetPracticeActive(bool Active);
	bool IsPracticeActive();

	float EffectiveOffsetTicks(bool ForOthers = false);

	int PredictionTicks(float OffsetTicks);
	int PredictionTicksOthers(float OffsetTicks);

	void ApplyOffset(float OffsetTicks, int &Tick, float &Intra);

	float BestInterpolationAmount(float Fraction, float DeltaLength, bool Enable);
	vec2 BestInterpolate(vec2 PrevPos, vec2 CurPos, float Fraction, bool Enable);
	float BestInterpolate(float PrevPos, float CurPos, float Fraction, bool Enable);

	// aX30 Engine (Formula Original)
	vec2 AX30_EvaluateTrajectory(vec2 P_prev, vec2 P0, vec2 P1, vec2 P_next, float Fraction, int Mode);
	float AX30_GetAdaptiveLatencyOffset();
	vec2 AX30_FilterBoundary(vec2 P0, vec2 TargetPos, const ::CCollision *pCollision);

	// aXv2 Engine (aX30v2)
	void AXv2_Reset();
	vec2 AXv2_EvaluateTrajectory(vec2 P_prev, vec2 P0, vec2 P1, vec2 P_next, vec2 Vel0, vec2 Vel1, bool HasVel, float Fraction, int Mode);
	vec2 AXv2_EvaluateTrajectory(vec2 P_prev, vec2 P0, vec2 P1, vec2 P_next, float Fraction, int Mode);
	void AXv2_UpdateNetworkTiming(float RttMs, float InputMarginMs);
	void AXv2_UpdateSnapshotArrival(int64_t NowTicks, int64_t Freq, int TickDelta, int TickSpeed);
	void AXv2_AdvanceFrame(float FrameDeltaSeconds);
	float AXv2_GetAdaptiveLatencyOffset();
	vec2 AXv2_FilterBoundary(vec2 P0, vec2 TargetPos, const ::CCollision *pCollision);

	// Compatibility aliases / Common
	void AX30_Reset();
	vec2 AX30_EvaluateTrajectory(vec2 P_prev, vec2 P0, vec2 P1, vec2 P_next, vec2 Vel0, vec2 Vel1, bool HasVel, float Fraction, int Mode);
	void AX30_UpdateNetworkTiming(float RttMs, float InputMarginMs);
	void AX30_UpdateSnapshotArrival(int64_t NowTicks, int64_t Freq, int TickDelta, int TickSpeed);
	void AX30_AdvanceFrame(float FrameDeltaSeconds);
	void UpdatePingSample(int PingMs);

	bool FastOthers();
	bool BestOthers();
	bool SaikoOthers();
	bool DeltaOthers();
	bool FOthers();
	bool CloudOthers();
	bool AClientOthers();
	bool AX30Others();
	bool AXv2Others();
	bool AnyOthers();
	bool ImmediateOthers();

	bool IsActive();
} // namespace AcInputs

#endif

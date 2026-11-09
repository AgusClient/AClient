#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_AX30_AX30_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_AX30_AX30_H

#include <base/vmath.h>
#include <cstdint>

// aX30 Engine - Formula Original
namespace aX30
{
	vec2 EvaluateTrajectory(vec2 P_prev, vec2 P0, vec2 P1, vec2 P_next, float Fraction);
	void UpdatePingSample(int PingMs);
	float GetAdaptiveLatencyOffset();
}

// aX30 2.0 Engine (aXv2)
namespace aX30v2
{
	void Reset();
	vec2 EvaluateTrajectory(vec2 P_prev, vec2 P0, vec2 P1, vec2 P_next, vec2 Vel0, vec2 Vel1, bool HasVel, float Fraction);
	vec2 EvaluateTrajectory(vec2 P_prev, vec2 P0, vec2 P1, vec2 P_next, float Fraction);
	void UpdateNetworkTiming(float RttMs, float InputMarginMs);
	void UpdateSnapshotArrival(int64_t NowTicks, int64_t Freq, int TickDelta, int TickSpeed);
	void UpdatePingSample(int PingMs);
	void AdvanceFrame(float FrameDeltaSeconds);
	float GetAdaptiveLatencyOffset();
}

#endif

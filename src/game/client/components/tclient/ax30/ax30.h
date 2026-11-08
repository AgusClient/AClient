#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_AX30_AX30_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_AX30_AX30_H

#include <base/vmath.h>

namespace aX30
{
	vec2 EvaluateTrajectory(vec2 P_prev, vec2 P0, vec2 P1, vec2 P_next, float Fraction);
	void UpdatePingSample(int PingMs);
	float GetAdaptiveLatencyOffset();
}

#endif

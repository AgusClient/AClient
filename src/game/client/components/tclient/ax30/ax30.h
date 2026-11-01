#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_AX30_AX30_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_AX30_AX30_H

#include <base/vmath.h>

#if defined(CONF_AX30_PROPRIETARY)

namespace aX30
{
	vec2 EvaluateTrajectory(vec2 P_prev, vec2 P0, vec2 P1, vec2 P_next, float Fraction);
	void UpdatePingSample(int PingMs);
	float GetAdaptiveLatencyOffset();
}

#else

#include <algorithm>

namespace aX30
{
	inline vec2 EvaluateTrajectory(vec2 P_prev, vec2 P0, vec2 P1, vec2 P_next, float Fraction)
	{
		(void)P_prev;
		(void)P_next;
		const float t = std::clamp(Fraction, 0.0f, 1.0f);
		return mix(P0, P1, t);
	}

	inline void UpdatePingSample(int PingMs)
	{
		(void)PingMs;
	}

	inline float GetAdaptiveLatencyOffset()
	{
		return 0.0f;
	}
}

#endif

#endif

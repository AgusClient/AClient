#include "ax30.h"

#include <algorithm>
#include <cmath>

namespace aX30
{
	static int s_aPingHistory[16] = {0};
	static int s_PingHistoryIndex = 0;
	static int s_PingHistoryCount = 0;

	vec2 EvaluateTrajectory(vec2 P_prev, vec2 P0, vec2 P1, vec2 P_next, float Fraction)
	{
		const float t = std::clamp(Fraction, 0.0f, 1.0f);

		if(std::isnan(Fraction) || std::isinf(Fraction) ||
		   std::isnan(P0.x) || std::isnan(P0.y) || std::isnan(P1.x) || std::isnan(P1.y))
			return P0;

		if(distance(P0, P1) > 256.0f || distance(P_prev, P0) > 256.0f || distance(P1, P_next) > 256.0f)
			return mix(P0, P1, t);

		// Horner matrix evaluation for Hermite / Catmull-Rom cubic spline
		const vec2 c3 = (P0 - P1) * 1.5f + (P_next - P_prev) * 0.5f;
		const vec2 c2 = P_prev - P0 * 2.5f + P1 * 2.0f - P_next * 0.5f;
		const vec2 c1 = (P1 - P_prev) * 0.5f;
		const vec2 c0 = P0;

		return ((c3 * t + c2) * t + c1) * t + c0;
	}

	void UpdatePingSample(int PingMs)
	{
		if(PingMs <= 0 || PingMs > 2000)
			return;
		s_aPingHistory[s_PingHistoryIndex] = PingMs;
		s_PingHistoryIndex = (s_PingHistoryIndex + 1) % 16;
		if(s_PingHistoryCount < 16)
			s_PingHistoryCount++;
	}

	float GetAdaptiveLatencyOffset()
	{
		if(s_PingHistoryCount < 4)
			return 0.0f;

		float sum = 0.0f;
		for(int i = 0; i < s_PingHistoryCount; ++i)
			sum += (float)s_aPingHistory[i];

		const float inv = 1.0f / (float)s_PingHistoryCount;
		const float avg = sum * inv;

		float var_sum = 0.0f;
		for(int i = 0; i < s_PingHistoryCount; ++i)
		{
			const float d = (float)s_aPingHistory[i] - avg;
			var_sum += d * d;
		}

		const float delta = std::sqrt(std::max(0.0f, var_sum * inv));
		if(std::isnan(delta) || std::isinf(delta))
			return 0.0f;

		return std::clamp(delta * 0.05f, 0.0f, 0.35f);
	}
}

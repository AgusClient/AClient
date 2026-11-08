#include "ax30.h"

#include <algorithm>
#include <cmath>

namespace aX30
{
	namespace
	{
		constexpr int TIMING_RING_SIZE = 64;
		constexpr int PING_FALLBACK_SIZE = 16;

		// High-resolution RTT + server input margin samples from NETMSG_INPUTTIMING (~50 Hz)
		float s_aRttMs[TIMING_RING_SIZE] = {0.0f};
		float s_aInputMarginMs[TIMING_RING_SIZE] = {0.0f};
		int s_TimingIndex = 0;
		int s_TimingCount = 0;

		// High-resolution snapshot inter-arrival jitter (~25-50 Hz)
		float s_aSnapJitterMs[TIMING_RING_SIZE] = {0.0f};
		int s_SnapJitterIndex = 0;
		int s_SnapJitterCount = 0;
		int64_t s_LastSnapTicks = 0;

		// Deduplicated scoreboard ping fallback when NETMSG_INPUTTIMING is unavailable
		int s_aPingHistory[PING_FALLBACK_SIZE] = {0};
		int s_PingHistoryIndex = 0;
		int s_PingHistoryCount = 0;
		int s_LastPingSample = -1;

		// Frame-rate-independent smoothed adaptive offset (ticks)
		float s_TargetOffsetTicks = 0.0f;
		float s_SmoothedOffsetTicks = 0.0f;
		bool s_OffsetInitialized = false;

		float EvalMonotoneHermite1D(float p_prev, float p0, float p1, float p_next, float v0, float v1, bool has_vel, float t)
		{
			const float delta = p1 - p0;
			if(std::fabs(delta) < 1e-4f)
				return p0;

			const float d_prev = p0 - p_prev;
			const float d_next = p_next - p1;

			// Geometric secant tangents with zero-slope clamping at local extrema (bounce/wall-stop)
			float m0_geom = (d_prev * delta > 0.0f) ? 0.5f * (d_prev + delta) : delta;
			float m1_geom = (delta * d_next > 0.0f) ? 0.5f * (delta + d_next) : delta;
			if(d_prev * delta < 0.0f)
				m0_geom = 0.0f;
			if(delta * d_next < 0.0f)
				m1_geom = 0.0f;

			float m0 = m0_geom;
			float m1 = m1_geom;
			if(has_vel && !std::isnan(v0) && !std::isinf(v0) && !std::isnan(v1) && !std::isinf(v1))
			{
				// Blend geometric secant slope with exact CCharacterCore per-tick velocity
				m0 = 0.65f * m0_geom + 0.35f * v0;
				m1 = 0.65f * m1_geom + 0.35f * v1;
			}

			// Enforce Fritsch-Carlson monotonicity (no overshoot past P0 or P1)
			if(m0 * delta <= 0.0f)
				m0 = 0.0f;
			if(m1 * delta <= 0.0f)
				m1 = 0.0f;

			const float inv_delta = 1.0f / delta;
			const float alpha = m0 * inv_delta;
			const float beta = m1 * inv_delta;
			const float radius_sq = alpha * alpha + beta * beta;
			if(radius_sq > 9.0f)
			{
				const float tau = 3.0f / std::sqrt(radius_sq);
				m0 = tau * alpha * delta;
				m1 = tau * beta * delta;
			}

			// Cubic Hermite basis in Horner form
			const float c3 = 2.0f * (p0 - p1) + m0 + m1;
			const float c2 = 3.0f * (p1 - p0) - 2.0f * m0 - m1;
			const float c1 = m0;
			const float c0 = p0;
			return ((c3 * t + c2) * t + c1) * t + c0;
		}

		float ComputeTargetOffsetTicks()
		{
			float rttStdMs = 0.0f;
			float latePressure = 0.0f;
			bool hasHighResTiming = false;

			if(s_TimingCount >= 6)
			{
				hasHighResTiming = true;
				float meanRtt = 0.0f;
				for(int i = 0; i < s_TimingCount; ++i)
					meanRtt += s_aRttMs[i];
				meanRtt /= (float)s_TimingCount;

				float varRtt = 0.0f;
				int tightCount = 0;
				for(int i = 0; i < s_TimingCount; ++i)
				{
					const float diff = s_aRttMs[i] - meanRtt;
					varRtt += diff * diff;
					// Margin < 4ms means our input arrived dangerously close to or after the server tick
					if(s_aInputMarginMs[i] < 4.0f)
						tightCount++;
				}
				rttStdMs = std::sqrt(std::max(0.0f, varRtt / (float)s_TimingCount));
				latePressure = (float)tightCount / (float)s_TimingCount;
			}

			float snapStdMs = 0.0f;
			if(s_SnapJitterCount >= 6)
			{
				float varSnap = 0.0f;
				for(int i = 0; i < s_SnapJitterCount; ++i)
					varSnap += s_aSnapJitterMs[i] * s_aSnapJitterMs[i];
				snapStdMs = std::sqrt(std::max(0.0f, varSnap / (float)s_SnapJitterCount));
			}

			if(hasHighResTiming || s_SnapJitterCount >= 6)
			{
				// 1 game tick at 50 Hz = 20.0 ms. Convert combined network jitter + late-margin pressure to ticks.
				const float combinedJitterMs = hasHighResTiming ? (0.70f * rttStdMs + 0.30f * snapStdMs) : snapStdMs;
				const float jitterTicks = (combinedJitterMs / 20.0f) * 0.65f;
				const float marginPenaltyTicks = latePressure * 0.18f;
				const float target = jitterTicks + marginPenaltyTicks;
				if(std::isnan(target) || std::isinf(target))
					return 0.0f;
				return std::clamp(target, 0.0f, 0.35f);
			}

			// Fallback to deduplicated scoreboard ping variance
			if(s_PingHistoryCount < 4)
				return 0.0f;

			float sum = 0.0f;
			for(int i = 0; i < s_PingHistoryCount; ++i)
				sum += (float)s_aPingHistory[i];

			const float inv = 1.0f / (float)s_PingHistoryCount;
			const float avg = sum * inv;

			float varSum = 0.0f;
			for(int i = 0; i < s_PingHistoryCount; ++i)
			{
				const float d = (float)s_aPingHistory[i] - avg;
				varSum += d * d;
			}

			const float delta = std::sqrt(std::max(0.0f, varSum * inv));
			if(std::isnan(delta) || std::isinf(delta))
				return 0.0f;

			return std::clamp((delta / 20.0f) * 0.65f, 0.0f, 0.35f);
		}
	} // namespace

	void Reset()
	{
		s_TimingIndex = 0;
		s_TimingCount = 0;
		s_SnapJitterIndex = 0;
		s_SnapJitterCount = 0;
		s_LastSnapTicks = 0;
		s_PingHistoryIndex = 0;
		s_PingHistoryCount = 0;
		s_LastPingSample = -1;
		s_TargetOffsetTicks = 0.0f;
		s_SmoothedOffsetTicks = 0.0f;
		s_OffsetInitialized = false;
	}

	vec2 EvaluateTrajectory(vec2 P_prev, vec2 P0, vec2 P1, vec2 P_next, vec2 Vel0, vec2 Vel1, bool HasVel, float Fraction)
	{
		if(std::isnan(Fraction) || std::isinf(Fraction) ||
		   std::isnan(P0.x) || std::isnan(P0.y) || std::isnan(P1.x) || std::isnan(P1.y))
			return P0;

		const float t = std::clamp(Fraction, 0.0f, 1.0f);

		const float segDist = distance(P0, P1);
		if(segDist > 320.0f)
			return P1; // Teleporter transition: snap cleanly without cross-map streak
		if(segDist > 256.0f)
			return mix(P0, P1, t);

		// Isolate neighbor teleports so adjacent discontinuities only flatten the local tangent
		if(std::isnan(P_prev.x) || std::isnan(P_prev.y) || distance(P_prev, P0) > 256.0f)
			P_prev = P0;
		if(std::isnan(P_next.x) || std::isnan(P_next.y) || distance(P1, P_next) > 256.0f)
			P_next = P1;

		return vec2(
			EvalMonotoneHermite1D(P_prev.x, P0.x, P1.x, P_next.x, Vel0.x, Vel1.x, HasVel, t),
			EvalMonotoneHermite1D(P_prev.y, P0.y, P1.y, P_next.y, Vel0.y, Vel1.y, HasVel, t));
	}

	vec2 EvaluateTrajectory(vec2 P_prev, vec2 P0, vec2 P1, vec2 P_next, float Fraction)
	{
		return EvaluateTrajectory(P_prev, P0, P1, P_next, vec2(0.0f, 0.0f), vec2(0.0f, 0.0f), false, Fraction);
	}

	void UpdateNetworkTiming(float RttMs, float InputMarginMs)
	{
		if(std::isnan(RttMs) || std::isinf(RttMs) || RttMs <= 0.0f || RttMs > 2000.0f)
			return;
		if(std::isnan(InputMarginMs) || std::isinf(InputMarginMs))
			InputMarginMs = 0.0f;

		s_aRttMs[s_TimingIndex] = RttMs;
		s_aInputMarginMs[s_TimingIndex] = std::clamp(InputMarginMs, -500.0f, 500.0f);
		s_TimingIndex = (s_TimingIndex + 1) % TIMING_RING_SIZE;
		if(s_TimingCount < TIMING_RING_SIZE)
			s_TimingCount++;

		s_TargetOffsetTicks = ComputeTargetOffsetTicks();
	}

	void UpdateSnapshotArrival(int64_t NowTicks, int64_t Freq, int TickDelta, int TickSpeed)
	{
		if(Freq <= 0 || TickSpeed <= 0 || NowTicks <= 0)
			return;

		if(s_LastSnapTicks > 0 && TickDelta > 0 && TickDelta <= 10)
		{
			const float dtMs = (float)((double)(NowTicks - s_LastSnapTicks) * 1000.0 / (double)Freq);
			const float expectedMs = (1000.0f * (float)TickDelta) / (float)TickSpeed;
			const float jitterMs = dtMs - expectedMs;
			if(!std::isnan(jitterMs) && !std::isinf(jitterMs) && std::fabs(jitterMs) < 250.0f)
			{
				s_aSnapJitterMs[s_SnapJitterIndex] = jitterMs;
				s_SnapJitterIndex = (s_SnapJitterIndex + 1) % TIMING_RING_SIZE;
				if(s_SnapJitterCount < TIMING_RING_SIZE)
					s_SnapJitterCount++;
				s_TargetOffsetTicks = ComputeTargetOffsetTicks();
			}
		}

		s_LastSnapTicks = NowTicks;
	}

	void UpdatePingSample(int PingMs)
	{
		if(PingMs <= 0 || PingMs > 2000)
			return;
		// Deduplicate identical consecutive scoreboard values so motionless ping doesn't wipe real variance
		if(PingMs == s_LastPingSample && s_PingHistoryCount >= 4)
			return;

		s_LastPingSample = PingMs;
		s_aPingHistory[s_PingHistoryIndex] = PingMs;
		s_PingHistoryIndex = (s_PingHistoryIndex + 1) % PING_FALLBACK_SIZE;
		if(s_PingHistoryCount < PING_FALLBACK_SIZE)
			s_PingHistoryCount++;

		s_TargetOffsetTicks = ComputeTargetOffsetTicks();
	}

	void AdvanceFrame(float FrameDeltaSeconds)
	{
		if(std::isnan(FrameDeltaSeconds) || std::isinf(FrameDeltaSeconds) || FrameDeltaSeconds <= 0.0f)
			return;

		const float dt = std::min(FrameDeltaSeconds, 0.1f);
		if(!s_OffsetInitialized)
		{
			s_SmoothedOffsetTicks = s_TargetOffsetTicks;
			s_OffsetInitialized = true;
			return;
		}

		// Frame-rate-independent exponential smoothing (tau = 250ms) + slew-rate limiter (0.08 ticks/sec)
		// Prevents abrupt integer CarryOverTicks jumps in AcInputs::ApplyOffset during gameplay.
		const float alpha = 1.0f - std::exp(-dt / 0.25f);
		const float desiredStep = (s_TargetOffsetTicks - s_SmoothedOffsetTicks) * alpha;
		const float maxStep = 0.08f * dt;
		s_SmoothedOffsetTicks += std::clamp(desiredStep, -maxStep, maxStep);
		s_SmoothedOffsetTicks = std::clamp(s_SmoothedOffsetTicks, 0.0f, 0.35f);
	}

	float GetAdaptiveLatencyOffset()
	{
		return s_OffsetInitialized ? s_SmoothedOffsetTicks : s_TargetOffsetTicks;
	}
}

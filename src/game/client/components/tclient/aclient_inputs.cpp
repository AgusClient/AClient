#include "aclient_inputs.h"

#include <base/math.h>
#include <engine/shared/config.h>
#include <game/collision.h>
#include <game/mapitems.h>

#include <algorithm>
#include <cmath>

namespace
{
	float FastModeOffsetTicks(bool ForOthers)
	{
		if(ForOthers)
		{
			const int Amount = g_Config.m_AcFastInputOthersAmount > 0 ? g_Config.m_AcFastInputOthersAmount : g_Config.m_TcFastInputOthersAmount;
			return Amount <= 0 ? 0.0f : Amount / 20.0f;
		}
		const int Amount = g_Config.m_AcFastInputAmount > 0 ? g_Config.m_AcFastInputAmount : g_Config.m_TcFastInputAmount;
		return Amount <= 0 ? 0.0f : Amount / 20.0f;
	}

	float BestModeOffsetTicks(bool ForOthers)
	{
		const int RawAmount = ForOthers ? g_Config.m_AcBestInputOthersAmount : g_Config.m_AcBestInputAmount;
		if(RawAmount <= 0)
			return 0.0f;

		float Offset = RawAmount / 100.0f;

		if(g_Config.m_AcBestInputSmoothing > 0)
			Offset *= 1.0f - (g_Config.m_AcBestInputSmoothing / 200.0f);

		if(g_Config.m_AcBestInputLatencyComp > 0)
			Offset *= 1.0f + (g_Config.m_AcBestInputLatencyComp / 100.0f);

		return Offset;
	}

	float SaikoModeOffsetTicks(bool ForOthers)
	{
		const int RawAmount = ForOthers ? g_Config.m_AcSaikoInputOthersAmount : g_Config.m_AcSaikoInputAmount;
		if(RawAmount <= 0)
			return 0.0f;
		return RawAmount / 100.0f;
	}

	float DeltaModeOffsetTicks(bool ForOthers)
	{
		const int RawAmount = ForOthers ? g_Config.m_AcDeltaInputOthersAmount : g_Config.m_AcDeltaInputAmount;
		if(RawAmount <= 0)
			return 0.0f;
		return RawAmount / 100.0f;
	}

	float FModeOffsetTicks(bool ForOthers)
	{
		const int RawAmount = ForOthers ? g_Config.m_AcFInputOthersAmount : g_Config.m_AcFInputAmount;
		if(RawAmount <= 0)
			return 0.0f;
		return RawAmount / 1000.0f;
	}

	float CloudModeOffsetTicks(bool ForOthers)
	{
		const int RawAmount = ForOthers ? g_Config.m_AcCloudInputOthersAmount : g_Config.m_AcCloudInputAmount;
		if(RawAmount <= 0)
			return 0.0f;
		return RawAmount / 100.0f;
	}

	float AClientModeOffsetTicks(bool ForOthers)
	{
		const int RawAmount = ForOthers ? g_Config.m_AcAClientInputOthersAmount : g_Config.m_AcAClientInputAmount;
		if(RawAmount <= 0)
			return 0.0f;

		float Offset = RawAmount / 100.0f;

		if(g_Config.m_AcAClientInputLatencyComp > 0)
			Offset *= 1.0f + (g_Config.m_AcAClientInputLatencyComp / 100.0f);

		return Offset;
	}

	static int s_aPingHistory[16] = {0};
	static int s_PingHistoryIndex = 0;
	static int s_PingHistoryCount = 0;

	float AX30ModeOffsetTicks(bool ForOthers)
	{
		const int RawAmount = ForOthers ? g_Config.m_AcAX30OthersAmount : g_Config.m_AcAX30InputAmount;
		if(RawAmount <= 0)
			return 0.0f;

		float Offset = RawAmount / 100.0f;

		if(g_Config.m_AcAX30AdaptiveJitter)
			Offset += AcInputs::GetAdaptiveJitterTicks();

		if(std::isnan(Offset) || std::isinf(Offset) || Offset <= 0.0f)
			return 0.0f;

		return Offset;
	}
} // namespace

float AcInputs::EffectiveOffsetTicks(bool ForOthers)
{
	switch(g_Config.m_AcInputs)
	{
	case AC_INPUTS_FAST: return FastModeOffsetTicks(ForOthers);
	case AC_INPUTS_BEST: return BestModeOffsetTicks(ForOthers);
	case AC_INPUTS_SAIKO: return SaikoModeOffsetTicks(ForOthers);
	case AC_INPUTS_DELTA: return DeltaModeOffsetTicks(ForOthers);
	case AC_INPUTS_F: return FModeOffsetTicks(ForOthers);
	case AC_INPUTS_CLOUD: return CloudModeOffsetTicks(ForOthers);
	case AC_INPUTS_ACLIENT: return AClientModeOffsetTicks(ForOthers);
	case AC_INPUTS_AX30: return AX30ModeOffsetTicks(ForOthers);
	case AC_INPUTS_OFF:
	default:
		if(g_Config.m_TcFastInput != 0)
			return FastModeOffsetTicks(ForOthers);
		return 0.0f;
	}
}

int AcInputs::PredictionTicks(float OffsetTicks)
{
	if(OffsetTicks <= 0.0f)
		return 0;
	return (int)std::ceil(OffsetTicks);
}

int AcInputs::PredictionTicksOthers(float OffsetTicks)
{
	return PredictionTicks(OffsetTicks);
}

void AcInputs::ApplyOffset(float OffsetTicks, int &Tick, float &Intra)
{
	if(OffsetTicks <= 0.0f || std::isnan(OffsetTicks) || std::isinf(OffsetTicks) || std::isnan(Intra) || std::isinf(Intra))
		return;

	const int WholeTicks = (int)OffsetTicks;
	const float OffsetIntra = OffsetTicks - WholeTicks;

	const float CombinedIntra = Intra + OffsetIntra;
	const int CarryOverTicks = (int)CombinedIntra;

	Tick += WholeTicks + CarryOverTicks;
	Intra = CombinedIntra - CarryOverTicks;
}

float AcInputs::BestInterpolationAmount(float Fraction, float DeltaLength, bool Enable)
{
	if(!Enable)
		return Fraction;

	const float T = std::clamp(Fraction, 0.0f, 1.0f);
	const float T2 = T * T;
	const float CubicT = 3.0f * T2 - 2.0f * T2 * T;
	const int InterpMode = (g_Config.m_AcInputs == AC_INPUTS_ACLIENT) ? g_Config.m_AcAClientInputInterpolation : g_Config.m_AcBestInputInterpolation;
	switch(std::clamp(InterpMode, 1, 3))
	{
	case 2:
		return CubicT;
	case 3:
		return mix(T, CubicT, std::clamp(DeltaLength / 1000.0f, 0.0f, 1.0f));
	default:
		return T;
	}
}

vec2 AcInputs::BestInterpolate(vec2 PrevPos, vec2 CurPos, float Fraction, bool Enable)
{
	return mix(PrevPos, CurPos, BestInterpolationAmount(Fraction, length(CurPos - PrevPos), Enable));
}

float AcInputs::BestInterpolate(float PrevPos, float CurPos, float Fraction, bool Enable)
{
	return mix(PrevPos, CurPos, BestInterpolationAmount(Fraction, absolute(CurPos - PrevPos), Enable));
}

bool AcInputs::FastOthers()
{
	return (g_Config.m_AcInputs == AC_INPUTS_FAST && (g_Config.m_AcFastInputOthers != 0 || g_Config.m_TcFastInputOthers != 0)) ||
	       (g_Config.m_AcInputs == AC_INPUTS_OFF && g_Config.m_TcFastInput != 0 && g_Config.m_TcFastInputOthers != 0);
}

bool AcInputs::BestOthers()
{
	return g_Config.m_AcInputs == AC_INPUTS_BEST && g_Config.m_AcBestInputOthers != 0;
}

bool AcInputs::SaikoOthers()
{
	return g_Config.m_AcInputs == AC_INPUTS_SAIKO && g_Config.m_AcSaikoInputOthers != 0;
}

bool AcInputs::DeltaOthers()
{
	return g_Config.m_AcInputs == AC_INPUTS_DELTA && g_Config.m_AcDeltaInputOthers != 0;
}

bool AcInputs::FOthers()
{
	return g_Config.m_AcInputs == AC_INPUTS_F && g_Config.m_AcFInputOthers != 0;
}

bool AcInputs::CloudOthers()
{
	return g_Config.m_AcInputs == AC_INPUTS_CLOUD && g_Config.m_AcCloudInputOthers != 0;
}

bool AcInputs::AClientOthers()
{
	return g_Config.m_AcInputs == AC_INPUTS_ACLIENT && g_Config.m_AcAClientInputOthers != 0;
}

bool AcInputs::AX30Others()
{
	return g_Config.m_AcInputs == AC_INPUTS_AX30 && g_Config.m_AcAX30Others != 0;
}

bool AcInputs::AnyOthers()
{
	return FastOthers() || BestOthers() || SaikoOthers() || DeltaOthers() || FOthers() || CloudOthers() || AClientOthers() || AX30Others();
}

bool AcInputs::ImmediateOthers()
{
	return BestOthers() || SaikoOthers() || DeltaOthers() || AClientOthers() || AX30Others();
}

vec2 AcInputs::HermiteCatmullRom(vec2 P_prev, vec2 P0, vec2 P1, vec2 P_next, float Fraction)
{
	if(std::isnan(Fraction) || std::isinf(Fraction))
		return P0;
	if(std::isnan(P0.x) || std::isnan(P0.y) || std::isnan(P1.x) || std::isnan(P1.y))
		return P0;
	if(distance(P0, P1) > 256.0f || distance(P_prev, P0) > 256.0f || distance(P1, P_next) > 256.0f)
		return mix(P0, P1, std::clamp(Fraction, 0.0f, 1.0f));

	const float t = std::clamp(Fraction, 0.0f, 1.0f);
	const float t2 = t * t;
	const float t3 = t2 * t;

	const vec2 m0 = (P1 - P_prev) * 0.5f;
	const vec2 m1 = (P_next - P0) * 0.5f;

	const float h00 = 2.0f * t3 - 3.0f * t2 + 1.0f;
	const float h10 = t3 - 2.0f * t2 + t;
	const float h01 = -2.0f * t3 + 3.0f * t2;
	const float h11 = t3 - t2;

	return P0 * h00 + m0 * h10 + P1 * h01 + m1 * h11;
}

void AcInputs::UpdatePingSample(int PingMs)
{
	if(PingMs <= 0 || PingMs > 2000)
		return;
	s_aPingHistory[s_PingHistoryIndex] = PingMs;
	s_PingHistoryIndex = (s_PingHistoryIndex + 1) % 16;
	if(s_PingHistoryCount < 16)
		s_PingHistoryCount++;
}

float AcInputs::GetAdaptiveJitterTicks()
{
	if(s_PingHistoryCount < 4)
		return 0.0f;

	float Mean = 0.0f;
	for(int i = 0; i < s_PingHistoryCount; ++i)
		Mean += s_aPingHistory[i];
	Mean /= (float)s_PingHistoryCount;

	float Variance = 0.0f;
	for(int i = 0; i < s_PingHistoryCount; ++i)
	{
		float Diff = s_aPingHistory[i] - Mean;
		Variance += Diff * Diff;
	}
	Variance /= (float)s_PingHistoryCount;
	float StdDevMs = std::sqrt(std::max(0.0f, Variance));
	if(std::isnan(StdDevMs) || std::isinf(StdDevMs))
		return 0.0f;

	// Convert jitter in ms to ticks (20ms = 1 tick), capped safely at +0.35 ticks
	return std::clamp(StdDevMs / 20.0f, 0.0f, 0.35f);
}

vec2 AcInputs::ClampCollision(vec2 P0, vec2 TargetPos, const ::CCollision *pCollision)
{
	if(!pCollision)
		return TargetPos;

	if(std::isnan(P0.x) || std::isnan(P0.y) || std::isnan(TargetPos.x) || std::isnan(TargetPos.y) ||
	   std::isinf(P0.x) || std::isinf(P0.y) || std::isinf(TargetPos.x) || std::isinf(TargetPos.y))
		return P0;

	const float Dist = distance(P0, TargetPos);
	if(Dist < 1.0f)
		return TargetPos;

	// Bypass raycasting if teleported or respawned across map
	if(Dist > 300.0f)
		return TargetPos;

	// 1. Raycast line collision against solid walls
	vec2 OutCollision, OutBefore;
	if(pCollision->IntersectLine(P0, TargetPos, &OutCollision, &OutBefore))
	{
		TargetPos = OutBefore;
	}

	// 2. Check if final predicted position lands inside a freeze tile
	const int Tile = pCollision->GetCollisionAt(TargetPos.x, TargetPos.y);
	const int FrontTile = pCollision->GetFrontCollisionAt(TargetPos.x, TargetPos.y);
	if(Tile == TILE_FREEZE || Tile == TILE_DFREEZE || Tile == TILE_LFREEZE ||
	   FrontTile == TILE_FREEZE || FrontTile == TILE_DFREEZE || FrontTile == TILE_LFREEZE)
	{
		return P0;
	}

	return TargetPos;
}

bool AcInputs::IsActive()
{
	return g_Config.m_AcInputs != AC_INPUTS_OFF || g_Config.m_TcFastInput != 0;
}

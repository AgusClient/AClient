#include "aclient_inputs.h"

#include <base/math.h>
#include <engine/shared/config.h>

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
	if(OffsetTicks <= 0.0f)
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
	switch(std::clamp(g_Config.m_AcBestInputInterpolation, 1, 3))
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

bool AcInputs::AnyOthers()
{
	return FastOthers() || BestOthers() || SaikoOthers() || DeltaOthers() || FOthers() || CloudOthers();
}

bool AcInputs::ImmediateOthers()
{
	return BestOthers() || SaikoOthers() || DeltaOthers();
}

bool AcInputs::IsActive()
{
	return g_Config.m_AcInputs != AC_INPUTS_OFF || g_Config.m_TcFastInput != 0;
}

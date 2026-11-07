#ifndef GAME_ACLIENT_FOG_H
#define GAME_ACLIENT_FOG_H

#include <base/math.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <algorithm>

inline bool IsAcHighDetailEnabled()
{
	return g_Config.m_GfxHighDetail && !(g_Config.m_AcOptimizer && g_Config.m_AcOptimizerDisableHighDetail);
}

inline bool IsAcFpsFogActive()
{
	return g_Config.m_AcOptimizer && g_Config.m_AcOptimizerFpsFog;
}

inline CScreenRect ComputeAcFogScreenRect(const CScreenRect &BaseScreenRect, float ExtraMarginWorld = 0.0f)
{
	CScreenRect Result = BaseScreenRect;
	Result.Expand(ExtraMarginWorld);

	if(!IsAcFpsFogActive())
		return Result;

	const vec2 Center = (BaseScreenRect.m_TopLeft + BaseScreenRect.m_BottomRight) * 0.5f;
	float HalfW = 0.0f;
	float HalfH = 0.0f;
	if(g_Config.m_AcOptimizerFpsFogMode == 1)
	{
		const float Scale = std::clamp(g_Config.m_AcOptimizerFpsFogZoomPercent, 20, 120) / 100.0f;
		HalfW = BaseScreenRect.Width() * 0.5f * Scale;
		HalfH = BaseScreenRect.Height() * 0.5f * Scale;
	}
	else
	{
		HalfW = std::clamp(g_Config.m_AcOptimizerFpsFogRadiusTiles, 10, 120) * 32.0f;
		HalfH = HalfW;
	}

	Result.m_TopLeft.x = std::max(Result.m_TopLeft.x, Center.x - (HalfW + ExtraMarginWorld));
	Result.m_TopLeft.y = std::max(Result.m_TopLeft.y, Center.y - (HalfH + ExtraMarginWorld));
	Result.m_BottomRight.x = std::min(Result.m_BottomRight.x, Center.x + (HalfW + ExtraMarginWorld));
	Result.m_BottomRight.y = std::min(Result.m_BottomRight.y, Center.y + (HalfH + ExtraMarginWorld));
	if(Result.m_BottomRight.x < Result.m_TopLeft.x)
		Result.m_BottomRight.x = Result.m_TopLeft.x;
	if(Result.m_BottomRight.y < Result.m_TopLeft.y)
		Result.m_BottomRight.y = Result.m_TopLeft.y;
	return Result;
}

#endif // GAME_ACLIENT_FOG_H

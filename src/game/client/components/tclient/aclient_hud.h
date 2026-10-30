#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_ACLIENT_HUD_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_ACLIENT_HUD_H

#include <game/client/component.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>

class CAClientHud : public CComponent
{
public:
	int Sizeof() const override { return sizeof(*this); }
	void OnRender() override;

private:
	void RenderKeystrokes();
	void RenderSpeedometer();
	void DrawKey(float X, float Y, float W, float H, float Rounding, bool Active, const char *pLabel, float Alpha);
};

#endif

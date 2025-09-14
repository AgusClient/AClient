#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_OUTLINES_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_OUTLINES_H

#include <game/client/component.h>

class CTile;
class CTeleTile;

class COutlines : public CComponent
{
private:
	void RenderGameTileOutlines(CTile *pTiles, int w, int h, float Scale, int TileType) const;
	void RenderTeleOutlines(CTile *pTiles, CTeleTile *pTele, int w, int h, float Scale) const;

public:
	int Sizeof() const override { return sizeof(*this); }
	void OnConsoleInit() override;
	void OnRender() override;
};

#endif

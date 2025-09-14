#include <game/client/animstate.h>
#include <game/client/gameclient.h>
#include <game/client/render.h>

#include <game/mapitems.h>

#include <engine/graphics.h>
#include <engine/shared/config.h>

#include "outlines.h"

void COutlines::OnConsoleInit()
{
	auto FixColorAlpha = [&](const char *pName, unsigned int &ColorInt) {
		Console()->Chain(
			pName, [](IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData) {
				pfnCallback(pResult, pCallbackUserData);
				unsigned int *pColorInt = decltype(pColorInt)(pUserData);
				ColorHSLA Color(*pColorInt);
				if(Color.a <= 0.0f)
				{
					Color.a = 1.0f;
					*pColorInt = Color.Pack(true);
				}
			},
			&ColorInt);
	};
	FixColorAlpha("tc_outline_color_solid", g_Config.m_TcOutlineColorSolid);
	FixColorAlpha("tc_outline_color_freeze", g_Config.m_TcOutlineColorFreeze);
	FixColorAlpha("tc_outline_color_unfreeze", g_Config.m_TcOutlineColorUnfreeze);
	FixColorAlpha("tc_outline_color_kill", g_Config.m_TcOutlineColorKill);
	FixColorAlpha("tc_outline_color_tele", g_Config.m_TcOutlineColorTele);
}

void COutlines::OnRender()
{
	if(GameClient()->m_MapLayersBackground.m_OnlineOnly && Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;
	if(!g_Config.m_ClOverlayEntities && g_Config.m_TcOutlineEntities)
		return;
	if(!g_Config.m_TcOutline)
		return;

	float ScreenX0, ScreenY0, ScreenX1, ScreenY1;
	Graphics()->GetScreen(&ScreenX0, &ScreenY0, &ScreenX1, &ScreenY1);
	const float Scale = 32.0f;

	Graphics()->TextureClear();
	Graphics()->QuadsBegin();

	auto DoLayer = [&](CMapItemLayerTilemap *pTMap, int CMapItemLayerTilemap::* pTMapData) {
		if(!pTMap)
			return;
		CTile *pTiles = (CTile *)GameClient()->Layers()->Map()->GetData(pTMap->*pTMapData);
		if (!pTiles)
			return;
		const unsigned Size = GameClient()->Layers()->Map()->GetDataSize(pTMap->*pTMapData);
		if (Size < (size_t)pTMap->m_Width * pTMap->m_Height * sizeof(CTile))
			return;

		const int w = pTMap->m_Width;
		const int h = pTMap->m_Height;
		auto ClampedIndex = [&](int x, int y)
		{
			x = std::clamp(x, 0, w - 1);
			y = std::clamp(y, 0, h - 1);
			return x + y * w;
		};

		int StartY = (int)(ScreenY0 / Scale) - 1;
		int StartX = (int)(ScreenX0 / Scale) - 1;
		int EndY = (int)(ScreenY1 / Scale) + 1;
		int EndX = (int)(ScreenX1 / Scale) + 1;
		int MaxScale = 12;
		if(EndX - StartX > Graphics()->ScreenWidth() / MaxScale || EndY - StartY > Graphics()->ScreenHeight() / MaxScale)
		{
			int EdgeX = (EndX - StartX) - (Graphics()->ScreenWidth() / MaxScale);
			StartX += EdgeX / 2;
			EndX -= EdgeX / 2;
			int EdgeY = (EndY - StartY) - (Graphics()->ScreenHeight() / MaxScale);
			StartY += EdgeY / 2;
			EndY -= EdgeY / 2;
		}

		for(int y = StartY; y < EndY; y++)
		{
			for(int x = StartX; x < EndX; x++)
			{
				const unsigned char Index = pTiles[ClampedIndex(x, y)].m_Index;
				auto DoType = [&](const bool &Enable, bool (*IsMatchingType)(unsigned char), const int &Width, const unsigned int &Color) {
					if(!Enable)
						return;
					if(!IsMatchingType(Index))
						return;
					// Find neighbours
					const bool aNeighbors[8] = {
						IsMatchingType(pTiles[ClampedIndex(x - 1, y - 1)].m_Index),
						IsMatchingType(pTiles[ClampedIndex(x - 0, y - 1)].m_Index),
						IsMatchingType(pTiles[ClampedIndex(x + 1, y - 1)].m_Index),
						IsMatchingType(pTiles[ClampedIndex(x - 1, y + 0)].m_Index),
						IsMatchingType(pTiles[ClampedIndex(x + 1, y + 0)].m_Index),
						IsMatchingType(pTiles[ClampedIndex(x - 1, y + 1)].m_Index),
						IsMatchingType(pTiles[ClampedIndex(x + 0, y + 1)].m_Index),
						IsMatchingType(pTiles[ClampedIndex(x + 1, y + 1)].m_Index),
					};
					// Figure out edges
					IGraphics::CQuadItem aQuads[8];
					int NumQuads = 0;
					// Lone corners first
					if(!aNeighbors[0] && aNeighbors[1] && aNeighbors[3])
						aQuads[NumQuads++] = IGraphics::CQuadItem(x * Scale, y * Scale, Width, Width);
					if(!aNeighbors[2] && aNeighbors[1] && aNeighbors[4])
						aQuads[NumQuads++] = IGraphics::CQuadItem(x * Scale + Scale - Width, y * Scale, Width, Width);
					if(!aNeighbors[5] && aNeighbors[3] && aNeighbors[6])
						aQuads[NumQuads++] = IGraphics::CQuadItem(x * Scale, y * Scale + Scale - Width, Width, Width);
					if(!aNeighbors[7] && aNeighbors[6] && aNeighbors[4])
						aQuads[NumQuads++] = IGraphics::CQuadItem(x * Scale + Scale - Width, y * Scale + Scale - Width, Width, Width);
					// Top
					if(!aNeighbors[1])
						aQuads[NumQuads++] = IGraphics::CQuadItem(x * Scale, y * Scale, Scale, Width);
					// Bottom
					if(!aNeighbors[6])
						aQuads[NumQuads++] = IGraphics::CQuadItem(x * Scale, y * Scale + Scale - Width, Scale, Width);
					// Left
					if(!aNeighbors[3])
					{
						if(aNeighbors[1] && aNeighbors[6])
							aQuads[NumQuads++] = IGraphics::CQuadItem(x * Scale, y * Scale, Width, Scale);
						else if(aNeighbors[6])
							aQuads[NumQuads++] = IGraphics::CQuadItem(x * Scale, y * Scale + Width, Width, Scale - Width);
						else if(aNeighbors[1])
							aQuads[NumQuads++] = IGraphics::CQuadItem(x * Scale, y * Scale, Width, Scale - Width);
						else
							aQuads[NumQuads++] = IGraphics::CQuadItem(x * Scale, y * Scale + Width, Width, Scale - Width * 2.0f);
					}
					// Right
					if(!aNeighbors[4])
					{
						if(aNeighbors[1] && aNeighbors[6])
							aQuads[NumQuads++] = IGraphics::CQuadItem(x * Scale + Scale - Width, y * Scale, Width, Scale);
						else if(aNeighbors[6])
							aQuads[NumQuads++] = IGraphics::CQuadItem(x * Scale + Scale - Width, y * Scale + Width, Width, Scale - Width);
						else if(aNeighbors[1])
							aQuads[NumQuads++] = IGraphics::CQuadItem(x * Scale + Scale - Width, y * Scale, Width, Scale - Width);
						else
							aQuads[NumQuads++] = IGraphics::CQuadItem(x * Scale + Scale - Width, y * Scale + Width, Width, Scale - Width * 2.0f);
					}
					if(NumQuads <= 0)
						return;
					Graphics()->SetColor(color_cast<ColorRGBA>(ColorHSLA(Color)));
					Graphics()->QuadsDrawTL(aQuads, NumQuads);
				};
				DoType(g_Config.m_TcOutlineSolid, [](unsigned char Tile){ return Tile == TILE_SOLID || Tile == TILE_NOHOOK; }, g_Config.m_TcOutlineWidthSolid, g_Config.m_TcOutlineColorSolid);
				DoType(g_Config.m_TcOutlineFreeze, [](unsigned char Tile){ return Tile == TILE_FREEZE || Tile == TILE_DFREEZE || Tile == TILE_LFREEZE; }, g_Config.m_TcOutlineWidthFreeze, g_Config.m_TcOutlineColorFreeze);
				DoType(g_Config.m_TcOutlineUnfreeze, [](unsigned char Tile){ return Tile == TILE_UNFREEZE || Tile == TILE_DUNFREEZE || Tile == TILE_LUNFREEZE; }, g_Config.m_TcOutlineWidthUnfreeze, g_Config.m_TcOutlineColorUnfreeze);
				DoType(g_Config.m_TcOutlineKill, [](unsigned char Tile){ return Tile == TILE_DEATH; }, g_Config.m_TcOutlineWidthKill, g_Config.m_TcOutlineColorKill);
				DoType(g_Config.m_TcOutlineKill, [](unsigned char Tile){ return Tile == TILE_DEATH; }, g_Config.m_TcOutlineWidthKill, g_Config.m_TcOutlineColorKill);
				DoType(g_Config.m_TcOutlineTele, [](unsigned char Tile){ return Tile == TILE_TELEOUT || Tile == TILE_TELEIN || Tile == TILE_TELEINEVIL || Tile == TILE_TELECHECKIN || Tile == TILE_TELECHECKINEVIL || Tile == TILE_TELEINHOOK || Tile == TILE_TELEINWEAPON; }, g_Config.m_TcOutlineWidthTele, g_Config.m_TcOutlineColorTele);
			}
		}
	};

	if(g_Config.m_TcOutlineSolid || g_Config.m_TcOutlineFreeze || g_Config.m_TcOutlineUnfreeze || g_Config.m_TcOutlineKill)
	{
		DoLayer(GameClient()->Layers()->GameLayer(), &CMapItemLayerTilemap::m_Data);
		DoLayer(GameClient()->Layers()->FrontLayer(), &CMapItemLayerTilemap::m_Front);
	}
	if(g_Config.m_TcOutlineTele)
	{
		DoLayer(GameClient()->Layers()->TeleLayer(), &CMapItemLayerTilemap::m_Tele);
	}

	Graphics()->QuadsEnd();
}

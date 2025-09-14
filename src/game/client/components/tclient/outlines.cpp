#include <game/client/animstate.h>
#include <game/client/gameclient.h>
#include <game/client/render.h>

#include <game/mapitems.h>

#include <engine/graphics.h>
#include <engine/shared/config.h>

#include "outlines.h"

// TClient

static int ClampedIndex(int x, int y, int w, int h)
{
	x = std::clamp(x, 0, w - 1);
	y = std::clamp(y, 0, h - 1);
	return x + y * w;
}

void COutlines::RenderGameTileOutlines(CTile *pTiles, int w, int h, float Scale, int TileType) const
{
	// Config
	float Width;
	ColorRGBA Color;
	if(TileType == TILE_SOLID)
	{
		Width = g_Config.m_TcOutlineWidthSolid;
		Color = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_TcOutlineColorSolid));
	}
	else if(TileType == TILE_FREEZE)
	{
		Width = g_Config.m_TcOutlineWidthFreeze;
		Color = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_TcOutlineColorFreeze));
	}
	else if(TileType == TILE_UNFREEZE)
	{
		Width = g_Config.m_TcOutlineWidthUnfreeze;
		Color = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_TcOutlineColorUnfreeze));
	}
	else if(TileType == TILE_DEATH)
	{
		Width = g_Config.m_TcOutlineWidthKill;
		Color = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_TcOutlineColorKill));
	}
	else
	{
		dbg_assert(false, "Invalid value for TileType");
	}

	float ScreenX0, ScreenY0, ScreenX1, ScreenY1;
	Graphics()->GetScreen(&ScreenX0, &ScreenY0, &ScreenX1, &ScreenY1);

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
	Graphics()->TextureClear();
	Graphics()->QuadsBegin();
	Graphics()->SetColor(Color);

	for(int y = StartY; y < EndY; y++)
	{
		for(int x = StartX; x < EndX; x++)
		{
			int mx = x;
			int my = y;

			int c = ClampedIndex(mx, my, w, h);

			const unsigned char Index = pTiles[c].m_Index;
			const bool IsSolid = Index == TILE_SOLID || Index == TILE_NOHOOK;
			const bool IsFreeze = Index == TILE_FREEZE || Index == TILE_DFREEZE;
			const bool IsUnfreeze = Index == TILE_UNFREEZE || Index == TILE_DUNFREEZE;
			const bool IsKill = Index == TILE_DEATH;
			const bool Render = (TileType == TILE_SOLID && IsSolid) ||
					    (TileType == TILE_FREEZE && IsFreeze) ||
					    (TileType == TILE_UNFREEZE && IsUnfreeze) ||
					    (TileType == TILE_DEATH && IsKill);
			if(!Render)
				continue;

			IGraphics::CQuadItem Array[8];
			bool Neighbors[8];
			if(IsFreeze && TileType == TILE_FREEZE)
			{
				int IndexN;

				IndexN = pTiles[ClampedIndex(mx - 1, my - 1, w, h)].m_Index;
				Neighbors[0] = IndexN == TILE_AIR || IndexN == TILE_UNFREEZE || IndexN == TILE_DUNFREEZE;
				IndexN = pTiles[ClampedIndex(mx - 0, my - 1, w, h)].m_Index;
				Neighbors[1] = IndexN == TILE_AIR || IndexN == TILE_UNFREEZE || IndexN == TILE_DUNFREEZE;
				IndexN = pTiles[ClampedIndex(mx + 1, my - 1, w, h)].m_Index;
				Neighbors[2] = IndexN == TILE_AIR || IndexN == TILE_UNFREEZE || IndexN == TILE_DUNFREEZE;
				IndexN = pTiles[ClampedIndex(mx - 1, my + 0, w, h)].m_Index;
				Neighbors[3] = IndexN == TILE_AIR || IndexN == TILE_UNFREEZE || IndexN == TILE_DUNFREEZE;
				IndexN = pTiles[ClampedIndex(mx + 1, my + 0, w, h)].m_Index;
				Neighbors[4] = IndexN == TILE_AIR || IndexN == TILE_UNFREEZE || IndexN == TILE_DUNFREEZE;
				IndexN = pTiles[ClampedIndex(mx - 1, my + 1, w, h)].m_Index;
				Neighbors[5] = IndexN == TILE_AIR || IndexN == TILE_UNFREEZE || IndexN == TILE_DUNFREEZE;
				IndexN = pTiles[ClampedIndex(mx + 0, my + 1, w, h)].m_Index;
				Neighbors[6] = IndexN == TILE_AIR || IndexN == TILE_UNFREEZE || IndexN == TILE_DUNFREEZE;
				IndexN = pTiles[ClampedIndex(mx + 1, my + 1, w, h)].m_Index;
				Neighbors[7] = IndexN == TILE_AIR || IndexN == TILE_UNFREEZE || IndexN == TILE_DUNFREEZE;
			}
			else if(IsSolid && TileType == TILE_SOLID)
			{
				int IndexN;
				IndexN = pTiles[ClampedIndex(mx - 1, my - 1, w, h)].m_Index;
				Neighbors[0] = IndexN != TILE_NOHOOK && IndexN != Index;
				IndexN = pTiles[ClampedIndex(mx - 0, my - 1, w, h)].m_Index;
				Neighbors[1] = IndexN != TILE_NOHOOK && IndexN != Index;
				IndexN = pTiles[ClampedIndex(mx + 1, my - 1, w, h)].m_Index;
				Neighbors[2] = IndexN != TILE_NOHOOK && IndexN != Index;
				IndexN = pTiles[ClampedIndex(mx - 1, my + 0, w, h)].m_Index;
				Neighbors[3] = IndexN != TILE_NOHOOK && IndexN != Index;
				IndexN = pTiles[ClampedIndex(mx + 1, my + 0, w, h)].m_Index;
				Neighbors[4] = IndexN != TILE_NOHOOK && IndexN != Index;
				IndexN = pTiles[ClampedIndex(mx - 1, my + 1, w, h)].m_Index;
				Neighbors[5] = IndexN != TILE_NOHOOK && IndexN != Index;
				IndexN = pTiles[ClampedIndex(mx + 0, my + 1, w, h)].m_Index;
				Neighbors[6] = IndexN != TILE_NOHOOK && IndexN != Index;
				IndexN = pTiles[ClampedIndex(mx + 1, my + 1, w, h)].m_Index;
				Neighbors[7] = IndexN != TILE_NOHOOK && IndexN != Index;
			}
			else if(IsKill && TileType == TILE_DEATH)
			{
				int IndexN;
				IndexN = pTiles[ClampedIndex(mx - 1, my - 1, w, h)].m_Index;
				Neighbors[0] = IndexN != TILE_DEATH && IndexN != Index;
				IndexN = pTiles[ClampedIndex(mx - 0, my - 1, w, h)].m_Index;
				Neighbors[1] = IndexN != TILE_DEATH && IndexN != Index;
				IndexN = pTiles[ClampedIndex(mx + 1, my - 1, w, h)].m_Index;
				Neighbors[2] = IndexN != TILE_DEATH && IndexN != Index;
				IndexN = pTiles[ClampedIndex(mx - 1, my + 0, w, h)].m_Index;
				Neighbors[3] = IndexN != TILE_DEATH && IndexN != Index;
				IndexN = pTiles[ClampedIndex(mx + 1, my + 0, w, h)].m_Index;
				Neighbors[4] = IndexN != TILE_DEATH && IndexN != Index;
				IndexN = pTiles[ClampedIndex(mx - 1, my + 1, w, h)].m_Index;
				Neighbors[5] = IndexN != TILE_DEATH && IndexN != Index;
				IndexN = pTiles[ClampedIndex(mx + 0, my + 1, w, h)].m_Index;
				Neighbors[6] = IndexN != TILE_DEATH && IndexN != Index;
				IndexN = pTiles[ClampedIndex(mx + 1, my + 1, w, h)].m_Index;
				Neighbors[7] = IndexN != TILE_DEATH && IndexN != Index;
			}
			else
			{
				int IndexN;
				IndexN = pTiles[ClampedIndex(mx - 1, my - 1, w, h)].m_Index;
				Neighbors[0] = IndexN != TILE_UNFREEZE && IndexN != TILE_DUNFREEZE;
				IndexN = pTiles[ClampedIndex(mx - 0, my - 1, w, h)].m_Index;
				Neighbors[1] = IndexN != TILE_UNFREEZE && IndexN != TILE_DUNFREEZE;
				IndexN = pTiles[ClampedIndex(mx + 1, my - 1, w, h)].m_Index;
				Neighbors[2] = IndexN != TILE_UNFREEZE && IndexN != TILE_DUNFREEZE;
				IndexN = pTiles[ClampedIndex(mx - 1, my + 0, w, h)].m_Index;
				Neighbors[3] = IndexN != TILE_UNFREEZE && IndexN != TILE_DUNFREEZE;
				IndexN = pTiles[ClampedIndex(mx + 1, my + 0, w, h)].m_Index;
				Neighbors[4] = IndexN != TILE_UNFREEZE && IndexN != TILE_DUNFREEZE;
				IndexN = pTiles[ClampedIndex(mx - 1, my + 1, w, h)].m_Index;
				Neighbors[5] = IndexN != TILE_UNFREEZE && IndexN != TILE_DUNFREEZE;
				IndexN = pTiles[ClampedIndex(mx + 0, my + 1, w, h)].m_Index;
				Neighbors[6] = IndexN != TILE_UNFREEZE && IndexN != TILE_DUNFREEZE;
				IndexN = pTiles[ClampedIndex(mx + 1, my + 1, w, h)].m_Index;
				Neighbors[7] = IndexN != TILE_UNFREEZE && IndexN != TILE_DUNFREEZE;
			}

			int NumQuads = 0;

			// Do lonely corners first
			if(Neighbors[0] && !Neighbors[1] && !Neighbors[3])
			{
				Array[NumQuads] = IGraphics::CQuadItem(mx * Scale, my * Scale, Width, Width);
				NumQuads++;
			}
			if(Neighbors[2] && !Neighbors[1] && !Neighbors[4])
			{
				Array[NumQuads] = IGraphics::CQuadItem(mx * Scale + Scale - Width, my * Scale, Width, Width);
				NumQuads++;
			}
			if(Neighbors[5] && !Neighbors[3] && !Neighbors[6])
			{
				Array[NumQuads] = IGraphics::CQuadItem(mx * Scale, my * Scale + Scale - Width, Width, Width);
				NumQuads++;
			}
			if(Neighbors[7] && !Neighbors[6] && !Neighbors[4])
			{
				Array[NumQuads] = IGraphics::CQuadItem(mx * Scale + Scale - Width, my * Scale + Scale - Width, Width, Width);
				NumQuads++;
			}
			// Top
			if(Neighbors[1])
			{
				Array[NumQuads] = IGraphics::CQuadItem(mx * Scale, my * Scale, Scale, Width);
				NumQuads++;
			}
			// Bottom
			if(Neighbors[6])
			{
				Array[NumQuads] = IGraphics::CQuadItem(mx * Scale, my * Scale + Scale - Width, Scale, Width);
				NumQuads++;
			}
			// Left
			if(Neighbors[3])
			{
				if(!Neighbors[1] && !Neighbors[6])
					Array[NumQuads] = IGraphics::CQuadItem(mx * Scale, my * Scale, Width, Scale);
				else if(!Neighbors[6])
					Array[NumQuads] = IGraphics::CQuadItem(mx * Scale, my * Scale + Width, Width, Scale - Width);
				else if(!Neighbors[1])
					Array[NumQuads] = IGraphics::CQuadItem(mx * Scale, my * Scale, Width, Scale - Width);
				else
					Array[NumQuads] = IGraphics::CQuadItem(mx * Scale, my * Scale + Width, Width, Scale - Width * 2.0f);
				NumQuads++;
			}
			// Right
			if(Neighbors[4])
			{
				if(!Neighbors[1] && !Neighbors[6])
					Array[NumQuads] = IGraphics::CQuadItem(mx * Scale + Scale - Width, my * Scale, Width, Scale);
				else if(!Neighbors[6])
					Array[NumQuads] = IGraphics::CQuadItem(mx * Scale + Scale - Width, my * Scale + Width, Width, Scale - Width);
				else if(!Neighbors[1])
					Array[NumQuads] = IGraphics::CQuadItem(mx * Scale + Scale - Width, my * Scale, Width, Scale - Width);
				else
					Array[NumQuads] = IGraphics::CQuadItem(mx * Scale + Scale - Width, my * Scale + Width, Width, Scale - Width * 2.0f);
				NumQuads++;
			}

			Graphics()->QuadsDrawTL(Array, NumQuads);
		}
	}
	Graphics()->QuadsEnd();
	Graphics()->MapScreen(ScreenX0, ScreenY0, ScreenX1, ScreenY1);
}

void COutlines::RenderTeleOutlines(CTile *pTiles, CTeleTile *pTele, int w, int h, float Scale) const
{
	float ScreenX0, ScreenY0, ScreenX1, ScreenY1;
	Graphics()->GetScreen(&ScreenX0, &ScreenY0, &ScreenX1, &ScreenY1);

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

	Graphics()->TextureClear();
	Graphics()->QuadsBegin();
	Graphics()->SetColor(color_cast<ColorRGBA>(ColorHSLA(g_Config.m_TcOutlineColorTele)));

	for(int y = StartY; y < EndY; y++)
	{
		for(int x = StartX; x < EndX; x++)
		{
			int mx = x;
			int my = y;

			if(mx < 1)
				continue; // mx = 0;
			if(mx >= w - 1)
				continue; // mx = w - 1;
			if(my < 1)
				continue; // my = 0;
			if(my >= h - 1)
				continue; // my = h - 1;

			int c = mx + my * w;

			unsigned char Index = pTele[c].m_Type;
			if(!Index)
				continue;
			if(!(Index == TILE_TELECHECKINEVIL || Index == TILE_TELEIN || Index == TILE_TELEINEVIL))
				continue;

			IGraphics::CQuadItem Array[8];
			bool Neighbors[8];
			Neighbors[0] = pTiles[(mx - 1) + (my - 1) * w].m_Index == 0 && !pTele[(mx - 1) + (my - 1) * w].m_Number;
			Neighbors[1] = pTiles[(mx + 0) + (my - 1) * w].m_Index == 0 && !pTele[(mx + 0) + (my - 1) * w].m_Number;
			Neighbors[2] = pTiles[(mx + 1) + (my - 1) * w].m_Index == 0 && !pTele[(mx + 1) + (my - 1) * w].m_Number;
			Neighbors[3] = pTiles[(mx - 1) + (my + 0) * w].m_Index == 0 && !pTele[(mx - 1) + (my + 0) * w].m_Number;
			Neighbors[4] = pTiles[(mx + 1) + (my + 0) * w].m_Index == 0 && !pTele[(mx + 1) + (my + 0) * w].m_Number;
			Neighbors[5] = pTiles[(mx - 1) + (my + 1) * w].m_Index == 0 && !pTele[(mx - 1) + (my + 1) * w].m_Number;
			Neighbors[6] = pTiles[(mx + 0) + (my + 1) * w].m_Index == 0 && !pTele[(mx + 0) + (my + 1) * w].m_Number;
			Neighbors[7] = pTiles[(mx + 1) + (my + 1) * w].m_Index == 0 && !pTele[(mx + 1) + (my + 1) * w].m_Number;

			float Size = (float)g_Config.m_TcOutlineWidthTele;
			int NumQuads = 0;

			// Do lonely corners first
			if(Neighbors[0] && !Neighbors[1] && !Neighbors[3])
			{
				Array[NumQuads] = IGraphics::CQuadItem(mx * Scale, my * Scale, Size, Size);
				NumQuads++;
			}
			if(Neighbors[2] && !Neighbors[1] && !Neighbors[4])
			{
				Array[NumQuads] = IGraphics::CQuadItem(mx * Scale + Scale - Size, my * Scale, Size, Size);
				NumQuads++;
			}
			if(Neighbors[5] && !Neighbors[3] && !Neighbors[6])
			{
				Array[NumQuads] = IGraphics::CQuadItem(mx * Scale, my * Scale + Scale - Size, Size, Size);
				NumQuads++;
			}
			if(Neighbors[7] && !Neighbors[6] && !Neighbors[4])
			{
				Array[NumQuads] = IGraphics::CQuadItem(mx * Scale + Scale - Size, my * Scale + Scale - Size, Size, Size);
				NumQuads++;
			}
			// Top
			if(Neighbors[1])
			{
				Array[NumQuads] = IGraphics::CQuadItem(mx * Scale, my * Scale, Scale, Size);
				NumQuads++;
			}
			// Bottom
			if(Neighbors[6])
			{
				Array[NumQuads] = IGraphics::CQuadItem(mx * Scale, my * Scale + Scale - Size, Scale, Size);
				NumQuads++;
			}
			// Left
			if(Neighbors[3])
			{
				if(!Neighbors[1] && !Neighbors[6])
					Array[NumQuads] = IGraphics::CQuadItem(mx * Scale, my * Scale, Size, Scale);
				else if(!Neighbors[6])
					Array[NumQuads] = IGraphics::CQuadItem(mx * Scale, my * Scale + Size, Size, Scale - Size);
				else if(!Neighbors[1])
					Array[NumQuads] = IGraphics::CQuadItem(mx * Scale, my * Scale, Size, Scale - Size);
				else
					Array[NumQuads] = IGraphics::CQuadItem(mx * Scale, my * Scale + Size, Size, Scale - Size * 2.0f);
				NumQuads++;
			}
			// Right
			if(Neighbors[4])
			{
				if(!Neighbors[1] && !Neighbors[6])
					Array[NumQuads] = IGraphics::CQuadItem(mx * Scale + Scale - Size, my * Scale, Size, Scale);
				else if(!Neighbors[6])
					Array[NumQuads] = IGraphics::CQuadItem(mx * Scale + Scale - Size, my * Scale + Size, Size, Scale - Size);
				else if(!Neighbors[1])
					Array[NumQuads] = IGraphics::CQuadItem(mx * Scale + Scale - Size, my * Scale, Size, Scale - Size);
				else
					Array[NumQuads] = IGraphics::CQuadItem(mx * Scale + Scale - Size, my * Scale + Size, Size, Scale - Size * 2.0f);
				NumQuads++;
			}

			Graphics()->QuadsDrawTL(Array, NumQuads);
		}
	}
	Graphics()->QuadsEnd();
}

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
	if(g_Config.m_TcOutlineSolid || g_Config.m_TcOutlineFreeze || g_Config.m_TcOutlineUnfreeze || g_Config.m_TcOutlineKill)
	{
		CMapItemLayerTilemap *pTMap = GameClient()->Layers()->GameLayer();
		if(pTMap)
		{
			CTile *pTiles = (CTile *)GameClient()->Layers()->Map()->GetData(pTMap->m_Data);
			if(pTiles)
			{
				unsigned int Size = GameClient()->Layers()->Map()->GetDataSize(pTMap->m_Data);
				if(Size >= (size_t)pTMap->m_Width * pTMap->m_Height * sizeof(CTile))
					RenderGameTileOutlines(pTiles, pTMap->m_Width, pTMap->m_Height, 32.0f, TILE_SOLID);
			}
		}
	}
	if(g_Config.m_TcOutlineTele)
	{
		CMapItemLayerTilemap *pTMap = GameClient()->Layers()->TeleLayer();
		if(pTMap)
		{
			CTile *pTiles = (CTile *)GameClient()->Layers()->Map()->GetData(pTMap->m_Data);
			if(pTiles)
			{
				unsigned int Size = GameClient()->Layers()->Map()->GetDataSize(pTMap->m_Tele);
				if(Size >= (size_t)pTMap->m_Width * pTMap->m_Height * sizeof(CTeleTile))
				{
					CTeleTile *pTeleTiles = (CTeleTile *)GameClient()->Layers()->Map()->GetData(pTMap->m_Tele);
					CTile *pGameTiles = pTiles;
					RenderTeleOutlines(pGameTiles, pTeleTiles, pTMap->m_Width, pTMap->m_Height, 32.0f);
				}
			}
		}
	}
}

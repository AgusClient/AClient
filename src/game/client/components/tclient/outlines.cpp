#include "outlines.h"

#include <base/dbg.h>

#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <game/aclient_fog.h>
#include <game/client/animstate.h>
#include <game/client/gameclient.h>
#include <game/client/render.h>
#include <game/mapitems.h>

// The order of this is the order of priority for outlines
enum
{
	OUTLINE_NONE = 0,
	OUTLINE_UNFREEZE,
	OUTLINE_FREEZE,
	OUTLINE_TELE,
	OUTLINE_KILL,
	OUTLINE_SOLID,
};

enum class OutlineLayer
{
	GAME,
	FRONT,
	TELE
};

class COutLineLayer
{
private:
	CMapItemLayerTilemap *GetLayer(CGameClient *pThis) const
	{
		if(m_Type == OutlineLayer::GAME)
			return pThis->Layers()->GameLayer();
		if(m_Type == OutlineLayer::FRONT)
			return pThis->Layers()->FrontLayer();
		if(m_Type == OutlineLayer::TELE)
			return pThis->Layers()->TeleLayer();
		dbg_assert(false, "Invalid value for m_Type");
	}
	int GetLayerData(CGameClient *pThis) const
	{
		if(m_Type == OutlineLayer::GAME)
			return pThis->Layers()->GameLayer()->m_Data;
		if(m_Type == OutlineLayer::FRONT)
			return pThis->Layers()->FrontLayer()->m_Front;
		if(m_Type == OutlineLayer::TELE)
			return pThis->Layers()->TeleLayer()->m_Tele;
		dbg_assert(false, "Invalid value for m_Type");
	}

public:
	const OutlineLayer m_Type;
	void GetMeta(CGameClient *pThis, ivec2 &Size) const
	{
		Size = {0, 0};
		const auto *pLayer = GetLayer(pThis);
		if(!pLayer)
			return;
		const size_t TileSize = m_Type == OutlineLayer::TELE ? sizeof(CTeleTile) : sizeof(CTile);
		const int DataSize = pThis->Layers()->Map()->GetDataSize(GetLayerData(pThis));
		if(DataSize <= 0 || (size_t)DataSize < (size_t)pLayer->m_Width * (size_t)pLayer->m_Height * TileSize)
			return;
		Size = {pLayer->m_Width, pLayer->m_Height};
	}
	void SetData(CGameClient *pThis, int *pData, const ivec2 &Size) const
	{
		const auto *pLayer = GetLayer(pThis);
		const auto *pTiles = (CTile *)pThis->Layers()->Map()->GetData(GetLayerData(pThis));
		for(int y = 0; y < pLayer->m_Height; ++y)
		{
			for(int x = 0; x < pLayer->m_Width; ++x)
			{
				const int Index = y * pLayer->m_Width + x;
				const int IndexOut = y * Size.x + x;
				if(m_Type == OutlineLayer::TELE)
				{
					const auto &Tile = ((CTeleTile *)pTiles)[Index];
					if(Tile.m_Number != 0 && Tile.m_Type != 0)
						pData[IndexOut] = OUTLINE_TELE;
				}
				else
				{
					const auto Tile = pTiles[Index].m_Index;
					if(Tile == TILE_SOLID || Tile == TILE_NOHOOK)
						pData[IndexOut] = OUTLINE_SOLID;
					else if(Tile == TILE_FREEZE || Tile == TILE_DFREEZE || Tile == TILE_LFREEZE)
						pData[IndexOut] = OUTLINE_FREEZE;
					else if(Tile == TILE_UNFREEZE || Tile == TILE_DUNFREEZE || Tile == TILE_LUNFREEZE)
						pData[IndexOut] = OUTLINE_UNFREEZE;
					else if(Tile == TILE_DEATH)
						pData[IndexOut] = OUTLINE_KILL;
				}
			}
		}
	}
};

// The order of this determines order of priority into the one map (tele + freeze = tele)
static constexpr COutLineLayer OUTLINE_LAYERS[] = {{OutlineLayer::TELE}, {OutlineLayer::GAME}, {OutlineLayer::FRONT}};

void COutlines::OnMapLoad()
{
	if(m_pMapData)
	{
		delete[] m_pMapData;
		m_pMapData = nullptr;
	}

	// Find valid layers and size
	std::vector<const COutLineLayer *> vValidOutlineLayers;
	m_MapDataSize = {0, 0};
	for(const auto &Layer : OUTLINE_LAYERS)
	{
		ivec2 LayerSize;
		Layer.GetMeta(GameClient(), LayerSize);
		if(LayerSize.x <= 0 || LayerSize.y <= 0)
			continue;
		m_MapDataSize.x = std::max(m_MapDataSize.x, LayerSize.x);
		m_MapDataSize.y = std::max(m_MapDataSize.y, LayerSize.y);
		vValidOutlineLayers.push_back(&Layer);
	}
	if(m_MapDataSize.x <= 0 || m_MapDataSize.y <= 0)
		return;
	m_pMapData = new int[m_MapDataSize.x * m_MapDataSize.y](OUTLINE_NONE);

	// Do it
	for(const auto *pLayer : vValidOutlineLayers)
	{
		pLayer->SetData(GameClient(), m_pMapData, m_MapDataSize);
	}
}

void COutlines::OnRender()
{
	// Runs after the map foreground layers: draw remote-entity hooked tiles on top of them
	GameClient()->m_Players.FlushQueuedHookedTiles();

	if(!m_pMapData)
		return;
	if(GameClient()->m_MapLayersBackground.m_OnlineOnly && Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;
	if(!g_Config.m_ClOverlayEntities && g_Config.m_TcOutlineEntities)
		return;
	if(!g_Config.m_TcOutline)
		return;
	if(!g_Config.m_TcOutlineSolid &&
		!g_Config.m_TcOutlineFreeze &&
		!g_Config.m_TcOutlineUnfreeze &&
		!g_Config.m_TcOutlineKill &&
		!g_Config.m_TcOutlineTele)
		return;

	struct SOutlineStyle
	{
		bool m_Enabled;
		float m_Width;
		ColorRGBA m_Color;
	};
	const SOutlineStyle aStyles[6] = {
		{false, 0.0f, ColorRGBA()}, // OUTLINE_NONE
		{g_Config.m_TcOutlineUnfreeze != 0 && g_Config.m_TcOutlineWidthUnfreeze > 0, (float)g_Config.m_TcOutlineWidthUnfreeze, color_cast<ColorRGBA>(ColorHSLA(g_Config.m_TcOutlineColorUnfreeze, true))}, // OUTLINE_UNFREEZE
		{g_Config.m_TcOutlineFreeze != 0 && g_Config.m_TcOutlineWidthFreeze > 0, (float)g_Config.m_TcOutlineWidthFreeze, color_cast<ColorRGBA>(ColorHSLA(g_Config.m_TcOutlineColorFreeze, true))}, // OUTLINE_FREEZE
		{g_Config.m_TcOutlineTele != 0 && g_Config.m_TcOutlineWidthTele > 0, (float)g_Config.m_TcOutlineWidthTele, color_cast<ColorRGBA>(ColorHSLA(g_Config.m_TcOutlineColorTele, true))}, // OUTLINE_TELE
		{g_Config.m_TcOutlineKill != 0 && g_Config.m_TcOutlineWidthKill > 0, (float)g_Config.m_TcOutlineWidthKill, color_cast<ColorRGBA>(ColorHSLA(g_Config.m_TcOutlineColorKill, true))}, // OUTLINE_KILL
		{g_Config.m_TcOutlineSolid != 0 && g_Config.m_TcOutlineWidthSolid > 0, (float)g_Config.m_TcOutlineWidthSolid, color_cast<ColorRGBA>(ColorHSLA(g_Config.m_TcOutlineColorSolid, true))}, // OUTLINE_SOLID
	};

	const float Scale = 32.0f;

	CScreenRect ScreenRect = Graphics()->GetScreen();
	if(IsAcFpsFogActive() && g_Config.m_AcOptimizerFpsFogCullMapTiles)
		ScreenRect = ComputeAcFogScreenRect(ScreenRect, 0.0f);
	int StartX = (int)(ScreenRect.m_TopLeft.x / Scale) - 1;
	int StartY = (int)(ScreenRect.m_TopLeft.y / Scale) - 1;
	int EndX = (int)(ScreenRect.m_BottomRight.x / Scale) + 1;
	int EndY = (int)(ScreenRect.m_BottomRight.y / Scale) + 1;
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

	auto GetTile = [&](int x, int y) {
		x = std::clamp(x, 0, m_MapDataSize.x - 1);
		y = std::clamp(y, 0, m_MapDataSize.y - 1);
		return m_pMapData[y * m_MapDataSize.x + x];
	};

	Graphics()->TextureClear();
	Graphics()->QuadsBegin();

	int LastType = -1;
	for(int y = StartY; y < EndY; y++)
	{
		for(int x = StartX; x < EndX; x++)
		{
			const int Type = GetTile(x, y);
			if(Type == OUTLINE_NONE)
				continue;
			dbg_assert(Type > OUTLINE_NONE && Type <= OUTLINE_SOLID, "Invalid value for Type at %d, %d/%d, %d", x, y, m_MapDataSize.x, m_MapDataSize.y);
			const SOutlineStyle &Style = aStyles[Type];
			if(!Style.m_Enabled)
				continue;
			// Find neighbours
			const bool aNeighbors[8] = {
				GetTile(x - 1, y - 1) >= Type,
				GetTile(x - 0, y - 1) >= Type,
				GetTile(x + 1, y - 1) >= Type,
				GetTile(x - 1, y + 0) >= Type,
				GetTile(x + 1, y + 0) >= Type,
				GetTile(x - 1, y + 1) >= Type,
				GetTile(x + 0, y + 1) >= Type,
				GetTile(x + 1, y + 1) >= Type,
			};
			// Figure out edges
			IGraphics::CQuadItem aQuads[8];
			int NumQuads = 0;
			const float OutlineWidth = Style.m_Width;
			// Lone corners first
			if(!aNeighbors[0] && aNeighbors[1] && aNeighbors[3])
				aQuads[NumQuads++] = IGraphics::CQuadItem(x * Scale, y * Scale, OutlineWidth, OutlineWidth);
			if(!aNeighbors[2] && aNeighbors[1] && aNeighbors[4])
				aQuads[NumQuads++] = IGraphics::CQuadItem(x * Scale + Scale - OutlineWidth, y * Scale, OutlineWidth, OutlineWidth);
			if(!aNeighbors[5] && aNeighbors[3] && aNeighbors[6])
				aQuads[NumQuads++] = IGraphics::CQuadItem(x * Scale, y * Scale + Scale - OutlineWidth, OutlineWidth, OutlineWidth);
			if(!aNeighbors[7] && aNeighbors[6] && aNeighbors[4])
				aQuads[NumQuads++] = IGraphics::CQuadItem(x * Scale + Scale - OutlineWidth, y * Scale + Scale - OutlineWidth, OutlineWidth, OutlineWidth);
			// Top
			if(!aNeighbors[1])
				aQuads[NumQuads++] = IGraphics::CQuadItem(x * Scale, y * Scale, Scale, OutlineWidth);
			// Bottom
			if(!aNeighbors[6])
				aQuads[NumQuads++] = IGraphics::CQuadItem(x * Scale, y * Scale + Scale - OutlineWidth, Scale, OutlineWidth);
			// Left
			if(!aNeighbors[3])
			{
				if(aNeighbors[1] && aNeighbors[6])
					aQuads[NumQuads++] = IGraphics::CQuadItem(x * Scale, y * Scale, OutlineWidth, Scale);
				else if(aNeighbors[6])
					aQuads[NumQuads++] = IGraphics::CQuadItem(x * Scale, y * Scale + OutlineWidth, OutlineWidth, Scale - OutlineWidth);
				else if(aNeighbors[1])
					aQuads[NumQuads++] = IGraphics::CQuadItem(x * Scale, y * Scale, OutlineWidth, Scale - OutlineWidth);
				else
					aQuads[NumQuads++] = IGraphics::CQuadItem(x * Scale, y * Scale + OutlineWidth, OutlineWidth, Scale - OutlineWidth * 2.0f);
			}
			// Right
			if(!aNeighbors[4])
			{
				if(aNeighbors[1] && aNeighbors[6])
					aQuads[NumQuads++] = IGraphics::CQuadItem(x * Scale + Scale - OutlineWidth, y * Scale, OutlineWidth, Scale);
				else if(aNeighbors[6])
					aQuads[NumQuads++] = IGraphics::CQuadItem(x * Scale + Scale - OutlineWidth, y * Scale + OutlineWidth, OutlineWidth, Scale - OutlineWidth);
				else if(aNeighbors[1])
					aQuads[NumQuads++] = IGraphics::CQuadItem(x * Scale + Scale - OutlineWidth, y * Scale, OutlineWidth, Scale - OutlineWidth);
				else
					aQuads[NumQuads++] = IGraphics::CQuadItem(x * Scale + Scale - OutlineWidth, y * Scale + OutlineWidth, OutlineWidth, Scale - OutlineWidth * 2.0f);
			}
			if(NumQuads <= 0)
				continue;
			if(Type != LastType)
			{
				Graphics()->SetColor(Style.m_Color);
				LastType = Type;
			}
			Graphics()->QuadsDrawTL(aQuads, NumQuads);
		}
	}

	Graphics()->QuadsEnd();
}

#include <game/client/animstate.h>
#include <game/client/gameclient.h>
#include <game/client/render.h>
#include <game/generated/client_data.h>
#include <game/generated/protocol.h>
#include <game/mapitems.h>

#include <engine/graphics.h>
#include <engine/shared/config.h>

#include "outlines.h"

void COutlines::OnRender()
{
	if(GameClient()->m_MapLayersBackground.m_OnlineOnly && Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;
	if(!g_Config.m_ClOverlayEntities && g_Config.m_TcOutlineEntities)
		return;
	if(!g_Config.m_TcOutline)
		return;
	if(g_Config.m_TcOutlineFreeze || g_Config.m_TcOutlineSolid || g_Config.m_TcOutlineUnFreeze || g_Config.m_TcOutlineKill)
	{
		CMapItemLayerTilemap *pTMap = GameClient()->Layers()->GameLayer();
		if(pTMap)
		{
			CTile *pTiles = (CTile *)GameClient()->Layers()->Map()->GetData(pTMap->m_Data);
			if(pTiles)
			{
				unsigned int Size = GameClient()->Layers()->Map()->GetDataSize(pTMap->m_Data);
				if(Size >= (size_t)pTMap->m_Width * pTMap->m_Height * sizeof(CTile))
				{
					if(g_Config.m_TcOutlineUnFreeze)
						RenderTools()->RenderGameTileOutlines(pTiles, pTMap->m_Width, pTMap->m_Height, 32.0f, TILE_UNFREEZE, (float)g_Config.m_TcOutlineAlpha / 100.0f);
					if(g_Config.m_TcOutlineFreeze)
						RenderTools()->RenderGameTileOutlines(pTiles, pTMap->m_Width, pTMap->m_Height, 32.0f, TILE_FREEZE, (float)g_Config.m_TcOutlineAlpha / 100.0f);
					if(g_Config.m_TcOutlineSolid)
						RenderTools()->RenderGameTileOutlines(pTiles, pTMap->m_Width, pTMap->m_Height, 32.0f, TILE_SOLID, (float)g_Config.m_TcOutlineAlphaSolid / 100.0f);
					if(g_Config.m_TcOutlineKill)
						RenderTools()->RenderGameTileOutlines(pTiles, pTMap->m_Width, pTMap->m_Height, 32.0f, TILE_DEATH, (float)g_Config.m_TcOutlineAlpha / 100.0f);
				}
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
					RenderTools()->RenderTeleOutlines(pGameTiles, pTeleTiles, pTMap->m_Width, pTMap->m_Height, 32.0f, (float)g_Config.m_TcOutlineAlpha / 100.0f);
				}
			}
		}
	}
}

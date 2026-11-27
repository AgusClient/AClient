#include "menus.h"

#include <base/dbg.h>
#include <base/str.h>
#include <base/time.h>

#include <engine/font_icons.h>
#include <engine/shared/config.h>
#include <engine/storage.h>
#include <engine/textrender.h>

#include <game/client/gameclient.h>
#include <game/client/ui_listbox.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>
#include <generated/client_data.h>

#include <chrono>

using namespace std::chrono_literals;

typedef std::function<void()> TMenuAssetScanLoadedFunc;

struct SMenuAssetScanUser
{
	void *m_pUser;
	TMenuAssetScanLoadedFunc m_LoadedFunc;
};

void CMenus::LoadEntities(SCustomEntities *pEntitiesItem, void *pUser)
{
	auto *pRealUser = (SMenuAssetScanUser *)pUser;
	auto *pThis = (CMenus *)pRealUser->m_pUser;

	char aPath[IO_MAX_PATH_LENGTH];
	if(str_comp(pEntitiesItem->m_aName, "default") == 0)
	{
		for(int i = 0; i < MAP_IMAGE_MOD_TYPE_COUNT; ++i)
		{
			str_format(aPath, sizeof(aPath), "editor/entities_clear/%s.png", gs_apModEntitiesNames[i]);
			pEntitiesItem->m_aImages[i].m_Texture = pThis->Graphics()->LoadTexture(aPath, IStorage::TYPE_ALL);
			if(!pEntitiesItem->m_RenderTexture.IsValid() || pEntitiesItem->m_RenderTexture.IsNullTexture())
				pEntitiesItem->m_RenderTexture = pEntitiesItem->m_aImages[i].m_Texture;
		}
	}
	else
	{
		for(int i = 0; i < MAP_IMAGE_MOD_TYPE_COUNT; ++i)
		{
			str_format(aPath, sizeof(aPath), "assets/entities/%s/%s.png", pEntitiesItem->m_aName, gs_apModEntitiesNames[i]);
			pEntitiesItem->m_aImages[i].m_Texture = pThis->Graphics()->LoadTexture(aPath, IStorage::TYPE_ALL);
			if(pEntitiesItem->m_aImages[i].m_Texture.IsNullTexture())
			{
				str_format(aPath, sizeof(aPath), "assets/entities/%s.png", pEntitiesItem->m_aName);
				pEntitiesItem->m_aImages[i].m_Texture = pThis->Graphics()->LoadTexture(aPath, IStorage::TYPE_ALL);
			}
			if(pEntitiesItem->m_aImages[i].m_Texture.IsNullTexture())
			{
				str_format(aPath, sizeof(aPath), "downloaded_entities/%s.png", pEntitiesItem->m_aName);
				pEntitiesItem->m_aImages[i].m_Texture = pThis->Graphics()->LoadTexture(aPath, IStorage::TYPE_ALL);
			}
			if(!pEntitiesItem->m_RenderTexture.IsValid() || pEntitiesItem->m_RenderTexture.IsNullTexture())
				pEntitiesItem->m_RenderTexture = pEntitiesItem->m_aImages[i].m_Texture;
		}
	}
}

int CMenus::EntitiesScan(const char *pName, int IsDir, int DirType, void *pUser)
{
	auto *pRealUser = (SMenuAssetScanUser *)pUser;
	auto *pThis = (CMenus *)pRealUser->m_pUser;
	if(IsDir)
	{
		if(pName[0] == '.')
			return 0;

		// default is reserved
		if(str_comp(pName, "default") == 0)
			return 0;

		SCustomEntities EntitiesItem;
		str_copy(EntitiesItem.m_aName, pName);
		CMenus::LoadEntities(&EntitiesItem, pUser);
		pThis->m_vEntitiesList.push_back(EntitiesItem);
	}
	else
	{
		if(str_endswith(pName, ".png"))
		{
			char aName[IO_MAX_PATH_LENGTH];
			str_truncate(aName, sizeof(aName), pName, str_length(pName) - 4);
			// default is reserved
			if(str_comp(aName, "default") == 0)
				return 0;

			SCustomEntities EntitiesItem;
			str_copy(EntitiesItem.m_aName, aName);
			CMenus::LoadEntities(&EntitiesItem, pUser);
			pThis->m_vEntitiesList.push_back(EntitiesItem);
		}
	}

	pRealUser->m_LoadedFunc();

	return 0;
}

template<typename TName>
static void LoadAsset(TName *pAssetItem, const char *pAssetName, IGraphics *pGraphics)
{
	char aPath[IO_MAX_PATH_LENGTH];
	if(str_comp(pAssetItem->m_aName, "default") == 0)
	{
		str_format(aPath, sizeof(aPath), "%s.png", pAssetName);
		pAssetItem->m_RenderTexture = pGraphics->LoadTexture(aPath, IStorage::TYPE_ALL);
	}
	else
	{
		str_format(aPath, sizeof(aPath), "assets/%s/%s.png", pAssetName, pAssetItem->m_aName);
		pAssetItem->m_RenderTexture = pGraphics->LoadTexture(aPath, IStorage::TYPE_ALL);
		if(pAssetItem->m_RenderTexture.IsNullTexture())
		{
			str_format(aPath, sizeof(aPath), "assets/%s/%s/%s.png", pAssetName, pAssetItem->m_aName, pAssetName);
			pAssetItem->m_RenderTexture = pGraphics->LoadTexture(aPath, IStorage::TYPE_ALL);
		}
	}
}

template<typename TName>
static int AssetScan(const char *pName, int IsDir, int DirType, std::vector<TName> &vAssetList, const char *pAssetName, IGraphics *pGraphics, void *pUser)
{
	auto *pRealUser = (SMenuAssetScanUser *)pUser;
	if(IsDir)
	{
		if(pName[0] == '.')
			return 0;

		// default is reserved
		if(str_comp(pName, "default") == 0)
			return 0;

		TName AssetItem;
		str_copy(AssetItem.m_aName, pName);
		LoadAsset(&AssetItem, pAssetName, pGraphics);
		vAssetList.push_back(AssetItem);
	}
	else
	{
		if(str_endswith(pName, ".png"))
		{
			char aName[IO_MAX_PATH_LENGTH];
			str_truncate(aName, sizeof(aName), pName, str_length(pName) - 4);
			// default is reserved
			if(str_comp(aName, "default") == 0)
				return 0;

			TName AssetItem;
			str_copy(AssetItem.m_aName, aName);
			LoadAsset(&AssetItem, pAssetName, pGraphics);
			vAssetList.push_back(AssetItem);
		}
	}

	pRealUser->m_LoadedFunc();

	return 0;
}

int CMenus::GameScan(const char *pName, int IsDir, int DirType, void *pUser)
{
	auto *pRealUser = (SMenuAssetScanUser *)pUser;
	auto *pThis = (CMenus *)pRealUser->m_pUser;
	IGraphics *pGraphics = pThis->Graphics();
	return AssetScan(pName, IsDir, DirType, pThis->m_vGameList, "game", pGraphics, pUser);
}

int CMenus::DownloadedGameScan(const char *pName, int IsDir, int DirType, void *pUser)
{
	if(IsDir || !pName || !pName[0])
		return 0;

	if(!str_endswith(pName, ".png") && !str_endswith(pName, ".PNG"))
		return 0;

	char aName[IO_MAX_PATH_LENGTH];
	str_truncate(aName, sizeof(aName), pName, str_length(pName) - 4);
	if(aName[0] == '\0' || str_comp(aName, "default") == 0)
		return 0;

	auto *pRealUser = (SMenuAssetScanUser *)pUser;
	auto *pThis = (CMenus *)pRealUser->m_pUser;

	SCustomGame AssetItem;
	str_copy(AssetItem.m_aName, aName, sizeof(AssetItem.m_aName));
	AssetItem.m_Deletable = true;
	pThis->m_vDownloadedGameList.push_back(AssetItem);

	pRealUser->m_LoadedFunc();
	return 0;
}

int CMenus::EmoticonsScan(const char *pName, int IsDir, int DirType, void *pUser)
{
	auto *pRealUser = (SMenuAssetScanUser *)pUser;
	auto *pThis = (CMenus *)pRealUser->m_pUser;
	IGraphics *pGraphics = pThis->Graphics();
	return AssetScan(pName, IsDir, DirType, pThis->m_vEmoticonList, "emoticons", pGraphics, pUser);
}

int CMenus::ParticlesScan(const char *pName, int IsDir, int DirType, void *pUser)
{
	auto *pRealUser = (SMenuAssetScanUser *)pUser;
	auto *pThis = (CMenus *)pRealUser->m_pUser;
	IGraphics *pGraphics = pThis->Graphics();
	return AssetScan(pName, IsDir, DirType, pThis->m_vParticlesList, "particles", pGraphics, pUser);
}

int CMenus::HudScan(const char *pName, int IsDir, int DirType, void *pUser)
{
	auto *pRealUser = (SMenuAssetScanUser *)pUser;
	auto *pThis = (CMenus *)pRealUser->m_pUser;
	IGraphics *pGraphics = pThis->Graphics();
	return AssetScan(pName, IsDir, DirType, pThis->m_vHudList, "hud", pGraphics, pUser);
}

int CMenus::ExtrasScan(const char *pName, int IsDir, int DirType, void *pUser)
{
	auto *pRealUser = (SMenuAssetScanUser *)pUser;
	auto *pThis = (CMenus *)pRealUser->m_pUser;
	IGraphics *pGraphics = pThis->Graphics();
	return AssetScan(pName, IsDir, DirType, pThis->m_vExtrasList, "extras", pGraphics, pUser);
}

static std::vector<const CMenus::SCustomEntities *> gs_vpSearchEntitiesList;
static std::vector<const CMenus::SCustomGame *> gs_vpSearchGamesList;
static std::vector<const CMenus::SCustomGame *> gs_vpSearchDownloadedGamesList;
static std::vector<const CMenus::SCustomEmoticon *> gs_vpSearchEmoticonsList;
static std::vector<const CMenus::SCustomParticle *> gs_vpSearchParticlesList;
static std::vector<const CMenus::SCustomHud *> gs_vpSearchHudList;
static std::vector<const CMenus::SCustomExtras *> gs_vpSearchExtrasList;

static bool gs_aInitCustomList[NUMBER_OF_ASSETS_TABS] = {
	true,
};

static size_t gs_aCustomListSize[NUMBER_OF_ASSETS_TABS] = {
	0,
};

void CMenus::InvalidateAssetsTab(int Tab)
{
	if(Tab >= 0 && Tab < NUMBER_OF_ASSETS_TABS)
		gs_aInitCustomList[Tab] = true;
}

static void SortGamesList(std::vector<const CMenus::SCustomGame *> &vpSearchList, const std::map<std::string, std::string> &InUseMap)
{
	std::stable_sort(vpSearchList.begin(), vpSearchList.end(), [&InUseMap](const CMenus::SCustomGame *pA, const CMenus::SCustomGame *pB) {
		const bool AInUse = InUseMap.find(pA->m_aName) != InUseMap.end();
		const bool BInUse = InUseMap.find(pB->m_aName) != InUseMap.end();
		if(AInUse != BInUse)
			return AInUse > BInUse;

		const bool AIsDefault = (str_comp(pA->m_aName, "default") == 0);
		const bool BIsDefault = (str_comp(pB->m_aName, "default") == 0);
		if(AIsDefault != BIsDefault)
			return AIsDefault > BIsDefault;

		return str_comp_nocase(pA->m_aName, pB->m_aName) < 0;
	});
}

static CLineInputBuffered<64> s_aFilterInputs[NUMBER_OF_ASSETS_TABS];

static int s_CurCustomTab = ASSETS_TAB_ENTITIES;

void CMenus::SetCustomAssetsTab(int Tab)
{
	if(Tab >= 0 && Tab < NUMBER_OF_ASSETS_TABS)
		s_CurCustomTab = Tab;
}

int CMenus::GetCustomAssetsTab() const
{
	return s_CurCustomTab;
}

static const CMenus::SCustomItem *GetCustomItem(int CurTab, size_t Index)
{
	if(CurTab == ASSETS_TAB_ENTITIES)
		return gs_vpSearchEntitiesList[Index];
	else if(CurTab == ASSETS_TAB_GAME)
		return gs_vpSearchGamesList[Index];
	else if(CurTab == ASSETS_TAB_DOWNLOADED_GAME)
		return gs_vpSearchDownloadedGamesList[Index];
	else if(CurTab == ASSETS_TAB_EMOTICONS)
		return gs_vpSearchEmoticonsList[Index];
	else if(CurTab == ASSETS_TAB_PARTICLES)
		return gs_vpSearchParticlesList[Index];
	else if(CurTab == ASSETS_TAB_HUD)
		return gs_vpSearchHudList[Index];
	else if(CurTab == ASSETS_TAB_EXTRAS)
		return gs_vpSearchExtrasList[Index];
	dbg_assert_failed("Invalid CurTab: %d", CurTab);
}

template<typename TName>
static void ClearAssetList(std::vector<TName> &vList, IGraphics *pGraphics)
{
	for(TName &Asset : vList)
	{
		pGraphics->UnloadTexture(&Asset.m_RenderTexture);
	}
	vList.clear();
}

void CMenus::ClearCustomItems(int CurTab)
{
	if(CurTab == ASSETS_TAB_ENTITIES)
	{
		for(auto &Entity : m_vEntitiesList)
		{
			for(auto &Image : Entity.m_aImages)
			{
				Graphics()->UnloadTexture(&Image.m_Texture);
			}
		}
		m_vEntitiesList.clear();

		// reload current entities
		GameClient()->m_MapImages.ChangeEntitiesPath(g_Config.m_ClAssetsEntities);
	}
	else if(CurTab == ASSETS_TAB_GAME)
	{
		ClearAssetList(m_vGameList, Graphics());

		// reload current game skin
		GameClient()->LoadGameSkin(g_Config.m_ClAssetGame);
	}
	else if(CurTab == ASSETS_TAB_DOWNLOADED_GAME)
	{
		ClearAssetList(m_vDownloadedGameList, Graphics());

		// reload current game skin
		GameClient()->LoadGameSkin(g_Config.m_ClAssetGame);
	}
	else if(CurTab == ASSETS_TAB_EMOTICONS)
	{
		ClearAssetList(m_vEmoticonList, Graphics());

		// reload current emoticons skin
		GameClient()->LoadEmoticonsSkin(g_Config.m_ClAssetEmoticons);
	}
	else if(CurTab == ASSETS_TAB_PARTICLES)
	{
		ClearAssetList(m_vParticlesList, Graphics());

		// reload current particles skin
		GameClient()->LoadParticlesSkin(g_Config.m_ClAssetParticles);
	}
	else if(CurTab == ASSETS_TAB_HUD)
	{
		ClearAssetList(m_vHudList, Graphics());

		// reload current hud skin
		GameClient()->LoadHudSkin(g_Config.m_ClAssetHud);
	}
	else if(CurTab == ASSETS_TAB_EXTRAS)
	{
		ClearAssetList(m_vExtrasList, Graphics());

		// reload current DDNet particles skin
		GameClient()->LoadExtrasSkin(g_Config.m_ClAssetExtras);
	}
	else
	{
		dbg_assert_failed("Invalid CurTab: %d", CurTab);
	}
	gs_aInitCustomList[CurTab] = true;
}

template<typename TName, typename TCaller>
static void InitAssetList(std::vector<TName> &vAssetList, const char *pAssetPath, const char *pAssetName, FS_LISTDIR_CALLBACK pfnCallback, IGraphics *pGraphics, IStorage *pStorage, TCaller Caller)
{
	if(vAssetList.empty())
	{
		TName AssetItem;
		str_copy(AssetItem.m_aName, "default");
		LoadAsset(&AssetItem, pAssetName, pGraphics);
		vAssetList.push_back(AssetItem);

		// load assets
		pStorage->ListDirectory(IStorage::TYPE_ALL, pAssetPath, pfnCallback, Caller);
		std::sort(vAssetList.begin(), vAssetList.end());
	}
	if(vAssetList.size() != gs_aCustomListSize[s_CurCustomTab])
		gs_aInitCustomList[s_CurCustomTab] = true;
}

template<typename TName>
static int InitSearchList(std::vector<const TName *> &vpSearchList, std::vector<TName> &vAssetList)
{
	vpSearchList.clear();
	int ListSize = vAssetList.size();
	for(int i = 0; i < ListSize; ++i)
	{
		const TName *pAsset = &vAssetList[i];

		// filter quick search
		if(!s_aFilterInputs[s_CurCustomTab].IsEmpty() && !str_utf8_find_nocase(pAsset->m_aName, s_aFilterInputs[s_CurCustomTab].GetString()))
			continue;

		vpSearchList.push_back(pAsset);
	}
	return vAssetList.size();
}

static const char *AssetsGetTabFolder(int Tab)
{
	switch(Tab)
	{
	case ASSETS_TAB_ENTITIES: return "entities";
	case ASSETS_TAB_GAME: return "game";
	case ASSETS_TAB_EMOTICONS: return "emoticons";
	case ASSETS_TAB_PARTICLES: return "particles";
	case ASSETS_TAB_HUD: return "hud";
	case ASSETS_TAB_EXTRAS: return "extras";
	default: return "game";
	}
}

static void AssetsGetRelativePaths(int Tab, const char *pName, char *pFilePath, int FilePathSize, char *pFolderPath, int FolderPathSize)
{
	if(pFilePath && FilePathSize > 0) pFilePath[0] = '\0';
	if(pFolderPath && FolderPathSize > 0) pFolderPath[0] = '\0';
	if(pName == nullptr || pName[0] == '\0') return;
	if(Tab == ASSETS_TAB_DOWNLOADED_GAME)
	{
		if(pFilePath && FilePathSize > 0)
			str_format(pFilePath, FilePathSize, "downloaded_gameskins/%s.png", pName);
		return;
	}
	const char *pFolder = AssetsGetTabFolder(Tab);
	if(pFilePath && FilePathSize > 0)
		str_format(pFilePath, FilePathSize, "assets/%s/%s.png", pFolder, pName);
	if(pFolderPath && FolderPathSize > 0)
		str_format(pFolderPath, FolderPathSize, "assets/%s/%s", pFolder, pName);
}

bool CMenus::CanDeleteCustomAsset(int Tab, const char *pName) const
{
	if(Tab < 0 || Tab >= NUMBER_OF_ASSETS_TABS || pName == nullptr || pName[0] == '\0')
		return false;
	if(str_comp(pName, "default") == 0)
		return false;

	char aFilePath[IO_MAX_PATH_LENGTH];
	char aFolderPath[IO_MAX_PATH_LENGTH];
	AssetsGetRelativePaths(Tab, pName, aFilePath, sizeof(aFilePath), aFolderPath, sizeof(aFolderPath));

	if(aFilePath[0] != '\0' && Storage()->FileExists(aFilePath, IStorage::TYPE_SAVE))
		return true;
	if(aFolderPath[0] != '\0' && Storage()->FolderExists(aFolderPath, IStorage::TYPE_SAVE))
		return true;
	return false;
}

struct SAssetsDeleteFolderContext
{
	IStorage *m_pStorage;
	char m_aBasePath[IO_MAX_PATH_LENGTH];
	bool m_Success = true;
	int m_Depth = 0;
};

static int AssetsDeleteFolderContents(const char *pName, int IsDir, int DirType, void *pUser)
{
	auto *pContext = static_cast<SAssetsDeleteFolderContext *>(pUser);
	if(pName[0] == '.' && (pName[1] == '\0' || (pName[1] == '.' && pName[2] == '\0')))
		return 0;

	char aPath[IO_MAX_PATH_LENGTH];
	str_format(aPath, sizeof(aPath), "%s/%s", pContext->m_aBasePath, pName);
	if(IsDir)
	{
		if(pContext->m_Depth >= 16)
		{
			pContext->m_Success = false;
			return 0;
		}
		SAssetsDeleteFolderContext ChildContext;
		ChildContext.m_pStorage = pContext->m_pStorage;
		ChildContext.m_Depth = pContext->m_Depth + 1;
		str_copy(ChildContext.m_aBasePath, aPath);
		pContext->m_pStorage->ListDirectory(IStorage::TYPE_SAVE, aPath, AssetsDeleteFolderContents, &ChildContext);
		if(!ChildContext.m_Success || !pContext->m_pStorage->RemoveFolder(aPath, IStorage::TYPE_SAVE))
			pContext->m_Success = false;
	}
	else if(!pContext->m_pStorage->RemoveFile(aPath, IStorage::TYPE_SAVE))
	{
		pContext->m_Success = false;
	}
	return 0;
}

static bool AssetsDeleteSavePath(IStorage *pStorage, const char *pPath, bool IsFolder)
{
	if(pPath == nullptr || pPath[0] == '\0')
		return true;

	if(IsFolder)
	{
		if(!pStorage->FolderExists(pPath, IStorage::TYPE_SAVE))
			return true;

		SAssetsDeleteFolderContext Context;
		Context.m_pStorage = pStorage;
		str_copy(Context.m_aBasePath, pPath);
		pStorage->ListDirectory(IStorage::TYPE_SAVE, pPath, AssetsDeleteFolderContents, &Context);
		if(!Context.m_Success)
			return false;
		return pStorage->RemoveFolder(pPath, IStorage::TYPE_SAVE);
	}

	if(!pStorage->FileExists(pPath, IStorage::TYPE_SAVE))
		return true;
	return pStorage->RemoveFile(pPath, IStorage::TYPE_SAVE);
}

bool CMenus::DeleteCustomAsset(int Tab, const char *pName)
{
	if(!CanDeleteCustomAsset(Tab, pName))
		return false;

	char aFilePath[IO_MAX_PATH_LENGTH];
	char aFolderPath[IO_MAX_PATH_LENGTH];
	AssetsGetRelativePaths(Tab, pName, aFilePath, sizeof(aFilePath), aFolderPath, sizeof(aFolderPath));

	AssetsDeleteSavePath(Storage(), aFilePath, false);
	AssetsDeleteSavePath(Storage(), aFolderPath, true);
	return !CanDeleteCustomAsset(Tab, pName);
}

static void AssetsResetSelectedToDefault(int Tab)
{
	if(Tab == ASSETS_TAB_ENTITIES)
		str_copy(g_Config.m_ClAssetsEntities, "default");
	else if(Tab == ASSETS_TAB_GAME || Tab == ASSETS_TAB_DOWNLOADED_GAME)
		str_copy(g_Config.m_ClAssetGame, "default");
	else if(Tab == ASSETS_TAB_EMOTICONS)
		str_copy(g_Config.m_ClAssetEmoticons, "default");
	else if(Tab == ASSETS_TAB_PARTICLES)
		str_copy(g_Config.m_ClAssetParticles, "default");
	else if(Tab == ASSETS_TAB_HUD)
		str_copy(g_Config.m_ClAssetHud, "default");
	else if(Tab == ASSETS_TAB_EXTRAS)
		str_copy(g_Config.m_ClAssetExtras, "default");
}

static bool AssetsIsCurrentlySelected(int Tab, const char *pName)
{
	if(Tab == ASSETS_TAB_ENTITIES)
		return str_comp(pName, g_Config.m_ClAssetsEntities) == 0;
	if(Tab == ASSETS_TAB_GAME || Tab == ASSETS_TAB_DOWNLOADED_GAME)
		return str_comp(pName, g_Config.m_ClAssetGame) == 0;
	if(Tab == ASSETS_TAB_EMOTICONS)
		return str_comp(pName, g_Config.m_ClAssetEmoticons) == 0;
	if(Tab == ASSETS_TAB_PARTICLES)
		return str_comp(pName, g_Config.m_ClAssetParticles) == 0;
	if(Tab == ASSETS_TAB_HUD)
		return str_comp(pName, g_Config.m_ClAssetHud) == 0;
	if(Tab == ASSETS_TAB_EXTRAS)
		return str_comp(pName, g_Config.m_ClAssetExtras) == 0;
	return false;
}

static void AssetsUnloadEntitiesPreview(CMenus::SCustomEntities &Entity, IGraphics *pGraphics)
{
	for(int i = 0; i < MAP_IMAGE_MOD_TYPE_COUNT; ++i)
	{
		IGraphics::CTextureHandle &Tex = Entity.m_aImages[i].m_Texture;
		if(!Tex.IsValid() || Tex.IsNullTexture())
			continue;

		const int TextureId = Tex.Id();
		for(int j = i + 1; j < MAP_IMAGE_MOD_TYPE_COUNT; ++j)
		{
			if(Entity.m_aImages[j].m_Texture.IsValid() && !Entity.m_aImages[j].m_Texture.IsNullTexture() && Entity.m_aImages[j].m_Texture.Id() == TextureId)
				Entity.m_aImages[j].m_Texture.Invalidate();
		}
		if(Entity.m_RenderTexture.IsValid() && !Entity.m_RenderTexture.IsNullTexture() && Entity.m_RenderTexture.Id() == TextureId)
			Entity.m_RenderTexture.Invalidate();

		pGraphics->UnloadTexture(&Tex);
	}
	Entity.m_RenderTexture.Invalidate();
}

template<typename TName>
static void AssetsEraseNamedPreview(std::vector<TName> &vList, IGraphics *pGraphics, const char *pName)
{
	for(auto It = vList.begin(); It != vList.end();)
	{
		if(str_comp(It->m_aName, pName) != 0)
		{
			++It;
			continue;
		}
		if(It->m_RenderTexture.IsValid())
			pGraphics->UnloadTexture(&It->m_RenderTexture);
		It = vList.erase(It);
	}
}

void CMenus::RemoveCustomAssetFromList(int Tab, const char *pName)
{
	if(Tab == ASSETS_TAB_ENTITIES)
	{
		for(auto It = m_vEntitiesList.begin(); It != m_vEntitiesList.end();)
		{
			if(str_comp(It->m_aName, pName) != 0)
			{
				++It;
				continue;
			}
			AssetsUnloadEntitiesPreview(*It, Graphics());
			It = m_vEntitiesList.erase(It);
		}
	}
	else if(Tab == ASSETS_TAB_GAME)
		AssetsEraseNamedPreview(m_vGameList, Graphics(), pName);
	else if(Tab == ASSETS_TAB_DOWNLOADED_GAME)
		AssetsEraseNamedPreview(m_vDownloadedGameList, Graphics(), pName);
	else if(Tab == ASSETS_TAB_EMOTICONS)
		AssetsEraseNamedPreview(m_vEmoticonList, Graphics(), pName);
	else if(Tab == ASSETS_TAB_PARTICLES)
		AssetsEraseNamedPreview(m_vParticlesList, Graphics(), pName);
	else if(Tab == ASSETS_TAB_HUD)
		AssetsEraseNamedPreview(m_vHudList, Graphics(), pName);
	else if(Tab == ASSETS_TAB_EXTRAS)
		AssetsEraseNamedPreview(m_vExtrasList, Graphics(), pName);
}

static int SingleAssetCategoryFromTab(int Tab)
{
	switch(Tab)
	{
	case ASSETS_TAB_ENTITIES: return SINGLE_ASSET_ENTITIES;
	case ASSETS_TAB_GAME: return SINGLE_ASSET_GAME;
	case ASSETS_TAB_EMOTICONS: return SINGLE_ASSET_EMOTICONS;
	case ASSETS_TAB_PARTICLES: return SINGLE_ASSET_PARTICLES;
	case ASSETS_TAB_HUD: return SINGLE_ASSET_HUD;
	case ASSETS_TAB_EXTRAS: return SINGLE_ASSET_EXTRAS;
	default: return -1;
	}
}

static const char *SingleAssetBasePackName(int SingleCat)
{
	switch(SingleCat)
	{
	case SINGLE_ASSET_ENTITIES: return g_Config.m_ClAssetsEntities;
	case SINGLE_ASSET_GAME: return g_Config.m_ClAssetGame;
	case SINGLE_ASSET_EMOTICONS: return g_Config.m_ClAssetEmoticons;
	case SINGLE_ASSET_PARTICLES: return g_Config.m_ClAssetParticles;
	case SINGLE_ASSET_HUD: return g_Config.m_ClAssetHud;
	case SINGLE_ASSET_EXTRAS: return g_Config.m_ClAssetExtras;
	default: return "default";
	}
}

static void SetSingleAssetBasePackName(int SingleCat, const char *pName)
{
	if(!pName || pName[0] == '\0')
		pName = "default";
	switch(SingleCat)
	{
	case SINGLE_ASSET_ENTITIES:
		str_copy(g_Config.m_ClAssetsEntities, pName);
		break;
	case SINGLE_ASSET_GAME:
		str_copy(g_Config.m_ClAssetGame, pName);
		break;
	case SINGLE_ASSET_EMOTICONS:
		str_copy(g_Config.m_ClAssetEmoticons, pName);
		break;
	case SINGLE_ASSET_PARTICLES:
		str_copy(g_Config.m_ClAssetParticles, pName);
		break;
	case SINGLE_ASSET_HUD:
		str_copy(g_Config.m_ClAssetHud, pName);
		break;
	case SINGLE_ASSET_EXTRAS:
		str_copy(g_Config.m_ClAssetExtras, pName);
		break;
	default:
		break;
	}
}

struct SSingleAssetSlotDef
{
	int m_SpriteId;
	const char *m_pDisplayName;
};

static const std::vector<SSingleAssetSlotDef> &GetSingleAssetSlots(int SingleCat)
{
	static const std::vector<SSingleAssetSlotDef> s_vGameSlots = {
		{SPRITE_WEAPON_HAMMER_BODY, "Martillo"},
		{SPRITE_WEAPON_GUN_BODY, "Pistola"},
		{SPRITE_WEAPON_SHOTGUN_BODY, "Escopeta"},
		{SPRITE_WEAPON_GRENADE_BODY, "Granada"},
		{SPRITE_WEAPON_LASER_BODY, "Laser"},
		{SPRITE_WEAPON_NINJA_BODY, "Ninja"},
		{SPRITE_HOOK_HEAD, "Gancho (cabeza)"},
		{SPRITE_HOOK_CHAIN, "Gancho (cadena)"},
		{SPRITE_WEAPON_HAMMER_CURSOR, "Mira Martillo"},
		{SPRITE_WEAPON_GUN_CURSOR, "Mira Pistola"},
		{SPRITE_WEAPON_SHOTGUN_CURSOR, "Mira Escopeta"},
		{SPRITE_WEAPON_GRENADE_CURSOR, "Mira Granada"},
		{SPRITE_WEAPON_LASER_CURSOR, "Mira Laser"},
		{SPRITE_WEAPON_NINJA_CURSOR, "Mira Ninja"},
		{SPRITE_HEALTH_FULL, "health_full"},
		{SPRITE_HEALTH_EMPTY, "health_empty"},
		{SPRITE_ARMOR_FULL, "armor_full"},
		{SPRITE_ARMOR_EMPTY, "armor_empty"},
		{SPRITE_PART1, "part1"},
		{SPRITE_PART2, "part2"},
		{SPRITE_PART3, "part3"},
		{SPRITE_PART4, "part4"},
		{SPRITE_PART5, "part5"},
		{SPRITE_PART6, "part6"},
		{SPRITE_PART7, "part7"},
		{SPRITE_PART8, "part8"},
		{SPRITE_PART9, "part9"},
		{SPRITE_STAR1, "star1"},
		{SPRITE_STAR2, "star2"},
		{SPRITE_STAR3, "star3"},
		{SPRITE_WEAPON_GUN_PROJ, "weapon_gun_proj"},
		{SPRITE_WEAPON_SHOTGUN_PROJ, "weapon_shotgun_proj"},
		{SPRITE_WEAPON_GRENADE_PROJ, "weapon_grenade_proj"},
		{SPRITE_WEAPON_LASER_PROJ, "weapon_laser_proj"},
		{SPRITE_WEAPON_GUN_MUZZLE1, "weapon_gun_muzzle1"},
		{SPRITE_WEAPON_GUN_MUZZLE2, "weapon_gun_muzzle2"},
		{SPRITE_WEAPON_GUN_MUZZLE3, "weapon_gun_muzzle3"},
		{SPRITE_WEAPON_SHOTGUN_MUZZLE1, "weapon_shotgun_muzzle1"},
		{SPRITE_WEAPON_SHOTGUN_MUZZLE2, "weapon_shotgun_muzzle2"},
		{SPRITE_WEAPON_SHOTGUN_MUZZLE3, "weapon_shotgun_muzzle3"},
		{SPRITE_WEAPON_NINJA_MUZZLE1, "weapon_ninja_muzzle1"},
		{SPRITE_WEAPON_NINJA_MUZZLE2, "weapon_ninja_muzzle2"},
		{SPRITE_WEAPON_NINJA_MUZZLE3, "weapon_ninja_muzzle3"},
		{SPRITE_PICKUP_HEALTH, "pickup_health"},
		{SPRITE_PICKUP_ARMOR, "pickup_armor"},
		{SPRITE_PICKUP_HAMMER, "pickup_hammer"},
		{SPRITE_PICKUP_GUN, "pickup_gun"},
		{SPRITE_PICKUP_SHOTGUN, "pickup_shotgun"},
		{SPRITE_PICKUP_GRENADE, "pickup_grenade"},
		{SPRITE_PICKUP_LASER, "pickup_laser"},
		{SPRITE_PICKUP_NINJA, "pickup_ninja"},
		{SPRITE_PICKUP_FREEZE, "pickup_freeze"},
		{SPRITE_PICKUP_ARMOR_SHOTGUN, "pickup_armor_shotgun"},
		{SPRITE_PICKUP_ARMOR_GRENADE, "pickup_armor_grenade"},
		{SPRITE_PICKUP_ARMOR_NINJA, "pickup_armor_ninja"},
		{SPRITE_PICKUP_ARMOR_LASER, "pickup_armor_laser"},
		{SPRITE_FLAG_BLUE, "flag_blue"},
		{SPRITE_FLAG_RED, "flag_red"},
	};

	static const std::vector<SSingleAssetSlotDef> s_vEmoticonsSlots = {
		{SPRITE_OOP, "Oop!"},
		{SPRITE_EXCLAMATION, "Exclamacion"},
		{SPRITE_HEARTS, "Corazones"},
		{SPRITE_DROP, "Gota"},
		{SPRITE_DOTDOT, "Puntos (...)"},
		{SPRITE_MUSIC, "Musica"},
		{SPRITE_SORRY, "Perdon"},
		{SPRITE_GHOST, "Fantasma"},
		{SPRITE_SUSHI, "Sushi"},
		{SPRITE_SPLATTEE, "Splattee"},
		{SPRITE_DEVILTEE, "Diablito"},
		{SPRITE_ZOMG, "Zomg!"},
		{SPRITE_ZZZ, "Zzz"},
		{SPRITE_WTF, "Wtf"},
		{SPRITE_EYES, "Ojos"},
		{SPRITE_QUESTION, "Pregunta"},
	};

	static const std::vector<SSingleAssetSlotDef> s_vParticlesSlots = {
		{SPRITE_PART_SLICE, "Corte (Slice)"},
		{SPRITE_PART_BALL, "Bola (Ball)"},
		{SPRITE_PART_SPLAT01, "Mancha 1"},
		{SPRITE_PART_SPLAT02, "Mancha 2"},
		{SPRITE_PART_SPLAT03, "Mancha 3"},
		{SPRITE_PART_SMOKE, "Humo (Smoke)"},
		{SPRITE_PART_SHELL, "Casquillo"},
		{SPRITE_PART_EXPL01, "Explosion"},
		{SPRITE_PART_AIRJUMP, "Doble salto"},
		{SPRITE_PART_HIT01, "Golpe (Hit)"},
	};

	static const std::vector<SSingleAssetSlotDef> s_vHudSlots = {
		{SPRITE_HUD_AIRJUMP, "Salto aire"},
		{SPRITE_HUD_AIRJUMP_EMPTY, "Salto aire vacio"},
		{SPRITE_HUD_SOLO, "Solo"},
		{SPRITE_HUD_COLLISION_DISABLED, "Sin colision"},
		{SPRITE_HUD_ENDLESS_JUMP, "Salto infinito"},
		{SPRITE_HUD_ENDLESS_HOOK, "Gancho infinito"},
		{SPRITE_HUD_JETPACK, "Jetpack"},
		{SPRITE_HUD_FREEZE_BAR_FULL_LEFT, "Barra Freeze Izq"},
		{SPRITE_HUD_FREEZE_BAR_FULL, "Barra Freeze Llena"},
		{SPRITE_HUD_FREEZE_BAR_EMPTY, "Barra Freeze Vacia"},
		{SPRITE_HUD_FREEZE_BAR_EMPTY_RIGHT, "Barra Freeze Der"},
		{SPRITE_HUD_NINJA_BAR_FULL_LEFT, "Barra Ninja Izq"},
		{SPRITE_HUD_NINJA_BAR_FULL, "Barra Ninja Llena"},
		{SPRITE_HUD_NINJA_BAR_EMPTY, "Barra Ninja Vacia"},
		{SPRITE_HUD_NINJA_BAR_EMPTY_RIGHT, "Barra Ninja Der"},
		{SPRITE_HUD_HOOK_HIT_DISABLED, "Sin hit Gancho"},
		{SPRITE_HUD_HAMMER_HIT_DISABLED, "Sin hit Martillo"},
		{SPRITE_HUD_SHOTGUN_HIT_DISABLED, "Sin hit Escopeta"},
		{SPRITE_HUD_GRENADE_HIT_DISABLED, "Sin hit Granada"},
		{SPRITE_HUD_LASER_HIT_DISABLED, "Sin hit Laser"},
		{SPRITE_HUD_GUN_HIT_DISABLED, "Sin hit Pistola"},
		{SPRITE_HUD_DEEP_FROZEN, "Deep Freeze"},
		{SPRITE_HUD_LIVE_FROZEN, "Live Freeze"},
		{SPRITE_HUD_TELEPORT_GRENADE, "Tele Granada"},
		{SPRITE_HUD_TELEPORT_GUN, "Tele Pistola"},
		{SPRITE_HUD_TELEPORT_LASER, "Tele Laser"},
		{SPRITE_HUD_PRACTICE_MODE, "Modo Practica"},
		{SPRITE_HUD_LOCK_MODE, "Team Bloqueado"},
		{SPRITE_HUD_TEAM0_MODE, "Modo Team 0"},
		{SPRITE_HUD_DUMMY_HAMMER, "Dummy Hammer"},
		{SPRITE_HUD_DUMMY_COPY, "Dummy Copy"},
	};

	static const std::vector<SSingleAssetSlotDef> s_vExtrasSlots = {
		{SPRITE_PART_SNOWFLAKE, "Copo de nieve"},
		{SPRITE_PART_SPARKLE, "Destello (Sparkle)"},
		{SPRITE_PART_PULLEY, "Polea (Pulley)"},
		{SPRITE_PART_HECTAGON, "Hexagono"},
	};

	static const std::vector<SSingleAssetSlotDef> s_vEntitiesSlots = {
		{TILE_SOLID, "Solido (Hookable)"},
		{TILE_NOHOOK, "No Hook (Unhook)"},
		{TILE_FREEZE, "Freeze"},
		{TILE_UNFREEZE, "Unfreeze"},
		{TILE_DFREEZE, "Deep Freeze"},
		{TILE_DUNFREEZE, "Deep Unfreeze"},
		{TILE_LFREEZE, "Live Freeze"},
		{TILE_LUNFREEZE, "Live Unfreeze"},
		{TILE_DEATH, "Muerte (Death)"},
		{TILE_START, "Start (Inicio)"},
		{TILE_FINISH, "Finish (Meta)"},
		{TILE_NOLASER, "No Laser"},
		{TILE_THROUGH_CUT, "Hookthrough"},
		{TILE_THROUGH, "Hookthrough (Viejo)"},
		{TILE_THROUGH_ALL, "Through All"},
		{TILE_THROUGH_DIR, "Through Dir"},
		{TILE_JUMP, "Salto extra"},
		{TILE_REFILL_JUMPS, "Recargar saltos"},
		{TILE_WALLJUMP, "Walljump"},
		{TILE_STOP, "Stopper (1 dir)"},
		{TILE_STOPS, "Stopper (2 dirs)"},
		{TILE_STOPA, "Stopper (4 dirs)"},
		{TILE_SPEED_BOOST, "Speedup / TeleCheck"},
		{TILE_TELEIN, "Tele In"},
		{TILE_TELEOUT, "Tele Out"},
		{TILE_TELEINEVIL, "Tele Evil"},
		{TILE_TELEINWEAPON, "Tele Arma"},
		{TILE_TELEINHOOK, "Tele Gancho"},
		{TILE_TELECHECKIN, "Tele Check In"},
		{TILE_TELECHECKOUT, "Tele Check Out"},
		{TILE_TELECHECKINEVIL, "Tele Check Evil"},
		{8, "Switch Timed Open"},
		{TILE_SWITCHTIMEDCLOSE, "Switch Timed Close"},
		{TILE_SWITCHOPEN, "Switch Open"},
		{TILE_SWITCHCLOSE, "Switch Close"},
		{TILE_SOLO_ENABLE, "Solo On"},
		{TILE_SOLO_DISABLE, "Solo Off"},
		{TILE_EHOOK_ENABLE, "Endless Hook On"},
		{TILE_EHOOK_DISABLE, "Endless Hook Off"},
		{TILE_HIT_ENABLE, "Hit On"},
		{TILE_HIT_DISABLE, "Hit Off"},
		{TILE_CP, "Checkpoint"},
		{TILE_CP_F, "Checkpoint F"},
		{TILE_TUNE, "Tune Zone"},
		{TILE_OLDLASER, "Old Laser"},
		{TILE_NPC, "No Colision"},
		{TILE_EHOOK, "Endless Hook"},
		{TILE_NOHIT, "No Hit"},
		{TILE_NPH, "No Player Hook"},
		{TILE_UNLOCK_TEAM, "Unlock Team"},
		{TILE_NPC_ENABLE, "Colision Off"},
		{TILE_NPC_DISABLE, "Colision On"},
		{TILE_UNLIMITED_JUMPS_ENABLE, "Endless Jump On"},
		{TILE_UNLIMITED_JUMPS_DISABLE, "Endless Jump Off"},
		{TILE_JETPACK_ENABLE, "Jetpack On"},
		{TILE_JETPACK_DISABLE, "Jetpack Off"},
		{TILE_NPH_ENABLE, "Player Hook Off"},
		{TILE_NPH_DISABLE, "Player Hook On"},
		{TILE_TELE_GUN_ENABLE, "Tele Gun On"},
		{TILE_TELE_GUN_DISABLE, "Tele Gun Off"},
		{TILE_ALLOW_TELE_GUN, "Allow Tele Gun"},
		{TILE_ALLOW_BLUE_TELE_GUN, "Allow Blue Tele Gun"},
		{TILE_TELE_GRENADE_ENABLE, "Tele Grenade On"},
		{TILE_TELE_GRENADE_DISABLE, "Tele Grenade Off"},
		{TILE_TELE_LASER_ENABLE, "Tele Laser On"},
		{TILE_TELE_LASER_DISABLE, "Tele Laser Off"},
		{TILE_ADD_TIME, "Add Time"},
		{TILE_SUBTRACT_TIME, "Subtract Time"},
		{TILE_ENTITIES_OFF_1, "Entities Off 1"},
		{TILE_ENTITIES_OFF_2, "Entities Off 2"},
		{ENTITY_OFFSET + ENTITY_SPAWN, "Spawn"},
		{ENTITY_OFFSET + ENTITY_SPAWN_RED, "Spawn Rojo"},
		{ENTITY_OFFSET + ENTITY_SPAWN_BLUE, "Spawn Azul"},
		{ENTITY_OFFSET + ENTITY_FLAGSTAND_RED, "Bandera Roja"},
		{ENTITY_OFFSET + ENTITY_FLAGSTAND_BLUE, "Bandera Azul"},
		{ENTITY_OFFSET + ENTITY_ARMOR_1, "Escudo (Armor)"},
		{ENTITY_OFFSET + ENTITY_HEALTH_1, "Vida (Health)"},
		{ENTITY_OFFSET + ENTITY_WEAPON_SHOTGUN, "Escopeta (Pickup)"},
		{ENTITY_OFFSET + ENTITY_WEAPON_GRENADE, "Granada (Pickup)"},
		{ENTITY_OFFSET + ENTITY_POWERUP_NINJA, "Ninja (Pickup)"},
		{ENTITY_OFFSET + ENTITY_WEAPON_LASER, "Laser (Pickup)"},
		{ENTITY_OFFSET + ENTITY_ARMOR_SHOTGUN, "Quitar Escopeta"},
		{ENTITY_OFFSET + ENTITY_ARMOR_GRENADE, "Quitar Granada"},
		{ENTITY_OFFSET + ENTITY_ARMOR_NINJA, "Quitar Ninja"},
		{ENTITY_OFFSET + ENTITY_ARMOR_LASER, "Quitar Laser"},
		{ENTITY_OFFSET + ENTITY_PLASMAE, "Plasma Explosiva"},
		{ENTITY_OFFSET + ENTITY_PLASMAF, "Plasma Freeze"},
		{ENTITY_OFFSET + ENTITY_PLASMA, "Plasma"},
		{ENTITY_OFFSET + ENTITY_PLASMAU, "Plasma Unfreeze"},
		{ENTITY_OFFSET + ENTITY_CRAZY_SHOTGUN, "Crazy Shotgun"},
		{ENTITY_OFFSET + ENTITY_CRAZY_SHOTGUN_EX, "Crazy Shotgun Ex"},
		{ENTITY_OFFSET + ENTITY_DRAGGER_WEAK, "Dragger Debil"},
		{ENTITY_OFFSET + ENTITY_DRAGGER_NORMAL, "Dragger Normal"},
		{ENTITY_OFFSET + ENTITY_DRAGGER_STRONG, "Dragger Fuerte"},
		{ENTITY_OFFSET + ENTITY_DRAGGER_WEAK_NW, "Dragger Debil NW"},
		{ENTITY_OFFSET + ENTITY_DRAGGER_NORMAL_NW, "Dragger Normal NW"},
		{ENTITY_OFFSET + ENTITY_DRAGGER_STRONG_NW, "Dragger Fuerte NW"},
		{ENTITY_OFFSET + ENTITY_DOOR, "Puerta (Door)"},
		{ENTITY_OFFSET + ENTITY_LASER_STOP, "Laser Stop"},
		{ENTITY_OFFSET + ENTITY_LASER_SHORT, "Laser Corto"},
		{ENTITY_OFFSET + ENTITY_LASER_MEDIUM, "Laser Medio"},
		{ENTITY_OFFSET + ENTITY_LASER_LONG, "Laser Largo"},
		{ENTITY_OFFSET + ENTITY_LASER_FAST_CCW, "Laser CCW Rapido"},
		{ENTITY_OFFSET + ENTITY_LASER_NORMAL_CCW, "Laser CCW Normal"},
		{ENTITY_OFFSET + ENTITY_LASER_SLOW_CCW, "Laser CCW Lento"},
		{ENTITY_OFFSET + ENTITY_LASER_SLOW_CW, "Laser CW Lento"},
		{ENTITY_OFFSET + ENTITY_LASER_NORMAL_CW, "Laser CW Normal"},
		{ENTITY_OFFSET + ENTITY_LASER_FAST_CW, "Laser CW Rapido"},
		{TILE_TIME_CHECKPOINT_FIRST + 0, "Tiempo CP 1"},
		{TILE_TIME_CHECKPOINT_FIRST + 1, "Tiempo CP 2"},
		{TILE_TIME_CHECKPOINT_FIRST + 2, "Tiempo CP 3"},
		{TILE_TIME_CHECKPOINT_FIRST + 3, "Tiempo CP 4"},
		{TILE_TIME_CHECKPOINT_FIRST + 4, "Tiempo CP 5"},
	};

	static const std::vector<SSingleAssetSlotDef> s_vEmpty;
	switch(SingleCat)
	{
	case SINGLE_ASSET_ENTITIES: return s_vEntitiesSlots;
	case SINGLE_ASSET_GAME: return s_vGameSlots;
	case SINGLE_ASSET_EMOTICONS: return s_vEmoticonsSlots;
	case SINGLE_ASSET_PARTICLES: return s_vParticlesSlots;
	case SINGLE_ASSET_HUD: return s_vHudSlots;
	case SINGLE_ASSET_EXTRAS: return s_vExtrasSlots;
	default: return s_vEmpty;
	}
}

static void CalcSafeAtlasUV(IGraphics *pGraphics, float DrawW, float DrawH, float GridX, float GridY, float CellX, float CellY, float CellW, float CellH, float &OutU0, float &OutV0, float &OutU1, float &OutV1)
{
	const CScreenRect Screen = pGraphics->GetScreen();
	const float PixelsPerUiX = (float)pGraphics->ScreenWidth() / std::max(1.0f, Screen.m_BottomRight.x - Screen.m_TopLeft.x);
	const float PixelsPerUiY = (float)pGraphics->ScreenHeight() / std::max(1.0f, Screen.m_BottomRight.y - Screen.m_TopLeft.y);
	const float DrawPxW = std::max(4.0f, DrawW * PixelsPerUiX);
	const float DrawPxH = std::max(4.0f, DrawH * PixelsPerUiY);

	const float SpanU = CellW / GridX;
	const float SpanV = CellH / GridY;
	const float InsetU = SpanU * std::min(0.06f, std::max(1.15f / DrawPxW, 0.5f / 64.0f));
	const float InsetV = SpanV * std::min(0.06f, std::max(1.15f / DrawPxH, 0.5f / 64.0f));

	OutU0 = (CellX / GridX) + InsetU;
	OutV0 = (CellY / GridY) + InsetV;
	OutU1 = ((CellX + CellW) / GridX) - InsetU;
	OutV1 = ((CellY + CellH) / GridY) - InsetV;
}

static void DrawSingleEntityTilePreview(IGraphics *pGraphics, IGraphics::CTextureHandle Texture, int TileIndex, const CUIRect &Box)
{
	if(!Texture.IsValid() || Texture.IsNullTexture() || TileIndex <= 0 || TileIndex >= 256)
		return;

	const float Side = std::min(Box.w, Box.h);
	const float DrawX = Box.x + (Box.w - Side) * 0.5f;
	const float DrawY = Box.y + (Box.h - Side) * 0.5f;

	float U0, V0, U1, V1;
	CalcSafeAtlasUV(pGraphics, Side, Side, 16.0f, 16.0f, (float)(TileIndex % 16), (float)(TileIndex / 16), 1.0f, 1.0f, U0, V0, U1, V1);

	pGraphics->WrapClamp();
	pGraphics->TextureSet(Texture);
	pGraphics->QuadsBegin();
	pGraphics->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
	pGraphics->QuadsSetSubset(U0, V0, U1, V1);
	IGraphics::CQuadItem Quad(DrawX, DrawY, Side, Side);
	pGraphics->QuadsDrawTL(&Quad, 1);
	pGraphics->QuadsSetSubset(0.0f, 0.0f, 1.0f, 1.0f);
	pGraphics->QuadsEnd();
	pGraphics->WrapNormal();
}

static void DrawSingleSpritePreview(IGraphics *pGraphics, IGraphics::CTextureHandle Texture, int SpriteId, const CUIRect &Box)
{
	if(!Texture.IsValid() || Texture.IsNullTexture() || SpriteId < 0 || SpriteId >= NUM_SPRITES)
		return;
	const CDataSprite &Sprite = g_pData->m_aSprites[SpriteId];
	if(!Sprite.m_pSet || Sprite.m_pSet->m_Gridx <= 0 || Sprite.m_pSet->m_Gridy <= 0 || Sprite.m_W <= 0 || Sprite.m_H <= 0)
		return;

	const float GridX = (float)Sprite.m_pSet->m_Gridx;
	const float GridY = (float)Sprite.m_pSet->m_Gridy;

	const float Aspect = (float)Sprite.m_W / (float)Sprite.m_H;
	float DrawW = Box.w;
	float DrawH = DrawW / Aspect;
	if(DrawH > Box.h)
	{
		DrawH = Box.h;
		DrawW = DrawH * Aspect;
	}
	const float DrawX = Box.x + (Box.w - DrawW) * 0.5f;
	const float DrawY = Box.y + (Box.h - DrawH) * 0.5f;

	float U0, V0, U1, V1;
	CalcSafeAtlasUV(pGraphics, DrawW, DrawH, GridX, GridY, (float)Sprite.m_X, (float)Sprite.m_Y, (float)Sprite.m_W, (float)Sprite.m_H, U0, V0, U1, V1);

	pGraphics->WrapClamp();
	pGraphics->TextureSet(Texture);
	pGraphics->QuadsBegin();
	pGraphics->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
	pGraphics->QuadsSetSubset(U0, V0, U1, V1);
	IGraphics::CQuadItem Quad(DrawX, DrawY, DrawW, DrawH);
	pGraphics->QuadsDrawTL(&Quad, 1);
	pGraphics->QuadsSetSubset(0.0f, 0.0f, 1.0f, 1.0f);
	pGraphics->QuadsEnd();
	pGraphics->WrapNormal();
}

static void DrawSinglePartPreview(IGraphics *pGraphics, IGraphics::CTextureHandle Texture, int SingleCat, int PartId, const CUIRect &Box)
{
	if(SingleCat == SINGLE_ASSET_ENTITIES)
		DrawSingleEntityTilePreview(pGraphics, Texture, PartId, Box);
	else
		DrawSingleSpritePreview(pGraphics, Texture, PartId, Box);
}

static void DrawSingleSheetPreview(IGraphics *pGraphics, IGraphics::CTextureHandle Texture, int SingleCat, const CUIRect &Box)
{
	if(!Texture.IsValid() || Texture.IsNullTexture())
		return;
	const float Aspect = (SingleCat == SINGLE_ASSET_GAME) ? 2.0f : 1.0f;
	float DrawW = Box.w;
	float DrawH = DrawW / Aspect;
	if(DrawH > Box.h)
	{
		DrawH = Box.h;
		DrawW = DrawH * Aspect;
	}
	const float DrawX = Box.x + (Box.w - DrawW) * 0.5f;
	const float DrawY = Box.y + (Box.h - DrawH) * 0.5f;

	pGraphics->WrapClamp();
	pGraphics->TextureSet(Texture);
	pGraphics->QuadsBegin();
	pGraphics->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
	IGraphics::CQuadItem Quad(DrawX, DrawY, DrawW, DrawH);
	pGraphics->QuadsDrawTL(&Quad, 1);
	pGraphics->QuadsEnd();
	pGraphics->WrapNormal();
}

struct SSingleAssetSpritePickerContext : public SPopupMenuId
{
	CMenus *m_pMenus = nullptr;
	int m_Category = -1;
	int m_SpriteId = -1;
	const char *m_pSpriteLabel = "";
	CScrollRegion m_ScrollRegion;
	CLineInputBuffered<32> m_FilterInput;
};

struct SSingleAssetBasePickerContext : public SPopupMenuId
{
	CMenus *m_pMenus = nullptr;
	int m_Category = -1;
	CScrollRegion m_ScrollRegion;
	CLineInputBuffered<32> m_FilterInput;
};

static SSingleAssetSpritePickerContext gs_SingleSpritePicker;
static SSingleAssetBasePickerContext gs_SingleBasePicker;

std::vector<const CMenus::SCustomItem *> CMenus::CollectSingleAssetPacks(int SingleCat) const
{
	std::vector<const SCustomItem *> vpOut;
	auto AppendList = [&](const auto &vList) {
		for(const auto &Item : vList)
		{
			if(Item.m_aName[0] == '\0')
				continue;
			vpOut.push_back(&Item);
		}
	};

	if(SingleCat == SINGLE_ASSET_ENTITIES)
		AppendList(m_vEntitiesList);
	else if(SingleCat == SINGLE_ASSET_GAME)
	{
		AppendList(m_vGameList);
		for(const auto &Dl : m_vDownloadedGameList)
		{
			if(Dl.m_aName[0] == '\0' || !Dl.m_RenderTexture.IsValid() || Dl.m_RenderTexture.IsNullTexture())
				continue;
			bool Exists = false;
			for(const SCustomItem *pExisting : vpOut)
			{
				if(str_comp(pExisting->m_aName, Dl.m_aName) == 0)
				{
					Exists = true;
					break;
				}
			}
			if(!Exists)
				vpOut.push_back(&Dl);
		}
	}
	else if(SingleCat == SINGLE_ASSET_EMOTICONS)
		AppendList(m_vEmoticonList);
	else if(SingleCat == SINGLE_ASSET_PARTICLES)
		AppendList(m_vParticlesList);
	else if(SingleCat == SINGLE_ASSET_HUD)
		AppendList(m_vHudList);
	else if(SingleCat == SINGLE_ASSET_EXTRAS)
		AppendList(m_vExtrasList);

	return vpOut;
}

IGraphics::CTextureHandle CMenus::FindSingleAssetPackTexture(int SingleCat, const char *pPackName) const
{
	const auto vpPacks = CollectSingleAssetPacks(SingleCat);
	IGraphics::CTextureHandle DefaultTex;
	for(const SCustomItem *pItem : vpPacks)
	{
		if(str_comp(pItem->m_aName, "default") == 0)
			DefaultTex = pItem->m_RenderTexture;
		if(pPackName && pPackName[0] != '\0' && str_comp(pItem->m_aName, pPackName) == 0 && pItem->m_RenderTexture.IsValid() && !pItem->m_RenderTexture.IsNullTexture())
			return pItem->m_RenderTexture;
	}
	return DefaultTex;
}

CUi::EPopupMenuFunctionResult CMenus::PopupSingleAssetSpritePicker(void *pContext, CUIRect View, bool Active)
{
	auto *pCtx = static_cast<SSingleAssetSpritePickerContext *>(pContext);
	CMenus *pMenus = pCtx->m_pMenus;
	if(!pMenus || pCtx->m_Category < 0 || pCtx->m_SpriteId < 0)
		return CUi::POPUP_CLOSE_CURRENT;

	if(Active && pMenus->Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE))
		return CUi::POPUP_CLOSE_CURRENT;

	View.Margin(6.0f, &View);

	CUIRect HeaderRect;
	View.HSplitTop(18.0f, &HeaderRect, &View);
	pMenus->TextRender()->TextColor(0.85f, 0.92f, 1.0f, 1.0f);
	pMenus->Ui()->DoLabel(&HeaderRect, pCtx->m_pSpriteLabel, 12.0f, TEXTALIGN_ML);
	pMenus->TextRender()->TextColor(pMenus->TextRender()->DefaultTextColor());
	View.HSplitTop(4.0f, nullptr, &View);

	const auto vpPacks = pMenus->CollectSingleAssetPacks(pCtx->m_Category);
	if(vpPacks.size() > 8)
	{
		CUIRect FilterRect;
		View.HSplitTop(20.0f, &FilterRect, &View);
		pCtx->m_FilterInput.SetEmptyText(Localize("Buscar pack..."));
		pMenus->Ui()->DoEditBox(&pCtx->m_FilterInput, &FilterRect, 11.5f);
		View.HSplitTop(5.0f, nullptr, &View);
	}

	const char *pBasePack = SingleAssetBasePackName(pCtx->m_Category);
	const char *pCurOverride = pMenus->GameClient()->m_SingleAssets.GetPartOverrideRaw(pCtx->m_Category, pCtx->m_SpriteId);
	const bool UsingBase = (!pCurOverride || pCurOverride[0] == '\0');
	const char *pFilter = pCtx->m_FilterInput.GetString();

	CScrollRegionParams ScrollParams;
	ScrollParams.m_ScrollbarThickness = 8.0f;
	ScrollParams.m_ScrollbarMargin = 3.0f;
	ScrollParams.m_ScrollUnit = 32.0f;
	ScrollParams.m_SliderColor = ColorRGBA(0.24f, 0.52f, 0.72f, 0.85f);
	ScrollParams.m_SliderColorHover = ColorRGBA(0.32f, 0.64f, 0.86f, 0.95f);
	ScrollParams.m_SliderColorGrabbed = ColorRGBA(0.38f, 0.72f, 0.95f, 1.0f);
	pCtx->m_ScrollRegion.Begin(&View, &ScrollParams);

	const float RowH = 30.0f;
	auto DrawOptionRow = [&](const void *pBtnId, const char *pLabel, IGraphics::CTextureHandle Tex, bool IsSelected, bool IsBaseOpt) -> bool {
		CUIRect Row;
		View.HSplitTop(RowH, &Row, &View);
		View.HSplitTop(2.0f, nullptr, &View);
		if(!pCtx->m_ScrollRegion.AddRect(Row))
			return false;

		const bool Hovered = Active && pMenus->Ui()->MouseInside(&Row);
		if(IsSelected)
			Row.Draw(Hovered ? ColorRGBA(0.16f, 0.36f, 0.52f, 0.95f) : ColorRGBA(0.13f, 0.28f, 0.42f, 0.92f), IGraphics::CORNER_ALL, 4.0f);
		else if(Hovered)
			Row.Draw(ColorRGBA(0.16f, 0.21f, 0.28f, 0.85f), IGraphics::CORNER_ALL, 4.0f);

		CUIRect Inner = Row;
		Inner.Margin(3.0f, &Inner);
		CUIRect PreviewRect, DotRect;
		Inner.VSplitLeft(44.0f, &PreviewRect, &Inner);
		Inner.VSplitLeft(6.0f, nullptr, &Inner);
		Inner.VSplitRight(14.0f, &Inner, &DotRect);

		DrawSinglePartPreview(pMenus->Graphics(), Tex, pCtx->m_Category, pCtx->m_SpriteId, PreviewRect);

		pMenus->TextRender()->TextColor(IsBaseOpt ? ColorRGBA(0.76f, 0.92f, 1.0f, 1.0f) : ColorRGBA(0.94f, 0.96f, 0.98f, 1.0f));
		SLabelProperties Props;
		Props.m_MaxWidth = Inner.w;
		Props.m_EllipsisAtEnd = true;
		pMenus->Ui()->DoLabel(&Inner, pLabel, 11.5f, TEXTALIGN_ML, Props);
		pMenus->TextRender()->TextColor(pMenus->TextRender()->DefaultTextColor());

		if(IsSelected)
		{
			const float DotSize = 5.0f;
			CUIRect Dot(DotRect.x + (DotRect.w - DotSize) * 0.5f, DotRect.y + (DotRect.h - DotSize) * 0.5f, DotSize, DotSize);
			Dot.Draw(ColorRGBA(0.95f, 0.98f, 1.0f, 1.0f), IGraphics::CORNER_ALL, DotSize * 0.5f);
		}

		return Active && pMenus->Ui()->DoButtonLogic(pBtnId, 0, &Row, BUTTONFLAG_LEFT) != 0;
	};

	static char s_BaseOptId;
	if(pFilter[0] == '\0' || str_utf8_find_nocase("Usar pack base", pFilter))
	{
		const IGraphics::CTextureHandle BaseTex = pMenus->FindSingleAssetPackTexture(pCtx->m_Category, pBasePack);
		if(DrawOptionRow(&s_BaseOptId, Localize("Usar pack base"), BaseTex, UsingBase, true))
		{
			pMenus->GameClient()->m_SingleAssets.SetPartOverride(pCtx->m_Category, pCtx->m_SpriteId, "");
			pMenus->GameClient()->m_SingleAssets.Save(pMenus->Storage());
			pMenus->GameClient()->ReloadSingleAssetCategory(pCtx->m_Category);
			pCtx->m_ScrollRegion.End();
			return CUi::POPUP_CLOSE_CURRENT;
		}
	}

	for(const SCustomItem *pPack : vpPacks)
	{
		if(pFilter[0] != '\0' && !str_utf8_find_nocase(pPack->m_aName, pFilter))
			continue;

		const bool IsSelected = !UsingBase && str_comp(pCurOverride, pPack->m_aName) == 0;
		if(DrawOptionRow(pPack, pPack->m_aName, pPack->m_RenderTexture, IsSelected, false))
		{
			const char *pNewOverride = (str_comp(pPack->m_aName, pBasePack) == 0) ? "" : pPack->m_aName;
			pMenus->GameClient()->m_SingleAssets.SetPartOverride(pCtx->m_Category, pCtx->m_SpriteId, pNewOverride);
			pMenus->GameClient()->m_SingleAssets.Save(pMenus->Storage());
			pMenus->GameClient()->ReloadSingleAssetCategory(pCtx->m_Category);
			pCtx->m_ScrollRegion.End();
			return CUi::POPUP_CLOSE_CURRENT;
		}
	}

	pCtx->m_ScrollRegion.End();
	return CUi::POPUP_KEEP_OPEN;
}

CUi::EPopupMenuFunctionResult CMenus::PopupSingleAssetBasePackPicker(void *pContext, CUIRect View, bool Active)
{
	auto *pCtx = static_cast<SSingleAssetBasePickerContext *>(pContext);
	CMenus *pMenus = pCtx->m_pMenus;
	if(!pMenus || pCtx->m_Category < 0)
		return CUi::POPUP_CLOSE_CURRENT;

	if(Active && pMenus->Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE))
		return CUi::POPUP_CLOSE_CURRENT;

	View.Margin(6.0f, &View);

	CUIRect HeaderRect;
	View.HSplitTop(18.0f, &HeaderRect, &View);
	pMenus->TextRender()->TextColor(0.85f, 0.92f, 1.0f, 1.0f);
	pMenus->Ui()->DoLabel(&HeaderRect, Localize("Elegir pack base"), 12.0f, TEXTALIGN_ML);
	pMenus->TextRender()->TextColor(pMenus->TextRender()->DefaultTextColor());
	View.HSplitTop(4.0f, nullptr, &View);

	const auto vpPacks = pMenus->CollectSingleAssetPacks(pCtx->m_Category);
	if(vpPacks.size() > 8)
	{
		CUIRect FilterRect;
		View.HSplitTop(20.0f, &FilterRect, &View);
		pCtx->m_FilterInput.SetEmptyText(Localize("Buscar pack..."));
		pMenus->Ui()->DoEditBox(&pCtx->m_FilterInput, &FilterRect, 11.5f);
		View.HSplitTop(5.0f, nullptr, &View);
	}

	const char *pBasePack = SingleAssetBasePackName(pCtx->m_Category);
	const char *pFilter = pCtx->m_FilterInput.GetString();

	CScrollRegionParams ScrollParams;
	ScrollParams.m_ScrollbarThickness = 8.0f;
	ScrollParams.m_ScrollbarMargin = 3.0f;
	ScrollParams.m_ScrollUnit = 66.0f;
	ScrollParams.m_SliderColor = ColorRGBA(0.24f, 0.52f, 0.72f, 0.85f);
	ScrollParams.m_SliderColorHover = ColorRGBA(0.32f, 0.64f, 0.86f, 0.95f);
	ScrollParams.m_SliderColorGrabbed = ColorRGBA(0.38f, 0.72f, 0.95f, 1.0f);
	pCtx->m_ScrollRegion.Begin(&View, &ScrollParams);

	const float RowH = 62.0f;
	const float PreviewW = (pCtx->m_Category == SINGLE_ASSET_GAME) ? 112.0f : 58.0f;
	for(const SCustomItem *pPack : vpPacks)
	{
		if(pFilter[0] != '\0' && !str_utf8_find_nocase(pPack->m_aName, pFilter))
			continue;

		CUIRect Row;
		View.HSplitTop(RowH, &Row, &View);
		View.HSplitTop(4.0f, nullptr, &View);
		if(!pCtx->m_ScrollRegion.AddRect(Row))
			continue;

		const bool IsSelected = str_comp(pBasePack, pPack->m_aName) == 0;
		const bool Hovered = Active && pMenus->Ui()->MouseInside(&Row);
		if(IsSelected)
			Row.Draw(Hovered ? ColorRGBA(0.16f, 0.36f, 0.52f, 0.95f) : ColorRGBA(0.13f, 0.28f, 0.42f, 0.92f), IGraphics::CORNER_ALL, 5.0f);
		else if(Hovered)
			Row.Draw(ColorRGBA(0.16f, 0.21f, 0.28f, 0.85f), IGraphics::CORNER_ALL, 5.0f);

		CUIRect Inner = Row;
		Inner.Margin(4.0f, &Inner);
		CUIRect PreviewRect, DotRect;
		Inner.VSplitLeft(PreviewW, &PreviewRect, &Inner);
		Inner.VSplitLeft(10.0f, nullptr, &Inner);
		Inner.VSplitRight(16.0f, &Inner, &DotRect);

		DrawSingleSheetPreview(pMenus->Graphics(), pPack->m_RenderTexture, pCtx->m_Category, PreviewRect);

		pMenus->TextRender()->TextColor(0.94f, 0.96f, 0.98f, 1.0f);
		SLabelProperties Props;
		Props.m_MaxWidth = Inner.w;
		Props.m_EllipsisAtEnd = true;
		pMenus->Ui()->DoLabel(&Inner, pPack->m_aName, 12.5f, TEXTALIGN_ML, Props);
		pMenus->TextRender()->TextColor(pMenus->TextRender()->DefaultTextColor());

		if(IsSelected)
		{
			const float DotSize = 5.0f;
			CUIRect Dot(DotRect.x + (DotRect.w - DotSize) * 0.5f, DotRect.y + (DotRect.h - DotSize) * 0.5f, DotSize, DotSize);
			Dot.Draw(ColorRGBA(0.95f, 0.98f, 1.0f, 1.0f), IGraphics::CORNER_ALL, DotSize * 0.5f);
		}

		if(Active && pMenus->Ui()->DoButtonLogic(pPack, 0, &Row, BUTTONFLAG_LEFT))
		{
			SetSingleAssetBasePackName(pCtx->m_Category, pPack->m_aName);
			if(pCtx->m_Category == SINGLE_ASSET_GAME)
			{
				if(pMenus->GameClient()->m_AClientGameSkins.IsGameSkinBlacklisted(pPack->m_aName))
					pMenus->GameClient()->m_AClientGameSkins.SetGameSkinBlacklisted(pPack->m_aName, false);
				pMenus->GameClient()->m_AClientGameSkins.UpdateActiveSkinUsers();
			}
			pMenus->GameClient()->ReloadSingleAssetCategory(pCtx->m_Category);
			pCtx->m_ScrollRegion.End();
			return CUi::POPUP_CLOSE_CURRENT;
		}
	}

	pCtx->m_ScrollRegion.End();
	return CUi::POPUP_KEEP_OPEN;
}

void CMenus::RenderSingleAssetsView(CUIRect View, int SingleCat, const char *pFilter)
{
	CAClientSingleAssets &SingleAssets = GameClient()->m_SingleAssets;
	const char *pBasePack = SingleAssetBasePackName(SingleCat);
	const size_t OverrideCount = SingleAssets.CountOverrides(SingleCat);

	// Top row: Choose base pack + current base pack name + Reset button
	CUIRect BaseBar, ChooseBaseBtn, BaseNameRect, ResetBtn;
	View.HSplitTop(22.0f, &BaseBar, &View);
	View.HSplitTop(8.0f, nullptr, &View);

	BaseBar.VSplitLeft(std::min(260.0f, BaseBar.w * 0.42f), &ChooseBaseBtn, &BaseNameRect);
	BaseNameRect.VSplitLeft(10.0f, nullptr, &BaseNameRect);
	if(OverrideCount > 0)
	{
		BaseNameRect.VSplitRight(120.0f, &BaseNameRect, &ResetBtn);
		BaseNameRect.VSplitRight(8.0f, &BaseNameRect, nullptr);
	}

	static CButtonContainer s_ChooseBaseBtn;
	const bool BaseHovered = Ui()->MouseInside(&ChooseBaseBtn);
	const bool BaseOpen = Ui()->IsPopupOpen(&gs_SingleBasePicker);
	ChooseBaseBtn.Draw(
		BaseOpen ? ColorRGBA(0.14f, 0.30f, 0.44f, 0.92f) : (BaseHovered ? ColorRGBA(0.18f, 0.22f, 0.28f, 0.88f) : ColorRGBA(0.12f, 0.14f, 0.18f, 0.82f)),
		IGraphics::CORNER_ALL, 4.0f);
	TextRender()->TextColor(0.92f, 0.95f, 0.98f, 1.0f);
	Ui()->DoLabel(&ChooseBaseBtn, Localize("Elegir pack base"), 12.0f, TEXTALIGN_MC);
	TextRender()->TextColor(TextRender()->DefaultTextColor());
	if(Ui()->DoButtonLogic(&s_ChooseBaseBtn, 0, &ChooseBaseBtn, BUTTONFLAG_LEFT))
	{
		gs_SingleBasePicker.m_pMenus = this;
		gs_SingleBasePicker.m_Category = SingleCat;
		gs_SingleBasePicker.m_FilterInput.Clear();
		const auto vpPacks = CollectSingleAssetPacks(SingleCat);
		const float PopupW = std::max(ChooseBaseBtn.w, (SingleCat == SINGLE_ASSET_GAME) ? 350.0f : 300.0f);
		const float PopupH = std::min(410.0f, 38.0f + (vpPacks.size() > 8 ? 25.0f : 0.0f) + (float)vpPacks.size() * 66.0f);
		SPopupMenuProperties Props;
		Props.m_Corners = IGraphics::CORNER_ALL;
		Props.m_BackgroundColor = ColorRGBA(0.06f, 0.08f, 0.11f, 0.96f);
		Props.m_BorderColor = ColorRGBA(0.24f, 0.42f, 0.58f, 0.85f);
		Ui()->DoPopupMenu(&gs_SingleBasePicker, ChooseBaseBtn.x, ChooseBaseBtn.y + ChooseBaseBtn.h + 2.0f, PopupW, PopupH, &gs_SingleBasePicker, PopupSingleAssetBasePackPicker, Props);
	}

	char aBaseInfo[128];
	if(OverrideCount > 0)
		str_format(aBaseInfo, sizeof(aBaseInfo), "%s  (%d %s)", pBasePack, (int)OverrideCount, OverrideCount == 1 ? "personalizado" : "personalizados");
	else
		str_copy(aBaseInfo, pBasePack);
	TextRender()->TextColor(0.90f, 0.93f, 0.97f, 1.0f);
	SLabelProperties BaseProps;
	BaseProps.m_MaxWidth = BaseNameRect.w;
	BaseProps.m_EllipsisAtEnd = true;
	Ui()->DoLabel(&BaseNameRect, aBaseInfo, 12.0f, TEXTALIGN_ML, BaseProps);
	TextRender()->TextColor(TextRender()->DefaultTextColor());

	if(OverrideCount > 0)
	{
		static CButtonContainer s_ResetAllBtn;
		const bool ResetHovered = Ui()->MouseInside(&ResetBtn);
		ResetBtn.Draw(ResetHovered ? ColorRGBA(0.75f, 0.24f, 0.24f, 0.92f) : ColorRGBA(0.58f, 0.19f, 0.19f, 0.82f), IGraphics::CORNER_ALL, 4.0f);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
		Ui()->DoLabel(&ResetBtn, Localize("Restablecer todo"), 11.5f, TEXTALIGN_MC);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
		if(Ui()->DoButtonLogic(&s_ResetAllBtn, 0, &ResetBtn, BUTTONFLAG_LEFT))
		{
			SingleAssets.ClearCategory(SingleCat);
			SingleAssets.Save(Storage());
			GameClient()->ReloadSingleAssetCategory(SingleCat);
		}
	}

	// Scrollable 5-column sprite grid
	const auto &vAllSlots = GetSingleAssetSlots(SingleCat);
	std::vector<const SSingleAssetSlotDef *> vpVisibleSlots;
	vpVisibleSlots.reserve(vAllSlots.size());
	for(const auto &Slot : vAllSlots)
	{
		if(pFilter && pFilter[0] != '\0')
		{
			char aRawBuf[64] = "";
			if(SingleCat == SINGLE_ASSET_ENTITIES)
			{
				str_format(aRawBuf, sizeof(aRawBuf), "tile_%03d", Slot.m_SpriteId);
			}
			else if(Slot.m_SpriteId >= 0 && Slot.m_SpriteId < NUM_SPRITES && g_pData->m_aSprites[Slot.m_SpriteId].m_pName)
			{
				str_copy(aRawBuf, g_pData->m_aSprites[Slot.m_SpriteId].m_pName);
			}
			if(!str_utf8_find_nocase(Slot.m_pDisplayName, pFilter) && !str_utf8_find_nocase(aRawBuf, pFilter))
				continue;
		}
		vpVisibleSlots.push_back(&Slot);
	}

	static CScrollRegion s_aGridScrolls[NUM_SINGLE_ASSET_CATEGORIES];
	CScrollRegion &Scroll = s_aGridScrolls[SingleCat];
	CScrollRegionParams ScrollParams;
	ScrollParams.m_ScrollbarThickness = 10.0f;
	ScrollParams.m_ScrollbarMargin = 5.0f;
	ScrollParams.m_ScrollUnit = 54.0f;
	ScrollParams.m_SliderColor = ColorRGBA(0.20f, 0.48f, 0.68f, 0.85f);
	ScrollParams.m_SliderColorHover = ColorRGBA(0.28f, 0.60f, 0.84f, 0.95f);
	ScrollParams.m_SliderColorGrabbed = ColorRGBA(0.36f, 0.70f, 0.94f, 1.0f);
	Scroll.Begin(&View, &ScrollParams);

	const int Columns = std::clamp((int)(View.w / 138.0f), 3, 5);
	const float Gap = 6.0f;
	const float CardW = (View.w - Gap * (float)(Columns - 1)) / (float)Columns;
	const float CardH = 98.0f;

	const bool SpritePopupOpen = Ui()->IsPopupOpen(&gs_SingleSpritePicker);
	const IGraphics::CTextureHandle BaseTex = FindSingleAssetPackTexture(SingleCat, pBasePack);
	static char s_aSlotBtnIds[NUM_SPRITES > 256 ? NUM_SPRITES : 256];

	const int TotalRows = ((int)vpVisibleSlots.size() + Columns - 1) / Columns;
	for(int RowIdx = 0; RowIdx < TotalRows; ++RowIdx)
	{
		CUIRect RowRect;
		View.HSplitTop(CardH, &RowRect, &View);
		View.HSplitTop(Gap, nullptr, &View);
		if(!Scroll.AddRect(RowRect))
			continue;

		for(int Col = 0; Col < Columns; ++Col)
		{
			const size_t Idx = (size_t)(RowIdx * Columns + Col);
			if(Idx >= vpVisibleSlots.size())
				break;

			CUIRect CardRect(RowRect.x + (float)Col * (CardW + Gap), RowRect.y, CardW, CardH);
			const SSingleAssetSlotDef *pSlot = vpVisibleSlots[Idx];
			const char *pOverride = SingleAssets.GetPartOverrideRaw(SingleCat, pSlot->m_SpriteId);
			const bool IsOverridden = (pOverride && pOverride[0] != '\0' && str_comp(pOverride, pBasePack) != 0);
			const char *pActivePack = IsOverridden ? pOverride : pBasePack;
			const bool IsPickerTarget = SpritePopupOpen && gs_SingleSpritePicker.m_Category == SingleCat && gs_SingleSpritePicker.m_SpriteId == pSlot->m_SpriteId;
			const bool Hovered = !Ui()->IsPopupOpen() && Ui()->MouseInside(&CardRect);

			ColorRGBA BgColor(0.07f, 0.09f, 0.12f, 0.58f);
			if(IsPickerTarget)
				BgColor = ColorRGBA(0.14f, 0.30f, 0.44f, 0.92f);
			else if(Hovered)
				BgColor = ColorRGBA(0.15f, 0.21f, 0.28f, 0.85f);
			else if(IsOverridden)
				BgColor = ColorRGBA(0.10f, 0.20f, 0.29f, 0.78f);

			CardRect.Draw(BgColor, IGraphics::CORNER_ALL, 6.0f);
			if(IsOverridden)
			{
				CUIRect Accent(CardRect.x + 10.0f, CardRect.y, CardRect.w - 20.0f, 2.0f);
				Accent.Draw(ColorRGBA(0.28f, 0.78f, 0.98f, 0.92f), IGraphics::CORNER_B, 1.5f);
			}

			CUIRect Inner = CardRect;
			Inner.Margin(6.0f, &Inner);
			CUIRect TitleRect, PackRect, PreviewRect;
			Inner.HSplitTop(16.0f, &TitleRect, &PreviewRect);
			PreviewRect.HSplitBottom(15.0f, &PreviewRect, &PackRect);
			PreviewRect.Margin(4.0f, &PreviewRect);

			TextRender()->TextColor(0.92f, 0.95f, 0.98f, 1.0f);
			SLabelProperties TitleProps;
			TitleProps.m_MaxWidth = TitleRect.w;
			TitleProps.m_EllipsisAtEnd = true;
			Ui()->DoLabel(&TitleRect, pSlot->m_pDisplayName, 11.5f, TEXTALIGN_MC, TitleProps);

			const IGraphics::CTextureHandle Tex = IsOverridden ? FindSingleAssetPackTexture(SingleCat, pActivePack) : BaseTex;
			DrawSinglePartPreview(Graphics(), Tex, SingleCat, pSlot->m_SpriteId, PreviewRect);

			TextRender()->TextColor(IsOverridden ? ColorRGBA(0.45f, 0.86f, 1.0f, 1.0f) : ColorRGBA(0.62f, 0.66f, 0.72f, 0.92f));
			SLabelProperties PackProps;
			PackProps.m_MaxWidth = PackRect.w;
			PackProps.m_EllipsisAtEnd = true;
			Ui()->DoLabel(&PackRect, pActivePack, 10.0f, TEXTALIGN_MC, PackProps);
			TextRender()->TextColor(TextRender()->DefaultTextColor());

			if(Ui()->DoButtonLogic(&s_aSlotBtnIds[pSlot->m_SpriteId], 0, &CardRect, BUTTONFLAG_LEFT))
			{
				gs_SingleSpritePicker.m_pMenus = this;
				gs_SingleSpritePicker.m_Category = SingleCat;
				gs_SingleSpritePicker.m_SpriteId = pSlot->m_SpriteId;
				gs_SingleSpritePicker.m_pSpriteLabel = pSlot->m_pDisplayName;
				gs_SingleSpritePicker.m_FilterInput.Clear();

				const auto vpPacks = CollectSingleAssetPacks(SingleCat);
				const float PopupW = std::max(CardRect.w * 1.45f, 220.0f);
				const float PopupH = std::min(320.0f, 40.0f + (vpPacks.size() > 8 ? 25.0f : 0.0f) + (float)(vpPacks.size() + 1) * 32.0f);
				SPopupMenuProperties Props;
				Props.m_Corners = IGraphics::CORNER_ALL;
				Props.m_BackgroundColor = ColorRGBA(0.06f, 0.08f, 0.11f, 0.96f);
				Props.m_BorderColor = ColorRGBA(0.24f, 0.42f, 0.58f, 0.85f);
				Ui()->DoPopupMenu(&gs_SingleSpritePicker, CardRect.x, CardRect.y + CardRect.h + 2.0f, PopupW, PopupH, &gs_SingleSpritePicker, PopupSingleAssetSpritePicker, Props);
			}
		}
	}

	Scroll.End();
}

void CMenus::PopupConfirmDeleteAsset()
{
	if(m_DeleteAssetTab < 0 || m_DeleteAssetTab >= NUMBER_OF_ASSETS_TABS || m_aDeleteAssetName[0] == '\0')
		return;

	const int Tab = m_DeleteAssetTab;
	const bool WasSelected = AssetsIsCurrentlySelected(Tab, m_aDeleteAssetName);
	if(!DeleteCustomAsset(Tab, m_aDeleteAssetName))
	{
		char aError[128 + sizeof(m_aDeleteAssetName)];
		str_format(aError, sizeof(aError), Localize("Unable to delete the asset '%s'"), m_aDeleteAssetName);
		PopupMessage(Localize("Error"), aError, Localize("Ok"));
		m_aDeleteAssetName[0] = '\0';
		m_DeleteAssetTab = -1;
		return;
	}

	if(WasSelected)
		AssetsResetSelectedToDefault(Tab);

	const int DeletedSingleCat = SingleAssetCategoryFromTab(Tab);
	bool ReloadForSingleAssets = false;
	if(DeletedSingleCat >= 0)
	{
		const size_t BeforeCount = GameClient()->m_SingleAssets.CountOverrides(DeletedSingleCat);
		GameClient()->m_SingleAssets.RemovePackReferences(DeletedSingleCat, m_aDeleteAssetName);
		if(GameClient()->m_SingleAssets.CountOverrides(DeletedSingleCat) != BeforeCount)
		{
			GameClient()->m_SingleAssets.Save(Storage());
			ReloadForSingleAssets = GameClient()->m_SingleAssets.IsEnabled(DeletedSingleCat);
		}
	}

	RemoveCustomAssetFromList(Tab, m_aDeleteAssetName);

	if(WasSelected || ReloadForSingleAssets)
	{
		if(Tab == ASSETS_TAB_ENTITIES)
			GameClient()->m_MapImages.ChangeEntitiesPath(g_Config.m_ClAssetsEntities);
		else if(Tab == ASSETS_TAB_GAME || Tab == ASSETS_TAB_DOWNLOADED_GAME)
			GameClient()->LoadGameSkin(g_Config.m_ClAssetGame);
		else if(Tab == ASSETS_TAB_EMOTICONS)
			GameClient()->LoadEmoticonsSkin(g_Config.m_ClAssetEmoticons);
		else if(Tab == ASSETS_TAB_PARTICLES)
			GameClient()->LoadParticlesSkin(g_Config.m_ClAssetParticles);
		else if(Tab == ASSETS_TAB_HUD)
			GameClient()->LoadHudSkin(g_Config.m_ClAssetHud);
		else if(Tab == ASSETS_TAB_EXTRAS)
			GameClient()->LoadExtrasSkin(g_Config.m_ClAssetExtras);
	}

	gs_aInitCustomList[Tab] = true;
	m_aDeleteAssetName[0] = '\0';
	m_DeleteAssetTab = -1;
}

void CMenus::MarkCustomAssetsDeletable(int Tab)
{
	auto MarkList = [this, Tab](auto &vList) {
		for(auto &Asset : vList)
			Asset.m_Deletable = CanDeleteCustomAsset(Tab, Asset.m_aName);
	};

	if(Tab == ASSETS_TAB_ENTITIES)
		MarkList(m_vEntitiesList);
	else if(Tab == ASSETS_TAB_GAME)
		MarkList(m_vGameList);
	else if(Tab == ASSETS_TAB_EMOTICONS)
		MarkList(m_vEmoticonList);
	else if(Tab == ASSETS_TAB_PARTICLES)
		MarkList(m_vParticlesList);
	else if(Tab == ASSETS_TAB_HUD)
		MarkList(m_vHudList);
	else if(Tab == ASSETS_TAB_EXTRAS)
		MarkList(m_vExtrasList);
}

void CMenus::RenderSettingsAssets(CUIRect MainView)
{
	const CUIRect FullMainView = MainView;
	CUIRect TabBar, CustomList, QuickSearch, DirectoryButton, ReloadButton;

	static bool s_LikeModalOpen = false;
	static char s_aLikeModalSkin[64] = "";

	if(s_LikeModalOpen)
	{
		auto &ConfigSync = GameClient()->m_AClientConfigSync;
		if(Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE))
		{
			s_LikeModalOpen = false;
			ConfigSync.ClearLikeStatus();
			return;
		}

		FullMainView.Draw(ColorRGBA(0.05f, 0.06f, 0.10f, 0.86f), IGraphics::CORNER_ALL, 6.0f);

		const float ModalW = std::min(430.0f, FullMainView.w - 24.0f);
		const float ModalH = 190.0f;
		CUIRect Box;
		Box.x = FullMainView.x + (FullMainView.w - ModalW) * 0.5f;
		Box.y = FullMainView.y + (FullMainView.h - ModalH) * 0.5f;
		Box.w = ModalW;
		Box.h = ModalH;

		Box.Draw(ColorRGBA(0.11f, 0.13f, 0.19f, 0.98f), IGraphics::CORNER_ALL, 10.0f);
		Box.Margin(18.0f, &Box);

		const int CurLikes = ConfigSync.GetGameSkinLikes(s_aLikeModalSkin);
		const bool LikedInSession = ConfigSync.HasLikedGameSkinInSession(s_aLikeModalSkin);

		CUIRect TitleRect, SubRect, DiscordBtnRect, StatusRect, ButtonsRow;
		Box.HSplitTop(22.0f, &TitleRect, &Box);
		char aTitle[128];
		str_format(aTitle, sizeof(aTitle), "Puntuar Gameskin: %s", s_aLikeModalSkin);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
		Ui()->DoLabel(&TitleRect, aTitle, 15.0f, TEXTALIGN_ML);

		Box.HSplitTop(18.0f, &SubRect, &Box);
		char aSub[160];
		str_format(aSub, sizeof(aSub), "Me gusta actuales: %d  |  Inicia sesion con Discord (sin contrasena).", CurLikes);
		TextRender()->TextColor(0.70f, 0.74f, 0.84f, 1.0f);
		Ui()->DoLabel(&SubRect, aSub, 11.0f, TEXTALIGN_ML);
		TextRender()->TextColor(TextRender()->DefaultTextColor());

		Box.HSplitTop(12.0f, nullptr, &Box);
		Box.HSplitTop(30.0f, &DiscordBtnRect, &Box);
		static CButtonContainer s_BtnModalDiscord;
		if(ConfigSync.IsDiscordBusy())
		{
			if(DoButton_Menu(&s_BtnModalDiscord, Localize("Esperando Discord en navegador... (Cancelar)"), 0, &DiscordBtnRect, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.45f, 0.38f, 0.20f, 0.85f)))
			{
				ConfigSync.CancelDiscordLogin();
			}
		}
		else if(ConfigSync.IsDiscordReady())
		{
			char aDiscVote[128];
			str_format(aDiscVote, sizeof(aDiscVote), "%s como %s (Discord)", LikedInSession ? "Quitar Me gusta" : "Dar Me gusta", ConfigSync.GetDiscordUsername());
			if(DoButton_Menu(&s_BtnModalDiscord, aDiscVote, 0, &DiscordBtnRect, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.345f, 0.396f, 0.949f, 0.90f)) && !ConfigSync.IsLiking())
			{
				ConfigSync.ToggleGameSkinLikeWithDiscord(s_aLikeModalSkin);
			}
		}
		else
		{
			if(DoButton_Menu(&s_BtnModalDiscord, Localize("Iniciar sesion con Discord"), 0, &DiscordBtnRect, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.345f, 0.396f, 0.949f, 0.90f)))
			{
				ConfigSync.StartDiscordLogin(s_aLikeModalSkin);
			}
		}

		Box.HSplitTop(10.0f, nullptr, &Box);
		Box.HSplitTop(20.0f, &StatusRect, &Box);
		if(ConfigSync.LikeState() != CAClientConfigSync::LIKE_IDLE && ConfigSync.GetLikeStatusMessage()[0] != '\0')
		{
			if(ConfigSync.LikeState() == CAClientConfigSync::LIKE_SENDING)
				TextRender()->TextColor(0.95f, 0.85f, 0.30f, 1.0f);
			else if(ConfigSync.LikeState() == CAClientConfigSync::LIKE_SUCCESS)
				TextRender()->TextColor(0.35f, 0.95f, 0.45f, 1.0f);
			else
				TextRender()->TextColor(0.95f, 0.35f, 0.35f, 1.0f);
			Ui()->DoLabel(&StatusRect, ConfigSync.GetLikeStatusMessage(), 11.0f, TEXTALIGN_ML);
			TextRender()->TextColor(TextRender()->DefaultTextColor());
		}
		else if(ConfigSync.GetDiscordStatusMessage()[0] != '\0')
		{
			if(ConfigSync.DiscordState() == CAClientConfigSync::DISCORD_ERROR)
				TextRender()->TextColor(0.95f, 0.35f, 0.35f, 1.0f);
			else
				TextRender()->TextColor(0.65f, 0.78f, 1.0f, 1.0f);
			Ui()->DoLabel(&StatusRect, ConfigSync.GetDiscordStatusMessage(), 11.0f, TEXTALIGN_ML);
			TextRender()->TextColor(TextRender()->DefaultTextColor());
		}

		Box.HSplitBottom(28.0f, &Box, &ButtonsRow);
		CUIRect CloseBtn;
		ButtonsRow.VSplitRight(120.0f, nullptr, &CloseBtn);

		static CButtonContainer s_BtnCloseLike;
		if(DoButton_Menu(&s_BtnCloseLike, Localize("Cerrar"), 0, &CloseBtn, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.22f, 0.24f, 0.30f, 0.75f)))
		{
			s_LikeModalOpen = false;
			ConfigSync.ClearLikeStatus();
		}

		return;
	}

	if(s_CurCustomTab == ASSETS_TAB_GAME || s_CurCustomTab == ASSETS_TAB_DOWNLOADED_GAME)
	{
		GameClient()->m_AClientConfigSync.RefreshGameSkinLikes(false);
	}

	MainView.HSplitTop(20.0f, &TabBar, &MainView);
	const float TabWidth = TabBar.w / (float)NUMBER_OF_ASSETS_TABS;
	static CButtonContainer s_aPageTabs[NUMBER_OF_ASSETS_TABS] = {};
	const char *apTabNames[NUMBER_OF_ASSETS_TABS] = {
		Localize("Entities"),
		Localize("Game"),
		Localize("Emoticons"),
		Localize("Particles"),
		Localize("HUD"),
		Localize("Extras"),
		Localize("Descargas")};

	for(int Tab = ASSETS_TAB_ENTITIES; Tab < NUMBER_OF_ASSETS_TABS; ++Tab)
	{
		CUIRect Button;
		TabBar.VSplitLeft(TabWidth, &Button, &TabBar);
		const int Corners = Tab == ASSETS_TAB_ENTITIES ? IGraphics::CORNER_L : (Tab == NUMBER_OF_ASSETS_TABS - 1 ? IGraphics::CORNER_R : IGraphics::CORNER_NONE);
		if(DoButton_MenuTab(&s_aPageTabs[Tab], apTabNames[Tab], s_CurCustomTab == Tab, &Button, Corners, nullptr, nullptr, nullptr, nullptr, 4.0f))
		{
			s_CurCustomTab = Tab;
		}
	}

	auto LoadStartTime = time_get_nanoseconds();
	SMenuAssetScanUser User;
	User.m_pUser = this;
	User.m_LoadedFunc = [&]() {
		if(time_get_nanoseconds() - LoadStartTime > 500ms)
			RenderLoading(Localize("Loading assets"), "", 0);
	};
	if(s_CurCustomTab == ASSETS_TAB_ENTITIES)
	{
		if(m_vEntitiesList.empty())
		{
			SCustomEntities EntitiesItem;
			str_copy(EntitiesItem.m_aName, "default");
			LoadEntities(&EntitiesItem, &User);
			m_vEntitiesList.push_back(EntitiesItem);

			// load entities
			Storage()->ListDirectory(IStorage::TYPE_ALL, "assets/entities", EntitiesScan, &User);
			Storage()->ListDirectory(IStorage::TYPE_ALL, "downloaded_entities", EntitiesScan, &User);
			std::sort(m_vEntitiesList.begin(), m_vEntitiesList.end());
			m_vEntitiesList.erase(std::unique(m_vEntitiesList.begin(), m_vEntitiesList.end(), [](const SCustomEntities &a, const SCustomEntities &b) {
				return str_comp(a.m_aName, b.m_aName) == 0;
			}), m_vEntitiesList.end());
			MarkCustomAssetsDeletable(ASSETS_TAB_ENTITIES);
		}
		if(m_vEntitiesList.size() != gs_aCustomListSize[s_CurCustomTab])
			gs_aInitCustomList[s_CurCustomTab] = true;
	}
	else if(s_CurCustomTab == ASSETS_TAB_GAME)
	{
		const bool WasEmpty = m_vGameList.empty();
		InitAssetList(m_vGameList, "assets/game", "game", GameScan, Graphics(), Storage(), &User);
		if(WasEmpty)
			MarkCustomAssetsDeletable(ASSETS_TAB_GAME);
	}
	else if(s_CurCustomTab == ASSETS_TAB_EMOTICONS)
	{
		const bool WasEmpty = m_vEmoticonList.empty();
		InitAssetList(m_vEmoticonList, "assets/emoticons", "emoticons", EmoticonsScan, Graphics(), Storage(), &User);
		if(WasEmpty)
			MarkCustomAssetsDeletable(ASSETS_TAB_EMOTICONS);
	}
	else if(s_CurCustomTab == ASSETS_TAB_PARTICLES)
	{
		const bool WasEmpty = m_vParticlesList.empty();
		InitAssetList(m_vParticlesList, "assets/particles", "particles", ParticlesScan, Graphics(), Storage(), &User);
		if(WasEmpty)
			MarkCustomAssetsDeletable(ASSETS_TAB_PARTICLES);
	}
	else if(s_CurCustomTab == ASSETS_TAB_HUD)
	{
		const bool WasEmpty = m_vHudList.empty();
		InitAssetList(m_vHudList, "assets/hud", "hud", HudScan, Graphics(), Storage(), &User);
		if(WasEmpty)
			MarkCustomAssetsDeletable(ASSETS_TAB_HUD);
	}
	else if(s_CurCustomTab == ASSETS_TAB_EXTRAS)
	{
		const bool WasEmpty = m_vExtrasList.empty();
		InitAssetList(m_vExtrasList, "assets/extras", "extras", ExtrasScan, Graphics(), Storage(), &User);
		if(WasEmpty)
			MarkCustomAssetsDeletable(ASSETS_TAB_EXTRAS);
	}
	else if(s_CurCustomTab == ASSETS_TAB_DOWNLOADED_GAME)
	{
		if(m_vDownloadedGameList.empty())
		{
			SCustomGame AssetItem;
			str_copy(AssetItem.m_aName, "default");
			LoadAsset(&AssetItem, "game", Graphics());
			AssetItem.m_Deletable = false;
			m_vDownloadedGameList.push_back(AssetItem);

			Storage()->ListDirectory(IStorage::TYPE_SAVE, "downloaded_gameskins", DownloadedGameScan, &User);
			std::sort(m_vDownloadedGameList.begin() + 1, m_vDownloadedGameList.end());
			auto Last = std::unique(m_vDownloadedGameList.begin() + 1, m_vDownloadedGameList.end(), [](const SCustomGame &a, const SCustomGame &b) {
				return str_comp(a.m_aName, b.m_aName) == 0;
			});
			m_vDownloadedGameList.erase(Last, m_vDownloadedGameList.end());
		}
		if(m_vDownloadedGameList.size() != gs_aCustomListSize[s_CurCustomTab])
			gs_aInitCustomList[s_CurCustomTab] = true;
	}
	else
	{
		dbg_assert_failed("Invalid s_CurCustomTab: %d", s_CurCustomTab);
	}

	MainView.HSplitTop(10.0f, nullptr, &MainView);

	const int SingleCat = SingleAssetCategoryFromTab(s_CurCustomTab);
	const bool EditorPick = m_AssetsEditorState.m_Open && m_AssetsEditorState.m_ExploreSide >= 0;
	const bool ShowSingleToggle = (SingleCat >= 0 && !EditorPick);
	bool SingleModeActive = ShowSingleToggle && GameClient()->m_SingleAssets.IsEnabled(SingleCat);

	if(ShowSingleToggle)
	{
		CUIRect ModeBar, FullPackBtn, SingleAssetsBtn;
		MainView.HSplitTop(22.0f, &ModeBar, &MainView);
		MainView.HSplitTop(6.0f, nullptr, &MainView);
		ModeBar.VSplitMid(&FullPackBtn, &SingleAssetsBtn, 8.0f);

		auto DrawModeTab = [&](CButtonContainer *pBtnId, const char *pLabel, const CUIRect &Rect, bool Selected) -> bool {
			const bool Hovered = !Ui()->IsPopupOpen() && Ui()->MouseInside(&Rect);
			const ColorRGBA Bg = Selected
				? ColorRGBA(0.13f, 0.28f, 0.42f, 0.94f)
				: (Hovered ? ColorRGBA(0.16f, 0.20f, 0.26f, 0.85f) : ColorRGBA(0.10f, 0.12f, 0.16f, 0.78f));
			Rect.Draw(Bg, IGraphics::CORNER_ALL, 4.0f);
			TextRender()->TextColor(Selected ? ColorRGBA(0.96f, 0.99f, 1.0f, 1.0f) : ColorRGBA(0.76f, 0.80f, 0.86f, 0.95f));
			Ui()->DoLabel(&Rect, pLabel, 12.0f, TEXTALIGN_MC);
			TextRender()->TextColor(TextRender()->DefaultTextColor());
			return Ui()->DoButtonLogic(pBtnId, 0, &Rect, BUTTONFLAG_LEFT) != 0;
		};

		static CButtonContainer s_ModeFullPackBtn, s_ModeSingleAssetsBtn;
		if(DrawModeTab(&s_ModeFullPackBtn, Localize("Pack completo"), FullPackBtn, !SingleModeActive) && SingleModeActive)
		{
			GameClient()->m_SingleAssets.SetEnabled(SingleCat, false);
			GameClient()->m_SingleAssets.Save(Storage());
			GameClient()->ReloadSingleAssetCategory(SingleCat);
			SingleModeActive = false;
		}

		char aSingleLabel[64];
		const size_t SavedOverrides = GameClient()->m_SingleAssets.CountOverrides(SingleCat);
		if(SavedOverrides > 0)
			str_format(aSingleLabel, sizeof(aSingleLabel), "%s (%d)", Localize("Assets individuales"), (int)SavedOverrides);
		else
			str_copy(aSingleLabel, Localize("Assets individuales"));

		if(DrawModeTab(&s_ModeSingleAssetsBtn, aSingleLabel, SingleAssetsBtn, SingleModeActive) && !SingleModeActive)
		{
			GameClient()->m_SingleAssets.SetEnabled(SingleCat, true);
			GameClient()->m_SingleAssets.Save(Storage());
			GameClient()->ReloadSingleAssetCategory(SingleCat);
			SingleModeActive = true;
		}
	}

	// skin selector
	MainView.HSplitTop(MainView.h - 10.0f - ms_ButtonHeight, &CustomList, &MainView);

	if(SingleModeActive)
	{
		RenderSingleAssetsView(CustomList, SingleCat, s_aFilterInputs[s_CurCustomTab].GetString());
	}
	else
	{

	static uint64_t s_LastSkinUsersRevision = 0;
	if(s_CurCustomTab == ASSETS_TAB_GAME || s_CurCustomTab == ASSETS_TAB_DOWNLOADED_GAME)
	{
		const uint64_t CurRev = GameClient()->m_AClientGameSkins.SkinUsersRevision();
		if(CurRev != s_LastSkinUsersRevision)
		{
			gs_aInitCustomList[s_CurCustomTab] = true;
			s_LastSkinUsersRevision = CurRev;
		}
	}

	const auto &ActiveSkinUsers = GameClient()->m_AClientGameSkins.GetActiveSkinUsers();

	if(gs_aInitCustomList[s_CurCustomTab])
	{
		int ListSize = 0;
		if(s_CurCustomTab == ASSETS_TAB_ENTITIES)
		{
			gs_vpSearchEntitiesList.clear();
			ListSize = m_vEntitiesList.size();
			for(int i = 0; i < ListSize; ++i)
			{
				const SCustomEntities *pEntity = &m_vEntitiesList[i];

				// filter quick search
				if(!s_aFilterInputs[s_CurCustomTab].IsEmpty() && !str_utf8_find_nocase(pEntity->m_aName, s_aFilterInputs[s_CurCustomTab].GetString()))
					continue;

				gs_vpSearchEntitiesList.push_back(pEntity);
			}
		}
		else if(s_CurCustomTab == ASSETS_TAB_GAME)
		{
			ListSize = InitSearchList(gs_vpSearchGamesList, m_vGameList);
			SortGamesList(gs_vpSearchGamesList, ActiveSkinUsers);
		}
		else if(s_CurCustomTab == ASSETS_TAB_EMOTICONS)
		{
			ListSize = InitSearchList(gs_vpSearchEmoticonsList, m_vEmoticonList);
		}
		else if(s_CurCustomTab == ASSETS_TAB_PARTICLES)
		{
			ListSize = InitSearchList(gs_vpSearchParticlesList, m_vParticlesList);
		}
		else if(s_CurCustomTab == ASSETS_TAB_HUD)
		{
			ListSize = InitSearchList(gs_vpSearchHudList, m_vHudList);
		}
		else if(s_CurCustomTab == ASSETS_TAB_EXTRAS)
		{
			ListSize = InitSearchList(gs_vpSearchExtrasList, m_vExtrasList);
		}
		else if(s_CurCustomTab == ASSETS_TAB_DOWNLOADED_GAME)
		{
			ListSize = InitSearchList(gs_vpSearchDownloadedGamesList, m_vDownloadedGameList);
			SortGamesList(gs_vpSearchDownloadedGamesList, ActiveSkinUsers);
		}
		gs_aInitCustomList[s_CurCustomTab] = false;
		gs_aCustomListSize[s_CurCustomTab] = ListSize;
	}

	int OldSelected = -1;
	float Margin = 10;
	float TextureWidth = 150;
	float TextureHeight = 150;

	size_t SearchListSize = 0;

	if(s_CurCustomTab == ASSETS_TAB_ENTITIES)
	{
		SearchListSize = gs_vpSearchEntitiesList.size();
	}
	else if(s_CurCustomTab == ASSETS_TAB_GAME || s_CurCustomTab == ASSETS_TAB_DOWNLOADED_GAME)
	{
		SearchListSize = (s_CurCustomTab == ASSETS_TAB_GAME) ? gs_vpSearchGamesList.size() : gs_vpSearchDownloadedGamesList.size();
		TextureHeight = 75;
	}
	else if(s_CurCustomTab == ASSETS_TAB_EMOTICONS)
	{
		SearchListSize = gs_vpSearchEmoticonsList.size();
	}
	else if(s_CurCustomTab == ASSETS_TAB_PARTICLES)
	{
		SearchListSize = gs_vpSearchParticlesList.size();
	}
	else if(s_CurCustomTab == ASSETS_TAB_HUD)
	{
		SearchListSize = gs_vpSearchHudList.size();
	}
	else if(s_CurCustomTab == ASSETS_TAB_EXTRAS)
	{
		SearchListSize = gs_vpSearchExtrasList.size();
	}

	static CListBox s_ListBox;
	const float ExtraItemHeight = (s_CurCustomTab == ASSETS_TAB_GAME || s_CurCustomTab == ASSETS_TAB_DOWNLOADED_GAME) ? 18.0f : 0.0f;
	s_ListBox.DoStart(TextureHeight + 15.0f + 10.0f + Margin + ExtraItemHeight, SearchListSize, CustomList.w / (Margin + TextureWidth), 1, OldSelected, &CustomList, false);
	bool SkipSelectionBecauseDelete = false;
	int DownloadedLoadedThisFrame = 0;
	for(size_t i = 0; i < SearchListSize; ++i)
	{
		const SCustomItem *pItem = GetCustomItem(s_CurCustomTab, i);
		if(pItem == nullptr)
			continue;

		if(s_CurCustomTab == ASSETS_TAB_ENTITIES)
		{
			if(str_comp(pItem->m_aName, g_Config.m_ClAssetsEntities) == 0)
				OldSelected = i;
		}
		else if(s_CurCustomTab == ASSETS_TAB_GAME || s_CurCustomTab == ASSETS_TAB_DOWNLOADED_GAME)
		{
			if(str_comp(pItem->m_aName, g_Config.m_ClAssetGame) == 0)
				OldSelected = i;
		}
		else if(s_CurCustomTab == ASSETS_TAB_EMOTICONS)
		{
			if(str_comp(pItem->m_aName, g_Config.m_ClAssetEmoticons) == 0)
				OldSelected = i;
		}
		else if(s_CurCustomTab == ASSETS_TAB_PARTICLES)
		{
			if(str_comp(pItem->m_aName, g_Config.m_ClAssetParticles) == 0)
				OldSelected = i;
		}
		else if(s_CurCustomTab == ASSETS_TAB_HUD)
		{
			if(str_comp(pItem->m_aName, g_Config.m_ClAssetHud) == 0)
				OldSelected = i;
		}
		else if(s_CurCustomTab == ASSETS_TAB_EXTRAS)
		{
			if(str_comp(pItem->m_aName, g_Config.m_ClAssetExtras) == 0)
				OldSelected = i;
		}

		const CListboxItem Item = s_ListBox.DoNextItem(pItem, OldSelected >= 0 && (size_t)OldSelected == i);
		CUIRect ItemRect = Item.m_Rect;
		ItemRect.Margin(Margin / 2, &ItemRect);
		if(!Item.m_Visible)
			continue;

		const bool CanDelete = pItem->m_Deletable;
		const bool CanBlacklist = (s_CurCustomTab == ASSETS_TAB_GAME || s_CurCustomTab == ASSETS_TAB_DOWNLOADED_GAME) && str_comp(pItem->m_aName, "default") != 0;
		const bool CanLike = CanBlacklist;
		const bool IsBlacklisted = CanBlacklist && GameClient()->m_AClientGameSkins.IsGameSkinBlacklisted(pItem->m_aName);

		CUIRect TopBar;
		Item.m_Rect.HSplitTop(20.0f, &TopBar, nullptr);

		CUIRect DeleteButton;
		CUIRect BlacklistButton;
		CUIRect LikeButton;

		if(CanLike)
		{
			TopBar.VSplitLeft(44.0f, &LikeButton, &TopBar);
			LikeButton.Margin(2.0f, &LikeButton);
		}

		if(CanDelete)
		{
			TopBar.VSplitRight(20.0f, &TopBar, &DeleteButton);
			DeleteButton.Margin(2.0f, &DeleteButton);
			if(CanBlacklist)
			{
				TopBar.VSplitRight(20.0f, nullptr, &BlacklistButton);
				BlacklistButton.Margin(2.0f, &BlacklistButton);
			}
		}
		else if(CanBlacklist)
		{
			TopBar.VSplitRight(20.0f, nullptr, &BlacklistButton);
			BlacklistButton.Margin(2.0f, &BlacklistButton);
		}

		CUIRect UserBadgeRect;
		if(s_CurCustomTab == ASSETS_TAB_GAME || s_CurCustomTab == ASSETS_TAB_DOWNLOADED_GAME)
		{
			ItemRect.HSplitBottom(16.0f, &ItemRect, &UserBadgeRect);
			UserBadgeRect.Margin(1.0f, &UserBadgeRect);
		}

		CUIRect TextureRect;
		ItemRect.HSplitTop(15, &ItemRect, &TextureRect);
		TextureRect.HSplitTop(10, nullptr, &TextureRect);
		Ui()->DoLabel(&ItemRect, pItem->m_aName, ItemRect.h - 2, TEXTALIGN_MC);

		if(s_CurCustomTab == ASSETS_TAB_DOWNLOADED_GAME && !pItem->m_RenderTexture.IsValid() && !pItem->m_LoadFailed && DownloadedLoadedThisFrame < 2)
		{
			SCustomItem *pMutableItem = const_cast<SCustomItem *>(pItem);
			if(str_comp(pItem->m_aName, "default") == 0)
			{
				pMutableItem->m_RenderTexture = Graphics()->LoadTexture("game.png", IStorage::TYPE_ALL);
			}
			else
			{
				char aPath[IO_MAX_PATH_LENGTH];
				str_format(aPath, sizeof(aPath), "downloaded_gameskins/%s.png", pItem->m_aName);
				pMutableItem->m_RenderTexture = Graphics()->LoadTexture(aPath, IStorage::TYPE_SAVE);
			}
			if(!pMutableItem->m_RenderTexture.IsValid() || pMutableItem->m_RenderTexture.IsNullTexture())
				pMutableItem->m_LoadFailed = true;
			DownloadedLoadedThisFrame++;
		}

		if(pItem->m_RenderTexture.IsValid())
		{
			Graphics()->WrapClamp();
			Graphics()->TextureSet(pItem->m_RenderTexture);
			Graphics()->QuadsBegin();
			if(IsBlacklisted)
				Graphics()->SetColor(0.45f, 0.45f, 0.45f, 0.5f);
			else
				Graphics()->SetColor(1, 1, 1, 1);
			IGraphics::CQuadItem QuadItem(TextureRect.x + (TextureRect.w - TextureWidth) / 2, TextureRect.y + (TextureRect.h - TextureHeight) / 2, TextureWidth, TextureHeight);
			Graphics()->QuadsDrawTL(&QuadItem, 1);
			Graphics()->QuadsEnd();
			Graphics()->WrapNormal();
		}

		if(CanLike)
		{
			const int LikeCount = GameClient()->m_AClientConfigSync.GetGameSkinLikes(pItem->m_aName);
			const bool LikedSession = GameClient()->m_AClientConfigSync.HasLikedGameSkinInSession(pItem->m_aName);
			const bool ItemHovered = Ui()->HotItem() == pItem;
			const bool LikeHovered = Ui()->HotItem() == &pItem->m_LikeButtonId;

			if(LikeCount > 0 || LikedSession || ItemHovered || LikeHovered)
			{
				const ColorRGBA HeartCol = LikedSession
					? ColorRGBA(0.98f, 0.28f, 0.45f, 1.0f)
					: (LikeHovered ? ColorRGBA(0.98f, 0.45f, 0.58f, 1.0f) : (LikeCount > 0 ? ColorRGBA(0.95f, 0.35f, 0.48f, 0.85f) : ColorRGBA(0.88f, 0.88f, 0.92f, 0.50f)));

				CUIRect HeartRect, CountRect;
				LikeButton.VSplitLeft(14.0f, &HeartRect, &CountRect);

				TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
				TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
				TextRender()->TextColor(HeartCol);
				SLabelProperties HeartProps;
				HeartProps.m_MaxWidth = HeartRect.w;
				Ui()->DoLabel(&HeartRect, FontIcon::HEART, 11.0f, TEXTALIGN_MC, HeartProps);
				TextRender()->SetRenderFlags(0);
				TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);

				if(LikeCount > 0)
				{
					char aCountBuf[16];
					str_format(aCountBuf, sizeof(aCountBuf), "%d", LikeCount);
					TextRender()->TextColor(ColorRGBA(0.95f, 0.95f, 0.98f, LikeHovered ? 1.0f : 0.88f));
					SLabelProperties CountProps;
					CountProps.m_MaxWidth = CountRect.w;
					Ui()->DoLabel(&CountRect, aCountBuf, 9.5f, TEXTALIGN_ML, CountProps);
				}
				TextRender()->TextColor(TextRender()->DefaultTextColor());
			}

			if(Ui()->DoButtonLogic(&pItem->m_LikeButtonId, 0, &LikeButton, BUTTONFLAG_LEFT))
			{
				SkipSelectionBecauseDelete = true;
				if(GameClient()->m_AClientConfigSync.IsDiscordReady())
				{
					GameClient()->m_AClientConfigSync.ToggleGameSkinLikeWithDiscord(pItem->m_aName);
				}
				else
				{
					str_copy(s_aLikeModalSkin, pItem->m_aName, sizeof(s_aLikeModalSkin));
					GameClient()->m_AClientConfigSync.ClearLikeStatus();
					s_LikeModalOpen = true;
				}
			}

			if(LikeHovered)
			{
				static char s_aLikeTooltip[128];
				str_format(s_aLikeTooltip, sizeof(s_aLikeTooltip), "Me gusta: %d (clic para puntuar)", LikeCount);
				GameClient()->m_Tooltips.DoToolTip(&pItem->m_LikeButtonId, &LikeButton, s_aLikeTooltip);
			}
		}

		if(CanBlacklist)
		{
			const bool ItemHovered = Ui()->HotItem() == pItem;
			const bool ButtonHovered = Ui()->HotItem() == &pItem->m_BlacklistButtonId;

			if(IsBlacklisted || ItemHovered || ButtonHovered)
			{
				TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
				TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);

				if(IsBlacklisted)
				{
					const float Alpha = ButtonHovered ? 1.0f : 0.8f;
					TextRender()->TextColor(ColorRGBA(0.95f, 0.25f, 0.25f, Alpha));
					SLabelProperties EyeProps;
					EyeProps.m_MaxWidth = BlacklistButton.w;
					Ui()->DoLabel(&BlacklistButton, FontIcon::EYE_SLASH, 12.0f, TEXTALIGN_MC, EyeProps);
				}
				else
				{
					const float Alpha = ButtonHovered ? 1.0f : 0.45f;
					TextRender()->TextColor(ColorRGBA(0.9f, 0.9f, 0.9f, Alpha));
					SLabelProperties EyeProps;
					EyeProps.m_MaxWidth = BlacklistButton.w;
					Ui()->DoLabel(&BlacklistButton, FontIcon::EYE, 12.0f, TEXTALIGN_MC, EyeProps);
				}

				TextRender()->TextColor(TextRender()->DefaultTextColor());
				TextRender()->SetRenderFlags(0);
				TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
			}

			if(Ui()->DoButtonLogic(&pItem->m_BlacklistButtonId, 0, &BlacklistButton, BUTTONFLAG_LEFT))
			{
				SkipSelectionBecauseDelete = true;
				GameClient()->m_AClientGameSkins.ToggleGameSkinBlacklist(pItem->m_aName);
			}

			if(IsBlacklisted)
				GameClient()->m_Tooltips.DoToolTip(&pItem->m_BlacklistButtonId, &BlacklistButton, Localize("Mostrar gameskin (quitar de blacklist)"));
			else
				GameClient()->m_Tooltips.DoToolTip(&pItem->m_BlacklistButtonId, &BlacklistButton, Localize("Ocultar gameskin (agregar a blacklist)"));
		}

		if(CanDelete)
		{
			if(Ui()->HotItem() == pItem || Ui()->HotItem() == &pItem->m_DeleteButtonId)
			{
				TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
				TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
				const float Alpha = Ui()->HotItem() == &pItem->m_DeleteButtonId ? 0.35f : 0.0f;
				TextRender()->TextColor(ColorRGBA(0.95f, 0.35f, 0.35f, 0.65f + Alpha));
				SLabelProperties DeleteProps;
				DeleteProps.m_MaxWidth = DeleteButton.w;
				Ui()->DoLabel(&DeleteButton, FontIcon::TRASH, 12.0f, TEXTALIGN_MC, DeleteProps);
				TextRender()->TextColor(TextRender()->DefaultTextColor());
				TextRender()->SetRenderFlags(0);
				TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
			}

			if(Ui()->DoButtonLogic(&pItem->m_DeleteButtonId, 0, &DeleteButton, BUTTONFLAG_LEFT))
			{
				SkipSelectionBecauseDelete = true;
				str_copy(m_aDeleteAssetName, pItem->m_aName);
				m_DeleteAssetTab = s_CurCustomTab;
				char aBuf[128 + sizeof(pItem->m_aName)];
				str_format(aBuf, sizeof(aBuf), Localize("Are you sure that you want to delete '%s'?"), pItem->m_aName);
				PopupConfirm(Localize("Delete asset"), aBuf, Localize("Yes"), Localize("No"), &CMenus::PopupConfirmDeleteAsset);
			}
			GameClient()->m_Tooltips.DoToolTip(&pItem->m_DeleteButtonId, &DeleteButton, Localize("Delete this asset from your assets directory."));
		}

		if(s_CurCustomTab == ASSETS_TAB_GAME || s_CurCustomTab == ASSETS_TAB_DOWNLOADED_GAME)
		{
			auto UserIt = ActiveSkinUsers.find(pItem->m_aName);
			if(UserIt != ActiveSkinUsers.end() && !UserIt->second.empty())
			{
				const ColorRGBA PillBg = IsBlacklisted ? ColorRGBA(0.45f, 0.15f, 0.15f, 0.85f) : ColorRGBA(0.12f, 0.42f, 0.22f, 0.85f);
				Graphics()->DrawRect(UserBadgeRect.x, UserBadgeRect.y, UserBadgeRect.w, UserBadgeRect.h, PillBg, IGraphics::CORNER_ALL, 4.0f);

				CUIRect IconRect, TextRect;
				UserBadgeRect.VSplitLeft(16.0f, &IconRect, &TextRect);

				TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
				TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
				const ColorRGBA IconColor = IsBlacklisted ? ColorRGBA(1.0f, 0.7f, 0.7f, 1.0f) : ColorRGBA(0.7f, 1.0f, 0.7f, 1.0f);
				TextRender()->TextColor(IconColor);
				SLabelProperties IconProps;
				IconProps.m_MaxWidth = IconRect.w;
				Ui()->DoLabel(&IconRect, FontIcon::USER, 10.0f, TEXTALIGN_MC, IconProps);
				TextRender()->TextColor(TextRender()->DefaultTextColor());
				TextRender()->SetRenderFlags(0);
				TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);

				TextRect.Margin(1.0f, &TextRect);
				SLabelProperties UserProps;
				UserProps.m_MaxWidth = TextRect.w;
				UserProps.m_EllipsisAtEnd = true;
				TextRender()->TextColor(ColorRGBA(0.95f, 0.95f, 0.95f, 1.0f));
				Ui()->DoLabel(&TextRect, UserIt->second.c_str(), 9.0f, TEXTALIGN_ML, UserProps);
				TextRender()->TextColor(TextRender()->DefaultTextColor());

				SCustomItem *pMutableItem = const_cast<SCustomItem *>(pItem);
				str_format(pMutableItem->m_aInUseTooltip, sizeof(pMutableItem->m_aInUseTooltip), "En uso por: %s", UserIt->second.c_str());
				if(Ui()->MouseInside(&UserBadgeRect))
				{
					GameClient()->m_Tooltips.DoToolTip(pItem, &UserBadgeRect, pMutableItem->m_aInUseTooltip);
				}
			}
		}
	}

	const int NewSelected = s_ListBox.DoEnd();
	// AClient: while the assets editor is picking a side, selecting an item assigns it to the editor
	if(EditorPick && s_ListBox.WasItemSelected() && NewSelected >= 0 && !SkipSelectionBecauseDelete)
	{
		const SCustomItem *pPicked = GetCustomItem(s_CurCustomTab, NewSelected);
		if(pPicked != nullptr && pPicked->m_aName[0] != '\0')
		{
			const int EditorCat = AssetsEditorCategoryFromTab(s_CurCustomTab);
			AssetsEditorAssignPickedAsset(m_AssetsEditorState.m_ExploreSide, EditorCat, pPicked->m_aName);
		}
	}
	else if(!EditorPick && OldSelected != NewSelected && !SkipSelectionBecauseDelete)
	{
		if(GetCustomItem(s_CurCustomTab, NewSelected)->m_aName[0] != '\0')
		{
			if(s_CurCustomTab == ASSETS_TAB_ENTITIES)
			{
				str_copy(g_Config.m_ClAssetsEntities, GetCustomItem(s_CurCustomTab, NewSelected)->m_aName);
				GameClient()->m_MapImages.ChangeEntitiesPath(GetCustomItem(s_CurCustomTab, NewSelected)->m_aName);
			}
			else if(s_CurCustomTab == ASSETS_TAB_GAME || s_CurCustomTab == ASSETS_TAB_DOWNLOADED_GAME)
			{
				const char *pSelectedName = GetCustomItem(s_CurCustomTab, NewSelected)->m_aName;
				if(GameClient()->m_AClientGameSkins.IsGameSkinBlacklisted(pSelectedName))
					GameClient()->m_AClientGameSkins.SetGameSkinBlacklisted(pSelectedName, false);
				str_copy(g_Config.m_ClAssetGame, pSelectedName);
				GameClient()->LoadGameSkin(g_Config.m_ClAssetGame);
				GameClient()->m_AClientGameSkins.UpdateActiveSkinUsers();
			}
			else if(s_CurCustomTab == ASSETS_TAB_EMOTICONS)
			{
				str_copy(g_Config.m_ClAssetEmoticons, GetCustomItem(s_CurCustomTab, NewSelected)->m_aName);
				GameClient()->LoadEmoticonsSkin(g_Config.m_ClAssetEmoticons);
			}
			else if(s_CurCustomTab == ASSETS_TAB_PARTICLES)
			{
				str_copy(g_Config.m_ClAssetParticles, GetCustomItem(s_CurCustomTab, NewSelected)->m_aName);
				GameClient()->LoadParticlesSkin(g_Config.m_ClAssetParticles);
			}
			else if(s_CurCustomTab == ASSETS_TAB_HUD)
			{
				str_copy(g_Config.m_ClAssetHud, GetCustomItem(s_CurCustomTab, NewSelected)->m_aName);
				GameClient()->LoadHudSkin(g_Config.m_ClAssetHud);
			}
			else if(s_CurCustomTab == ASSETS_TAB_EXTRAS)
			{
				str_copy(g_Config.m_ClAssetExtras, GetCustomItem(s_CurCustomTab, NewSelected)->m_aName);
				GameClient()->LoadExtrasSkin(g_Config.m_ClAssetExtras);
			}
		}
	}
	} // end if(!SingleModeActive)

	// Quick search
	MainView.HSplitBottom(ms_ButtonHeight, &MainView, &QuickSearch);
	QuickSearch.VSplitLeft(220.0f, &QuickSearch, &DirectoryButton);
	QuickSearch.HSplitTop(5.0f, nullptr, &QuickSearch);
	if(Ui()->DoEditBox_Search(&s_aFilterInputs[s_CurCustomTab], &QuickSearch, 14.0f, !Ui()->IsPopupOpen() && !GameClient()->m_GameConsole.IsActive()))
	{
		gs_aInitCustomList[s_CurCustomTab] = true;
	}

	DirectoryButton.HSplitTop(5.0f, nullptr, &DirectoryButton);
	// AClient: assets editor button next to the directory button
	CUIRect AssetsEditorButton;
	DirectoryButton.VSplitRight(25.0f + 10.0f + 175.0f, nullptr, &DirectoryButton);
	DirectoryButton.VSplitLeft(25.0f, &AssetsEditorButton, &DirectoryButton);
	DirectoryButton.VSplitLeft(10.0f, nullptr, &DirectoryButton);
	DirectoryButton.VSplitRight(25.0f, &DirectoryButton, &ReloadButton);
	DirectoryButton.VSplitRight(10.0f, &DirectoryButton, nullptr);
	{
		TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
		TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
		static CButtonContainer s_AssetsEditorButton;
		const bool EditorPicking = m_AssetsEditorState.m_Open && m_AssetsEditorState.m_ExploreSide >= 0;
		if(DoButton_Menu(&s_AssetsEditorButton, EditorPicking ? FontIcon::CHEVRON_LEFT : FontIcon::PEN_TO_SQUARE, 0, &AssetsEditorButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, -0.2f))
		{
			if(EditorPicking)
			{
				m_AssetsEditorState.m_ExploreSide = -1;
			}
			else
			{
				AssetsEditorOpen(AssetsEditorCategoryFromTab(s_CurCustomTab));
			}
		}
		TextRender()->SetRenderFlags(0);
		TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
		GameClient()->m_Tooltips.DoToolTip(&s_AssetsEditorButton, &AssetsEditorButton, EditorPicking ? Localize("Back to the assets editor") : Localize("AClient assets editor"));
	}
	static CButtonContainer s_AssetsDirId;
	if(DoButton_Menu(&s_AssetsDirId, Localize("Assets directory"), 0, &DirectoryButton))
	{
		char aBuf[IO_MAX_PATH_LENGTH];
		char aBufFull[IO_MAX_PATH_LENGTH + 7];
		if(s_CurCustomTab == ASSETS_TAB_ENTITIES)
			str_copy(aBufFull, "assets/entities");
		else if(s_CurCustomTab == ASSETS_TAB_GAME)
			str_copy(aBufFull, "assets/game");
		else if(s_CurCustomTab == ASSETS_TAB_EMOTICONS)
			str_copy(aBufFull, "assets/emoticons");
		else if(s_CurCustomTab == ASSETS_TAB_PARTICLES)
			str_copy(aBufFull, "assets/particles");
		else if(s_CurCustomTab == ASSETS_TAB_HUD)
			str_copy(aBufFull, "assets/hud");
		else if(s_CurCustomTab == ASSETS_TAB_EXTRAS)
			str_copy(aBufFull, "assets/extras");
		else if(s_CurCustomTab == ASSETS_TAB_DOWNLOADED_GAME)
			str_copy(aBufFull, "downloaded_gameskins");
		Storage()->GetCompletePath(IStorage::TYPE_SAVE, aBufFull, aBuf, sizeof(aBuf));
		Storage()->CreateFolder("assets", IStorage::TYPE_SAVE);
		Storage()->CreateFolder(aBufFull, IStorage::TYPE_SAVE);
		Client()->ViewFile(aBuf);
	}
	GameClient()->m_Tooltips.DoToolTip(&s_AssetsDirId, &DirectoryButton, Localize("Open the directory to add custom assets"));

	TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
	TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
	static CButtonContainer s_AssetsReloadBtnId;
	if(DoButton_Menu(&s_AssetsReloadBtnId, FontIcon::ARROW_ROTATE_RIGHT, 0, &ReloadButton) || Input()->KeyPress(KEY_F5) || (Input()->KeyPress(KEY_R) && Input()->ModifierIsPressed()))
	{
		ClearCustomItems(s_CurCustomTab);
		if(s_CurCustomTab == ASSETS_TAB_GAME || s_CurCustomTab == ASSETS_TAB_DOWNLOADED_GAME)
			GameClient()->m_AClientConfigSync.RefreshGameSkinLikes(true);
	}
	TextRender()->SetRenderFlags(0);
	TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
}

void CMenus::ConchainAssetsEntities(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	CMenus *pThis = (CMenus *)pUserData;
	if(pResult->NumArguments() == 1)
	{
		const char *pArg = pResult->GetString(0);
		if(str_comp(pArg, g_Config.m_ClAssetsEntities) != 0)
		{
			pThis->GameClient()->m_MapImages.ChangeEntitiesPath(pArg);
		}
	}

	pfnCallback(pResult, pCallbackUserData);
}

void CMenus::ConchainAssetGame(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	CMenus *pThis = (CMenus *)pUserData;
	if(pResult->NumArguments() == 1)
	{
		const char *pArg = pResult->GetString(0);
		if(str_comp(pArg, g_Config.m_ClAssetGame) != 0)
		{
			pThis->GameClient()->LoadGameSkin(pArg);
		}
	}

	pfnCallback(pResult, pCallbackUserData);
}

void CMenus::ConchainAssetParticles(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	CMenus *pThis = (CMenus *)pUserData;
	if(pResult->NumArguments() == 1)
	{
		const char *pArg = pResult->GetString(0);
		if(str_comp(pArg, g_Config.m_ClAssetParticles) != 0)
		{
			pThis->GameClient()->LoadParticlesSkin(pArg);
		}
	}

	pfnCallback(pResult, pCallbackUserData);
}

void CMenus::ConchainAssetEmoticons(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	CMenus *pThis = (CMenus *)pUserData;
	if(pResult->NumArguments() == 1)
	{
		const char *pArg = pResult->GetString(0);
		if(str_comp(pArg, g_Config.m_ClAssetEmoticons) != 0)
		{
			pThis->GameClient()->LoadEmoticonsSkin(pArg);
		}
	}

	pfnCallback(pResult, pCallbackUserData);
}

void CMenus::ConchainAssetHud(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	CMenus *pThis = (CMenus *)pUserData;
	if(pResult->NumArguments() == 1)
	{
		const char *pArg = pResult->GetString(0);
		if(str_comp(pArg, g_Config.m_ClAssetHud) != 0)
		{
			pThis->GameClient()->LoadHudSkin(pArg);
		}
	}

	pfnCallback(pResult, pCallbackUserData);
}

void CMenus::ConchainAssetExtras(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	CMenus *pThis = (CMenus *)pUserData;
	if(pResult->NumArguments() == 1)
	{
		const char *pArg = pResult->GetString(0);
		if(str_comp(pArg, g_Config.m_ClAssetExtras) != 0)
		{
			pThis->GameClient()->LoadExtrasSkin(pArg);
		}
	}

	pfnCallback(pResult, pCallbackUserData);
}

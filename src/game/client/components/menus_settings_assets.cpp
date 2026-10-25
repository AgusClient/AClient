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
#include <game/localization.h>

#include <chrono>

using namespace std::chrono_literals;

typedef std::function<void()> TMenuAssetScanLoadedFunc;

struct SMenuAssetScanUser
{
	void *m_pUser;
	TMenuAssetScanLoadedFunc m_LoadedFunc;
};

// IDs of the tabs in the Assets menu
enum
{
	ASSETS_TAB_ENTITIES = 0,
	ASSETS_TAB_GAME = 1,
	ASSETS_TAB_EMOTICONS = 2,
	ASSETS_TAB_PARTICLES = 3,
	ASSETS_TAB_HUD = 4,
	ASSETS_TAB_EXTRAS = 5,
	NUMBER_OF_ASSETS_TABS = 6,
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

static CLineInputBuffered<64> s_aFilterInputs[NUMBER_OF_ASSETS_TABS];

static int s_CurCustomTab = ASSETS_TAB_ENTITIES;

static const CMenus::SCustomItem *GetCustomItem(int CurTab, size_t Index)
{
	if(CurTab == ASSETS_TAB_ENTITIES)
		return gs_vpSearchEntitiesList[Index];
	else if(CurTab == ASSETS_TAB_GAME)
		return gs_vpSearchGamesList[Index];
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
	else if(Tab == ASSETS_TAB_GAME)
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
	if(Tab == ASSETS_TAB_GAME)
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
	else if(Tab == ASSETS_TAB_EMOTICONS)
		AssetsEraseNamedPreview(m_vEmoticonList, Graphics(), pName);
	else if(Tab == ASSETS_TAB_PARTICLES)
		AssetsEraseNamedPreview(m_vParticlesList, Graphics(), pName);
	else if(Tab == ASSETS_TAB_HUD)
		AssetsEraseNamedPreview(m_vHudList, Graphics(), pName);
	else if(Tab == ASSETS_TAB_EXTRAS)
		AssetsEraseNamedPreview(m_vExtrasList, Graphics(), pName);
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

	RemoveCustomAssetFromList(Tab, m_aDeleteAssetName);

	if(WasSelected)
	{
		if(Tab == ASSETS_TAB_ENTITIES)
			GameClient()->m_MapImages.ChangeEntitiesPath(g_Config.m_ClAssetsEntities);
		else if(Tab == ASSETS_TAB_GAME)
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
	CUIRect TabBar, CustomList, QuickSearch, DirectoryButton, ReloadButton;

	MainView.HSplitTop(20.0f, &TabBar, &MainView);
	const float TabWidth = TabBar.w / (float)NUMBER_OF_ASSETS_TABS;
	static CButtonContainer s_aPageTabs[NUMBER_OF_ASSETS_TABS] = {};
	const char *apTabNames[NUMBER_OF_ASSETS_TABS] = {
		Localize("Entities"),
		Localize("Game"),
		Localize("Emoticons"),
		Localize("Particles"),
		Localize("HUD"),
		Localize("Extras")};

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
			std::sort(m_vEntitiesList.begin(), m_vEntitiesList.end());
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
	else
	{
		dbg_assert_failed("Invalid s_CurCustomTab: %d", s_CurCustomTab);
	}

	MainView.HSplitTop(10.0f, nullptr, &MainView);

	// skin selector
	MainView.HSplitTop(MainView.h - 10.0f - ms_ButtonHeight, &CustomList, &MainView);
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
	else if(s_CurCustomTab == ASSETS_TAB_GAME)
	{
		SearchListSize = gs_vpSearchGamesList.size();
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
	s_ListBox.DoStart(TextureHeight + 15.0f + 10.0f + Margin, SearchListSize, CustomList.w / (Margin + TextureWidth), 1, OldSelected, &CustomList, false);
	bool SkipSelectionBecauseDelete = false;
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
		else if(s_CurCustomTab == ASSETS_TAB_GAME)
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
		CUIRect DeleteButton;
		Item.m_Rect.HSplitTop(20.0f, &DeleteButton, nullptr);
		DeleteButton.VSplitRight(20.0f, nullptr, &DeleteButton);
		DeleteButton.Margin(2.0f, &DeleteButton);

		CUIRect TextureRect;
		ItemRect.HSplitTop(15, &ItemRect, &TextureRect);
		TextureRect.HSplitTop(10, nullptr, &TextureRect);
		Ui()->DoLabel(&ItemRect, pItem->m_aName, ItemRect.h - 2, TEXTALIGN_MC);
		if(pItem->m_RenderTexture.IsValid())
		{
			Graphics()->WrapClamp();
			Graphics()->TextureSet(pItem->m_RenderTexture);
			Graphics()->QuadsBegin();
			Graphics()->SetColor(1, 1, 1, 1);
			IGraphics::CQuadItem QuadItem(TextureRect.x + (TextureRect.w - TextureWidth) / 2, TextureRect.y + (TextureRect.h - TextureHeight) / 2, TextureWidth, TextureHeight);
			Graphics()->QuadsDrawTL(&QuadItem, 1);
			Graphics()->QuadsEnd();
			Graphics()->WrapNormal();
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
	}

	const int NewSelected = s_ListBox.DoEnd();
	// AClient: while the assets editor is picking a side, selecting an item assigns it to the editor
	const bool EditorPick = m_AssetsEditorState.m_Open && m_AssetsEditorState.m_ExploreSide >= 0;
	if(EditorPick && s_ListBox.WasItemSelected() && NewSelected >= 0 && !SkipSelectionBecauseDelete)
	{
		const SCustomItem *pPicked = GetCustomItem(s_CurCustomTab, NewSelected);
		if(pPicked != nullptr && pPicked->m_aName[0] != '\0')
			AssetsEditorAssignPickedAsset(m_AssetsEditorState.m_ExploreSide, s_CurCustomTab, pPicked->m_aName);
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
			else if(s_CurCustomTab == ASSETS_TAB_GAME)
			{
				str_copy(g_Config.m_ClAssetGame, GetCustomItem(s_CurCustomTab, NewSelected)->m_aName);
				GameClient()->LoadGameSkin(g_Config.m_ClAssetGame);
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
			m_AssetsEditorState.m_Open = true;
			m_AssetsEditorState.m_ExploreSide = -1;
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

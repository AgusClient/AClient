#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_ACLIENT_SINGLE_ASSETS_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_ACLIENT_SINGLE_ASSETS_H

#include <base/io.h>
#include <base/str.h>

#include <engine/shared/linereader.h>
#include <engine/storage.h>

#include <generated/client_data.h>

#include <cstring>
#include <map>
#include <string>

enum ESingleAssetCategory
{
	SINGLE_ASSET_GAME = 0,
	SINGLE_ASSET_EMOTICONS = 1,
	SINGLE_ASSET_PARTICLES = 2,
	SINGLE_ASSET_HUD = 3,
	SINGLE_ASSET_EXTRAS = 4,
	SINGLE_ASSET_ENTITIES = 5,
	NUM_SINGLE_ASSET_CATEGORIES = 6,
};

class CAClientSingleAssets
{
public:
	static constexpr const char *SAVE_FILENAME = "aclient_single_assets.txt";
	static constexpr const char *SAVE_TMP_FILENAME = "aclient_single_assets.tmp";

	static const char *CategoryKey(int Category)
	{
		switch(Category)
		{
		case SINGLE_ASSET_GAME: return "game";
		case SINGLE_ASSET_EMOTICONS: return "emoticons";
		case SINGLE_ASSET_PARTICLES: return "particles";
		case SINGLE_ASSET_HUD: return "hud";
		case SINGLE_ASSET_EXTRAS: return "extras";
		case SINGLE_ASSET_ENTITIES: return "entities";
		default: return nullptr;
		}
	}

	static int CategoryFromKey(const char *pKey)
	{
		if(!pKey)
			return -1;
		for(int i = 0; i < NUM_SINGLE_ASSET_CATEGORIES; ++i)
		{
			if(str_comp(pKey, CategoryKey(i)) == 0)
				return i;
		}
		return -1;
	}

	bool IsEnabled(int Category) const
	{
		if(Category < 0 || Category >= NUM_SINGLE_ASSET_CATEGORIES)
			return false;
		return m_aEnabled[Category];
	}

	void SetEnabled(int Category, bool Enabled)
	{
		if(Category < 0 || Category >= NUM_SINGLE_ASSET_CATEGORIES)
			return;
		m_aEnabled[Category] = Enabled;
	}

	const char *GetSpriteOverride(int Category, int SpriteId) const
	{
		if(!IsEnabled(Category))
			return nullptr;
		return GetSpriteOverrideRaw(Category, SpriteId);
	}

	const char *GetSpriteOverrideRaw(int Category, int SpriteId) const
	{
		if(Category < 0 || Category >= NUM_SINGLE_ASSET_CATEGORIES)
			return nullptr;
		if(SpriteId < 0 || SpriteId >= NUM_SPRITES)
			return nullptr;
		const char *pSpriteName = g_pData->m_aSprites[SpriteId].m_pName;
		if(!pSpriteName || pSpriteName[0] == '\0')
			return nullptr;
		auto It = m_aOverrides[Category].find(pSpriteName);
		if(It == m_aOverrides[Category].end() || It->second.empty())
			return nullptr;
		return It->second.c_str();
	}

	void SetSpriteOverride(int Category, int SpriteId, const char *pPackName)
	{
		if(Category < 0 || Category >= NUM_SINGLE_ASSET_CATEGORIES)
			return;
		if(SpriteId < 0 || SpriteId >= NUM_SPRITES)
			return;
		const char *pSpriteName = g_pData->m_aSprites[SpriteId].m_pName;
		if(!pSpriteName || pSpriteName[0] == '\0')
			return;
		if(!pPackName || pPackName[0] == '\0')
			m_aOverrides[Category].erase(pSpriteName);
		else
			m_aOverrides[Category][pSpriteName] = pPackName;
	}

	const char *GetEntityTileOverride(int TileIndex) const
	{
		if(!IsEnabled(SINGLE_ASSET_ENTITIES))
			return nullptr;
		return GetEntityTileOverrideRaw(TileIndex);
	}

	const char *GetEntityTileOverrideRaw(int TileIndex) const
	{
		if(TileIndex <= 0 || TileIndex >= 256)
			return nullptr;
		char aKey[16];
		str_format(aKey, sizeof(aKey), "tile_%03d", TileIndex);
		auto It = m_aOverrides[SINGLE_ASSET_ENTITIES].find(aKey);
		if(It == m_aOverrides[SINGLE_ASSET_ENTITIES].end() || It->second.empty())
			return nullptr;
		return It->second.c_str();
	}

	void SetEntityTileOverride(int TileIndex, const char *pPackName)
	{
		if(TileIndex <= 0 || TileIndex >= 256)
			return;
		char aKey[16];
		str_format(aKey, sizeof(aKey), "tile_%03d", TileIndex);
		if(!pPackName || pPackName[0] == '\0')
			m_aOverrides[SINGLE_ASSET_ENTITIES].erase(aKey);
		else
			m_aOverrides[SINGLE_ASSET_ENTITIES][aKey] = pPackName;
	}

	const char *GetPartOverride(int Category, int PartId) const
	{
		if(Category == SINGLE_ASSET_ENTITIES)
			return GetEntityTileOverride(PartId);
		return GetSpriteOverride(Category, PartId);
	}

	const char *GetPartOverrideRaw(int Category, int PartId) const
	{
		if(Category == SINGLE_ASSET_ENTITIES)
			return GetEntityTileOverrideRaw(PartId);
		return GetSpriteOverrideRaw(Category, PartId);
	}

	void SetPartOverride(int Category, int PartId, const char *pPackName)
	{
		if(Category == SINGLE_ASSET_ENTITIES)
			SetEntityTileOverride(PartId, pPackName);
		else
			SetSpriteOverride(Category, PartId, pPackName);
	}

	void ClearCategory(int Category)
	{
		if(Category < 0 || Category >= NUM_SINGLE_ASSET_CATEGORIES)
			return;
		m_aOverrides[Category].clear();
	}

	void RemovePackReferences(int Category, const char *pPackName)
	{
		if(Category < 0 || Category >= NUM_SINGLE_ASSET_CATEGORIES || !pPackName || pPackName[0] == '\0')
			return;
		auto &Map = m_aOverrides[Category];
		for(auto It = Map.begin(); It != Map.end();)
		{
			if(str_comp(It->second.c_str(), pPackName) == 0)
				It = Map.erase(It);
			else
				++It;
		}
	}

	size_t CountOverrides(int Category) const
	{
		if(Category < 0 || Category >= NUM_SINGLE_ASSET_CATEGORIES)
			return 0;
		return m_aOverrides[Category].size();
	}

	void Load(IStorage *pStorage)
	{
		for(int i = 0; i < NUM_SINGLE_ASSET_CATEGORIES; ++i)
		{
			m_aEnabled[i] = false;
			m_aOverrides[i].clear();
		}
		if(!pStorage)
			return;

		CLineReader LineReader;
		if(!LineReader.OpenFile(pStorage->OpenFile(SAVE_FILENAME, IOFLAG_READ, IStorage::TYPE_SAVE)))
			return;

		while(const char *pLine = LineReader.Get())
		{
			if(pLine[0] == '\0' || pLine[0] == '#')
				continue;

			if(str_startswith(pLine, "mode "))
			{
				const char *pRest = pLine + 5;
				const char *pSpace = str_find(pRest, " ");
				if(!pSpace)
					continue;
				char aCat[32];
				str_truncate(aCat, sizeof(aCat), pRest, (int)(pSpace - pRest));
				const int Cat = CategoryFromKey(aCat);
				if(Cat >= 0)
					m_aEnabled[Cat] = str_toint(pSpace + 1) != 0;
			}
			else if(str_startswith(pLine, "part "))
			{
				const char *pRest = pLine + 5;
				const char *pSp1 = str_find(pRest, " ");
				if(!pSp1)
					continue;
				const char *pSp2 = str_find(pSp1 + 1, " ");
				if(!pSp2)
					continue;

				char aCat[32];
				char aSprite[64];
				str_truncate(aCat, sizeof(aCat), pRest, (int)(pSp1 - pRest));
				str_truncate(aSprite, sizeof(aSprite), pSp1 + 1, (int)(pSp2 - (pSp1 + 1)));
				const char *pPack = pSp2 + 1;

				const int Cat = CategoryFromKey(aCat);
				if(Cat >= 0 && aSprite[0] != '\0' && pPack[0] != '\0')
					m_aOverrides[Cat][aSprite] = pPack;
			}
		}
	}

	bool Save(IStorage *pStorage) const
	{
		if(!pStorage)
			return false;

		IOHANDLE File = pStorage->OpenFile(SAVE_TMP_FILENAME, IOFLAG_WRITE, IStorage::TYPE_SAVE);
		if(!File)
			return false;

		auto WriteLine = [&](const char *pLine) {
			io_write(File, pLine, str_length(pLine));
			io_write(File, "\n", 1);
		};

		WriteLine("AClientSingleAssets 1");
		for(int Cat = 0; Cat < NUM_SINGLE_ASSET_CATEGORIES; ++Cat)
		{
			const char *pCatKey = CategoryKey(Cat);
			char aLine[256];
			str_format(aLine, sizeof(aLine), "mode %s %d", pCatKey, m_aEnabled[Cat] ? 1 : 0);
			WriteLine(aLine);

			for(const auto &[SpriteName, PackName] : m_aOverrides[Cat])
			{
				if(SpriteName.empty() || PackName.empty())
					continue;
				str_format(aLine, sizeof(aLine), "part %s %s %s", pCatKey, SpriteName.c_str(), PackName.c_str());
				WriteLine(aLine);
			}
		}

		io_close(File);
		if(!pStorage->RenameFile(SAVE_TMP_FILENAME, SAVE_FILENAME, IStorage::TYPE_SAVE))
		{
			// Fallback: remove destination and retry rename on Windows if overwrite failed
			pStorage->RemoveFile(SAVE_FILENAME, IStorage::TYPE_SAVE);
			return pStorage->RenameFile(SAVE_TMP_FILENAME, SAVE_FILENAME, IStorage::TYPE_SAVE);
		}
		return true;
	}

private:
	bool m_aEnabled[NUM_SINGLE_ASSET_CATEGORIES] = {};
	std::map<std::string, std::string> m_aOverrides[NUM_SINGLE_ASSET_CATEGORIES];
};

#endif // GAME_CLIENT_COMPONENTS_TCLIENT_ACLIENT_SINGLE_ASSETS_H

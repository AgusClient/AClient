#include "translate.h"

#include <base/dbg.h>
#include <base/log.h>

#include <engine/shared/json.h>
#include <engine/shared/jsonwriter.h>
#include <engine/shared/protocol.h>

#include <game/client/gameclient.h>
#include <game/client/lineinput.h>
#include <game/localization.h>

#include <algorithm>
#include <memory>

static void UrlEncode(const char *pText, char *pOut, size_t Length)
{
	if(Length == 0)
		return;
	size_t OutPos = 0;
	for(const char *p = pText; *p && OutPos < Length - 1; ++p)
	{
		unsigned char c = *(const unsigned char *)p;
		if(isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
		{
			if(OutPos >= Length - 1)
				break;
			pOut[OutPos++] = c;
		}
		else
		{
			if(OutPos + 3 >= Length)
				break;
			snprintf(pOut + OutPos, 4, "%%%02X", c);
			OutPos += 3;
		}
	}
	pOut[OutPos] = '\0';
}

const char *ITranslateBackend::EncodeTarget(const char *pTarget) const
{
	if(!pTarget || pTarget[0] == '\0')
		return DefaultConfig::TcTranslateTarget;
	return pTarget;
}

bool ITranslateBackend::CompareTargets(const char *pA, const char *pB) const
{
	if(pA == pB) // if(!pA && !pB)
		return true;
	if(!pA || !pB)
		return false;
	if(str_comp_nocase(EncodeTarget(pA), EncodeTarget(pB)) == 0)
		return true;
	return false;
}

class ITranslateBackendHttp : public ITranslateBackend
{
protected:
	using ITranslateBackend::ITranslateBackend;
	std::shared_ptr<IHttpRequest> m_pHttpRequest = nullptr;
	virtual bool ParseResponse(CTranslateResponse &Out) = 0;
	virtual bool ParseHttpError() const { return false; }

	void CreateHttpRequest(IHttp &Http, const char *pUrl)
	{
		auto pGet = HttpGet(pUrl);
		pGet->LogProgress(HTTPLOG::FAILURE);
		pGet->FailOnErrorStatus(false);
		pGet->Timeout(CTimeout{10000, 0, 500, 10});

		m_pHttpRequest = std::move(pGet);
		Http.Run(m_pHttpRequest);
	}

	// Same as CreateHttpRequest, but POSTs pBody as a
	// application/x-www-form-urlencoded body instead of encoding everything
	// into the URL's query string -- keeps the URL itself short (the fixed
	// CHttpRequest::m_aUrl buffer is 256 bytes, easily exceeded by a long
	// chat message otherwise) and matches what the endpoint actually expects.
	void CreateHttpRequestPost(IHttp &Http, const char *pUrl, const char *pBody)
	{
		auto pPost = HttpGet(pUrl);
		pPost->LogProgress(HTTPLOG::FAILURE);
		pPost->FailOnErrorStatus(false);
		pPost->Timeout(CTimeout{10000, 0, 500, 10});
		pPost->HeaderString("Content-Type", "application/x-www-form-urlencoded");
		pPost->Post((const unsigned char *)pBody, str_length(pBody));

		m_pHttpRequest = std::move(pPost);
		Http.Run(m_pHttpRequest);
	}

public:
	std::optional<bool> Update(CTranslateResponse &Out) override
	{
		dbg_assert(m_pHttpRequest != nullptr, "m_pHttpRequest is nullptr");
		if(m_pHttpRequest->State() == EHttpState::RUNNING || m_pHttpRequest->State() == EHttpState::QUEUED)
			return std::nullopt;
		if(m_pHttpRequest->State() == EHttpState::ABORTED)
		{
			str_copy(Out.m_Text, "Aborted");
			return false;
		}
		if(m_pHttpRequest->State() != EHttpState::DONE)
		{
			str_copy(Out.m_Text, "Curl error, see console");
			return false;
		}
		if(m_pHttpRequest->StatusCode() != 200 && !ParseHttpError())
		{
			str_format(Out.m_Text, sizeof(Out.m_Text), "Got http code %d", m_pHttpRequest->StatusCode());
			return false;
		}
		return ParseResponse(Out);
	}
	~ITranslateBackendHttp() override
	{
		if(m_pHttpRequest)
			m_pHttpRequest->Abort();
	}
};

class CTranslateBackendLibretranslate : public ITranslateBackendHttp
{
private:
	bool ParseResponseJson(const json_value *pObj, CTranslateResponse &Out)
	{
		if(!pObj)
		{
			str_copy(Out.m_Text, "Response is not JSON");
			return false;
		}

		if(pObj->type != json_object)
		{
			str_copy(Out.m_Text, "Response is not object");
			return false;
		}

		const json_value *pError = json_object_get(pObj, "error");
		if(pError != &json_value_none)
		{
			if(pError->type != json_string)
				str_copy(Out.m_Text, "Error is not string");
			else
				str_copy(Out.m_Text, pError->u.string.ptr);
			return false;
		}

		const json_value *pTranslatedText = json_object_get(pObj, "translatedText");
		if(pTranslatedText == &json_value_none)
		{
			str_copy(Out.m_Text, "No translatedText");
			return false;
		}
		if(pTranslatedText->type != json_string)
		{
			str_copy(Out.m_Text, "translatedText is not string");
			return false;
		}

		const json_value *pDetectedLanguage = json_object_get(pObj, "detectedLanguage");
		if(pDetectedLanguage == &json_value_none)
		{
			str_copy(Out.m_Text, "No pDetectedLanguage");
			return false;
		}
		if(pDetectedLanguage->type != json_object)
		{
			str_copy(Out.m_Text, "pDetectedLanguage is not object");
			return false;
		}

		const json_value *pConfidence = json_object_get(pDetectedLanguage, "confidence");
		if(pConfidence == &json_value_none || ((pConfidence->type == json_double && pConfidence->u.dbl == 0.0f) ||
							      (pConfidence->type == json_integer && pConfidence->u.integer == 0)))
		{
			str_copy(Out.m_Text, "Unknown language");
			return false;
		}

		const json_value *pLanguage = json_object_get(pDetectedLanguage, "language");
		if(pLanguage == &json_value_none)
		{
			str_copy(Out.m_Text, "No language");
			return false;
		}
		if(pLanguage->type != json_string)
		{
			str_copy(Out.m_Text, "language is not string");
			return false;
		}

		str_copy(Out.m_Text, pTranslatedText->u.string.ptr);
		str_copy(Out.m_Language, pLanguage->u.string.ptr);

		return true;
	}

protected:
	bool ParseResponse(CTranslateResponse &Out) override
	{
		json_value *pObj = m_pHttpRequest->ResultJson();
		bool Res = ParseResponseJson(pObj, Out);
		json_value_free(pObj);
		return Res;
	}
	bool ParseHttpError() const override { return true; }

public:
	CTranslateBackendLibretranslate(IHttp &Http, const char *pText, const char *pTargetLang, const char *pName) :
		ITranslateBackendHttp(pName)
	{
		CJsonStringWriter Json = CJsonStringWriter();
		Json.BeginObject();
		Json.WriteAttribute("q");
		Json.WriteStrValue(pText);
		Json.WriteAttribute("source");
		Json.WriteStrValue("auto");
		Json.WriteAttribute("target");
		Json.WriteStrValue(EncodeTarget(pTargetLang));
		Json.WriteAttribute("format");
		Json.WriteStrValue("text");
		if(g_Config.m_TcTranslateKey[0] != '\0')
		{
			Json.WriteAttribute("api_key");
			Json.WriteStrValue(g_Config.m_TcTranslateKey);
		}
		Json.EndObject();
		CreateHttpRequest(Http, g_Config.m_TcTranslateEndpoint[0] == '\0' ? "localhost:5000/translate" : g_Config.m_TcTranslateEndpoint);
		const char *pJson = Json.GetOutputString().c_str();
		m_pHttpRequest->PostJson(pJson);
	}
};

// Talks directly to Google's free, unofficial (undocumented) translate
// endpoint -- this is the same endpoint the now-defunct FreeTranslateAPI
// (ftapi.pythonanywhere.com) used to wrap, without needing a third-party
// mirror to stay up. See https://github.com/TaterClient/TClient/pull/203
// for context on why the old FTApi-wrapping backend was removed instead of
// just re-pointed: its hosting is gone, and anyone who wants a self-hosted
// option already has the LibreTranslate backend for that.
class CTranslateBackendGoogle : public ITranslateBackendHttp
{
private:
	bool ParseResponseJson(const json_value *pObj, CTranslateResponse &Out)
	{
		if(!pObj)
		{
			str_copy(Out.m_Text, "Response is not JSON");
			return false;
		}
		if(pObj->type != json_array || json_array_length(pObj) < 3)
		{
			str_copy(Out.m_Text, "Unexpected response format");
			return false;
		}

		const json_value *pSentences = json_array_get(pObj, 0);
		if(!pSentences || pSentences->type != json_array)
		{
			str_copy(Out.m_Text, "No translated sentences");
			return false;
		}

		char aText[sizeof(Out.m_Text)] = "";
		const int NumSentences = json_array_length(pSentences);
		for(int i = 0; i < NumSentences; i++)
		{
			const json_value *pSentence = json_array_get(pSentences, i);
			if(!pSentence || pSentence->type != json_array || json_array_length(pSentence) < 1)
				continue;
			const json_value *pChunk = json_array_get(pSentence, 0);
			if(!pChunk || pChunk->type != json_string)
				continue;
			str_append(aText, pChunk->u.string.ptr);
		}
		if(aText[0] == '\0')
		{
			str_copy(Out.m_Text, "Empty translation");
			return false;
		}

		const json_value *pLanguage = json_array_get(pObj, 2);
		if(pLanguage && pLanguage->type == json_string)
			str_copy(Out.m_Language, pLanguage->u.string.ptr);

		str_copy(Out.m_Text, aText);
		return true;
	}

protected:
	bool ParseResponse(CTranslateResponse &Out) override
	{
		json_value *pObj = m_pHttpRequest->ResultJson();
		bool Res = ParseResponseJson(pObj, Out);
		json_value_free(pObj);
		return Res;
	}

public:
	CTranslateBackendGoogle(IHttp &Http, const char *pText, const char *pTargetLang, const char *pName) :
		ITranslateBackendHttp(pName)
	{
		// Query params go in the POST body, not the URL -- keeps the URL
		// itself short and fixed (see CreateHttpRequestPost) regardless of
		// how long pText is.
		char aBody[4096];
		str_format(aBody, sizeof(aBody), "client=gtx&sl=auto&dt=t&tl=%s&q=", EncodeTarget(pTargetLang));

		UrlEncode(pText, aBody + strlen(aBody), sizeof(aBody) - strlen(aBody));

		const char *pEndpoint = g_Config.m_TcTranslateEndpoint[0] != '\0' ? g_Config.m_TcTranslateEndpoint : "https://translate.googleapis.com/translate_a/single";
		CreateHttpRequestPost(Http, pEndpoint, aBody);
	}
};

// Per-backend supported-language lists for the settings menu -- Google
// supports far more languages than a typical self-hosted LibreTranslate
// instance, so these are deliberately not one shared list. Both are curated
// subsets (Google supports 100+ languages; LibreTranslate's exact set
// depends on which models a given self-hosted instance has installed), not
// a claim of exhaustive coverage.
static const std::vector<STranslateLanguage> s_vGoogleLanguages = {
	{"Arabic", "ar"},
	{"Bulgarian", "bg"},
	{"Chinese (Simplified)", "zh"},
	{"Chinese (Traditional)", "zh-TW"},
	{"Croatian", "hr"},
	{"Czech", "cs"},
	{"Danish", "da"},
	{"Dutch", "nl"},
	{"English", "en"},
	{"Estonian", "et"},
	{"Finnish", "fi"},
	{"French", "fr"},
	{"German", "de"},
	{"Greek", "el"},
	{"Hebrew", "he"},
	{"Hindi", "hi"},
	{"Hungarian", "hu"},
	{"Indonesian", "id"},
	{"Italian", "it"},
	{"Japanese", "ja"},
	{"Korean", "ko"},
	{"Latvian", "lv"},
	{"Lithuanian", "lt"},
	{"Norwegian", "no"},
	{"Persian", "fa"},
	{"Polish", "pl"},
	{"Portuguese", "pt"},
	{"Romanian", "ro"},
	{"Russian", "ru"},
	{"Serbian", "sr"},
	{"Slovak", "sk"},
	{"Slovenian", "sl"},
	{"Spanish", "es"},
	{"Swedish", "sv"},
	{"Thai", "th"},
	{"Turkish", "tr"},
	{"Ukrainian", "uk"},
	{"Vietnamese", "vi"},
};

static const std::vector<STranslateLanguage> s_vLibretranslateLanguages = {
	{"Arabic", "ar"},
	{"Chinese (Simplified)", "zh"},
	{"Czech", "cs"},
	{"Dutch", "nl"},
	{"English", "en"},
	{"Finnish", "fi"},
	{"French", "fr"},
	{"German", "de"},
	{"Greek", "el"},
	{"Hebrew", "he"},
	{"Hindi", "hi"},
	{"Hungarian", "hu"},
	{"Indonesian", "id"},
	{"Italian", "it"},
	{"Japanese", "ja"},
	{"Korean", "ko"},
	{"Polish", "pl"},
	{"Portuguese", "pt"},
	{"Russian", "ru"},
	{"Spanish", "es"},
	{"Swedish", "sv"},
	{"Turkish", "tr"},
	{"Ukrainian", "uk"},
	{"Vietnamese", "vi"},
};

const std::array<STranslateBackendInfo, 2> g_aTranslateBackends = {{
	{"google", "Google", s_vGoogleLanguages, false},
	{"libretranslate", "LibreTranslate", s_vLibretranslateLanguages, true},
}};

int TranslateLanguageIndex(const std::vector<STranslateLanguage> &vLanguages, const char *pCode)
{
	for(size_t i = 0; i < vLanguages.size(); i++)
		if(str_comp_nocase(pCode, vLanguages[i].m_pCode) == 0)
			return (int)i;
	for(size_t i = 0; i < vLanguages.size(); i++)
		if(str_comp_nocase("en", vLanguages[i].m_pCode) == 0)
			return (int)i;
	return 0;
}

static std::unique_ptr<ITranslateBackend> CreateTranslateBackend(IHttp &Http, const char *pText, const char *pTargetLang)
{
	for(const STranslateBackendInfo &Backend : g_aTranslateBackends)
	{
		if(str_comp_nocase(g_Config.m_TcTranslateBackend, Backend.m_pValue) != 0)
			continue;
		if(str_comp_nocase(Backend.m_pValue, "libretranslate") == 0)
			return std::make_unique<CTranslateBackendLibretranslate>(Http, pText, pTargetLang, Backend.m_pName);
		if(str_comp_nocase(Backend.m_pValue, "google") == 0)
			return std::make_unique<CTranslateBackendGoogle>(Http, pText, pTargetLang, Backend.m_pName);
	}
	return nullptr;
}

void CTranslate::ConTranslate(IConsole::IResult *pResult, void *pUserData)
{
	const char *pName;
	if(pResult->NumArguments() == 0)
		pName = nullptr;
	else
		pName = pResult->GetString(0);

	CTranslate *pThis = static_cast<CTranslate *>(pUserData);
	pThis->Translate(pName);
}

void CTranslate::ConTranslateId(IConsole::IResult *pResult, void *pUserData)
{
	CTranslate *pThis = static_cast<CTranslate *>(pUserData);
	pThis->Translate(pResult->GetInteger(0));
}

void CTranslate::ConTranslateAuto(IConsole::IResult *pResult, void *pUserData)
{
	CTranslate *pThis = static_cast<CTranslate *>(pUserData);
	if(pResult->NumArguments() > 0)
		g_Config.m_TcTranslateAuto = pResult->GetInteger(0) ? 1 : 0;
	else
	{
		char aBuf[64];
		str_format(aBuf, sizeof(aBuf), "Value: %d", g_Config.m_TcTranslateAuto);
		pThis->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "translate", aBuf);
	}
}

void CTranslate::OnConsoleInit()
{
	Console()->Register("translate", "?r[name]", CFGFLAG_CLIENT, ConTranslate, this, "Translate last message (of a given name)");
	Console()->Register("translate_id", "v[id]", CFGFLAG_CLIENT, ConTranslateId, this, "Translate last message of the person with this id");
	Console()->Register("translate_auto", "?i[value]", CFGFLAG_CLIENT, ConTranslateAuto, this, "Toggle or set automatically translating incoming messages (alias for tc_translate_auto)");
}

void CTranslate::Translate(int Id, bool ShowProgress)
{
	if(Id < 0 || Id > (int)std::size(GameClient()->m_aClients))
	{
		GameClient()->m_Chat.Echo("Not a valid ID");
		return;
	}
	const auto &Player = GameClient()->m_aClients[Id];
	if(!Player.m_Active)
	{
		GameClient()->m_Chat.Echo("ID not connected");
		return;
	}
	Translate(Player.m_aName, ShowProgress);
}

void CTranslate::Translate(const char *pName, bool ShowProgress)
{
	CChat::CLine *pLineBest = nullptr;
	if(GameClient()->m_Chat.m_CurrentLine > 0)
	{
		int ScoreBest = -1;
		for(int i = 0; i < CChat::MAX_LINES; i++)
		{
			CChat::CLine *pLine = &GameClient()->m_Chat.m_aLines[((GameClient()->m_Chat.m_CurrentLine - i) + CChat::MAX_LINES) % CChat::MAX_LINES];
			if(pLine->m_pTranslateResponse != nullptr)
				continue;
			if(pLine->m_ClientId == CChat::CLIENT_MSG)
				continue;
			for(int Id : GameClient()->m_aLocalIds)
				if(pLine->m_ClientId == Id)
					continue;
			int Score = 0;
			if(pName)
			{
				if(pLine->m_ClientId == CChat::SERVER_MSG)
					continue;
				if(str_comp(pLine->m_aName, pName) == 0)
					Score = 2;
				else if(str_comp_nocase(pLine->m_aName, pName) == 0)
					Score = 1;
				else
					continue;
			}
			if(Score > ScoreBest)
			{
				ScoreBest = Score;
				pLineBest = pLine;
			}
		}
	}
	if(!pLineBest || pLineBest->m_aText[0] == '\0')
	{
		GameClient()->m_Chat.Echo("No message to translate");
		return;
	}

	Translate(*pLineBest, ShowProgress);
}

namespace
{
struct STranslateCacheEntry
{
	uint64_t m_Hash = 0;
	char m_aTarget[16] = "";
	char m_aSource[MAX_CHAT_LENGTH] = "";
	char m_aTranslated[MAX_CHAT_LENGTH] = "";
	char m_aLanguage[16] = "";
	bool m_Valid = false;
};

constexpr int TRANSLATE_CACHE_SIZE = 32;
STranslateCacheEntry gs_aTranslateCache[TRANSLATE_CACHE_SIZE];
int gs_TranslateCacheWriteIdx = 0;

uint64_t HashTranslateKey(const char *pTarget, const char *pSource)
{
	uint64_t Hash = 14695981039346656037ULL;
	for(const char *p = pTarget; *p; ++p)
	{
		Hash ^= (uint8_t)tolower((unsigned char)*p);
		Hash *= 1099511628211ULL;
	}
	Hash ^= 0xff;
	Hash *= 1099511628211ULL;
	for(const char *p = pSource; *p; ++p)
	{
		Hash ^= (uint8_t)*p;
		Hash *= 1099511628211ULL;
	}
	return Hash;
}

bool LookupTranslateCache(const char *pTarget, const char *pSource, CTranslateResponse &Out)
{
	const uint64_t Hash = HashTranslateKey(pTarget, pSource);
	for(const auto &Entry : gs_aTranslateCache)
	{
		if(Entry.m_Valid && Entry.m_Hash == Hash && str_comp_nocase(Entry.m_aTarget, pTarget) == 0 && str_comp(Entry.m_aSource, pSource) == 0)
		{
			str_copy(Out.m_Text, Entry.m_aTranslated);
			str_copy(Out.m_Language, Entry.m_aLanguage);
			Out.m_Error = false;
			return true;
		}
	}
	return false;
}

void StoreTranslateCache(const char *pTarget, const char *pSource, const CTranslateResponse &Res)
{
	if(!pSource || !pSource[0] || !pTarget || !pTarget[0] || Res.m_Error)
		return;
	const uint64_t Hash = HashTranslateKey(pTarget, pSource);
	for(auto &Entry : gs_aTranslateCache)
	{
		if(Entry.m_Valid && Entry.m_Hash == Hash && str_comp_nocase(Entry.m_aTarget, pTarget) == 0 && str_comp(Entry.m_aSource, pSource) == 0)
		{
			str_copy(Entry.m_aTranslated, Res.m_Text);
			str_copy(Entry.m_aLanguage, Res.m_Language);
			return;
		}
	}
	STranslateCacheEntry &Slot = gs_aTranslateCache[gs_TranslateCacheWriteIdx];
	gs_TranslateCacheWriteIdx = (gs_TranslateCacheWriteIdx + 1) % TRANSLATE_CACHE_SIZE;
	Slot.m_Valid = true;
	Slot.m_Hash = Hash;
	str_copy(Slot.m_aTarget, pTarget);
	str_copy(Slot.m_aSource, pSource);
	str_copy(Slot.m_aTranslated, Res.m_Text);
	str_copy(Slot.m_aLanguage, Res.m_Language);
}

bool ShouldSkipAutoTranslate(const char *pText)
{
	if(!pText || pText[0] == '\0')
		return true;
	if(str_find(pText, "[AC-CHESS]"))
		return true;
	if(str_startswith_nocase(pText, "http://") || str_startswith_nocase(pText, "https://"))
		return true;
	int AlphaCount = 0;
	for(const unsigned char *p = (const unsigned char *)pText; *p; ++p)
	{
		if((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || *p >= 0x80)
		{
			if(++AlphaCount >= 2)
				return false;
		}
	}
	return true;
}
} // namespace

void CTranslate::Translate(CChat::CLine &Line, bool ShowProgress)
{
	auto CachedResp = std::make_shared<CTranslateResponse>();
	if(LookupTranslateCache(g_Config.m_TcTranslateTarget, Line.m_aText, *CachedResp))
	{
		Line.m_pTranslateResponse = std::move(CachedResp);
		GameClient()->m_Chat.RebuildChat();
		return;
	}

	if(m_vJobs.size() > 15)
	{
		return;
	}

	CTranslateJob Job;
	Job.m_pLine = &Line;
	Job.m_pTranslateResponse = std::move(CachedResp);
	Job.m_pLine->m_pTranslateResponse = Job.m_pTranslateResponse;

	Job.m_pBackend = CreateTranslateBackend(*Http(), Job.m_pLine->m_aText, g_Config.m_TcTranslateTarget);
	if(!Job.m_pBackend)
	{
		GameClient()->m_Chat.Echo("Invalid translate backend");
		return;
	}

	if(ShowProgress)
	{
		str_format(Job.m_pTranslateResponse->m_Text, sizeof(Job.m_pTranslateResponse->m_Text), TCLocalize("%s translating to %s", "translate"), Job.m_pBackend->Name(), g_Config.m_TcTranslateTarget);
		Job.m_pLine->m_Time = time();
	}
	else
	{
		Job.m_pTranslateResponse->m_Text[0] = '\0';
	}

	m_vJobs.emplace_back(std::move(Job));

	if(ShowProgress)
		GameClient()->m_Chat.RebuildChat();
}

void CTranslate::OnRender()
{
	const auto Time = time();
	auto ForEach = [&](CTranslateJob &Job) {
		if(Job.m_IsOutgoing)
		{
			const std::optional<bool> Done = Job.m_pBackend->Update(*Job.m_pTranslateResponse);
			if(!Done.has_value())
				return false; // Keep ongoing tasks
			if(*Done && Job.m_pTranslateResponse->m_Text[0] != '\0')
				GameClient()->m_Chat.SendChat(Job.m_OutgoingTeam, Job.m_pTranslateResponse->m_Text);
			else
			{
				// Translation failed (or came back empty): don't swallow the message, send it untranslated
				char aBuf[sizeof(Job.m_pTranslateResponse->m_Text) + 64];
				str_format(aBuf, sizeof(aBuf), TCLocalize("Translating your message failed, sending it untranslated: %s", "translate"), Job.m_pTranslateResponse->m_Text);
				GameClient()->m_Chat.Echo(aBuf);
				GameClient()->m_Chat.SendChat(Job.m_OutgoingTeam, Job.m_aOutgoingOriginalText);
			}
			return true;
		}
		if(Job.m_pLine->m_pTranslateResponse != Job.m_pTranslateResponse)
			return true; // Not the same line anymore
		const std::optional<bool> Done = Job.m_pBackend->Update(*Job.m_pTranslateResponse);
		if(!Done.has_value())
			return false; // Keep ongoing tasks
		if(*Done)
		{
			if(str_comp_nocase(Job.m_pLine->m_aText, Job.m_pTranslateResponse->m_Text) == 0) // Check for no translation difference
				Job.m_pTranslateResponse->m_Text[0] = '\0';
			StoreTranslateCache(g_Config.m_TcTranslateTarget, Job.m_pLine->m_aText, *Job.m_pTranslateResponse);
		}
		else
		{
			char aBuf[sizeof(Job.m_pTranslateResponse->m_Text)];
			str_format(aBuf, sizeof(aBuf), TCLocalize("%s to %s failed: %s", "translate"), Job.m_pBackend->Name(), g_Config.m_TcTranslateTarget, Job.m_pTranslateResponse->m_Text);
			Job.m_pTranslateResponse->m_Error = true;
			str_copy(Job.m_pTranslateResponse->m_Text, aBuf);
		}
		Job.m_pLine->m_Time = Time;
		GameClient()->m_Chat.RebuildChat();
		return true;
	};
	m_vJobs.erase(std::remove_if(m_vJobs.begin(), m_vJobs.end(), ForEach), m_vJobs.end());
}

void CTranslate::AutoTranslate(CChat::CLine &Line)
{
	if(!g_Config.m_TcTranslateAuto)
		return;
	if(Line.m_ClientId == CChat::CLIENT_MSG)
		return;
	for(const int Id : GameClient()->m_aLocalIds)
	{
		if(Id >= 0 && Id == Line.m_ClientId)
			return;
	}
	if(ShouldSkipAutoTranslate(Line.m_aText))
		return;
	Translate(Line, false);
}

bool CTranslate::ChatDoTranslateOutgoing(int Team, const char *pText)
{
	// Don't translate server commands (e.g. "/w name msg"), translating would break their syntax
	if(!g_Config.m_TcTranslateOutgoing || pText[0] == '\0' || pText[0] == '/')
		return false;

	if(m_vJobs.size() > 15)
	{
		// Too many jobs in flight: don't queue another, let the caller send this one untranslated
		return false;
	}

	CTranslateJob Job;
	Job.m_IsOutgoing = true;
	Job.m_OutgoingTeam = Team;
	str_copy(Job.m_aOutgoingOriginalText, pText);
	Job.m_pTranslateResponse = std::make_shared<CTranslateResponse>();

	Job.m_pBackend = CreateTranslateBackend(*Http(), pText, g_Config.m_TcTranslateOutgoingTarget);
	if(!Job.m_pBackend)
	{
		GameClient()->m_Chat.Echo("Invalid translate backend");
		return false;
	}

	m_vJobs.emplace_back(std::move(Job));
	return true;
}

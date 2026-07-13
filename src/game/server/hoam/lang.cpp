#include "lang.h"

#include <base/str.h>

#include <engine/console.h>
#include <engine/server.h>
#include <engine/shared/config.h>
#include <engine/shared/protocol.h>

#include <game/server/gamecontext.h>
#include <game/server/player.h>

// ISO 3166-1 numeric: CN, TW, HK, MO
static bool HoLangCountryPrefersZh(int Country)
{
	return Country == 156 || Country == 158 || Country == 344 || Country == 446;
}

static int HoLangDefaultFromConfig()
{
	const int Parsed = HoLangParse(g_Config.m_HoLang);
	if(Parsed >= 0)
		return Parsed;
	return HO_LANG_AUTO;
}

void HoLangInitPlayer(CPlayer *pPlayer)
{
	if(!pPlayer)
		return;
	pPlayer->m_HoLang = HoLangDefaultFromConfig();
}

int HoLangResolve(CGameContext *pGameServer, const CPlayer *pPlayer)
{
	if(!pPlayer)
		return HO_LANG_EN;

	const int Lang = pPlayer->m_HoLang;
	if(Lang == HO_LANG_EN || Lang == HO_LANG_ZH)
		return Lang;

	// AUTO: client language is not sent to the server; use ISO country flag.
	if(!pGameServer)
		return HO_LANG_EN;
	const int Country = pGameServer->Server()->ClientCountry(pPlayer->GetCid());
	if(HoLangCountryPrefersZh(Country))
		return HO_LANG_ZH;
	return HO_LANG_EN;
}

const char *HoLangCode(int Lang)
{
	switch(Lang)
	{
	case HO_LANG_ZH: return "zh";
	case HO_LANG_EN: return "en";
	case HO_LANG_AUTO:
	default: return "auto";
	}
}

const char *HoLangDisplayName(int Lang)
{
	switch(Lang)
	{
	case HO_LANG_ZH: return "Chinese (zh)";
	case HO_LANG_EN: return "English (en)";
	case HO_LANG_AUTO:
	default: return "Auto (auto)";
	}
}

int HoLangParse(const char *pName)
{
	if(!pName || !pName[0])
		return -1;

	if(str_comp_nocase(pName, "auto") == 0 || str_comp_nocase(pName, "default") == 0)
		return HO_LANG_AUTO;
	if(str_comp_nocase(pName, "en") == 0 || str_comp_nocase(pName, "eng") == 0 || str_comp_nocase(pName, "english") == 0)
		return HO_LANG_EN;
	if(str_comp_nocase(pName, "zh") == 0 || str_comp_nocase(pName, "cn") == 0 || str_comp_nocase(pName, "zh-cn") == 0 ||
		str_comp_nocase(pName, "zh_cn") == 0 || str_comp_nocase(pName, "chinese") == 0 ||
		str_comp(pName, "中文") == 0 || str_comp(pName, "简体中文") == 0)
		return HO_LANG_ZH;

	return -1;
}

void ConHoLang(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const int ClientId = pResult->m_ClientId;
	if(ClientId < 0 || ClientId >= MAX_CLIENTS || !pSelf->m_apPlayers[ClientId])
		return;

	CPlayer *pPlayer = pSelf->m_apPlayers[ClientId];

	if(pResult->NumArguments() == 0)
	{
		const int Resolved = HoLangResolve(pSelf, pPlayer);
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf),
			"Language: %s (resolved: %s). Available: auto, en, zh. Usage: /lang [auto|en|zh]",
			HoLangCode(pPlayer->m_HoLang), HoLangCode(Resolved));
		pSelf->SendChatTarget(ClientId, aBuf);
		pSelf->SendChatTarget(ClientId, "语言: auto=按国旗自动, en=English, zh=中文. 用法: /lang [auto|en|zh]");
		return;
	}

	const int Parsed = HoLangParse(pResult->GetString(0));
	if(Parsed < 0)
	{
		pSelf->SendChatTarget(ClientId, "Unknown language. Available: auto, en, zh");
		pSelf->SendChatTarget(ClientId, "未知语言。可用: auto, en, zh");
		return;
	}

	pPlayer->m_HoLang = Parsed;
	const int Resolved = HoLangResolve(pSelf, pPlayer);
	char aBuf[128];
	if(Parsed == HO_LANG_ZH || Resolved == HO_LANG_ZH)
		str_format(aBuf, sizeof(aBuf), "语言已切换为 %s（实际显示: %s）", HoLangCode(Parsed), HoLangCode(Resolved));
	else
		str_format(aBuf, sizeof(aBuf), "Language set to %s (resolved: %s)", HoLangCode(Parsed), HoLangCode(Resolved));
	pSelf->SendChatTarget(ClientId, aBuf);
}

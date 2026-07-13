#ifndef GAME_SERVER_HOAM_LANG_H
#define GAME_SERVER_HOAM_LANG_H

#include <engine/console.h>

class CGameContext;
class CPlayer;

// Per-player language for hoam messages (death chat, etc.).
// AUTO uses the client's country flag when available.
enum
{
	HO_LANG_AUTO = 0,
	HO_LANG_EN = 1,
	HO_LANG_ZH = 2,
};

void HoLangInitPlayer(CPlayer *pPlayer);
// Resolved language: never AUTO (maps AUTO → en/zh by country).
int HoLangResolve(CGameContext *pGameServer, const CPlayer *pPlayer);
const char *HoLangCode(int Lang); // "auto" / "en" / "zh"
const char *HoLangDisplayName(int Lang);
// Parse "auto", "en", "english", "zh", "cn", "中文", ...
// Returns HO_LANG_* or -1 if unknown.
int HoLangParse(const char *pName);

// Chat: /lang  /language
void ConHoLang(IConsole::IResult *pResult, void *pUserData);

#endif

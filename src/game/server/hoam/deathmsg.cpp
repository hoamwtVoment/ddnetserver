#include "deathmsg.h"

#include "lang.h"

#include <base/str.h>

#include <engine/shared/config.h>

#include <generated/protocol.h>

#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

// DDRace shotgun is the freeze laser, not a pellet gun.
static void HoDeathMsgFormat(int Lang, int DeathCause, int Weapon, bool Self, const char *pVictim, const char *pKiller, char *pBuf, int BufSize)
{
	if(DeathCause == HO_DEATH_FALL)
	{
		if(Lang == HO_LANG_ZH)
			str_format(pBuf, BufSize, "%s 从高处摔了下来", pVictim);
		else
			str_format(pBuf, BufSize, "%s fell from a high place", pVictim);
		return;
	}

	if(DeathCause == HO_DEATH_KINETIC)
	{
		if(Lang == HO_LANG_ZH)
			str_format(pBuf, BufSize, "%s 感受到了动能", pVictim);
		else
			str_format(pBuf, BufSize, "%s experienced kinetic energy", pVictim);
		return;
	}

	if(DeathCause == HO_DEATH_BORDER)
	{
		if(Lang == HO_LANG_ZH)
			str_format(pBuf, BufSize, "%s 掉到地图外了", pVictim);
		else
			str_format(pBuf, BufSize, "%s fell out of the map", pVictim);
		return;
	}

	if(DeathCause == HO_DEATH_SPIKE)
	{
		if(Lang == HO_LANG_ZH)
			str_format(pBuf, BufSize, "%s 被刺扎死了", pVictim);
		else
			str_format(pBuf, BufSize, "%s was impaled on spikes", pVictim);
		return;
	}

	if(Self || !pKiller)
	{
		if(Lang == HO_LANG_ZH)
		{
			switch(Weapon)
			{
			case WEAPON_SELF:
				str_format(pBuf, BufSize, "%s 死了", pVictim);
				break;
			case WEAPON_WORLD:
				str_format(pBuf, BufSize, "%s 被世界杀死了", pVictim);
				break;
			default:
				str_format(pBuf, BufSize, "%s 死了", pVictim);
				break;
			}
		}
		else
		{
			switch(Weapon)
			{
			case WEAPON_SELF:
				str_format(pBuf, BufSize, "%s died", pVictim);
				break;
			case WEAPON_WORLD:
				str_format(pBuf, BufSize, "%s was slain by the world", pVictim);
				break;
			default:
				str_format(pBuf, BufSize, "%s died", pVictim);
				break;
			}
		}
		return;
	}

	if(Lang == HO_LANG_ZH)
	{
		switch(Weapon)
		{
		case WEAPON_HAMMER:
			str_format(pBuf, BufSize, "%s 被 %s 击杀了", pVictim, pKiller);
			break;
		case WEAPON_GUN:
			str_format(pBuf, BufSize, "%s 被 %s 开枪打死了", pVictim, pKiller);
			break;
		case WEAPON_SHOTGUN:
			// DDRace: shotgun slot = freeze laser
			str_format(pBuf, BufSize, "%s 被 %s 的冰冻激光杀死了", pVictim, pKiller);
			break;
		case WEAPON_GRENADE:
			str_format(pBuf, BufSize, "%s 被 %s 炸死了", pVictim, pKiller);
			break;
		case WEAPON_LASER:
			str_format(pBuf, BufSize, "%s 被 %s 用激光狙杀了", pVictim, pKiller);
			break;
		case WEAPON_NINJA:
			str_format(pBuf, BufSize, "%s 被 %s 斩杀了", pVictim, pKiller);
			break;
		case WEAPON_WORLD:
			str_format(pBuf, BufSize, "%s 被 %s 杀死了", pVictim, pKiller);
			break;
		default:
			str_format(pBuf, BufSize, "%s 被 %s 击杀了", pVictim, pKiller);
			break;
		}
	}
	else
	{
		switch(Weapon)
		{
		case WEAPON_HAMMER:
			str_format(pBuf, BufSize, "%s was slain by %s", pVictim, pKiller);
			break;
		case WEAPON_GUN:
			str_format(pBuf, BufSize, "%s was shot by %s", pVictim, pKiller);
			break;
		case WEAPON_SHOTGUN:
			// DDRace: shotgun slot = freeze laser
			str_format(pBuf, BufSize, "%s was freeze-lasered by %s", pVictim, pKiller);
			break;
		case WEAPON_GRENADE:
			str_format(pBuf, BufSize, "%s was blown up by %s", pVictim, pKiller);
			break;
		case WEAPON_LASER:
			str_format(pBuf, BufSize, "%s was sniped by %s", pVictim, pKiller);
			break;
		case WEAPON_NINJA:
			str_format(pBuf, BufSize, "%s was sliced by %s", pVictim, pKiller);
			break;
		case WEAPON_WORLD:
			str_format(pBuf, BufSize, "%s was killed by %s", pVictim, pKiller);
			break;
		default:
			str_format(pBuf, BufSize, "%s was slain by %s", pVictim, pKiller);
			break;
		}
	}
}

void HoDeathMsgOnDie(CGameContext *pGameServer, CCharacter *pVictim, int Killer, int Weapon)
{
	if(!pGameServer || !pVictim || !g_Config.m_HoDeathmsg)
		return;

	CPlayer *pVictimPlayer = pVictim->GetPlayer();
	if(!pVictimPlayer)
		return;

	// Silent admin/game transitions (team load, map tools, etc.).
	if(Weapon == WEAPON_GAME)
		return;

	const int VictimId = pVictimPlayer->GetCid();
	const char *pVictimName = pGameServer->Server()->ClientName(VictimId);
	const int DeathCause = pVictim->m_HoDeathCause;
	pVictim->m_HoDeathCause = HO_DEATH_NONE;

	const bool Self = Killer < 0 || Killer == VictimId;
	const char *pKillerName = nullptr;
	if(!Self && Killer >= 0 && Killer < MAX_CLIENTS && pGameServer->m_apPlayers[Killer])
		pKillerName = pGameServer->Server()->ClientName(Killer);

	// Per-client language (same look as system chat: ClientId -1).
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		CPlayer *pViewer = pGameServer->m_apPlayers[i];
		if(!pViewer || pViewer->m_DND)
			continue;

		char aMsg[256];
		HoDeathMsgFormat(HoLangResolve(pGameServer, pViewer), DeathCause, Weapon, Self, pVictimName, pKillerName, aMsg, sizeof(aMsg));
		pGameServer->SendChatTarget(i, aMsg);
	}

	// Demo recording: English system line
	if(g_Config.m_SvDemoChat)
	{
		char aDemo[256];
		HoDeathMsgFormat(HO_LANG_EN, DeathCause, Weapon, Self, pVictimName, pKillerName, aDemo, sizeof(aDemo));
		CNetMsg_Sv_Chat Msg;
		Msg.m_Team = 0;
		Msg.m_ClientId = -1;
		Msg.m_pMessage = aDemo;
		pGameServer->Server()->SendPackMsg(&Msg, MSGFLAG_NOSEND, SERVER_DEMO_CLIENT);
	}
}

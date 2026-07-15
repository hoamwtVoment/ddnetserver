#include "deathmsg.h"

#include "lang.h"

#include <base/str.h>

#include <engine/shared/config.h>

#include <generated/protocol.h>

#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

// Combat tag duration for "doomed to fall" / "whilst trying to escape" lines.
static constexpr int HO_LAST_HIT_SECS = 8;

// Java Edition ZH death messages (MC wiki localization), EN MC vanilla style.
// pAttacker: recent combat tag (hammer etc.); may be null.
static void HoDeathMsgFormat(int Lang, int DeathCause, int Weapon, bool Self, const char *pVictim, const char *pKiller, const char *pAttacker, char *pBuf, int BufSize)
{
	// death.fell.accident.generic / death.fell.assist
	if(DeathCause == HO_DEATH_FALL)
	{
		if(pAttacker)
		{
			if(Lang == HO_LANG_ZH)
				str_format(pBuf, BufSize, "%s 因为 %s 注定要摔死", pVictim, pAttacker);
			else
				str_format(pBuf, BufSize, "%s was doomed to fall by %s", pVictim, pAttacker);
		}
		else if(Lang == HO_LANG_ZH)
			str_format(pBuf, BufSize, "%s 从高处摔了下来", pVictim);
		else
			str_format(pBuf, BufSize, "%s fell from a high place", pVictim);
		return;
	}

	// death.attack.flyIntoWall / .player
	if(DeathCause == HO_DEATH_KINETIC)
	{
		if(pAttacker)
		{
			if(Lang == HO_LANG_ZH)
				str_format(pBuf, BufSize, "%s 在试图逃离 %s 时感受到了动能", pVictim, pAttacker);
			else
				str_format(pBuf, BufSize, "%s experienced kinetic energy whilst trying to escape %s", pVictim, pAttacker);
		}
		else if(Lang == HO_LANG_ZH)
			str_format(pBuf, BufSize, "%s 感受到了动能", pVictim);
		else
			str_format(pBuf, BufSize, "%s experienced kinetic energy", pVictim);
		return;
	}

	// death.attack.outsideBorder / .player (map layer clip)
	if(DeathCause == HO_DEATH_BORDER)
	{
		if(pAttacker)
		{
			if(Lang == HO_LANG_ZH)
				str_format(pBuf, BufSize, "%s 在与 %s 战斗时脱离了这个世界", pVictim, pAttacker);
			else
				str_format(pBuf, BufSize, "%s left the confines of this world whilst fighting %s", pVictim, pAttacker);
		}
		else if(Lang == HO_LANG_ZH)
			str_format(pBuf, BufSize, "%s 脱离了这个世界", pVictim);
		else
			str_format(pBuf, BufSize, "%s left the confines of this world", pVictim);
		return;
	}

	// Spikes (TILE_DEATH); cactus-like combat tag uses 试图逃离
	if(DeathCause == HO_DEATH_SPIKE)
	{
		if(pAttacker)
		{
			if(Lang == HO_LANG_ZH)
				str_format(pBuf, BufSize, "%s 在试图逃离 %s 时被刺扎死了", pVictim, pAttacker);
			else
				str_format(pBuf, BufSize, "%s walked into spikes whilst trying to escape %s", pVictim, pAttacker);
		}
		else if(Lang == HO_LANG_ZH)
			str_format(pBuf, BufSize, "%s 被刺扎死了", pVictim);
		else
			str_format(pBuf, BufSize, "%s was impaled on spikes", pVictim);
		return;
	}

	// death.attack.mace_smash / .item — killer is pKiller
	if(DeathCause == HO_DEATH_MACE)
	{
		const char *pWho = pKiller ? pKiller : pAttacker;
		if(pWho)
		{
			if(Lang == HO_LANG_ZH)
				str_format(pBuf, BufSize, "%s 被 %s 一锤毙命", pVictim, pWho);
			else
				str_format(pBuf, BufSize, "%s was smashed by %s with a mace", pVictim, pWho);
		}
		else if(Lang == HO_LANG_ZH)
			str_format(pBuf, BufSize, "%s 被钉头锤砸死了", pVictim);
		else
			str_format(pBuf, BufSize, "%s was smashed by a mace", pVictim);
		return;
	}

	if(DeathCause == HO_DEATH_GOJO_BLUE)
	{
		if(Lang == HO_LANG_ZH)
			str_format(pBuf, BufSize, "%s 被 %s 的苍吸入了", pVictim, pKiller ? pKiller : "?");
		else
			str_format(pBuf, BufSize, "%s was pulled in by %s's Blue", pVictim, pKiller ? pKiller : "?");
		return;
	}
	if(DeathCause == HO_DEATH_GOJO_RED)
	{
		if(Lang == HO_LANG_ZH)
			str_format(pBuf, BufSize, "%s 被 %s 的赫弹飞了", pVictim, pKiller ? pKiller : "?");
		else
			str_format(pBuf, BufSize, "%s was blasted by %s's Red", pVictim, pKiller ? pKiller : "?");
		return;
	}
	if(DeathCause == HO_DEATH_GOJO_PURPLE)
	{
		if(Lang == HO_LANG_ZH)
			str_format(pBuf, BufSize, "%s 被 %s 的茈抹消了", pVictim, pKiller ? pKiller : "?");
		else
			str_format(pBuf, BufSize, "%s was erased by %s's Purple", pVictim, pKiller ? pKiller : "?");
		return;
	}

	// death.attack.genericKill / .player  ( /kill )
	// death.attack.generic
	if(Self || !pKiller)
	{
		if(Lang == HO_LANG_ZH)
		{
			switch(Weapon)
			{
			case WEAPON_SELF:
				// genericKill
				str_format(pBuf, BufSize, "%s 被杀死了", pVictim);
				break;
			case WEAPON_WORLD:
				str_format(pBuf, BufSize, "%s 死了", pVictim);
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
				str_format(pBuf, BufSize, "%s was killed", pVictim);
				break;
			case WEAPON_WORLD:
				str_format(pBuf, BufSize, "%s died", pVictim);
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

	// Recent combat tag (hammer knockback etc.) for doomed/escape lines.
	const char *pAttackerName = nullptr;
	const int Now = pGameServer->Server()->Tick();
	const int HitWindow = pGameServer->Server()->TickSpeed() * HO_LAST_HIT_SECS;
	if(pVictim->m_HoLastHitCid >= 0 && pVictim->m_HoLastHitCid < MAX_CLIENTS &&
		pVictim->m_HoLastHitCid != VictimId &&
		pVictim->m_HoLastHitTick > 0 &&
		Now - pVictim->m_HoLastHitTick <= HitWindow &&
		pGameServer->m_apPlayers[pVictim->m_HoLastHitCid])
	{
		pAttackerName = pGameServer->Server()->ClientName(pVictim->m_HoLastHitCid);
	}

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		CPlayer *pViewer = pGameServer->m_apPlayers[i];
		if(!pViewer || pViewer->m_DND)
			continue;

		char aMsg[256];
		HoDeathMsgFormat(HoLangResolve(pGameServer, pViewer), DeathCause, Weapon, Self, pVictimName, pKillerName, pAttackerName, aMsg, sizeof(aMsg));
		pGameServer->SendChatTarget(i, aMsg);
	}

	if(g_Config.m_SvDemoChat)
	{
		char aDemo[256];
		HoDeathMsgFormat(HO_LANG_EN, DeathCause, Weapon, Self, pVictimName, pKillerName, pAttackerName, aDemo, sizeof(aDemo));
		CNetMsg_Sv_Chat Msg;
		Msg.m_Team = 0;
		Msg.m_ClientId = -1;
		Msg.m_pMessage = aDemo;
		pGameServer->Server()->SendPackMsg(&Msg, MSGFLAG_NOSEND, SERVER_DEMO_CLIENT);
	}
}

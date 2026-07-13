#include "deathmsg.h"

#include <base/str.h>

#include <engine/shared/config.h>

#include <generated/protocol.h>

#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

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

	char aMsg[256];

	if(DeathCause == HO_DEATH_FALL)
	{
		// MC: "Player fell from a high place"
		str_format(aMsg, sizeof(aMsg), "%s fell from a high place", pVictimName);
	}
	else if(Self)
	{
		switch(Weapon)
		{
		case WEAPON_SELF:
			// MC /kill-ish
			str_format(aMsg, sizeof(aMsg), "%s died", pVictimName);
			break;
		case WEAPON_WORLD:
			// death tiles, world hazards
			str_format(aMsg, sizeof(aMsg), "%s was slain by the world", pVictimName);
			break;
		default:
			str_format(aMsg, sizeof(aMsg), "%s died", pVictimName);
			break;
		}
	}
	else if(pKillerName)
	{
		switch(Weapon)
		{
		case WEAPON_HAMMER:
			str_format(aMsg, sizeof(aMsg), "%s was slain by %s", pVictimName, pKillerName);
			break;
		case WEAPON_GUN:
			str_format(aMsg, sizeof(aMsg), "%s was shot by %s", pVictimName, pKillerName);
			break;
		case WEAPON_SHOTGUN:
			str_format(aMsg, sizeof(aMsg), "%s was shot by %s", pVictimName, pKillerName);
			break;
		case WEAPON_GRENADE:
			str_format(aMsg, sizeof(aMsg), "%s was blown up by %s", pVictimName, pKillerName);
			break;
		case WEAPON_LASER:
			str_format(aMsg, sizeof(aMsg), "%s was sniped by %s", pVictimName, pKillerName);
			break;
		case WEAPON_NINJA:
			str_format(aMsg, sizeof(aMsg), "%s was sliced by %s", pVictimName, pKillerName);
			break;
		case WEAPON_WORLD:
			str_format(aMsg, sizeof(aMsg), "%s was killed by %s", pVictimName, pKillerName);
			break;
		default:
			str_format(aMsg, sizeof(aMsg), "%s was slain by %s", pVictimName, pKillerName);
			break;
		}
	}
	else
	{
		str_format(aMsg, sizeof(aMsg), "%s died", pVictimName);
	}

	// System chat (ClientId -1), same channel as server announcements.
	pGameServer->SendChat(-1, TEAM_ALL, aMsg);
}

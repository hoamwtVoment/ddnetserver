/* (c) Shereef Marzouk. See "licence DDRace.txt" and the readme.txt in the root of the distribution for more information. */
#include "gamecontext.h"

#include <base/io.h>
#include <base/log.h>
#include <base/time.h>

#include <engine/antibot.h>
#include <engine/shared/config.h>

#include <game/mapitems.h>
#include <game/server/entities/character.h>
#include <game/server/gamemodes/ddnet.h>
#include <game/server/player.h>
#include <game/server/save.h>
#include <game/server/teams.h>

void CGameContext::ConGoLeft(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Tiles = pResult->NumArguments() == 1 ? pResult->GetInteger(0) : 1;

	if(!CheckClientId(pResult->m_ClientId))
		return;
	pSelf->MoveCharacter(pResult->m_ClientId, -1 * Tiles, 0);
}

void CGameContext::ConGoRight(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Tiles = pResult->NumArguments() == 1 ? pResult->GetInteger(0) : 1;

	if(!CheckClientId(pResult->m_ClientId))
		return;
	pSelf->MoveCharacter(pResult->m_ClientId, Tiles, 0);
}

void CGameContext::ConGoDown(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Tiles = pResult->NumArguments() == 1 ? pResult->GetInteger(0) : 1;

	if(!CheckClientId(pResult->m_ClientId))
		return;
	pSelf->MoveCharacter(pResult->m_ClientId, 0, Tiles);
}

void CGameContext::ConGoUp(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Tiles = pResult->NumArguments() == 1 ? pResult->GetInteger(0) : 1;

	if(!CheckClientId(pResult->m_ClientId))
		return;
	pSelf->MoveCharacter(pResult->m_ClientId, 0, -1 * Tiles);
}

void CGameContext::ConMove(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;
	pSelf->MoveCharacter(pResult->m_ClientId, pResult->GetInteger(0),
		pResult->GetInteger(1));
}

void CGameContext::ConMoveRaw(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;
	pSelf->MoveCharacter(pResult->m_ClientId, pResult->GetInteger(0),
		pResult->GetInteger(1), true);
}

void CGameContext::MoveCharacter(int ClientId, int X, int Y, bool Raw)
{
	CCharacter *pChr = GetPlayerChar(ClientId);

	if(!pChr)
		return;

	pChr->Move(vec2((Raw ? 1 : 32) * X, (Raw ? 1 : 32) * Y));
	pChr->ResetVelocity();
	pChr->m_DDRaceState = ERaceState::CHEATED;
}

void CGameContext::ConKillPlayer(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(pResult->m_ClientId != IConsole::CLIENT_ID_UNSPECIFIED && !CheckClientId(pResult->m_ClientId))
		return;
	int Victim = pResult->GetVictim();

	if(pSelf->m_apPlayers[Victim])
	{
		pSelf->m_apPlayers[Victim]->KillCharacter(WEAPON_GAME);
		char aBuf[512];
		if(pResult->NumArguments() == 2)
			str_format(aBuf, sizeof(aBuf), "%s was killed by authorized player (%s)",
				pSelf->Server()->ClientName(Victim),
				pResult->GetString(1));
		else
			str_format(aBuf, sizeof(aBuf), "%s was killed by authorized player",
				pSelf->Server()->ClientName(Victim));
		pSelf->SendChat(-1, TEAM_ALL, aBuf);
	}
}

void CGameContext::ConNinja(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->ModifyWeapons(pResult, pUserData, WEAPON_NINJA, false);
}

void CGameContext::ConUnNinja(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->ModifyWeapons(pResult, pUserData, WEAPON_NINJA, true);
}

void CGameContext::ConEndlessHook(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;
	CCharacter *pChr = pSelf->GetPlayerChar(pResult->m_ClientId);
	if(pChr)
	{
		pChr->SetEndlessHook(true);
	}
}

void CGameContext::ConUnEndlessHook(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;
	CCharacter *pChr = pSelf->GetPlayerChar(pResult->m_ClientId);
	if(pChr)
	{
		pChr->SetEndlessHook(false);
	}
}

void CGameContext::ConSuper(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;
	CCharacter *pChr = pSelf->GetPlayerChar(pResult->m_ClientId);
	if(pChr && !pChr->IsSuper())
	{
		pChr->SetSuper(true);
		pChr->Unfreeze();
	}
}

void CGameContext::ConUnSuper(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;
	CCharacter *pChr = pSelf->GetPlayerChar(pResult->m_ClientId);
	if(pChr && pChr->IsSuper())
	{
		pChr->SetSuper(false);
	}
}

void CGameContext::ConToggleInvincible(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;
	CCharacter *pChr = pSelf->GetPlayerChar(pResult->m_ClientId);
	if(pChr)
		pChr->SetInvincible(pResult->NumArguments() == 0 ? !pChr->Core()->m_Invincible : pResult->GetInteger(0));
}

void CGameContext::ConSolo(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;
	CCharacter *pChr = pSelf->GetPlayerChar(pResult->m_ClientId);
	if(pChr)
		pChr->SetSolo(true);
}

void CGameContext::ConUnSolo(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;
	CCharacter *pChr = pSelf->GetPlayerChar(pResult->m_ClientId);
	if(pChr)
		pChr->SetSolo(false);
}

void CGameContext::ConFreeze(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;
	CCharacter *pChr = pSelf->GetPlayerChar(pResult->m_ClientId);
	if(pChr)
		pChr->Freeze();
}

void CGameContext::ConUnfreeze(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;
	CCharacter *pChr = pSelf->GetPlayerChar(pResult->m_ClientId);
	if(pChr)
		pChr->Unfreeze();
}

void CGameContext::ConDeep(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;
	CCharacter *pChr = pSelf->GetPlayerChar(pResult->m_ClientId);
	if(pChr)
		pChr->SetDeepFrozen(true);
}

void CGameContext::ConUnDeep(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;
	CCharacter *pChr = pSelf->GetPlayerChar(pResult->m_ClientId);
	if(pChr)
	{
		pChr->SetDeepFrozen(false);
		pChr->Unfreeze();
	}
}

void CGameContext::ConLiveFreeze(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;
	CCharacter *pChr = pSelf->GetPlayerChar(pResult->m_ClientId);
	if(pChr)
		pChr->SetLiveFrozen(true);
}

void CGameContext::ConUnLiveFreeze(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;
	CCharacter *pChr = pSelf->GetPlayerChar(pResult->m_ClientId);
	if(pChr)
		pChr->SetLiveFrozen(false);
}

void CGameContext::ConShotgun(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->ModifyWeapons(pResult, pUserData, WEAPON_SHOTGUN, false);
}

void CGameContext::ConGrenade(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->ModifyWeapons(pResult, pUserData, WEAPON_GRENADE, false);
}

void CGameContext::ConLaser(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->ModifyWeapons(pResult, pUserData, WEAPON_LASER, false);
}

void CGameContext::ConJetpack(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	CCharacter *pChr = pSelf->GetPlayerChar(pResult->m_ClientId);
	if(pChr)
		pChr->SetJetpack(true);
}

void CGameContext::ConEndlessJump(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	CCharacter *pChr = pSelf->GetPlayerChar(pResult->m_ClientId);
	if(pChr)
		pChr->SetEndlessJump(true);
}

void CGameContext::ConSetJumps(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	CCharacter *pChr = pSelf->GetPlayerChar(pResult->m_ClientId);
	if(pChr)
		pChr->SetJumps(pResult->GetInteger(0));
}

void CGameContext::ConWeapons(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->ModifyWeapons(pResult, pUserData, -1, false);
}

void CGameContext::ConUnShotgun(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->ModifyWeapons(pResult, pUserData, WEAPON_SHOTGUN, true);
}

void CGameContext::ConUnGrenade(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->ModifyWeapons(pResult, pUserData, WEAPON_GRENADE, true);
}

void CGameContext::ConUnLaser(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->ModifyWeapons(pResult, pUserData, WEAPON_LASER, true);
}

void CGameContext::ConUnJetpack(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	CCharacter *pChr = pSelf->GetPlayerChar(pResult->m_ClientId);
	if(pChr)
		pChr->SetJetpack(false);
}

void CGameContext::ConUnEndlessJump(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	CCharacter *pChr = pSelf->GetPlayerChar(pResult->m_ClientId);
	if(pChr)
		pChr->SetEndlessJump(false);
}

void CGameContext::ConSetSwitch(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	CCharacter *pChr = pSelf->GetPlayerChar(pResult->m_ClientId);
	if(!pChr)
	{
		log_info("chatresp", "You can't set switch while you are dead/a spectator.");
		return;
	}
	const int Team = pChr->Team();
	const int Switch = pResult->GetInteger(0);
	if(!in_range(Switch, (int)pSelf->Switchers().size() - 1))
	{
		log_info("chatresp", "Invalid switch ID");
		return;
	}
	const bool State = pResult->NumArguments() == 1 ? !pSelf->Switchers()[Switch].m_aStatus[Team] : pResult->GetInteger(1) != 0;
	const int EndTick = pResult->NumArguments() == 3 ? pSelf->Server()->Tick() + 1 + pResult->GetInteger(2) * pSelf->Server()->TickSpeed() : 0;
	pSelf->Switchers()[Switch].m_aStatus[Team] = State;
	pSelf->Switchers()[Switch].m_aEndTick[Team] = EndTick;
	if(State)
		pSelf->Switchers()[Switch].m_aType[Team] = EndTick ? TILE_SWITCHTIMEDOPEN : TILE_SWITCHOPEN;
	else
		pSelf->Switchers()[Switch].m_aType[Team] = EndTick ? TILE_SWITCHTIMEDCLOSE : TILE_SWITCHCLOSE;
}

void CGameContext::ConUnWeapons(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->ModifyWeapons(pResult, pUserData, -1, true);
}

void CGameContext::ConAddWeapon(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->ModifyWeapons(pResult, pUserData, pResult->GetInteger(0), false);
}

void CGameContext::ConRemoveWeapon(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->ModifyWeapons(pResult, pUserData, pResult->GetInteger(0), true);
}

void CGameContext::ModifyWeapons(IConsole::IResult *pResult, void *pUserData,
	int Weapon, bool Remove)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	CCharacter *pChr = GetPlayerChar(pResult->m_ClientId);
	if(!pChr)
		return;

	if(std::clamp(Weapon, -1, NUM_WEAPONS - 1) != Weapon)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "info",
			"invalid weapon id");
		return;
	}

	if(Weapon == -1)
	{
		pChr->GiveWeapon(WEAPON_SHOTGUN, Remove);
		pChr->GiveWeapon(WEAPON_GRENADE, Remove);
		pChr->GiveWeapon(WEAPON_LASER, Remove);
	}
	else
	{
		pChr->GiveWeapon(Weapon, Remove);
	}

	pChr->m_DDRaceState = ERaceState::CHEATED;
}

void CGameContext::Teleport(CCharacter *pChr, vec2 Pos)
{
	pChr->SetPosition(Pos);
	pChr->m_Pos = Pos;
	pChr->m_PrevPos = Pos;
	pChr->m_DDRaceState = ERaceState::CHEATED;
}

void CGameContext::ConToTeleporter(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	unsigned int TeleTo = pResult->GetInteger(0);

	if(!pSelf->Collision()->TeleOuts(TeleTo - 1).empty())
	{
		CCharacter *pChr = pSelf->GetPlayerChar(pResult->m_ClientId);
		if(pChr)
		{
			int TeleOut = pSelf->m_World.m_Core.RandomOr0(pSelf->Collision()->TeleOuts(TeleTo - 1).size());
			pSelf->Teleport(pChr, pSelf->Collision()->TeleOuts(TeleTo - 1)[TeleOut]);
		}
	}
}

void CGameContext::ConToCheckTeleporter(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	unsigned int TeleTo = pResult->GetInteger(0);

	if(!pSelf->Collision()->TeleCheckOuts(TeleTo - 1).empty())
	{
		CCharacter *pChr = pSelf->GetPlayerChar(pResult->m_ClientId);
		if(pChr)
		{
			int TeleOut = pSelf->m_World.m_Core.RandomOr0(pSelf->Collision()->TeleCheckOuts(TeleTo - 1).size());
			pSelf->Teleport(pChr, pSelf->Collision()->TeleCheckOuts(TeleTo - 1)[TeleOut]);
			pChr->m_TeleCheckpoint = TeleTo;
		}
	}
}

void CGameContext::ConTeleport(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const bool FromServerConsole = pResult->m_ClientId == IConsole::CLIENT_ID_UNSPECIFIED;
	if(!FromServerConsole && !CheckClientId(pResult->m_ClientId))
		return;

	if(FromServerConsole && pResult->NumArguments() != 2)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tele", "server console usage: tele [id] [id]");
		return;
	}

	int Tele = pResult->NumArguments() == 2 ? pResult->GetInteger(0) : pResult->m_ClientId;
	int TeleTo = pResult->NumArguments() ? pResult->GetInteger(pResult->NumArguments() - 1) : pResult->m_ClientId;
	int AuthLevel = pSelf->Server()->GetAuthedState(pResult->m_ClientId);

	if(Tele != pResult->m_ClientId && AuthLevel < g_Config.m_SvTeleOthersAuthLevel)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tele", "you aren't allowed to tele others");
		return;
	}

	CCharacter *pChr = pSelf->GetPlayerChar(Tele);
	CPlayer *pPlayer = FromServerConsole ? nullptr : pSelf->m_apPlayers[pResult->m_ClientId];
	CCharacter *pTeleToChr = pSelf->GetPlayerChar(TeleTo);

	if(pChr && pPlayer && pResult->NumArguments() == 0)
	{
		vec2 Pos = pPlayer->m_ViewPos;
		if(!pPlayer->IsPaused() && pChr->IsAlive())
		{
			vec2 Target = vec2(pChr->Core()->m_Input.m_TargetX, pChr->Core()->m_Input.m_TargetY);
			Pos = pPlayer->m_CameraInfo.ConvertTargetToWorld(pChr->GetPos(), Target);
		}
		pSelf->Teleport(pChr, Pos);
		pChr->ResetJumps();
		pChr->Unfreeze();
		pChr->SetVelocity(vec2(0, 0));
	}
	else if(pChr && pTeleToChr)
	{
		vec2 Pos = pTeleToChr->GetPos();
		pSelf->Teleport(pChr, Pos);
		pChr->ResetJumps();
		pChr->Unfreeze();
		pChr->SetVelocity(vec2(0, 0));
	}
}

void CGameContext::ConHoTp(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const bool FromServerConsole = pResult->m_ClientId == IConsole::CLIENT_ID_UNSPECIFIED;
	if(!FromServerConsole && !CheckClientId(pResult->m_ClientId))
		return;

	if(FromServerConsole && pResult->NumArguments() < 3)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "ho_tp", "server console usage: ho_tp [x] [y] [id] [reset]");
		return;
	}

	const int ClientId = pResult->NumArguments() >= 3 ? pResult->GetInteger(2) : pResult->m_ClientId;
	const bool ResetRace = pResult->NumArguments() >= 4 ? pResult->GetInteger(3) != 0 : false;
	const int AuthLevel = pSelf->Server()->GetAuthedState(pResult->m_ClientId);
	CPlayer *pPlayer = FromServerConsole ? nullptr : pSelf->m_apPlayers[pResult->m_ClientId];

	if(pResult->NumArguments() == 1 || (pResult->NumArguments() == 0 && !pPlayer))
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "ho_tp", "usage: ho_tp [x] [y] [id] [reset]");
		return;
	}

	if(!CheckClientId(ClientId))
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "ho_tp", "invalid client id");
		return;
	}

	if(ClientId != pResult->m_ClientId && AuthLevel < g_Config.m_SvTeleOthersAuthLevel)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "ho_tp", "you aren't allowed to tele others");
		return;
	}

	CCharacter *pChr = pSelf->GetPlayerChar(ClientId);
	if(!pChr)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "ho_tp", "player has no active character");
		return;
	}

	vec2 Pos = pPlayer->m_ViewPos;
	if(pResult->NumArguments() >= 2)
	{
		float TileX = 0.0f;
		float TileY = 0.0f;
		if(!str_tofloat(pResult->GetString(0), &TileX) || !str_tofloat(pResult->GetString(1), &TileY))
		{
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "ho_tp", "invalid tile coordinate");
			return;
		}
		Pos = vec2(TileX * 32.0f, TileY * 32.0f);
	}
	if(pResult->NumArguments() == 0 && ClientId == pResult->m_ClientId && !pPlayer->IsPaused() && pChr->IsAlive())
	{
		vec2 Target = vec2(pChr->Core()->m_Input.m_TargetX, pChr->Core()->m_Input.m_TargetY);
		Pos = pPlayer->m_CameraInfo.ConvertTargetToWorld(pChr->GetPos(), Target);
	}

	if(ResetRace)
	{
		pSelf->Teleport(pChr, Pos);
		pChr->ResetJumps();
		pChr->Unfreeze();
		pChr->SetVelocity(vec2(0, 0));
	}
	else
	{
		pChr->SetPosition(Pos);
		pChr->m_Pos = Pos;
		pChr->m_PrevPos = Pos;
	}

	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "teleported client %d to tile %.2f %.2f%s", ClientId, Pos.x / 32.0f, Pos.y / 32.0f, ResetRace ? " and reset race" : "");
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "ho_tp", aBuf);
}

void CGameContext::ConHoSpeed(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const int ClientId = pResult->GetInteger(0);
	const char *pAxis = pResult->GetString(1);

	if(!CheckClientId(ClientId))
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "ho_speed", "invalid client id");
		return;
	}

	CCharacter *pChr = pSelf->GetPlayerChar(ClientId);
	if(!pChr)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "ho_speed", "player has no active character");
		return;
	}

	const bool IsX = str_comp_nocase(pAxis, "x") == 0;
	const bool IsY = str_comp_nocase(pAxis, "y") == 0;
	if(!IsX && !IsY)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "ho_speed", "usage: ho_speed [id] [x|y] [value]");
		return;
	}

	vec2 Vel = pChr->Core()->m_Vel;
	if(pResult->NumArguments() == 2)
	{
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "client %d speed %s = %.2f", ClientId, IsX ? "x" : "y", IsX ? Vel.x : Vel.y);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "ho_speed", aBuf);
		return;
	}

	if(IsX)
		Vel.x = pResult->GetFloat(2);
	else
		Vel.y = pResult->GetFloat(2);

	pChr->SetVelocity(Vel);

	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "client %d speed x=%.2f y=%.2f", ClientId, pChr->Core()->m_Vel.x, pChr->Core()->m_Vel.y);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "ho_speed", aBuf);
}

void CGameContext::ConHoSpeedLimit(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	if(pResult->NumArguments() == 0)
	{
		char aBuf[64];
		str_format(aBuf, sizeof(aBuf), "ho_speedlimit is %d", pSelf->m_World.m_Core.m_HoSpeedLimit ? 1 : 0);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "ho_speedlimit", aBuf);
		return;
	}

	pSelf->m_World.m_Core.m_HoSpeedLimit = pResult->GetInteger(0) != 0;

	char aBuf[64];
	str_format(aBuf, sizeof(aBuf), "ho_speedlimit %d", pSelf->m_World.m_Core.m_HoSpeedLimit ? 1 : 0);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "ho_speedlimit", aBuf);
}

void CGameContext::ConHoRaceTime(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const int ClientId = pResult->GetInteger(0);

	if(!CheckClientId(ClientId))
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "ho_racetime", "invalid client id");
		return;
	}

	CPlayer *pPlayer = pSelf->m_apPlayers[ClientId];
	CCharacter *pChr = pSelf->GetPlayerChar(ClientId);
	if(!pPlayer || !pChr)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "ho_racetime", "player has no active character");
		return;
	}

	if(pResult->NumArguments() == 1)
	{
		if(pChr->m_DDRaceState != ERaceState::STARTED)
		{
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "ho_racetime", "player has not started");
			return;
		}

		const int RaceTicks = pSelf->Server()->Tick() - pChr->m_StartTime;
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "client %d race time is %d ticks", ClientId, RaceTicks);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "ho_racetime", aBuf);
		return;
	}

	int RaceTicks = pResult->GetInteger(1);
	if(RaceTicks < 0)
		RaceTicks = 0;

	CGameTeams &Teams = pSelf->m_pController->Teams();
	const int Team = pChr->Team();
	if(Team > TEAM_FLOCK && Team < TEAM_SUPER && Teams.GetTeamState(Team) < ETeamState::STARTED)
		Teams.ChangeTeamState(Team, ETeamState::STARTED);
	Teams.SetStarted(ClientId, true);
	Teams.SetDDRaceState(pPlayer, ERaceState::STARTED);
	Teams.SetStartTime(pPlayer, pSelf->Server()->Tick() - RaceTicks);

	if(RaceTicks == 0)
	{
		pChr->m_LastTimeCp = -1;
		pChr->m_LastTimeCpBroadcasted = -1;
		for(float &CurrentTimeCp : pChr->m_aCurrentTimeCp)
			CurrentTimeCp = 0.0f;
	}

	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "client %d race time set to %d ticks", ClientId, RaceTicks);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "ho_racetime", aBuf);
}

void CGameContext::ConHoFakeDeath(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const int ClientId = pResult->GetInteger(0);
	const int Killer = pResult->NumArguments() >= 2 ? pResult->GetInteger(1) : ClientId;
	const int Weapon = pResult->NumArguments() >= 3 ? pResult->GetInteger(2) : WEAPON_WORLD;

	if(!CheckClientId(ClientId) || !pSelf->m_apPlayers[ClientId])
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "ho_fakedeath", "invalid client id");
		return;
	}
	if(!CheckClientId(Killer) || !pSelf->m_apPlayers[Killer])
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "ho_fakedeath", "invalid killer id");
		return;
	}

	CNetMsg_Sv_KillMsg Msg;
	Msg.m_Killer = Killer;
	Msg.m_Victim = ClientId;
	Msg.m_Weapon = Weapon;
	Msg.m_ModeSpecial = 0;
	pSelf->Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, -1);

	CCharacter *pChr = pSelf->GetPlayerChar(ClientId);
	if(pChr)
	{
		pSelf->CreateSound(pChr->GetPos(), SOUND_PLAYER_DIE, pChr->TeamMask());
		pSelf->CreateDeath(pChr->GetPos(), ClientId, pChr->TeamMask());
	}

	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "sent fake death victim=%d killer=%d weapon=%d", ClientId, Killer, Weapon);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "ho_fakedeath", aBuf);
}

void CGameContext::ConHoFlyMode(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const int ClientId = pResult->m_ClientId;

	if(!CheckClientId(ClientId) || !pSelf->m_apPlayers[ClientId])
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "ho_flymode", "this command must be executed by a player");
		return;
	}

	CPlayer *pPlayer = pSelf->m_apPlayers[ClientId];
	if(pPlayer->m_HoFlyMode)
	{
		pPlayer->m_HoFlyMode = false;
		pPlayer->m_HoFlyRemainder = vec2(0, 0);
		CCharacter *pChr = pSelf->GetPlayerChar(ClientId);
		if(pChr)
			pChr->ResetVelocity();
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "ho_flymode", "ho_flymode off");
		return;
	}

	float Speed = pResult->NumArguments() > 0 ? pResult->GetFloat(0) : pPlayer->m_HoFlySpeed;
	if(Speed <= 0.0f)
		Speed = 600.0f;

	pPlayer->m_HoFlySpeed = Speed;
	pPlayer->m_HoFlyMode = true;
	pPlayer->m_HoFlyRemainder = vec2(0, 0);

	CCharacter *pChr = pSelf->GetPlayerChar(ClientId);
	if(pChr)
		pChr->ResetVelocity();

	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "ho_flymode on, speed %.2f px/s", pPlayer->m_HoFlySpeed);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "ho_flymode", aBuf);
}

void CGameContext::ConHoMaceHammer(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	if(pResult->NumArguments() == 0)
	{
		int Count = 0;
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			CPlayer *pPlayer = pSelf->m_apPlayers[i];
			if(!pPlayer || !pPlayer->m_HoMaceHammer)
				continue;
			char aBuf[128];
			str_format(aBuf, sizeof(aBuf), "mace: %d '%s'", i, pSelf->Server()->ClientName(i));
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "ho_macehammer", aBuf);
			Count++;
		}
		if(Count == 0)
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "ho_macehammer", "no players have mace hammer");
		else
		{
			char aBuf[64];
			str_format(aBuf, sizeof(aBuf), "total: %d", Count);
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "ho_macehammer", aBuf);
		}
		return;
	}

	const int ClientId = pResult->GetInteger(0);
	if(ClientId < 0 || ClientId >= MAX_CLIENTS || !pSelf->m_apPlayers[ClientId])
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "ho_macehammer", "invalid client id");
		return;
	}

	CPlayer *pPlayer = pSelf->m_apPlayers[ClientId];
	pPlayer->m_HoMaceHammer = !pPlayer->m_HoMaceHammer;
	// Ownership toggle: enable selects mace mode; disable falls back to vanilla.
	if(pPlayer->m_HoMaceHammer)
		pPlayer->m_aHoWeaponMode[WEAPON_HAMMER] = 1; // HO_WPNMODE_HAMMER_MACE
	else
		pPlayer->m_aHoWeaponMode[WEAPON_HAMMER] = 0;

	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "mace hammer %s for %d '%s' (F3 while holding hammer; lost on death)",
		pPlayer->m_HoMaceHammer ? "OWNED" : "REMOVED",
		ClientId, pSelf->Server()->ClientName(ClientId));
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "ho_macehammer", aBuf);
}

void CGameContext::ConHoNinjaController(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const bool FromServerConsole = pResult->m_ClientId == IConsole::CLIENT_ID_UNSPECIFIED;
	if(FromServerConsole && pResult->NumArguments() == 0)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "ho_ninjacontroller", "server console usage: ho_ninjacontroller [id]");
		return;
	}

	const int ClientId = pResult->NumArguments() > 0 ? pResult->GetInteger(0) : pResult->m_ClientId;
	if(!CheckClientId(ClientId) || !pSelf->m_apPlayers[ClientId])
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "ho_ninjacontroller", "invalid client id");
		return;
	}

	CCharacter *pChr = pSelf->GetPlayerChar(ClientId);
	if(!pChr)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "ho_ninjacontroller", "player has no active character");
		return;
	}

	if(pSelf->m_aHoNinjaController[ClientId])
	{
		pSelf->DisableHoNinjaController(ClientId, true);
		pSelf->m_apPlayers[ClientId]->m_aHoWeaponMode[WEAPON_NINJA] = 0; // vanilla ninja
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "ho_ninjacontroller disabled for client %d", ClientId);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "ho_ninjacontroller", aBuf);
		pSelf->SendChatTarget(ClientId, "ho_ninjacontroller disabled (F3 ninja select: vanilla only)");
		return;
	}

	pSelf->m_aHoNinjaController[ClientId] = true;
	pSelf->m_aHoNinjaControllerHadNinja[ClientId] = pChr->GetWeaponGot(WEAPON_NINJA);
	pSelf->m_aHoNinjaControllerOldWeapon[ClientId] = pChr->GetActiveWeapon();
	pSelf->m_aHoNinjaControllerTarget[ClientId] = -1;
	pChr->SetWeaponGot(WEAPON_NINJA, true);
	pChr->SetWeaponAmmo(WEAPON_NINJA, -1);
	pChr->SetNinjaActivationTick(pSelf->Server()->Tick());
	pChr->SetNinjaCurrentMoveTime(0);
	pChr->SetWeapon(WEAPON_NINJA);
	// Default active mode = controller; F3 can switch to vanilla ninja dash.
	pSelf->m_apPlayers[ClientId]->m_aHoWeaponMode[WEAPON_NINJA] = 1; // HO_WPNMODE_NINJA_CONTROLLER

	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "ho_ninjacontroller enabled for client %d", ClientId);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "ho_ninjacontroller", aBuf);
	pSelf->SendChatTarget(ClientId, "ho_ninjacontroller owned: hold ninja, F3 pick Controller or Ninja; fire grabs at cursor");
}

void CGameContext::ConKill(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;
	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];

	if(!pPlayer || (pPlayer->m_LastKill && pPlayer->m_LastKill + pSelf->Server()->TickSpeed() * g_Config.m_SvKillDelay > pSelf->Server()->Tick()))
		return;

	pPlayer->m_LastKill = pSelf->Server()->Tick();
	pPlayer->KillCharacter(WEAPON_SELF);
}

void CGameContext::ConForcePause(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->GetVictim();
	int Seconds = 0;
	if(pResult->NumArguments() > 1)
		Seconds = std::clamp(pResult->GetInteger(1), 0, 360);

	CPlayer *pPlayer = pSelf->m_apPlayers[Victim];
	if(!pPlayer)
		return;

	pPlayer->ForcePause(Seconds);
}

void CGameContext::ConModerate(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;

	bool HadModerator = pSelf->PlayerModerating();

	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	pPlayer->m_Moderating = !pPlayer->m_Moderating;

	if(!HadModerator && pPlayer->m_Moderating)
		pSelf->SendChat(-1, TEAM_ALL, "Server kick/spec votes will now be actively moderated.", 0);

	if(!pSelf->PlayerModerating())
		pSelf->SendChat(-1, TEAM_ALL, "Server kick/spec votes are no longer actively moderated.", 0);

	if(pPlayer->m_Moderating)
		pSelf->SendChatTarget(pResult->m_ClientId, "Active moderator mode enabled for you.");
	else
		pSelf->SendChatTarget(pResult->m_ClientId, "Active moderator mode disabled for you.");
}

void CGameContext::ConSetDDRTeam(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	auto *pController = pSelf->m_pController;

	if(g_Config.m_SvTeam == SV_TEAM_FORBIDDEN || g_Config.m_SvTeam == SV_TEAM_FORCED_SOLO)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "join",
			"Teams are disabled");
		return;
	}

	const int Target = pResult->GetVictim();
	CPlayer *pPlayer = pSelf->m_apPlayers[Target];
	if(!pPlayer)
		return;

	const int Team = pResult->GetInteger(1);
	if(!pController->Teams().IsValidTeamNumber(Team))
		return;

	CCharacter *pChr = pSelf->GetPlayerChar(Target);

	if((pSelf->GetDDRaceTeam(Target) && pController->Teams().GetDDRaceState(pPlayer) == ERaceState::STARTED) || (pChr && pController->Teams().IsPractice(pChr->Team())))
		pPlayer->KillCharacter(WEAPON_GAME);

	pController->Teams().SetForceCharacterTeam(Target, Team);
	pController->Teams().SetTeamLock(Team, true);
}

void CGameContext::ConUninvite(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	auto *pController = pSelf->m_pController;

	const int Target = pResult->GetVictim();
	if(!pSelf->m_apPlayers[Target])
		return;

	pController->Teams().SetClientInvited(pResult->GetInteger(1), Target, false);
}

void CGameContext::ConVoteNo(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	pSelf->ForceVote(false);
}

void CGameContext::ConDrySave(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;
	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	if(!pPlayer || !pSelf->Server()->IsRconAuthedAdmin(pResult->m_ClientId))
		return;

	CSaveTeam SavedTeam;
	int Team = pSelf->GetDDRaceTeam(pResult->m_ClientId);
	ESaveResult Result = SavedTeam.Save(pSelf, Team, true);
	if(CSaveTeam::HandleSaveError(Result, pResult->m_ClientId, pSelf))
		return;

	char aTimestamp[32];
	str_timestamp(aTimestamp, sizeof(aTimestamp));
	char aBuf[64];
	str_format(aBuf, sizeof(aBuf), "%s_%s_%s.save", pSelf->Map()->BaseName(), aTimestamp, pSelf->Server()->GetAuthName(pResult->m_ClientId));
	IOHANDLE File = pSelf->Storage()->OpenFile(aBuf, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(!File)
		return;

	int Len = str_length(SavedTeam.GetString());
	io_write(File, SavedTeam.GetString(), Len);
	io_close(File);
}

void CGameContext::ConReloadCensorlist(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->ReadCensorList();
}

void CGameContext::ConDumpAntibot(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->Antibot()->ConsoleCommand("dump");
}

void CGameContext::ConAntibot(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->Antibot()->ConsoleCommand(pResult->GetString(0));
}

void CGameContext::ConDumpLog(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int LimitSecs = MAX_LOG_SECONDS;
	if(pResult->NumArguments() > 0)
		LimitSecs = pResult->GetInteger(0);

	if(LimitSecs < 0)
		return;

	int Iterator = pSelf->m_LatestLog;
	for(int i = 0; i < MAX_LOGS; i++)
	{
		CLog *pEntry = &pSelf->m_aLogs[Iterator];
		Iterator = (Iterator + 1) % MAX_LOGS;

		if(!pEntry->m_Timestamp)
			continue;

		int Seconds = (time_get() - pEntry->m_Timestamp) / time_freq();
		if(Seconds > LimitSecs)
			continue;

		char aBuf[sizeof(pEntry->m_aDescription) + 128];
		if(pEntry->m_FromServer)
			str_format(aBuf, sizeof(aBuf), "%s, %d seconds ago", pEntry->m_aDescription, Seconds);
		else
			str_format(aBuf, sizeof(aBuf), "%s, %d seconds ago < addr=<{%s}> name='%s' client=%d",
				pEntry->m_aDescription, Seconds, pEntry->m_aClientAddrStr, pEntry->m_aClientName, pEntry->m_ClientVersion);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "log", aBuf);
	}
}

void CGameContext::LogEvent(const char *Description, int ClientId)
{
	CLog *pNewEntry = &m_aLogs[m_LatestLog];
	m_LatestLog = (m_LatestLog + 1) % MAX_LOGS;

	pNewEntry->m_Timestamp = time_get();
	str_copy(pNewEntry->m_aDescription, Description);
	pNewEntry->m_FromServer = ClientId < 0;
	if(!pNewEntry->m_FromServer)
	{
		pNewEntry->m_ClientVersion = Server()->GetClientVersion(ClientId);
		str_copy(pNewEntry->m_aClientAddrStr, Server()->ClientAddrString(ClientId, false));
		str_copy(pNewEntry->m_aClientName, Server()->ClientName(ClientId));
	}
}

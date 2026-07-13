#include "weaponselect.h"

#include "hp.h"

#include <base/math.h>
#include <base/str.h>
#include <base/vmath.h>

#include <engine/shared/config.h>
#include <engine/shared/protocol.h>

#include <generated/protocol.h>

#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

#include <algorithm>
#include <cmath>

namespace
{
	constexpr float OPTION_RADIUS = 96.0f; // distance from owner
	constexpr float SELECT_AIM_DOT = 0.85f; // how close aim must be to pick
	constexpr float SELECT_MAX_DIST = 140.0f;

	struct SOptionDef
	{
		int m_ModeId;
		int m_PickupType;
		int m_PickupSubtype;
		const char *m_pName;
		const char *m_pDesc;
	};

	// Hammer: always vanilla; mace if owned via ho_macehammer.
	static int BuildHammerOptions(const CPlayer *pPlayer, SOptionDef *pOut, int Max)
	{
		int N = 0;
		if(N < Max)
		{
			pOut[N++] = {HO_WPNMODE_VANILLA, POWERUP_WEAPON, WEAPON_HAMMER, "Hammer", "Vanilla hammer"};
		}
		if(pPlayer->m_HoMaceHammer && N < Max)
		{
			pOut[N++] = {HO_WPNMODE_HAMMER_MACE, POWERUP_NINJA, 0, "Mace", "Minecraft mace smash (fall damage)"};
		}
		return N;
	}

	static int BuildGunOptions(const CPlayer *pPlayer, SOptionDef *pOut, int Max)
	{
		int N = 0;
		if(N < Max)
			pOut[N++] = {HO_WPNMODE_VANILLA, POWERUP_WEAPON, WEAPON_GUN, "Gun", "Vanilla gun"};
		(void)pPlayer;
		return N;
	}

	static int BuildShotgunOptions(const CPlayer *pPlayer, SOptionDef *pOut, int Max)
	{
		int N = 0;
		if(N < Max)
			pOut[N++] = {HO_WPNMODE_VANILLA, POWERUP_WEAPON, WEAPON_SHOTGUN, "Shotgun", "Vanilla shotgun / freeze laser"};
		(void)pPlayer;
		return N;
	}

	static int BuildGrenadeOptions(const CPlayer *pPlayer, SOptionDef *pOut, int Max)
	{
		int N = 0;
		if(N < Max)
			pOut[N++] = {HO_WPNMODE_VANILLA, POWERUP_WEAPON, WEAPON_GRENADE, "Grenade", "Vanilla grenade"};
		(void)pPlayer;
		return N;
	}

	static int BuildLaserOptions(const CPlayer *pPlayer, SOptionDef *pOut, int Max)
	{
		int N = 0;
		if(N < Max)
			pOut[N++] = {HO_WPNMODE_VANILLA, POWERUP_WEAPON, WEAPON_LASER, "Laser", "Vanilla laser / rifle"};
		// Portal modes always available as laser variants (chat /portal also sets these).
		if(N < Max)
			pOut[N++] = {HO_WPNMODE_LASER_PORTAL1, POWERUP_ARMOR, 0, "Portal 1", "Place portal entrance (laser)"};
		if(N < Max)
			pOut[N++] = {HO_WPNMODE_LASER_PORTAL2, POWERUP_HEALTH, 0, "Portal 2", "Place portal exit (laser)"};
		(void)pPlayer;
		return N;
	}

	static int BuildNinjaOptions(CGameContext *pGameServer, const CPlayer *pPlayer, SOptionDef *pOut, int Max)
	{
		int N = 0;
		if(N < Max)
			pOut[N++] = {HO_WPNMODE_VANILLA, POWERUP_NINJA, 0, "Ninja", "Vanilla ninja dash"};
		// Owned via rcon ho_ninjacontroller
		if(pGameServer && pPlayer && pGameServer->IsHoNinjaController(pPlayer->GetCid()) && N < Max)
		{
			pOut[N++] = {HO_WPNMODE_NINJA_CONTROLLER, POWERUP_ARMOR, 0, "Controller", "Grab players with ninja (aim + fire)"};
		}
		return N;
	}

	static int BuildOptionsForSlot(CGameContext *pGameServer, const CPlayer *pPlayer, int WeaponSlot, SOptionDef *pOut, int Max)
	{
		switch(WeaponSlot)
		{
		case WEAPON_HAMMER: return BuildHammerOptions(pPlayer, pOut, Max);
		case WEAPON_GUN: return BuildGunOptions(pPlayer, pOut, Max);
		case WEAPON_SHOTGUN: return BuildShotgunOptions(pPlayer, pOut, Max);
		case WEAPON_GRENADE: return BuildGrenadeOptions(pPlayer, pOut, Max);
		case WEAPON_LASER: return BuildLaserOptions(pPlayer, pOut, Max);
		case WEAPON_NINJA: return BuildNinjaOptions(pGameServer, pPlayer, pOut, Max);
		default: return 0;
		}
	}

	static void DestroyOptions(CGameContext *pGameServer, CPlayer *pPlayer)
	{
		for(int i = 0; i < HO_WEAPONSELECT_MAX_OPTIONS; i++)
		{
			if(pPlayer->m_apHoWeaponSelectOptions[i])
			{
				pPlayer->m_apHoWeaponSelectOptions[i]->Reset();
				pPlayer->m_apHoWeaponSelectOptions[i] = nullptr;
			}
		}
		pPlayer->m_HoWeaponSelectCount = 0;
		pPlayer->m_HoWeaponSelectHover = -1;
	}

	static int ActiveModeSlotIndex(const CPlayer *pPlayer, int WeaponSlot)
	{
		if(WeaponSlot < 0 || WeaponSlot >= NUM_WEAPONS)
			return 0;
		return pPlayer->m_aHoWeaponMode[WeaponSlot];
	}
}

CHoWeaponSelectOption::CHoWeaponSelectOption(CGameWorld *pGameWorld, int OwnerId, int OptionIndex, int WeaponSlot, int ModeId, int PickupType, int PickupSubtype) :
	CEntity(pGameWorld, CGameWorld::ENTTYPE_PICKUP, true, vec2(0, 0), 14),
	m_OwnerId(OwnerId),
	m_OptionIndex(OptionIndex),
	m_WeaponSlot(WeaponSlot),
	m_ModeId(ModeId),
	m_PickupType(PickupType),
	m_PickupSubtype(PickupSubtype)
{
	GameWorld()->InsertEntity(this);
}

void CHoWeaponSelectOption::Reset()
{
	m_MarkedForDestroy = true;
}

void CHoWeaponSelectOption::Tick()
{
	// Position is updated by HoWeaponSelectTickPlayer.
}

void CHoWeaponSelectOption::Snap(int SnappingClient)
{
	if(NetworkClipped(SnappingClient) || !GetId().has_value())
		return;

	// Only owner (and demos) see the menu icons.
	if(SnappingClient != SERVER_DEMO_CLIENT && SnappingClient != m_OwnerId)
	{
		// Spectators of owner
		if(SnappingClient >= 0 && SnappingClient < MAX_CLIENTS)
		{
			CPlayer *pSnap = GameServer()->m_apPlayers[SnappingClient];
			if(!pSnap || pSnap->SpectatorId() != m_OwnerId)
				return;
		}
		else
			return;
	}

	const int SnappingClientVersion = GameServer()->GetClientVersion(SnappingClient);
	const bool Sixup = Server()->IsSixup(SnappingClient);
	GameServer()->SnapPickup(CSnapContext(SnappingClientVersion, Sixup, SnappingClient), GetId().value(), m_Pos, m_PickupType, m_PickupSubtype, -1, 0);
}

bool HoWeaponSelectIsOpen(const CPlayer *pPlayer)
{
	return pPlayer && pPlayer->m_HoWeaponSelectOpen;
}

int HoWeaponSelectActiveMode(const CPlayer *pPlayer, int WeaponSlot)
{
	if(!pPlayer || WeaponSlot < 0 || WeaponSlot >= CPlayer::HO_WEAPON_MODE_SLOTS)
		return HO_WPNMODE_VANILLA;
	return pPlayer->m_aHoWeaponMode[WeaponSlot];
}

void HoWeaponSelectSetActiveMode(CPlayer *pPlayer, int WeaponSlot, int ModeId)
{
	if(!pPlayer || WeaponSlot < 0 || WeaponSlot >= CPlayer::HO_WEAPON_MODE_SLOTS)
		return;
	pPlayer->m_aHoWeaponMode[WeaponSlot] = ModeId;

	// Sync legacy flags used by existing features.
	if(WeaponSlot == WEAPON_HAMMER)
	{
		// m_HoMaceHammer = ownership (rcon); active use is mode.
		// Firing checks ownership + mode in macehammer.cpp
	}
	if(WeaponSlot == WEAPON_LASER)
	{
		// 0 classic, 1 portal1, 2 portal2
		pPlayer->m_HoLaserMode = ModeId;
		if(ModeId == HO_WPNMODE_LASER_PORTAL1 || ModeId == HO_WPNMODE_LASER_PORTAL2)
			pPlayer->m_HoLastPortalMode = ModeId;
	}
	// Ninja controller: ownership stays on gamecontext; mode picks fire behavior.
}

int HoWeaponSelectBuildOptions(const CPlayer *pPlayer, int WeaponSlot, int *pModeIds, int *pPickupTypes, int *pPickupSubtypes, const char **ppNames, const char **ppDescs, int MaxOptions)
{
	SOptionDef aDefs[HO_WEAPONSELECT_MAX_OPTIONS];
	// No GameServer here — ninja controller list needs context; callers should use open menu path.
	const int N = BuildOptionsForSlot(nullptr, pPlayer, WeaponSlot, aDefs, HO_WEAPONSELECT_MAX_OPTIONS);
	const int Count = std::min(N, MaxOptions);
	for(int i = 0; i < Count; i++)
	{
		if(pModeIds)
			pModeIds[i] = aDefs[i].m_ModeId;
		if(pPickupTypes)
			pPickupTypes[i] = aDefs[i].m_PickupType;
		if(pPickupSubtypes)
			pPickupSubtypes[i] = aDefs[i].m_PickupSubtype;
		if(ppNames)
			ppNames[i] = aDefs[i].m_pName;
		if(ppDescs)
			ppDescs[i] = aDefs[i].m_pDesc;
	}
	return Count;
}

void HoWeaponSelectClose(CGameContext *pGameServer, CPlayer *pPlayer, bool Silent)
{
	if(!pPlayer)
		return;
	DestroyOptions(pGameServer, pPlayer);
	pPlayer->m_HoWeaponSelectOpen = false;
	pPlayer->m_HoWeaponSelectSlot = -1;
	pPlayer->m_HoWeaponSelectHover = -1;
	if(!Silent && pGameServer)
		pGameServer->SendChatTarget(pPlayer->GetCid(), "Weapon select: closed");
}

static void HoWeaponSelectOpenMenu(CGameContext *pGameServer, CPlayer *pPlayer)
{
	CCharacter *pChr = pPlayer->GetCharacter();
	if(!pChr || !pChr->IsAlive())
	{
		pGameServer->SendChatTarget(pPlayer->GetCid(), "Weapon select: need an alive tee");
		return;
	}

	const int Slot = pChr->GetActiveWeapon();
	if(Slot < 0 || Slot >= CPlayer::HO_WEAPON_MODE_SLOTS)
	{
		pGameServer->SendChatTarget(pPlayer->GetCid(), "Weapon select: invalid weapon");
		return;
	}

	SOptionDef aDefs[HO_WEAPONSELECT_MAX_OPTIONS];
	const int Count = BuildOptionsForSlot(pGameServer, pPlayer, Slot, aDefs, HO_WEAPONSELECT_MAX_OPTIONS);
	if(Count <= 0)
	{
		pGameServer->SendChatTarget(pPlayer->GetCid(), "Weapon select: no options for this weapon");
		return;
	}

	// If only vanilla exists, still allow open (one option) so player can confirm.
	DestroyOptions(pGameServer, pPlayer);
	pPlayer->m_HoWeaponSelectOpen = true;
	pPlayer->m_HoWeaponSelectSlot = Slot;
	pPlayer->m_HoWeaponSelectCount = Count;
	pPlayer->m_HoWeaponSelectHover = -1;

	for(int i = 0; i < Count; i++)
	{
		pPlayer->m_apHoWeaponSelectOptions[i] = new CHoWeaponSelectOption(
			&pGameServer->m_World, pPlayer->GetCid(), i, Slot,
			aDefs[i].m_ModeId, aDefs[i].m_PickupType, aDefs[i].m_PickupSubtype);
	}

	pGameServer->SendBroadcast("Use crosshair + left click to select weapon mode (F3 to close)", pPlayer->GetCid(), true);
	pGameServer->SendChatTarget(pPlayer->GetCid(), "Weapon select: open — aim and fire to choose, F3 to close");
}

bool HoWeaponSelectToggle(CGameContext *pGameServer, CPlayer *pPlayer)
{
	if(!pGameServer || !pPlayer)
		return false;

	if(pPlayer->m_HoWeaponSelectOpen)
	{
		HoWeaponSelectClose(pGameServer, pPlayer, false);
		return true;
	}

	HoWeaponSelectOpenMenu(pGameServer, pPlayer);
	return true;
}

static int HoWeaponSelectFindAimed(CPlayer *pPlayer, CCharacter *pChr)
{
	if(!pPlayer || !pChr)
		return -1;

	vec2 Dir = vec2(pChr->Core()->m_Input.m_TargetX, pChr->Core()->m_Input.m_TargetY);
	if(Dir == vec2(0, 0))
		Dir = vec2(0, -1);
	else
		Dir = normalize(Dir);

	int Best = -1;
	float BestDot = SELECT_AIM_DOT;
	for(int i = 0; i < pPlayer->m_HoWeaponSelectCount; i++)
	{
		CHoWeaponSelectOption *pOpt = pPlayer->m_apHoWeaponSelectOptions[i];
		if(!pOpt)
			continue;
		vec2 ToOpt = pOpt->GetPos() - pChr->GetPos();
		const float Dist = length(ToOpt);
		if(Dist < 1.0f || Dist > SELECT_MAX_DIST)
			continue;
		const float Dot = dot(Dir, ToOpt / Dist);
		if(Dot > BestDot)
		{
			BestDot = Dot;
			Best = i;
		}
	}
	return Best;
}

void HoWeaponSelectTickPlayer(CGameContext *pGameServer, CPlayer *pPlayer)
{
	if(!pGameServer || !pPlayer || !pPlayer->m_HoWeaponSelectOpen)
		return;

	CCharacter *pChr = pPlayer->GetCharacter();
	if(!pChr || !pChr->IsAlive())
	{
		HoWeaponSelectClose(pGameServer, pPlayer, true);
		return;
	}

	// Keep menu bound to the weapon that was open; if player switched weapon, refresh menu.
	const int Active = pChr->GetActiveWeapon();
	if(Active != pPlayer->m_HoWeaponSelectSlot)
	{
		// Re-open for new weapon
		HoWeaponSelectClose(pGameServer, pPlayer, true);
		HoWeaponSelectOpenMenu(pGameServer, pPlayer);
		return;
	}

	const int Count = pPlayer->m_HoWeaponSelectCount;
	if(Count <= 0)
	{
		HoWeaponSelectClose(pGameServer, pPlayer, true);
		return;
	}

	// Arrange options in an arc above the tee (like the reference UI).
	const vec2 Base = pChr->GetPos();
	const float StartAngle = -pi * 0.85f; // upper-left
	const float EndAngle = -pi * 0.15f; // upper-right
	for(int i = 0; i < Count; i++)
	{
		CHoWeaponSelectOption *pOpt = pPlayer->m_apHoWeaponSelectOptions[i];
		if(!pOpt)
			continue;
		const float T = Count == 1 ? 0.5f : (float)i / (float)(Count - 1);
		const float Ang = StartAngle + (EndAngle - StartAngle) * T;
		// Y down in DDNet
		const vec2 Off(std::cos(Ang) * OPTION_RADIUS, std::sin(Ang) * OPTION_RADIUS);
		pOpt->SetWorldPos(Base + Off);
	}

	// Hover info
	const int Hover = HoWeaponSelectFindAimed(pPlayer, pChr);
	if(Hover != pPlayer->m_HoWeaponSelectHover)
	{
		pPlayer->m_HoWeaponSelectHover = Hover;
		if(Hover >= 0)
		{
			SOptionDef aDefs[HO_WEAPONSELECT_MAX_OPTIONS];
			const int N = BuildOptionsForSlot(pGameServer, pPlayer, pPlayer->m_HoWeaponSelectSlot, aDefs, HO_WEAPONSELECT_MAX_OPTIONS);
			if(Hover < N)
			{
				char aBuf[192];
				const bool Selected = ActiveModeSlotIndex(pPlayer, pPlayer->m_HoWeaponSelectSlot) == aDefs[Hover].m_ModeId;
				str_format(aBuf, sizeof(aBuf), "%s%s\n%s\n[Fire] select  [F3] close",
					aDefs[Hover].m_pName, Selected ? " (current)" : "", aDefs[Hover].m_pDesc);
				// Bypass HP rate limit for hover UX
				pPlayer->m_HoHpLastBroadcastTick = 0;
				pGameServer->SendBroadcast(aBuf, pPlayer->GetCid(), true);
			}
		}
		else
		{
			pGameServer->SendBroadcast("Weapon select: aim at an icon, left click to choose (F3 close)", pPlayer->GetCid(), true);
		}
	}
}

bool HoWeaponSelectOnFire(CCharacter *pChr)
{
	if(!pChr || !pChr->IsAlive())
		return false;

	CPlayer *pPlayer = pChr->GetPlayer();
	CGameContext *pGameServer = pChr->GameServer();
	if(!pPlayer || !pGameServer || !pPlayer->m_HoWeaponSelectOpen)
		return false;

	const int Hover = HoWeaponSelectFindAimed(pPlayer, pChr);
	if(Hover < 0)
	{
		pGameServer->SendChatTarget(pPlayer->GetCid(), "Weapon select: aim closer to an option");
		return true; // consume fire so you don't shoot while menu open
	}

	CHoWeaponSelectOption *pOpt = pPlayer->m_apHoWeaponSelectOptions[Hover];
	if(!pOpt)
		return true;

	const int Slot = pOpt->WeaponSlot();
	const int Mode = pOpt->ModeId();
	HoWeaponSelectSetActiveMode(pPlayer, Slot, Mode);

	SOptionDef aDefs[HO_WEAPONSELECT_MAX_OPTIONS];
	const int N = BuildOptionsForSlot(pGameServer, pPlayer, Slot, aDefs, HO_WEAPONSELECT_MAX_OPTIONS);
	const char *pName = "mode";
	for(int i = 0; i < N; i++)
	{
		if(aDefs[i].m_ModeId == Mode)
		{
			pName = aDefs[i].m_pName;
			break;
		}
	}

	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "Weapon select: %s", pName);
	pGameServer->SendChatTarget(pPlayer->GetCid(), aBuf);
	pGameServer->SendBroadcast(aBuf, pPlayer->GetCid(), true);

	HoWeaponSelectClose(pGameServer, pPlayer, true);
	return true;
}

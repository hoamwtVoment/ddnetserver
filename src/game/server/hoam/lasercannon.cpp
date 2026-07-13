#include "lasercannon.h"

#include "hp.h"
#include "weaponselect.h"

#include <base/math.h>
#include <base/vmath.h>

#include <engine/shared/config.h>
#include <engine/shared/protocol.h>

#include <generated/protocol.h>

#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

#include <cmath>

namespace
{
	constexpr float TILE = 32.0f;

	float CannonLength()
	{
		const int Tiles = std::max(1, g_Config.m_HoLasercannonLength);
		return (float)Tiles * TILE;
	}

	int CannonDamage()
	{
		return std::max(0, g_Config.m_HoLasercannonDamage);
	}

	int LaserModeOf(const CPlayer *pPlayer)
	{
		if(!pPlayer)
			return HO_WPNMODE_VANILLA;
		const int Mode = HoWeaponSelectActiveMode(pPlayer, WEAPON_LASER);
		if(Mode == HO_WPNMODE_LASER_CANNON || Mode == HO_WPNMODE_LASER_CANNON_LOCK)
			return Mode;
		// Legacy field may still hold mode when set via older paths.
		if(pPlayer->m_HoLaserMode == HO_WPNMODE_LASER_CANNON || pPlayer->m_HoLaserMode == HO_WPNMODE_LASER_CANNON_LOCK)
			return pPlayer->m_HoLaserMode;
		return HO_WPNMODE_VANILLA;
	}
}

bool HoLaserCannonModeActive(const CPlayer *pPlayer)
{
	const int Mode = LaserModeOf(pPlayer);
	return Mode == HO_WPNMODE_LASER_CANNON || Mode == HO_WPNMODE_LASER_CANNON_LOCK;
}

bool HoLaserCannonLockModeActive(const CPlayer *pPlayer)
{
	return LaserModeOf(pPlayer) == HO_WPNMODE_LASER_CANNON_LOCK;
}

CHoLaserCannonBeam::CHoLaserCannonBeam(CGameWorld *pGameWorld, int Owner) :
	CEntity(pGameWorld, CGameWorld::ENTTYPE_LASER, true),
	m_Owner(Owner),
	m_From(0.0f, 0.0f),
	m_To(0.0f, 0.0f),
	m_EvalTick(0),
	m_LastDamageTick(0)
{
	GameWorld()->InsertEntity(this);
	// Stable StartTick base; Snap clamps to recent ticks so the beam stays continuous
	// without re-triggering rifle fire FX every server tick.
	m_EvalTick = Server()->Tick();
	if(Owner >= 0 && Owner < MAX_CLIENTS && GameServer()->m_apPlayers[Owner])
		GameServer()->m_apPlayers[Owner]->m_pHoLaserCannon = this;

	UpdateBeam();

	// One fire sound on start only (no hold-fire spam).
	if(CCharacter *pOwner = GameServer()->GetPlayerChar(Owner))
		GameServer()->CreateSound(pOwner->GetPos(), SOUND_LASER_FIRE, pOwner->TeamMask());
}

void CHoLaserCannonBeam::Reset()
{
	if(m_Owner >= 0 && m_Owner < MAX_CLIENTS && GameServer()->m_apPlayers[m_Owner] &&
		GameServer()->m_apPlayers[m_Owner]->m_pHoLaserCannon == this)
	{
		GameServer()->m_apPlayers[m_Owner]->m_pHoLaserCannon = nullptr;
	}
	m_MarkedForDestroy = true;
}

bool CHoLaserCannonBeam::OwnerStillFiring()
{
	CCharacter *pOwner = GameServer()->GetPlayerChar(m_Owner);
	if(!pOwner || !pOwner->IsAlive())
		return false;
	const CPlayer *pPlayer = pOwner->GetPlayer();
	if(!pPlayer || !HoLaserCannonModeActive(pPlayer))
		return false;
	if(pOwner->GetActiveWeapon() != WEAPON_LASER)
		return false;
	if(pOwner->m_FreezeTime > 0)
		return false;
	const CCharacterCore *pCore = pOwner->Core();
	if(!pCore->m_aWeapons[WEAPON_LASER].m_Got)
		return false;
	if(!(pCore->m_Input.m_Fire & 1))
		return false;
	if(pCore->m_aWeapons[WEAPON_LASER].m_Ammo == 0)
		return false;
	return true;
}

CCharacter *CHoLaserCannonBeam::FindLockTarget(CCharacter *pOwner, vec2 AimDir, float MaxRange)
{
	if(!pOwner || MaxRange <= 0.0f)
		return nullptr;

	CCharacter *pBest = nullptr;
	// Higher cosine = closer to crosshair direction. Front hemisphere only (cos > 0).
	float BestCos = 0.0f;
	float BestDist = 0.0f;

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(i == m_Owner)
			continue;
		CCharacter *pChr = GameServer()->GetPlayerChar(i);
		if(!pChr || !pChr->IsAlive())
			continue;
		if(!pOwner->CanCollide(i))
			continue;
		if(pOwner->LaserHitDisabled())
			continue;

		const vec2 Delta = pChr->GetPos() - pOwner->GetPos();
		const float Dist = length(Delta);
		if(Dist < 1.0f || Dist > MaxRange)
			continue;

		const float Cos = dot(Delta / Dist, AimDir);
		if(Cos <= 0.0f)
			continue;

		// Prefer higher aim alignment; on near-equal angle pick the nearer tee.
		if(!pBest || Cos > BestCos + 0.0001f || (Cos >= BestCos - 0.0001f && Dist < BestDist))
		{
			BestCos = Cos;
			BestDist = Dist;
			pBest = pChr;
		}
	}

	return pBest;
}

void CHoLaserCannonBeam::UpdateBeam()
{
	CCharacter *pOwner = GameServer()->GetPlayerChar(m_Owner);
	if(!pOwner)
		return;

	CPlayer *pPlayer = pOwner->GetPlayer();
	vec2 Target((float)pOwner->Core()->m_Input.m_TargetX, (float)pOwner->Core()->m_Input.m_TargetY);
	if(Target == vec2(0.0f, 0.0f))
		Target = vec2(0.0f, -1.0f);
	vec2 Dir = normalize(Target);

	const float Energy = CannonLength();

	// Auto-lock: steer beam toward the living player nearest the crosshair direction.
	// Path blocking is still handled below (first character along the ray wins).
	if(pPlayer && HoLaserCannonLockModeActive(pPlayer))
	{
		if(CCharacter *pLock = FindLockTarget(pOwner, Dir, Energy))
		{
			const vec2 ToLock = pLock->GetPos() - pOwner->GetPos();
			if(length(ToLock) > 0.001f)
				Dir = normalize(ToLock);
		}
	}

	const vec2 Start = pOwner->GetPos() + Dir * pOwner->GetProximityRadius() * 0.75f;
	vec2 End = Start + Dir * Energy;

	// Wall stop — no bounce / reflection.
	GameServer()->Collision()->IntersectLine(Start, End, nullptr, &End);

	// First character along beam (blocker or locked target).
	vec2 HitAt;
	CCharacter *pHit = GameWorld()->IntersectCharacter(Start, End, 0.0f, HitAt, pOwner, m_Owner);
	if(pHit && pHit->IsAlive() && pOwner->CanCollide(pHit->GetPlayer()->GetCid()) && !pOwner->LaserHitDisabled())
	{
		End = HitAt;
		const int Dmg = CannonDamage();
		if(Dmg > 0)
		{
			const int Delay = std::max(1, g_Config.m_HoLasercannonDamageDelay);
			if(Server()->Tick() - m_LastDamageTick >= Delay)
			{
				m_LastDamageTick = Server()->Tick();
				const vec2 Force = Dir * 1.5f;
				pHit->TakeDamage(Force, 0, m_Owner, WEAPON_LASER);
				HoHpTakeDamage(pHit, Dmg, m_Owner, WEAPON_LASER, true, 0);
			}
		}
	}

	m_From = Start;
	m_To = End;
	m_Pos = End;
	// Do not refresh m_EvalTick every tick — that makes rifle lasers look like they re-fire (stutter + FX).
}

void CHoLaserCannonBeam::Tick()
{
	if(!OwnerStillFiring())
	{
		Reset();
		return;
	}
	UpdateBeam();
}

void CHoLaserCannonBeam::Snap(int SnappingClient)
{
	if((NetworkClipped(SnappingClient) && NetworkClipped(SnappingClient, m_From)) || !GetId().has_value())
		return;

	CCharacter *pOwner = GameServer()->GetPlayerChar(m_Owner);
	if(SnappingClient != SERVER_DEMO_CLIENT && SnappingClient != m_Owner && pOwner)
	{
		if(SnappingClient >= 0 && SnappingClient < MAX_CLIENTS)
		{
			CPlayer *pSnapPl = GameServer()->m_apPlayers[SnappingClient];
			const bool SpecOwner = pSnapPl && pSnapPl->SpectatorId() == m_Owner;
			if(!SpecOwner && !pOwner->CanCollide(SnappingClient))
				return;
		}
	}

	// Same continuous-laser StartTick clamp as doors/draggers/lights: keep the segment
	// visible and stable without resetting fire age every tick.
	int StartTick = m_EvalTick;
	if(StartTick < Server()->Tick() - 4)
		StartTick = Server()->Tick() - 4;
	else if(StartTick > Server()->Tick())
		StartTick = Server()->Tick();

	const int Version = GameServer()->GetClientVersion(SnappingClient);
	// NO_PREDICT: do not put this long beam into the client's rifle prediction world
	// (would fight with laser_reach-sized predicted shots).
	GameServer()->SnapLaserObject(
		CSnapContext(Version, Server()->IsSixup(SnappingClient), SnappingClient),
		GetId().value(), m_To, m_From, StartTick, m_Owner, LASERTYPE_RIFLE, 0, -1, LASERFLAG_NO_PREDICT);
}

void CHoLaserCannonBeam::SwapClients(int Client1, int Client2)
{
	m_Owner = m_Owner == Client1 ? Client2 : (m_Owner == Client2 ? Client1 : m_Owner);
}

void HoLaserCannonTickCharacter(CCharacter *pChr)
{
	if(!pChr || !pChr->IsAlive())
		return;

	CPlayer *pPlayer = pChr->GetPlayer();
	CGameContext *pGameServer = pChr->GameServer();
	if(!pPlayer || !pGameServer)
		return;

	CHoLaserCannonBeam *pBeam = pPlayer->m_pHoLaserCannon;

	const bool Want =
		HoLaserCannonModeActive(pPlayer) &&
		pChr->GetActiveWeapon() == WEAPON_LASER &&
		pChr->m_FreezeTime <= 0 &&
		(pChr->Core()->m_Input.m_Fire & 1) &&
		pChr->GetWeaponGot(WEAPON_LASER) &&
		pChr->GetWeaponAmmo(WEAPON_LASER) != 0;

	if(!Want)
	{
		if(pBeam)
			pBeam->Reset();
		return;
	}

	if(!pBeam)
		new CHoLaserCannonBeam(&pGameServer->m_World, pPlayer->GetCid());
}

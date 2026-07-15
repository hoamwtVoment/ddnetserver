#include "gojo.h"

#include "deathmsg.h"
#include "hp.h"
#include "weaponselect.h"

#include <base/math.h>
#include <base/vmath.h>

#include <engine/shared/config.h>

#include <generated/protocol.h>

#include <game/gamecore.h>
#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

#include <cmath>

namespace
{
	constexpr float PI = 3.14159265358979323846f;

	float ClampChargeFrac(int ChargeTicks)
	{
		const int MinT = std::max(1, g_Config.m_HoGojoChargeTicksMin);
		const int MaxT = std::max(MinT, g_Config.m_HoGojoChargeTicksMax);
		if(ChargeTicks <= 0)
			return 0.0f;
		if(ChargeTicks < MinT)
			return (float)ChargeTicks / (float)MaxT;
		const float t = (float)(ChargeTicks - MinT) / (float)(MaxT - MinT);
		const float Base = (float)MinT / (float)MaxT;
		return std::clamp(Base + t * (1.0f - Base), 0.0f, 1.0f);
	}

	float LerpF(float A, float B, float T)
	{
		return A + (B - A) * std::clamp(T, 0.0f, 1.0f);
	}

	int StableLaserTick(int SpawnTick, IServer *pServer)
	{
		int StartTick = SpawnTick;
		if(StartTick < pServer->Tick() - 4)
			StartTick = pServer->Tick() - 4;
		else if(StartTick > pServer->Tick())
			StartTick = pServer->Tick();
		return StartTick;
	}

	void SnapOrb(CGameContext *pGameServer, int SnappingClient, int SnapId, vec2 Pos, float Radius, int Owner, int SpawnTick, int LaserType)
	{
		if(SnapId < 0)
			return;
		const float Ang = (pGameServer->Server()->Tick() % 50) * (2.0f * PI / 50.0f);
		const vec2 A = Pos + vec2(std::cos(Ang), std::sin(Ang)) * Radius;
		const vec2 B = Pos - vec2(std::cos(Ang), std::sin(Ang)) * Radius;
		const int Version = pGameServer->GetClientVersion(SnappingClient);
		pGameServer->SnapLaserObject(
			CSnapContext(Version, pGameServer->Server()->IsSixup(SnappingClient), SnappingClient),
			SnapId, B, A, StableLaserTick(SpawnTick, pGameServer->Server()), Owner, LaserType, 0, -1, LASERFLAG_NO_PREDICT);
	}

	bool CanHit(CCharacter *pOwner, CCharacter *pTarget)
	{
		if(!pOwner || !pTarget || !pTarget->IsAlive())
			return false;
		if(pTarget == pOwner)
			return false;
		if(!pOwner->CanCollide(pTarget->GetPlayer()->GetCid()))
			return false;
		// Unlimited Void blocks external techniques (pull/push/damage).
		if(HoGojoVoidBlocksExternal(pTarget, pOwner->GetPlayer()->GetCid()))
			return false;
		return true;
	}

	void PullOrPush(CCharacter *pTarget, vec2 Center, float Strength, bool Attract)
	{
		vec2 Diff = pTarget->m_Pos - Center;
		const float Len = length(Diff);
		if(Len < 0.001f)
			Diff = vec2(0.0f, -1.0f);
		else
			Diff = normalize(Diff);
		if(Attract)
		{
			pTarget->AddVelocity(-Diff * Strength);
			return;
		}

		// 赫: hard knockback that can shove grounded tees (pop slightly up to break friction).
		vec2 Kick = Diff * Strength;
		if(pTarget->IsGrounded())
		{
			Kick.x *= 1.55f;
			if(Kick.y > -4.0f)
				Kick.y = -4.0f - Strength * 0.08f;
		}
		pTarget->AddVelocity(Kick);
	}

	void DamageInRadius(CCharacter *pOwner, vec2 Center, float Radius, int Damage, float ForceStr, bool Attract, int Weapon, int DeathCause)
	{
		if(!pOwner)
			return;
		CGameContext *pGameServer = pOwner->GameServer();
		CEntity *apEnts[MAX_CLIENTS];
		const int Num = pGameServer->m_World.FindEntities(Center, Radius, apEnts, MAX_CLIENTS, CGameWorld::ENTTYPE_CHARACTER);
		for(int i = 0; i < Num; i++)
		{
			auto *pChr = static_cast<CCharacter *>(apEnts[i]);
			if(!CanHit(pOwner, pChr))
				continue;
			if(ForceStr > 0.0f)
				PullOrPush(pChr, Center, ForceStr, Attract);
			if(Damage > 0)
			{
				const int Killer = pOwner->GetPlayer()->GetCid();
				pChr->TakeDamage(vec2(0, 0), 0, Killer, Weapon);
				HoHpTakeDamage(pChr, Damage, Killer, Weapon, true, DeathCause);
			}
		}
	}

	// First intersection of segment From->To with sphere (C,R). Returns t in [0,1] or -1.
	float SegmentSphereT(vec2 From, vec2 To, vec2 C, float R)
	{
		const vec2 D = To - From;
		const float A = dot(D, D);
		if(A < 0.0001f)
			return distance(From, C) <= R ? 0.0f : -1.0f;
		const vec2 F = From - C;
		const float B = 2.0f * dot(F, D);
		const float Cc = dot(F, F) - R * R;
		const float Disc = B * B - 4.0f * A * Cc;
		if(Disc < 0.0f)
			return -1.0f;
		const float S = std::sqrt(Disc);
		const float T0 = (-B - S) / (2.0f * A);
		const float T1 = (-B + S) / (2.0f * A);
		// Prefer entry point when coming from outside.
		if(T0 >= 0.0f && T0 <= 1.0f)
			return T0;
		if(distance(From, C) <= R)
		{
			// Already inside: stop at surface along travel (exit) so tip sits on the sphere.
			if(T1 >= 0.0f && T1 <= 1.0f)
				return T1;
			return 0.0f;
		}
		if(T1 >= 0.0f && T1 <= 1.0f)
			return T1;
		return -1.0f;
	}
}

bool HoGojoOwned(const CPlayer *pPlayer)
{
	return pPlayer && pPlayer->m_HoGojo;
}

bool HoGojoVoidActive(const CPlayer *pPlayer)
{
	return HoGojoOwned(pPlayer) && pPlayer->m_HoGojoUnlimitedVoid;
}

float HoGojoVoidRadius()
{
	return (float)std::max(32, g_Config.m_HoGojoVoidRadius);
}

bool HoGojoVoidClipSegment(CGameContext *pGameServer, vec2 From, vec2 To, int ShooterCid, vec2 *pHit)
{
	if(!pGameServer)
		return false;

	const float Radius = HoGojoVoidRadius();
	float BestT = 2.0f;
	vec2 BestPos = To;
	bool Any = false;

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(i == ShooterCid)
			continue; // own weapons pass through own void
		CPlayer *pPl = pGameServer->m_apPlayers[i];
		if(!HoGojoVoidActive(pPl))
			continue;
		CCharacter *pChr = pPl->GetCharacter();
		if(!pChr || !pChr->IsAlive())
			continue;

		const float T = SegmentSphereT(From, To, pChr->GetPos(), Radius);
		if(T < 0.0f || T > 1.0f)
			continue;
		if(T < BestT)
		{
			BestT = T;
			BestPos = From + (To - From) * T;
			vec2 Out = BestPos - pChr->GetPos();
			if(length(Out) > 0.001f)
				BestPos = pChr->GetPos() + normalize(Out) * Radius;
			else
				BestPos = pChr->GetPos() + vec2(0.0f, -Radius);
			Any = true;
		}
	}

	if(Any && pHit)
		*pHit = BestPos;
	return Any;
}

bool HoGojoVoidBlocksExternal(const CCharacter *pVictim, int FromCid)
{
	if(!pVictim || !pVictim->IsAlive())
		return false;
	const CPlayer *pPlayer = pVictim->GetPlayer();
	if(!HoGojoVoidActive(pPlayer))
		return false;
	// Own weapons still work (self is not "external").
	if(FromCid == pPlayer->GetCid())
		return false;
	return true;
}

int HoGojoShotgunMode(const CPlayer *pPlayer)
{
	if(!HoGojoOwned(pPlayer))
		return HO_WPNMODE_VANILLA;
	const int Mode = HoWeaponSelectActiveMode(pPlayer, WEAPON_SHOTGUN);
	// 无下限 is a toggle option, never a fireable active mode.
	if(Mode == HO_WPNMODE_SHOTGUN_VOID)
		return HO_WPNMODE_VANILLA;
	return Mode;
}

bool HoGojoTechniqueMode(const CPlayer *pPlayer)
{
	const int Mode = HoGojoShotgunMode(pPlayer);
	return Mode == HO_WPNMODE_SHOTGUN_BLUE || Mode == HO_WPNMODE_SHOTGUN_RED || Mode == HO_WPNMODE_SHOTGUN_PURPLE;
}

// Stick foreign hooks on the void sphere surface (do not attach to the body).
static void HoGojoVoidRedirectHook(CCharacter *pHooker, vec2 VoidCenter, float Radius)
{
	if(!pHooker)
		return;
	vec2 From = pHooker->GetPos();
	vec2 Diff = From - VoidCenter;
	if(length(Diff) < 0.001f)
		Diff = vec2(0.0f, -1.0f);
	else
		Diff = normalize(Diff);
	// Surface point facing the hooker (around the void user, not on their body).
	const vec2 Surface = VoidCenter + Diff * Radius;
	pHooker->SetHookGrabWorld(Surface);
}

static void HoGojoVoidProtectHooks(CCharacter *pVoidChr)
{
	if(!pVoidChr || !HoGojoVoidActive(pVoidChr->GetPlayer()))
		return;

	CGameContext *pGameServer = pVoidChr->GameServer();
	const int VoidId = pVoidChr->GetPlayer()->GetCid();
	const vec2 Center = pVoidChr->GetPos();
	const float Radius = HoGojoVoidRadius();

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(i == VoidId)
			continue;
		CCharacter *pOther = pGameServer->GetPlayerChar(i);
		if(!pOther || !pOther->IsAlive())
			continue;

		const CCharacterCore *pCore = pOther->Core();
		// Already attached to void user → peel off onto sphere.
		if(pCore->HookedPlayer() == VoidId)
		{
			HoGojoVoidRedirectHook(pOther, Center, Radius);
			continue;
		}

		// Flying hook that enters / would enter the sphere.
		if(pCore->m_HookState == HOOK_FLYING)
		{
			const vec2 HookPos = pCore->m_HookPos;
			vec2 Closest;
			if(closest_point_on_line(pOther->GetPos(), HookPos, Center, Closest))
			{
				if(distance(Closest, Center) <= Radius)
					HoGojoVoidRedirectHook(pOther, Center, Radius);
			}
			else if(distance(HookPos, Center) <= Radius)
			{
				HoGojoVoidRedirectHook(pOther, Center, Radius);
			}
		}
		// Ground-grabbed hook tip pushed into sphere while void user walks into it.
		else if(pCore->m_HookState == HOOK_GRABBED && pCore->HookedPlayer() == -1)
		{
			if(distance(pCore->m_HookPos, Center) < Radius - 2.0f)
				HoGojoVoidRedirectHook(pOther, Center, Radius);
		}
	}
}

void HoGojoClearBlue(CPlayer *pPlayer)
{
	if(!pPlayer || !pPlayer->m_pHoGojoBlue)
		return;
	pPlayer->m_pHoGojoBlue->Reset();
	pPlayer->m_pHoGojoBlue = nullptr;
}

void HoGojoClearFusionOrbs(CPlayer *pPlayer)
{
	if(!pPlayer)
		return;
	if(pPlayer->m_pHoGojoFusionBlue)
	{
		pPlayer->m_pHoGojoFusionBlue->Reset();
		pPlayer->m_pHoGojoFusionBlue = nullptr;
	}
	if(pPlayer->m_pHoGojoFusionRed)
	{
		pPlayer->m_pHoGojoFusionRed->Reset();
		pPlayer->m_pHoGojoFusionRed = nullptr;
	}
}

void HoGojoOnDeath(CPlayer *pPlayer)
{
	if(!pPlayer)
		return;
	HoGojoClearBlue(pPlayer);
	HoGojoClearFusionOrbs(pPlayer);
	pPlayer->m_HoGojo = false;
	pPlayer->m_HoGojoUnlimitedVoid = false;
	pPlayer->m_aHoWeaponMode[WEAPON_SHOTGUN] = HO_WPNMODE_VANILLA;
}

// --- 茈 charge fusion orbs (visual) ---

CHoGojoFusionOrb::CHoGojoFusionOrb(CGameWorld *pGameWorld, int Owner, int Style) :
	CEntity(pGameWorld, CGameWorld::ENTTYPE_LASER, true, vec2(0, 0), 8),
	m_Owner(Owner),
	m_Style(Style),
	m_Radius(8.0f),
	m_SpawnTick(0)
{
	m_SpawnTick = Server()->Tick();
	GameWorld()->InsertEntity(this);
	if(Owner >= 0 && Owner < MAX_CLIENTS && GameServer()->m_apPlayers[Owner])
	{
		CPlayer *pPl = GameServer()->m_apPlayers[Owner];
		if(Style == STYLE_BLUE)
		{
			if(pPl->m_pHoGojoFusionBlue)
				pPl->m_pHoGojoFusionBlue->Reset();
			pPl->m_pHoGojoFusionBlue = this;
		}
		else
		{
			if(pPl->m_pHoGojoFusionRed)
				pPl->m_pHoGojoFusionRed->Reset();
			pPl->m_pHoGojoFusionRed = this;
		}
	}
}

void CHoGojoFusionOrb::Reset()
{
	if(m_Owner >= 0 && m_Owner < MAX_CLIENTS && GameServer()->m_apPlayers[m_Owner])
	{
		CPlayer *pPl = GameServer()->m_apPlayers[m_Owner];
		if(m_Style == STYLE_BLUE && pPl->m_pHoGojoFusionBlue == this)
			pPl->m_pHoGojoFusionBlue = nullptr;
		if(m_Style == STYLE_RED && pPl->m_pHoGojoFusionRed == this)
			pPl->m_pHoGojoFusionRed = nullptr;
	}
	m_MarkedForDestroy = true;
}

void CHoGojoFusionOrb::SetVisual(vec2 Pos, float Radius)
{
	m_Pos = Pos;
	m_Radius = std::max(4.0f, Radius);
}

void CHoGojoFusionOrb::Tick()
{
	// Owner-driven; if owner gone, drop.
	CCharacter *pOwner = GameServer()->GetPlayerChar(m_Owner);
	if(!pOwner || !pOwner->IsAlive())
		Reset();
}

void CHoGojoFusionOrb::Snap(int SnappingClient)
{
	if(!GetId().has_value())
		return;

	// Diameter = full 茈 radius so charge orbs match the ball that will launch.
	const int LaserType = m_Style == STYLE_BLUE ? LASERTYPE_SHOTGUN : LASERTYPE_GUN;
	const float Ang = (Server()->Tick() % 40) * (2.0f * PI / 40.0f);
	const vec2 A = m_Pos + vec2(std::cos(Ang), std::sin(Ang)) * m_Radius;
	const vec2 B = m_Pos - vec2(std::cos(Ang), std::sin(Ang)) * m_Radius;
	if(NetworkClipped(SnappingClient) && NetworkClipped(SnappingClient, A) && NetworkClipped(SnappingClient, B))
		return;

	const int Version = GameServer()->GetClientVersion(SnappingClient);
	GameServer()->SnapLaserObject(
		CSnapContext(Version, Server()->IsSixup(SnappingClient), SnappingClient),
		GetId().value(), B, A, StableLaserTick(m_SpawnTick, Server()), m_Owner, LaserType, 0, -1, LASERFLAG_NO_PREDICT);
}

void CHoGojoFusionOrb::SwapClients(int Client1, int Client2)
{
	m_Owner = m_Owner == Client1 ? Client2 : (m_Owner == Client2 ? Client1 : m_Owner);
}

void HoGojoToggleUnlimitedVoid(CGameContext *pGameServer, CPlayer *pPlayer)
{
	if(!pGameServer || !pPlayer || !pPlayer->m_HoGojo)
		return;
	pPlayer->m_HoGojoUnlimitedVoid = !pPlayer->m_HoGojoUnlimitedVoid;
	if(pPlayer->m_HoGojoUnlimitedVoid)
	{
		pGameServer->SendChatTarget(pPlayer->GetCid(), "无下限：展开");
		pGameServer->SendBroadcast("Unlimited Void ON", pPlayer->GetCid(), true);
		if(CCharacter *pChr = pPlayer->GetCharacter())
			pGameServer->CreateSound(pChr->GetPos(), SOUND_NINJA_HIT, pChr->TeamMask());
	}
	else
	{
		pGameServer->SendChatTarget(pPlayer->GetCid(), "无下限：解除");
		pGameServer->SendBroadcast("Unlimited Void OFF", pPlayer->GetCid(), true);
	}
}

// --- Blue (controllable) ---

CHoGojoBlue::CHoGojoBlue(CGameWorld *pGameWorld, int Owner, vec2 Pos, float Radius) :
	CEntity(pGameWorld, CGameWorld::ENTTYPE_LASER, true, Pos, (int)Radius),
	m_Owner(Owner),
	m_Radius(Radius),
	m_Life(std::max(10, g_Config.m_HoGojoBlueLife)),
	m_LastDamageTick(0),
	m_SpawnTick(0),
	m_Controlled(true),
	m_Vel(0.0f, 0.0f)
{
	m_SpawnTick = Server()->Tick();
	GameWorld()->InsertEntity(this);
	if(Owner >= 0 && Owner < MAX_CLIENTS && GameServer()->m_apPlayers[Owner])
	{
		if(GameServer()->m_apPlayers[Owner]->m_pHoGojoBlue)
			GameServer()->m_apPlayers[Owner]->m_pHoGojoBlue->Reset();
		GameServer()->m_apPlayers[Owner]->m_pHoGojoBlue = this;
	}
	if(CCharacter *pOwner = GameServer()->GetPlayerChar(Owner))
		GameServer()->CreateSound(Pos, SOUND_LASER_FIRE, pOwner->TeamMask());
}

void CHoGojoBlue::Reset()
{
	if(m_Owner >= 0 && m_Owner < MAX_CLIENTS && GameServer()->m_apPlayers[m_Owner] &&
		GameServer()->m_apPlayers[m_Owner]->m_pHoGojoBlue == this)
	{
		GameServer()->m_apPlayers[m_Owner]->m_pHoGojoBlue = nullptr;
	}
	m_MarkedForDestroy = true;
}

void CHoGojoBlue::ReleaseControl()
{
	m_Controlled = false;
	// Keep m_Vel from last follow step; free-flight continues that velocity.
}

void CHoGojoBlue::Tick()
{
	CCharacter *pOwner = GameServer()->GetPlayerChar(m_Owner);
	if(!pOwner || !pOwner->IsAlive() || m_Life-- <= 0)
	{
		Reset();
		return;
	}

	if(m_Controlled)
	{
		// Control: move toward owner's cursor world position; track velocity for release.
		const vec2 Target = pOwner->GetPlayer()->m_CameraInfo.ConvertTargetToWorld(
			pOwner->GetPos(), vec2(pOwner->Core()->m_Input.m_TargetX, pOwner->Core()->m_Input.m_TargetY));

		const float Ctrl = std::max(1.0f, (float)g_Config.m_HoGojoBlueControlSpeed);
		const vec2 Prev = m_Pos;
		vec2 Diff = Target - m_Pos;
		const float Dist = length(Diff);
		if(Dist > 1.0f)
		{
			const float Step = std::min(Dist, Ctrl);
			const vec2 Next = m_Pos + normalize(Diff) * Step;
			if(!Collision()->TestBox(Next, vec2(14.0f, 14.0f)))
				m_Pos = Next;
			else if(!Collision()->TestBox(vec2(Next.x, m_Pos.y), vec2(14.0f, 14.0f)))
				m_Pos.x = Next.x;
			else if(!Collision()->TestBox(vec2(m_Pos.x, Next.y), vec2(14.0f, 14.0f)))
				m_Pos.y = Next.y;
		}
		m_Vel = m_Pos - Prev;
	}
	else
	{
		// Free flight at release velocity.
		const vec2 Next = m_Pos + m_Vel;
		vec2 ColPos;
		if(Collision()->IntersectLine(m_Pos, Next, &ColPos, nullptr))
		{
			m_Pos = ColPos;
			m_Vel *= 0.35f;
		}
		else
			m_Pos = Next;
	}

	// Attraction each tick (stronger pull).
	const float Pull = std::max(0.0f, g_Config.m_HoGojoBluePull / 10.0f);
	CEntity *apEnts[MAX_CLIENTS];
	const int Num = GameServer()->m_World.FindEntities(m_Pos, m_Radius, apEnts, MAX_CLIENTS, CGameWorld::ENTTYPE_CHARACTER);
	for(int i = 0; i < Num; i++)
	{
		auto *pChr = static_cast<CCharacter *>(apEnts[i]);
		if(!CanHit(pOwner, pChr))
			continue;
		const float DistT = length(pChr->m_Pos - m_Pos);
		const float Falloff = 1.0f - std::clamp(DistT / m_Radius, 0.0f, 1.0f);
		// Stronger near-center + base pull so grounded tees get dragged.
		const float Str = Pull * (0.55f + 1.15f * Falloff);
		PullOrPush(pChr, m_Pos, Str, true);
		if(pChr->IsGrounded())
			pChr->AddVelocity(vec2(0.0f, -1.2f * Falloff));
	}

	const int DmgDelay = std::max(1, g_Config.m_HoGojoBlueDamageDelay);
	if(Server()->Tick() >= m_LastDamageTick + DmgDelay)
	{
		m_LastDamageTick = Server()->Tick();
		const int Dmg = std::max(0, g_Config.m_HoGojoBlueDamage);
		if(Dmg > 0)
			DamageInRadius(pOwner, m_Pos, m_Radius, Dmg, 0.0f, true, WEAPON_SHOTGUN, HO_DEATH_GOJO_BLUE);
	}

	if(Server()->Tick() % 5 == 0)
		GameServer()->CreateDamageInd(m_Pos, Server()->Tick() * 0.3f, 1, pOwner->TeamMask());
}

void CHoGojoBlue::Snap(int SnappingClient)
{
	if(NetworkClipped(SnappingClient) || !GetId().has_value())
		return;
	SnapOrb(GameServer(), SnappingClient, GetId().value(), m_Pos, m_Radius, m_Owner, m_SpawnTick, LASERTYPE_SHOTGUN);
}

void CHoGojoBlue::SwapClients(int Client1, int Client2)
{
	m_Owner = m_Owner == Client1 ? Client2 : (m_Owner == Client2 ? Client1 : m_Owner);
}

// --- Red / Purple projectile ---

CHoGojoProjectile::CHoGojoProjectile(CGameWorld *pGameWorld, int Owner, vec2 Pos, vec2 Dir, float Radius, int Type) :
	CEntity(pGameWorld, CGameWorld::ENTTYPE_LASER, true, Pos, (int)Radius),
	m_Owner(Owner),
	m_Dir(Dir),
	m_Radius(Radius),
	m_Type(Type),
	m_Life(0),
	m_SpawnTick(0)
{
	if(length(m_Dir) > 0.001f)
		m_Dir = normalize(m_Dir);
	else
		m_Dir = vec2(1, 0);
	m_Life = m_Type == TYPE_PURPLE ? std::max(10, g_Config.m_HoGojoPurpleLife) : std::max(10, g_Config.m_HoGojoRedLife);
	m_SpawnTick = Server()->Tick();
	GameWorld()->InsertEntity(this);
	if(CCharacter *pOwner = GameServer()->GetPlayerChar(Owner))
		GameServer()->CreateSound(Pos, m_Type == TYPE_PURPLE ? SOUND_GRENADE_EXPLODE : SOUND_GRENADE_FIRE, pOwner->TeamMask());
}

void CHoGojoProjectile::Reset()
{
	m_MarkedForDestroy = true;
}

void CHoGojoProjectile::ApplyAoE(bool FinalBurst)
{
	CCharacter *pOwner = GameServer()->GetPlayerChar(m_Owner);
	if(!pOwner)
		return;
	const int DeathCause = m_Type == TYPE_PURPLE ? HO_DEATH_GOJO_PURPLE : HO_DEATH_GOJO_RED;
	const int Dmg = m_Type == TYPE_PURPLE ? std::max(0, g_Config.m_HoGojoPurpleDamage) : std::max(0, g_Config.m_HoGojoRedDamage);
	// Push in tenths → strong impulse (red must shove grounded players).
	float Push = m_Type == TYPE_PURPLE ? (float)g_Config.m_HoGojoPurplePush / 10.0f : (float)g_Config.m_HoGojoRedPush / 10.0f;
	if(m_Type == TYPE_RED)
		Push *= FinalBurst ? 2.4f : 1.1f;
	else
		Push *= FinalBurst ? 1.8f : 0.9f;
	DamageInRadius(pOwner, m_Pos, m_Radius, FinalBurst ? Dmg : std::max(0, Dmg / 3), Push, false, WEAPON_SHOTGUN, DeathCause);
	if(FinalBurst || Server()->Tick() % 4 == 0)
		GameServer()->CreateExplosion(m_Pos, m_Owner, WEAPON_SHOTGUN, true, -1, pOwner->TeamMask());
}

void CHoGojoProjectile::Tick()
{
	CCharacter *pOwner = GameServer()->GetPlayerChar(m_Owner);
	if(!pOwner || !pOwner->IsAlive() || m_Life-- <= 0)
	{
		ApplyAoE(true);
		Reset();
		return;
	}

	const float Speed = m_Type == TYPE_PURPLE ? (float)std::max(1, g_Config.m_HoGojoPurpleSpeed) : (float)std::max(1, g_Config.m_HoGojoRedSpeed);
	const vec2 Next = m_Pos + m_Dir * Speed;
	// 茈 phases through walls; 赫 still collides.
	if(m_Type != TYPE_PURPLE)
	{
		vec2 ColPos;
		if(Collision()->IntersectLine(m_Pos, Next, &ColPos, nullptr))
		{
			m_Pos = ColPos;
			ApplyAoE(true);
			Reset();
			return;
		}
	}
	m_Pos = Next;
	ApplyAoE(false);
}

void CHoGojoProjectile::Snap(int SnappingClient)
{
	if(NetworkClipped(SnappingClient) || !GetId().has_value())
		return;
	const int LaserType = m_Type == TYPE_PURPLE ? LASERTYPE_RIFLE : LASERTYPE_GUN;
	SnapOrb(GameServer(), SnappingClient, GetId().value(), m_Pos, m_Radius, m_Owner, m_SpawnTick, LaserType);
}

void CHoGojoProjectile::SwapClients(int Client1, int Client2)
{
	m_Owner = m_Owner == Client1 ? Client2 : (m_Owner == Client2 ? Client1 : m_Owner);
}

// --- Charge / cast / void ---

void HoGojoApplyChargeTuning(CCharacter *pChr, CTuningParams *pTuning)
{
	if(!pChr || !pTuning)
		return;
	CPlayer *pPlayer = pChr->GetPlayer();
	if(!HoGojoOwned(pPlayer))
		return;

	float SlowFrac = 0.0f;

	// Charge slow (larger charge → slower).
	if(pChr->m_HoGojoChargeTicks > 0)
	{
		const float Frac = ClampChargeFrac(pChr->m_HoGojoChargeTicks);
		const float Floor = std::clamp(g_Config.m_HoGojoChargeSpeedMin, 5, 100) / 100.0f;
		// Frac 0 → 100% speed, Frac 1 → Floor speed.
		const float Remain = 1.0f - (1.0f - Floor) * Frac;
		SlowFrac = std::max(SlowFrac, 1.0f - Remain);
	}

	// Purple merge also pins movement.
	if(pChr->m_HoGojoPurpleMergeLeft > 0)
		SlowFrac = std::max(SlowFrac, 0.75f);

	// Standing in own void: mild self slow optional — skip, only affect others in Tick.

	if(SlowFrac <= 0.0f)
		return;

	const float Remain = std::clamp(1.0f - SlowFrac, 0.05f, 1.0f);
	pTuning->m_GroundControlSpeed = (float)pTuning->m_GroundControlSpeed * Remain;
	pTuning->m_GroundControlAccel = (float)pTuning->m_GroundControlAccel * Remain;
	pTuning->m_AirControlSpeed = (float)pTuning->m_AirControlSpeed * Remain;
	pTuning->m_AirControlAccel = (float)pTuning->m_AirControlAccel * Remain;
}

static void HoGojoTickVoidDomain(CCharacter *pOwner)
{
	CPlayer *pPlayer = pOwner->GetPlayer();
	if(!HoGojoVoidActive(pPlayer))
	{
		// Restore collision if we disabled it for void.
		if(pOwner->m_HoGojoVoidHadCollisionOff)
		{
			pOwner->SetCollisionDisabled(false);
			pOwner->m_HoGojoVoidHadCollisionOff = false;
		}
		return;
	}

	CGameContext *pGameServer = pOwner->GameServer();
	const float Radius = HoGojoVoidRadius();
	const float Damp = std::clamp(g_Config.m_HoGojoVoidDamp, 10, 100) / 100.0f;

	// External body collision / pushes do not affect the void user.
	if(!pOwner->Core()->m_CollisionDisabled)
	{
		pOwner->SetCollisionDisabled(true);
		pOwner->m_HoGojoVoidHadCollisionOff = true;
	}

	// Hooks stop on the sphere around the user (not on the body).
	HoGojoVoidProtectHooks(pOwner);

	// Information overload: quiet velocity damp only (no hammer SFX spam).
	CEntity *apEnts[MAX_CLIENTS];
	const int Num = pGameServer->m_World.FindEntities(pOwner->m_Pos, Radius, apEnts, MAX_CLIENTS, CGameWorld::ENTTYPE_CHARACTER);
	for(int i = 0; i < Num; i++)
	{
		auto *pChr = static_cast<CCharacter *>(apEnts[i]);
		if(!CanHit(pOwner, pChr))
			continue;
		pChr->SetVelocity(pChr->Core()->m_Vel * Damp);
	}
}

static void HoGojoEnsureFusionOrbs(CCharacter *pChr)
{
	CPlayer *pPlayer = pChr->GetPlayer();
	CGameContext *pGameServer = pChr->GameServer();
	if(!pPlayer || !pGameServer)
		return;
	if(!pPlayer->m_pHoGojoFusionBlue)
		new CHoGojoFusionOrb(&pGameServer->m_World, pPlayer->GetCid(), CHoGojoFusionOrb::STYLE_BLUE);
	if(!pPlayer->m_pHoGojoFusionRed)
		new CHoGojoFusionOrb(&pGameServer->m_World, pPlayer->GetCid(), CHoGojoFusionOrb::STYLE_RED);
}

// Same radius formula as the 茈 projectile that will launch (must stay in sync).
static float HoGojoPurpleRadiusFromFrac(float Frac)
{
	const float RMin = (float)std::max(16, g_Config.m_HoGojoPurpleRadiusMin);
	const float RMax = (float)std::max((int)RMin, g_Config.m_HoGojoPurpleRadiusMax);
	return LerpF(RMin, RMax, std::clamp(Frac, 0.0f, 1.0f));
}

// Frac 0→1 while charging: orbs grow (to 茈 size) and slide toward center.
// MergeProgress 0→1 after release: finish slamming together into one ball of that size.
static void HoGojoUpdateFusionOrbs(CCharacter *pChr, float Frac, float MergeProgress)
{
	CPlayer *pPlayer = pChr->GetPlayer();
	if(!pPlayer)
		return;
	HoGojoEnsureFusionOrbs(pChr);

	// Orb radius == eventual 茈 radius at this charge (not a tiny decorative ball).
	const float PurpleR = HoGojoPurpleRadiusFromFrac(Frac);
	const float OrbR = PurpleR;

	// Head higher so large orbs don't bury the tee.
	const vec2 Head = pChr->m_Pos + vec2(0.0f, -40.0f - OrbR * 0.35f);
	// Start far enough that two full-size spheres barely touch when nearly merged.
	const float ChargeIn = Frac * 0.65f;
	const float Spread0 = OrbR * 1.25f + 36.0f;
	const float Spread = Spread0 * (1.0f - ChargeIn) * (1.0f - MergeProgress);

	const vec2 BluePos = Head + vec2(-Spread, 0.0f);
	const vec2 RedPos = Head + vec2(Spread, 0.0f);

	if(pPlayer->m_pHoGojoFusionBlue)
		pPlayer->m_pHoGojoFusionBlue->SetVisual(BluePos, OrbR);
	if(pPlayer->m_pHoGojoFusionRed)
		pPlayer->m_pHoGojoFusionRed->SetVisual(RedPos, OrbR);

	// Soft spark only when nearly merged.
	if(MergeProgress > 0.65f || Frac > 0.92f)
	{
		CGameContext *pGameServer = pChr->GameServer();
		if(pGameServer && pGameServer->Server()->Tick() % 3 == 0)
			pGameServer->CreateDamageInd(Head, Frac * PI, 1, pChr->TeamMask());
	}
}

// 茈 charge-time VFX: real laser orbs (not only damage stars).
static void HoGojoTickPurpleChargeVfx(CCharacter *pChr, float Frac)
{
	HoGojoUpdateFusionOrbs(pChr, Frac, 0.0f);
}

static void HoGojoStartPurpleMerge(CCharacter *pChr, vec2 Dir, float ChargeFrac)
{
	pChr->m_HoGojoPurpleMergeLeft = std::max(4, g_Config.m_HoGojoPurpleMergeTicks);
	pChr->m_HoGojoPurpleDir = Dir;
	pChr->m_HoGojoPurpleChargeFrac = ChargeFrac;
	pChr->m_HoGojoChargeTicks = 0;
	// Keep fusion orbs alive through the snap-together.
	HoGojoEnsureFusionOrbs(pChr);
	if(CGameContext *pGameServer = pChr->GameServer())
		pGameServer->CreateSound(pChr->GetPos(), SOUND_WEAPON_SPAWN, pChr->TeamMask());
}

static void HoGojoTickPurpleMerge(CCharacter *pChr)
{
	if(pChr->m_HoGojoPurpleMergeLeft <= 0)
		return;

	CGameContext *pGameServer = pChr->GameServer();
	CPlayer *pPlayer = pChr->GetPlayer();
	const int Total = std::max(4, g_Config.m_HoGojoPurpleMergeTicks);
	const int Left = pChr->m_HoGojoPurpleMergeLeft;
	const float Progress = 1.0f - (float)Left / (float)Total; // 0 → 1 final snap-together

	// Continue fusion motion from full charge toward center.
	HoGojoUpdateFusionOrbs(pChr, pChr->m_HoGojoPurpleChargeFrac, Progress);

	pChr->m_HoGojoPurpleMergeLeft--;
	if(pChr->m_HoGojoPurpleMergeLeft > 0)
		return;

	// Orbs met → launch 茈 at the same radius the fusion balls were showing.
	const float Radius = HoGojoPurpleRadiusFromFrac(pChr->m_HoGojoPurpleChargeFrac);
	vec2 Dir = pChr->m_HoGojoPurpleDir;
	if(length(Dir) < 0.001f)
		Dir = vec2(1, 0);
	else
		Dir = normalize(Dir);
	const vec2 Head = pChr->m_Pos + vec2(0.0f, -40.0f - Radius * 0.35f);
	// Spawn slightly outside body so the ball doesn't clip the tee.
	const vec2 Start = pChr->m_Pos + Dir * (28.0f + Radius * 0.15f);
	new CHoGojoProjectile(&pGameServer->m_World, pPlayer->GetCid(), Start, Dir, Radius, CHoGojoProjectile::TYPE_PURPLE);
	pGameServer->CreateSound(pChr->GetPos(), SOUND_GRENADE_EXPLODE, pChr->TeamMask());
	pGameServer->CreateDamageInd(Head, 0.0f, 4, pChr->TeamMask());
	HoGojoClearFusionOrbs(pPlayer);
}

static void HoGojoSendChargeIndicator(CCharacter *pChr, int Mode, int ChargeTicks)
{
	CPlayer *pPlayer = pChr->GetPlayer();
	CGameContext *pGameServer = pChr->GameServer();
	if(!pPlayer || !pGameServer)
		return;
	// ~4 Hz so it feels live without flooding.
	if(pGameServer->Server()->Tick() % (std::max(1, pGameServer->Server()->TickSpeed() / 4)) != 0)
		return;

	const float Frac = ClampChargeFrac(ChargeTicks);
	const int Pct = (int)std::lround(Frac * 100.0f);
	const int Bars = 10;
	const int Filled = std::clamp((int)std::lround(Frac * Bars), 0, Bars);
	char aBar[32];
	for(int i = 0; i < Bars; i++)
		aBar[i] = i < Filled ? '|' : '.';
	aBar[Bars] = '\0';

	const char *pName = "Charge";
	float SizePx = 0.0f;
	if(Mode == HO_WPNMODE_SHOTGUN_BLUE)
	{
		pName = "苍";
		SizePx = LerpF((float)g_Config.m_HoGojoBlueRadiusMin, (float)g_Config.m_HoGojoBlueRadiusMax, Frac);
	}
	else if(Mode == HO_WPNMODE_SHOTGUN_RED)
	{
		pName = "赫";
		SizePx = LerpF((float)g_Config.m_HoGojoRedRadiusMin, (float)g_Config.m_HoGojoRedRadiusMax, Frac);
	}
	else if(Mode == HO_WPNMODE_SHOTGUN_PURPLE)
	{
		pName = "茈";
		SizePx = HoGojoPurpleRadiusFromFrac(Frac);
	}

	const float DiamTiles = (SizePx * 2.0f) / 32.0f;
	char aBuf[160];
	str_format(aBuf, sizeof(aBuf), "%s 蓄力 %d%% [%s]\n半径 %.0fpx (~%.1f格直径)",
		pName, Pct, aBar, SizePx, DiamTiles);
	pPlayer->m_HoHpLastBroadcastTick = 0;
	pGameServer->SendBroadcast(aBuf, pPlayer->GetCid(), true);
}

static void HoGojoCast(CCharacter *pChr, int Mode, int ChargeTicks, vec2 Dir)
{
	CGameContext *pGameServer = pChr->GameServer();
	CPlayer *pPlayer = pChr->GetPlayer();
	if(!pGameServer || !pPlayer)
		return;

	const float Frac = ClampChargeFrac(ChargeTicks);
	if(length(Dir) < 0.001f)
		Dir = vec2(1, 0);
	else
		Dir = normalize(Dir);

	if(Mode == HO_WPNMODE_SHOTGUN_BLUE)
	{
		const float R = LerpF((float)g_Config.m_HoGojoBlueRadiusMin, (float)g_Config.m_HoGojoBlueRadiusMax, Frac);
		const vec2 Start = pChr->m_Pos + Dir * 36.0f;
		new CHoGojoBlue(&pGameServer->m_World, pPlayer->GetCid(), Start, R);
	}
	else if(Mode == HO_WPNMODE_SHOTGUN_RED)
	{
		const float R = LerpF((float)g_Config.m_HoGojoRedRadiusMin, (float)g_Config.m_HoGojoRedRadiusMax, Frac);
		const vec2 Start = pChr->m_Pos + Dir * 28.0f;
		new CHoGojoProjectile(&pGameServer->m_World, pPlayer->GetCid(), Start, Dir, R, CHoGojoProjectile::TYPE_RED);
	}
	else if(Mode == HO_WPNMODE_SHOTGUN_PURPLE)
	{
		// Grow happened while charging; short final fusion then launch (size from charge).
		HoGojoStartPurpleMerge(pChr, Dir, Frac);
	}
}

void HoGojoTickCharacter(CCharacter *pChr)
{
	if(!pChr || !pChr->IsAlive())
		return;

	CPlayer *pPlayer = pChr->GetPlayer();
	if(!HoGojoOwned(pPlayer))
	{
		pChr->m_HoGojoChargeTicks = 0;
		pChr->m_HoGojoFireHeld = false;
		pChr->m_HoGojoPurpleMergeLeft = 0;
		HoGojoClearFusionOrbs(pPlayer);
		return;
	}

	// Domain each tick while active.
	HoGojoTickVoidDomain(pChr);

	// Purple merge locks casting.
	if(pChr->m_HoGojoPurpleMergeLeft > 0)
	{
		HoGojoTickPurpleMerge(pChr);
		pChr->m_HoGojoChargeTicks = 0;
		pChr->m_HoGojoFireHeld = false;
		return;
	}

	if(!HoGojoTechniqueMode(pPlayer) || pChr->GetActiveWeapon() != WEAPON_SHOTGUN || pChr->m_FreezeTime > 0 ||
		pPlayer->m_HoWeaponSelectOpen)
	{
		pChr->m_HoGojoChargeTicks = 0;
		pChr->m_HoGojoFireHeld = false;
		// Drop fusion orbs if no longer charging purple.
		if(HoGojoShotgunMode(pPlayer) != HO_WPNMODE_SHOTGUN_PURPLE || pChr->GetActiveWeapon() != WEAPON_SHOTGUN)
			HoGojoClearFusionOrbs(pPlayer);
		return;
	}

	const CCharacterCore *pCore = pChr->Core();
	const bool Fire = (pCore->m_Input.m_Fire & 1) != 0;
	const bool FirePress = Fire && !pChr->m_HoGojoFireHeld;
	const int Mode = HoGojoShotgunMode(pPlayer);
	const int MinT = std::max(1, g_Config.m_HoGojoChargeTicksMin);
	const int MaxT = std::max(MinT, g_Config.m_HoGojoChargeTicksMax);

	// 苍: while controlled, another click detaches (keeps last follow velocity).
	if(Mode == HO_WPNMODE_SHOTGUN_BLUE && pPlayer->m_pHoGojoBlue && pPlayer->m_pHoGojoBlue->IsControlled())
	{
		if(FirePress)
		{
			pPlayer->m_pHoGojoBlue->ReleaseControl();
			pChr->m_HoGojoFireHeld = true;
			pChr->m_HoGojoChargeTicks = 0;
			return;
		}
		// Holding while still controlled: do not charge a second blue.
		pChr->m_HoGojoFireHeld = Fire;
		pChr->m_HoGojoChargeTicks = 0;
		return;
	}

	if(Fire)
	{
		if(pChr->m_HoGojoChargeTicks < MaxT)
			pChr->m_HoGojoChargeTicks++;
		const float Frac = ClampChargeFrac(pChr->m_HoGojoChargeTicks);
		// 茈: grow 苍/赫 orbs while charging (not only after release).
		if(Mode == HO_WPNMODE_SHOTGUN_PURPLE)
			HoGojoTickPurpleChargeVfx(pChr, Frac);
		else if(pChr->m_HoGojoChargeTicks > 0 && pChr->GameServer()->Server()->Tick() % 3 == 0)
		{
			const float Orbit = 20.0f + Frac * 40.0f;
			const float Ang = pChr->GameServer()->Server()->Tick() * 0.4f;
			const vec2 P = pChr->m_Pos + vec2(std::cos(Ang), std::sin(Ang)) * Orbit;
			pChr->GameServer()->CreateDamageInd(P, Ang, 1, pChr->TeamMask());
		}
		HoGojoSendChargeIndicator(pChr, Mode, pChr->m_HoGojoChargeTicks);
		pChr->m_HoGojoFireHeld = true;
	}
	else if(pChr->m_HoGojoFireHeld)
	{
		if(pChr->m_HoGojoChargeTicks >= MinT)
		{
			const vec2 Mouse((float)pCore->m_Input.m_TargetX, (float)pCore->m_Input.m_TargetY);
			vec2 Dir = Mouse;
			if(length(Dir) > 0.001f)
				Dir = normalize(Dir);
			else
				Dir = vec2(1, 0);
			// New 苍 replaces free-flying previous one.
			HoGojoCast(pChr, Mode, pChr->m_HoGojoChargeTicks, Dir);
		}
		else if(Mode == HO_WPNMODE_SHOTGUN_PURPLE)
		{
			// Released too early — cancel fusion orbs.
			HoGojoClearFusionOrbs(pPlayer);
		}
		pChr->m_HoGojoChargeTicks = 0;
		pChr->m_HoGojoFireHeld = false;
	}
	else
	{
		pChr->m_HoGojoChargeTicks = 0;
		if(Mode != HO_WPNMODE_SHOTGUN_PURPLE || pChr->m_HoGojoPurpleMergeLeft <= 0)
		{
			// Not charging purple: no lingering orbs (except mid-merge handled above).
			if(Mode != HO_WPNMODE_SHOTGUN_PURPLE)
				HoGojoClearFusionOrbs(pPlayer);
		}
	}
}

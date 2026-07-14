#include "falldamage.h"

#include "deathmsg.h"
#include "fracture.h"
#include "hp.h"

#include <engine/shared/config.h>

#include <generated/protocol.h>

#include <game/server/entities/character.h>
#include <game/server/player.h>

#include <cmath>

void HoFallDamageReset(CCharacter *pChr)
{
	if(!pChr)
		return;

	pChr->m_HoFallAirVelY = 0.0f;
	pChr->m_HoFallWasGrounded = true;
}

static int HoImpactDamage(float Excess, float Scale)
{
	const int Damage = (int)std::floor(Excess * HO_IMPACT_DAMAGE_PER_VEL * Scale);
	return Damage > 0 ? Damage : 0;
}

void HoFallDamageAfterMove(CCharacter *pChr, vec2 PreMoveVel)
{
	if(!pChr || !pChr->IsAlive())
		return;

	// No falldamage: skip impact checks (damage + new fractures). Existing fractures stay.
	if(!g_Config.m_HoFalldamage)
	{
		// Keep fall state coherent while disabled so enabling mid-session is safe.
		pChr->m_HoFallWasGrounded = pChr->IsGrounded();
		if(pChr->m_HoFallWasGrounded)
			pChr->m_HoFallAirVelY = 0.0f;
		return;
	}

	CPlayer *pPlayer = pChr->GetPlayer();
	if(!pPlayer || pPlayer->GetTeam() == TEAM_SPECTATORS)
		return;

	// Skip non-normal movement states (super/invincible handled in HoHpTakeDamage).
	if(pPlayer->m_HoFlyMode)
	{
		pChr->m_HoFallAirVelY = 0.0f;
		pChr->m_HoFallWasGrounded = pChr->IsGrounded();
		return;
	}

	const float Scale = g_Config.m_HoFalldamageScale / 100.0f;
	const int Cid = pPlayer->GetCid();
	const bool WantFracture = g_Config.m_HoFracture != 0;

	// --- Wall slam (horizontal): kinetic deathmsg + arm fracture ---
	// gamecore sets m_Colliding when X velocity is stopped by a wall.
	if(pChr->Core()->m_Colliding != 0)
	{
		const float ImpactX = std::fabs(PreMoveVel.x);
		const float ExcessX = ImpactX - HO_WALL_VEL_THRESHOLD;
		if(ExcessX > 0.0f)
		{
			if(WantFracture)
				HoFractureOnWallImpact(pChr, ExcessX);
			const int WallDamage = HoImpactDamage(ExcessX, Scale);
			if(WallDamage > 0)
			{
				if(HoHpTakeDamage(pChr, WallDamage, Cid, WEAPON_WORLD, true, HO_DEATH_KINETIC))
					return;
			}
		}
	}

	if(!pChr->IsAlive())
		return;

	// --- Fall / landing: fall deathmsg + leg fracture ---
	const bool Grounded = pChr->IsGrounded();
	const float FallVel = PreMoveVel.y > pChr->m_HoFallAirVelY ? PreMoveVel.y : pChr->m_HoFallAirVelY;

	if(!Grounded)
	{
		if(PreMoveVel.y > pChr->m_HoFallAirVelY)
			pChr->m_HoFallAirVelY = PreMoveVel.y;
		pChr->m_HoFallWasGrounded = false;
		return;
	}

	const bool Landed = !pChr->m_HoFallWasGrounded;
	pChr->m_HoFallWasGrounded = true;
	pChr->m_HoFallAirVelY = 0.0f;

	if(!Landed)
		return;

	const float ExcessY = FallVel - HO_FALL_VEL_THRESHOLD;
	if(ExcessY <= 0.0f)
		return;

	if(WantFracture)
		HoFractureOnFallImpact(pChr, ExcessY);

	const int FallDamage = HoImpactDamage(ExcessY, Scale);
	if(FallDamage > 0)
		HoHpTakeDamage(pChr, FallDamage, Cid, WEAPON_WORLD, true, HO_DEATH_FALL);
}

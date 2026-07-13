#include "falldamage.h"

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

void HoFallDamageAfterMove(CCharacter *pChr, float PreMoveVelY)
{
	if(!pChr || !pChr->IsAlive())
		return;

	if(!g_Config.m_HoFalldamage)
	{
		// Keep state coherent while disabled so enabling mid-session is safe.
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

	const bool Grounded = pChr->IsGrounded();
	const float FallVel = PreMoveVelY > pChr->m_HoFallAirVelY ? PreMoveVelY : pChr->m_HoFallAirVelY;

	if(!Grounded)
	{
		if(PreMoveVelY > pChr->m_HoFallAirVelY)
			pChr->m_HoFallAirVelY = PreMoveVelY;
		pChr->m_HoFallWasGrounded = false;
		return;
	}

	const bool Landed = !pChr->m_HoFallWasGrounded;
	pChr->m_HoFallWasGrounded = true;
	pChr->m_HoFallAirVelY = 0.0f;

	if(!Landed)
		return;

	const float Excess = FallVel - HO_FALL_VEL_THRESHOLD;
	if(Excess <= 0.0f)
		return;

	// Scale with ho_falldamage_scale (%). Damage is absolute on the independent HP bar.
	const float Scale = g_Config.m_HoFalldamageScale / 100.0f;
	const int Damage = (int)std::floor(Excess * HO_FALL_DAMAGE_PER_VEL * Scale);
	if(Damage <= 0)
		return;

	HoHpTakeDamage(pChr, Damage, pPlayer->GetCid(), WEAPON_WORLD, true);
}

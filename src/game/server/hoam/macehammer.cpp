#include "macehammer.h"

#include "deathmsg.h"
#include "hp.h"

#include <base/math.h>
#include <base/vmath.h>

#include <generated/protocol.h>

#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

#include <algorithm>
#include <cmath>

void HoMaceUpdateFall(CCharacter *pChr)
{
	if(!pChr || !pChr->IsAlive())
		return;

	CPlayer *pPlayer = pChr->GetPlayer();
	if(!pPlayer || !pPlayer->m_HoMaceHammer)
	{
		pChr->m_HoMaceInAir = false;
		return;
	}

	// DDNet Y increases downward: higher altitude = smaller Y.
	if(pChr->IsGrounded())
	{
		pChr->m_HoMaceInAir = false;
		return;
	}

	if(!pChr->m_HoMaceInAir)
	{
		pChr->m_HoMaceInAir = true;
		pChr->m_HoMaceFallStartY = pChr->m_Pos.y;
	}
	else
	{
		pChr->m_HoMaceFallStartY = std::min(pChr->m_HoMaceFallStartY, pChr->m_Pos.y);
	}
}

float HoMaceFallBlocks(const CCharacter *pChr)
{
	if(!pChr || !pChr->m_HoMaceInAir)
		return 0.0f;
	const float Dy = pChr->m_Pos.y - pChr->m_HoMaceFallStartY;
	return Dy > 0.0f ? Dy / HO_MACE_TILE : 0.0f;
}

int HoMaceDamage(float FallBlocks, bool Smash)
{
	// Base mace attack: 6 HP (Minecraft wiki).
	int Damage = 6;
	if(!Smash)
		return Damage;

	// Smash extra: +4 for first 3 blocks, +2 for next 5, +1 after that.
	int Blocks = (int)std::floor(FallBlocks);
	if(Blocks < 1)
		Blocks = 1;

	int Rem = Blocks;
	const int N1 = std::min(Rem, 3);
	Damage += N1 * 4;
	Rem -= N1;
	const int N2 = std::min(Rem, 5);
	Damage += N2 * 2;
	Rem -= N2;
	Damage += Rem * 1;
	return Damage;
}

bool HoMaceTryHammerHit(CCharacter *pAttacker, CCharacter *pTarget)
{
	if(!pAttacker || !pTarget || !pAttacker->IsAlive() || !pTarget->IsAlive())
		return false;

	CPlayer *pAtkPlayer = pAttacker->GetPlayer();
	// Owned via ho_macehammer AND selected as active hammer mode (weapon select).
	if(!pAtkPlayer || !pAtkPlayer->m_HoMaceHammer || pAtkPlayer->m_aHoWeaponMode[WEAPON_HAMMER] != 1) // 1 = mace mode
		return false;

	CGameContext *pGameServer = pAttacker->GameServer();
	if(!pGameServer)
		return false;

	const float FallBlocks = HoMaceFallBlocks(pAttacker);
	const bool Smash = FallBlocks >= HO_MACE_SMASH_MIN_BLOCKS;
	const int Damage = HoMaceDamage(FallBlocks, Smash);

	vec2 Dir;
	if(length(pTarget->m_Pos - pAttacker->m_Pos) > 0.0f)
		Dir = normalize(pTarget->m_Pos - pAttacker->m_Pos);
	else
		Dir = vec2(0.f, -1.f);

	float Strength = pAttacker->GetTuning(pAttacker->m_TuneZone)->m_HammerStrength;
	if(Smash)
		Strength *= 1.35f;

	// Same knockback construction as vanilla hammer, then HP damage via independent system.
	vec2 Temp = pTarget->Core()->m_Vel + normalize(Dir + vec2(0.f, -1.1f)) * 10.0f;
	// Temp is desired absolute velocity contribution pattern from original code:
	// Force = (vec2(0,-1) + (Temp - Vel)) * Strength after ClampVel — use TakeDamage for force.
	// Replicate original:
	//   Temp = Vel + normalize(Dir+up)*10; Temp = ClampVel; Temp -= Vel; Force = (up + Temp)*Strength
	// ClampVel is internal — approximate with unclamped path matching original structure.
	Temp = Temp - pTarget->Core()->m_Vel;
	const vec2 Force = (vec2(0.f, -1.0f) + Temp) * Strength;

	const int DeathCause = Smash ? HO_DEATH_MACE : HO_DEATH_NONE;
	HoHpTakeDamage(pTarget, Damage, pAtkPlayer->GetCid(), WEAPON_HAMMER, true, DeathCause);

	if(pTarget->IsAlive())
	{
		pTarget->TakeDamage(Force, 0, pAtkPlayer->GetCid(), WEAPON_HAMMER);
		pTarget->Unfreeze();
	}

	if(Smash)
	{
		// Successful smash: reset fall accumulation (wiki: nullify prior fall damage).
		pAttacker->m_HoMaceFallStartY = pAttacker->m_Pos.y;
		pAttacker->m_HoFallAirVelY = 0.0f;
		pAttacker->m_HoFallWasGrounded = true;

		// AOE knockback within 2.5 blocks of the hit (wind-charge-like).
		CEntity *apEnts[MAX_CLIENTS];
		const int Num = pGameServer->m_World.FindEntities(pTarget->m_Pos, HO_MACE_AOE_RADIUS, apEnts, MAX_CLIENTS, CGameWorld::ENTTYPE_CHARACTER);
		for(int i = 0; i < Num; i++)
		{
			auto *pOther = static_cast<CCharacter *>(apEnts[i]);
			if(!pOther || pOther == pAttacker || !pOther->IsAlive())
				continue;
			if(!pAttacker->CanCollide(pOther->GetPlayer()->GetCid()))
				continue;

			vec2 Away = pOther->m_Pos - pTarget->m_Pos;
			if(Away == vec2(0, 0))
				Away = vec2(0, -1);
			else
				Away = normalize(Away);
			pOther->TakeDamage(Away * 12.0f + vec2(0, -4.0f), 0, pAtkPlayer->GetCid(), WEAPON_HAMMER);
		}

		pGameServer->CreateSound(pTarget->m_Pos, SOUND_GRENADE_EXPLODE, pAttacker->TeamMask());
	}

	return true;
}

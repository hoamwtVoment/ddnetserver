#include "fracture.h"

#include "hp.h"
#include "lang.h"

#include <base/secure.h>
#include <base/str.h>

#include <engine/shared/config.h>
#include <engine/shared/protocol.h>

#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>
#include <game/tuning.h>

#include <algorithm>

namespace
{
	// Map excess impact speed to fracture level 1..3.
	int LevelFromExcess(float Excess)
	{
		if(Excess <= 0.0f)
			return 0;
		if(Excess < 8.0f)
			return 1;
		if(Excess < 18.0f)
			return 2;
		return HO_FRACTURE_LEVEL_MAX;
	}

	// Level L of Max uses fraction L/Max of the full penalty.
	float LevelFrac(int Level)
	{
		Level = std::clamp(Level, 0, (int)HO_FRACTURE_LEVEL_MAX);
		if(Level <= 0)
			return 0.0f;
		return (float)Level / (float)HO_FRACTURE_LEVEL_MAX;
	}

	const char *Roman(int Level)
	{
		switch(std::clamp(Level, 1, (int)HO_FRACTURE_LEVEL_MAX))
		{
		case 1: return "I";
		case 2: return "II";
		default: return "III";
		}
	}

	void RefreshHud(CCharacter *pChr)
	{
		if(!pChr || !pChr->IsAlive())
			return;
		if(CPlayer *pPlayer = pChr->GetPlayer())
			pPlayer->m_HoHpLastBroadcastTick = 0;
		HoHpSendBroadcast(pChr);
	}

	// Raise level to at least NewLevel (never decreases until reset/heal).
	bool RaiseLevel(int *pLevel, int NewLevel)
	{
		if(!pLevel || NewLevel <= 0)
			return false;
		NewLevel = std::min(NewLevel, (int)HO_FRACTURE_LEVEL_MAX);
		if(*pLevel >= NewLevel)
			return false;
		*pLevel = NewLevel;
		return true;
	}
}

void HoFractureReset(CCharacter *pChr)
{
	if(!pChr)
		return;
	pChr->m_HoFractureLegLevel = 0;
	pChr->m_HoFractureArmLevel = 0;
	pChr->m_HoWeaponSwitchReadyTick = 0;
	pChr->m_HoWeaponSwitchPending = false;
}

bool HoFractureHasAny(const CCharacter *pChr)
{
	return pChr && (pChr->m_HoFractureLegLevel > 0 || pChr->m_HoFractureArmLevel > 0);
}

void HoFractureOnFallImpact(CCharacter *pChr, float ExcessY)
{
	if(!pChr || !pChr->IsAlive() || !g_Config.m_HoFracture || !g_Config.m_HoFalldamage)
		return;
	const int L = LevelFromExcess(ExcessY);
	if(RaiseLevel(&pChr->m_HoFractureLegLevel, L))
		RefreshHud(pChr);
}

void HoFractureOnWallImpact(CCharacter *pChr, float ExcessX)
{
	if(!pChr || !pChr->IsAlive() || !g_Config.m_HoFracture || !g_Config.m_HoFalldamage)
		return;
	const int L = LevelFromExcess(ExcessX);
	if(RaiseLevel(&pChr->m_HoFractureArmLevel, L))
	{
		// Reset any in-progress switch wind-up so new severity applies next request.
		pChr->m_HoWeaponSwitchReadyTick = 0;
		pChr->m_HoWeaponSwitchPending = false;
		RefreshHud(pChr);
	}
}

void HoFractureApplyLegTuning(CCharacter *pChr, CTuningParams *pTuning)
{
	if(!pChr || !pTuning || !g_Config.m_HoFracture || pChr->m_HoFractureLegLevel <= 0)
		return;

	const float Frac = LevelFrac(pChr->m_HoFractureLegLevel);
	// Config = remaining percent at level III; higher level → closer to that floor.
	const float SpeedFloor = std::clamp(g_Config.m_HoFractureLegSpeed, 10, 100) / 100.0f;
	const float JumpFloor = std::clamp(g_Config.m_HoFractureLegJump, 10, 100) / 100.0f;
	const float SpeedPct = 1.0f - (1.0f - SpeedFloor) * Frac;
	const float JumpPct = 1.0f - (1.0f - JumpFloor) * Frac;

	// Ground only for movement — air strafe stays normal (broken leg, not mid-air control).
	pTuning->m_GroundControlSpeed = (float)pTuning->m_GroundControlSpeed * SpeedPct;
	pTuning->m_GroundControlAccel = (float)pTuning->m_GroundControlAccel * SpeedPct;
	// Jump height reduced (ground + air jump).
	pTuning->m_GroundJumpImpulse = (float)pTuning->m_GroundJumpImpulse * JumpPct;
	pTuning->m_AirJumpImpulse = (float)pTuning->m_AirJumpImpulse * JumpPct;
}

int HoFractureArmFireDelayMs(const CCharacter *pChr)
{
	if(!pChr || !g_Config.m_HoFracture || pChr->m_HoFractureArmLevel <= 0)
		return 0;

	int MinMs = std::max(0, g_Config.m_HoFractureArmFireMsMin);
	int MaxMs = std::max(0, g_Config.m_HoFractureArmFireMsMax);
	if(MaxMs < MinMs)
		std::swap(MinMs, MaxMs);

	int Roll = MinMs;
	if(MaxMs > MinMs)
		Roll = MinMs + secure_rand_below(MaxMs - MinMs + 1);

	// Scale by level (I = 1/3 of roll, III = full).
	const int Scaled = (int)std::lround((double)Roll * LevelFrac(pChr->m_HoFractureArmLevel));
	return std::max(0, Scaled);
}

int HoFractureArmSwitchDelayTicks(const CCharacter *pChr)
{
	if(!pChr || !g_Config.m_HoFracture || pChr->m_HoFractureArmLevel <= 0)
		return 0;
	const int Ms = std::max(0, g_Config.m_HoFractureArmSwitchMs);
	if(Ms <= 0)
		return 0;
	// Scale by level.
	const int ScaledMs = (int)std::lround((double)Ms * LevelFrac(pChr->m_HoFractureArmLevel));
	if(ScaledMs <= 0)
		return 0;
	return (ScaledMs * SERVER_TICK_SPEED + 999) / 1000;
}

void HoFractureAppendBroadcastLine(CCharacter *pChr, char *pBuf, int BufSize)
{
	if(!pBuf || BufSize <= 0 || !HoFractureHasAny(pChr) || !g_Config.m_HoFracture)
		return;

	const int Lang = HoLangResolve(pChr->GameServer(), pChr->GetPlayer());
	const bool Zh = Lang == HO_LANG_ZH;

	char aLine[96];
	aLine[0] = '\0';

	if(pChr->m_HoFractureLegLevel > 0)
	{
		str_format(aLine, sizeof(aLine), Zh ? "腿骨折 %s" : "Broken leg %s", Roman(pChr->m_HoFractureLegLevel));
	}
	if(pChr->m_HoFractureArmLevel > 0)
	{
		char aArm[48];
		str_format(aArm, sizeof(aArm), Zh ? "手骨折 %s" : "Broken arm %s", Roman(pChr->m_HoFractureArmLevel));
		if(aLine[0])
		{
			str_append(aLine, Zh ? " · " : " · ", sizeof(aLine));
			str_append(aLine, aArm, sizeof(aLine));
		}
		else
			str_copy(aLine, aArm);
	}

	const int Len = str_length(pBuf);
	if(Len + 1 + str_length(aLine) + 1 > BufSize)
		return;
	if(Len > 0)
		str_append(pBuf, "\n", BufSize);
	str_append(pBuf, aLine, BufSize);
}

#include "fracture.h"

#include "hp.h"
#include "lang.h"

#include <base/str.h>

#include <engine/shared/config.h>
#include <engine/shared/protocol.h>

#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>
#include <game/tuning.h>

#include <algorithm>

void HoFractureReset(CCharacter *pChr)
{
	if(!pChr)
		return;
	pChr->m_HoFractureLeg = false;
	pChr->m_HoFractureArm = false;
	pChr->m_HoWeaponSwitchReadyTick = 0;
}

bool HoFractureHasAny(const CCharacter *pChr)
{
	return pChr && (pChr->m_HoFractureLeg || pChr->m_HoFractureArm);
}

static void HoFractureRefreshHud(CCharacter *pChr)
{
	if(!pChr || !pChr->IsAlive())
		return;
	if(CPlayer *pPlayer = pChr->GetPlayer())
		pPlayer->m_HoHpLastBroadcastTick = 0;
	HoHpSendBroadcast(pChr);
}

void HoFractureOnFallImpact(CCharacter *pChr)
{
	// New fractures only while both toggles are on. Turning falldamage off later
	// does not clear existing fractures (effects still apply if ho_fracture is on).
	if(!pChr || !pChr->IsAlive() || !g_Config.m_HoFracture || !g_Config.m_HoFalldamage)
		return;
	if(pChr->m_HoFractureLeg)
		return;
	pChr->m_HoFractureLeg = true;
	HoFractureRefreshHud(pChr);
}

void HoFractureOnWallImpact(CCharacter *pChr)
{
	if(!pChr || !pChr->IsAlive() || !g_Config.m_HoFracture || !g_Config.m_HoFalldamage)
		return;
	if(pChr->m_HoFractureArm)
		return;
	pChr->m_HoFractureArm = true;
	HoFractureRefreshHud(pChr);
}

void HoFractureApplyLegTuning(CCharacter *pChr, CTuningParams *pTuning)
{
	if(!pChr || !pTuning || !g_Config.m_HoFracture || !pChr->m_HoFractureLeg)
		return;

	const float SpeedPct = std::clamp(g_Config.m_HoFractureLegSpeed, 10, 100) / 100.0f;
	const float JumpPct = std::clamp(g_Config.m_HoFractureLegJump, 10, 100) / 100.0f;

	// CTuneParam has no operator*=; assign scaled floats.
	pTuning->m_GroundControlSpeed = (float)pTuning->m_GroundControlSpeed * SpeedPct;
	pTuning->m_AirControlSpeed = (float)pTuning->m_AirControlSpeed * SpeedPct;
	pTuning->m_GroundControlAccel = (float)pTuning->m_GroundControlAccel * SpeedPct;
	pTuning->m_AirControlAccel = (float)pTuning->m_AirControlAccel * SpeedPct;
	pTuning->m_GroundJumpImpulse = (float)pTuning->m_GroundJumpImpulse * JumpPct;
	pTuning->m_AirJumpImpulse = (float)pTuning->m_AirJumpImpulse * JumpPct;
}

int HoFractureArmFireDelayMs(const CCharacter *pChr)
{
	if(!pChr || !g_Config.m_HoFracture || !pChr->m_HoFractureArm)
		return 0;
	return std::max(0, g_Config.m_HoFractureArmFireMs);
}

int HoFractureArmSwitchDelayTicks(const CCharacter *pChr)
{
	if(!pChr || !g_Config.m_HoFracture || !pChr->m_HoFractureArm)
		return 0;
	const int Ms = std::max(0, g_Config.m_HoFractureArmSwitchMs);
	if(Ms <= 0)
		return 0;
	return (Ms * SERVER_TICK_SPEED + 999) / 1000;
}

void HoFractureAppendBroadcastLine(CCharacter *pChr, char *pBuf, int BufSize)
{
	if(!pBuf || BufSize <= 0 || !HoFractureHasAny(pChr) || !g_Config.m_HoFracture)
		return;

	const int Lang = HoLangResolve(pChr->GameServer(), pChr->GetPlayer());
	const bool Zh = Lang == HO_LANG_ZH;

	char aLine[64];
	if(pChr->m_HoFractureLeg && pChr->m_HoFractureArm)
		str_copy(aLine, Zh ? "腿骨折 · 手骨折" : "Broken leg · Broken arm");
	else if(pChr->m_HoFractureLeg)
		str_copy(aLine, Zh ? "腿骨折" : "Broken leg");
	else
		str_copy(aLine, Zh ? "手骨折" : "Broken arm");

	const int Len = str_length(pBuf);
	if(Len + 1 + str_length(aLine) + 1 > BufSize)
		return;
	if(Len > 0)
		str_append(pBuf, "\n", BufSize);
	str_append(pBuf, aLine, BufSize);
}

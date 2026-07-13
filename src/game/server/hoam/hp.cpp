#include "hp.h"

#include <base/str.h>

#include <engine/shared/config.h>

#include <generated/protocol.h>

#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

#include <algorithm>

// How long the second-line delta stays visible (seconds).
static constexpr int HO_HP_DELTA_VISIBLE_SECS = 3;

int HoHpMax()
{
	return std::max(0, g_Config.m_HoHp);
}

void HoHpReset(CCharacter *pChr)
{
	if(!pChr)
		return;

	pChr->m_HoHp = HoHpMax();
	pChr->m_HoHpLastDelta = 0;
	pChr->m_HoHpLastDeltaTick = 0;
}

void HoHpNoteDelta(CCharacter *pChr, int Delta)
{
	if(!pChr || Delta == 0)
		return;

	pChr->m_HoHpLastDelta = Delta;
	pChr->m_HoHpLastDeltaTick = pChr->GameServer()->Server()->Tick();
}

bool HoHpShouldBroadcast(const CPlayer *pPlayer)
{
	return pPlayer && pPlayer->m_HoHpBroadcast && HoHpMax() > 0;
}

static bool HoHpDeltaVisible(CCharacter *pChr, const CPlayer *pPlayer)
{
	if(!pChr || !pPlayer || !pPlayer->m_HoHpDeltaBroadcast)
		return false;
	if(pChr->m_HoHpLastDelta == 0 || pChr->m_HoHpLastDeltaTick <= 0)
		return false;

	const int Tick = pChr->GameServer()->Server()->Tick();
	const int Speed = pChr->GameServer()->Server()->TickSpeed();
	return Tick - pChr->m_HoHpLastDeltaTick <= Speed * HO_HP_DELTA_VISIBLE_SECS;
}

void HoHpFormatBroadcast(CCharacter *pChr, char *pBuf, int BufSize)
{
	if(!pBuf || BufSize <= 0)
		return;

	if(!pChr)
	{
		pBuf[0] = '\0';
		return;
	}

	// Line 1: current HP
	// Line 2 (optional): last change, e.g. "-20" or "+10"
	CPlayer *pPlayer = pChr->GetPlayer();
	if(HoHpDeltaVisible(pChr, pPlayer))
		str_format(pBuf, BufSize, "HP %d/%d\n%+d", pChr->m_HoHp, HoHpMax(), pChr->m_HoHpLastDelta);
	else
		str_format(pBuf, BufSize, "HP %d/%d", pChr->m_HoHp, HoHpMax());
}

void HoHpSendBroadcast(CCharacter *pChr)
{
	if(!pChr || !pChr->IsAlive())
		return;

	CPlayer *pPlayer = pChr->GetPlayer();
	if(!HoHpShouldBroadcast(pPlayer))
		return;

	char aBuf[96];
	HoHpFormatBroadcast(pChr, aBuf, sizeof(aBuf));
	pChr->GameServer()->SendBroadcast(aBuf, pPlayer->GetCid(), false);
}

bool HoHpTakeDamage(CCharacter *pChr, int Damage, int Killer, int Weapon, bool ShowFeedback, int DeathCause)
{
	if(!pChr || !pChr->IsAlive() || Damage <= 0)
		return false;

	const int Max = HoHpMax();
	if(Max <= 0)
		return false;

	CPlayer *pPlayer = pChr->GetPlayer();
	if(!pPlayer)
		return false;

	// Super / invincible ignore independent HP damage.
	if(pChr->IsSuper() || pChr->Core()->m_Invincible)
		return false;

	if(pChr->m_HoHp > Max)
		pChr->m_HoHp = Max;

	if(Damage > pChr->m_HoHp)
		Damage = pChr->m_HoHp;

	pChr->m_HoHp -= Damage;
	if(DeathCause != 0)
		pChr->m_HoDeathCause = DeathCause;

	HoHpNoteDelta(pChr, -Damage);

	if(ShowFeedback)
	{
		CGameContext *pGameServer = pChr->GameServer();
		const CClientMask Mask = pChr->TeamMask();
		const int Stars = std::clamp(Damage, 1, 10);
		pGameServer->CreateDamageInd(pChr->m_Pos, 0.0f, Stars, Mask);
		pGameServer->CreateSound(pChr->m_Pos, SOUND_PLAYER_PAIN_SHORT, Mask);
		pChr->SetEmote(EMOTE_PAIN, pGameServer->Server()->Tick() + pGameServer->Server()->TickSpeed() / 2);
		HoHpSendBroadcast(pChr);
	}

	if(pChr->m_HoHp <= 0)
	{
		pChr->m_HoHp = 0;
		pChr->Die(Killer, Weapon);
		return true;
	}

	return false;
}

bool HoHpHeal(CCharacter *pChr, int Amount)
{
	if(!pChr || !pChr->IsAlive() || Amount <= 0)
		return false;

	const int Max = HoHpMax();
	if(Max <= 0)
		return false;

	if(pChr->m_HoHp >= Max)
		return false;

	const int Before = pChr->m_HoHp;
	pChr->m_HoHp = std::min(Max, pChr->m_HoHp + Amount);
	const int Healed = pChr->m_HoHp - Before;
	if(Healed > 0)
		HoHpNoteDelta(pChr, Healed);
	HoHpSendBroadcast(pChr);
	return Healed > 0;
}

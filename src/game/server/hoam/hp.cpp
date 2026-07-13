#include "hp.h"

#include <base/str.h>

#include <engine/shared/config.h>

#include <generated/protocol.h>

#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

#include <algorithm>

int HoHpMax()
{
	return std::max(0, g_Config.m_HoHp);
}

void HoHpClearPostDeath(CGameContext *pGameServer, CPlayer *pPlayer, bool ClearBroadcast)
{
	if(!pPlayer)
		return;

	if(ClearBroadcast && pPlayer->m_HoHpPostDeathUntil > 0 && pGameServer)
		pGameServer->SendBroadcast("", pPlayer->GetCid(), false);

	pPlayer->m_HoHpPostDeathUntil = 0;
	pPlayer->m_aHoHpPostDeathMsg[0] = '\0';
}

void HoHpReset(CCharacter *pChr)
{
	if(!pChr)
		return;

	pChr->m_HoHp = HoHpMax();
	pChr->m_HoHpLastDelta = 0;
	pChr->m_HoHpLastDeltaTick = 0;

	// Respawn: stop holding the death HP line.
	if(CPlayer *pPlayer = pChr->GetPlayer())
		HoHpClearPostDeath(pChr->GameServer(), pPlayer, true);
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

static int HoHpDeltaExpireTick(CCharacter *pChr)
{
	if(!pChr || pChr->m_HoHpLastDelta == 0 || pChr->m_HoHpLastDeltaTick <= 0)
		return 0;
	return pChr->m_HoHpLastDeltaTick + pChr->GameServer()->Server()->TickSpeed() * HO_HP_DELTA_VISIBLE_SECS;
}

static bool HoHpDeltaVisible(CCharacter *pChr, const CPlayer *pPlayer)
{
	if(!pChr || !pPlayer || !pPlayer->m_HoHpDeltaBroadcast)
		return false;
	if(pChr->m_HoHpLastDelta == 0 || pChr->m_HoHpLastDeltaTick <= 0)
		return false;

	const int Tick = pChr->GameServer()->Server()->Tick();
	return Tick <= HoHpDeltaExpireTick(pChr);
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

void HoHpArmPostDeathBroadcast(CCharacter *pChr)
{
	if(!pChr)
		return;

	CPlayer *pPlayer = pChr->GetPlayer();
	if(!HoHpShouldBroadcast(pPlayer))
		return;

	// Continue whatever is left of the delta visibility window — do not restart the timer.
	const int Expire = HoHpDeltaExpireTick(pChr);
	const int Now = pChr->GameServer()->Server()->Tick();
	if(Expire <= Now)
		return;

	char aBuf[96];
	HoHpFormatBroadcast(pChr, aBuf, sizeof(aBuf));
	if(!aBuf[0])
		return;

	str_copy(pPlayer->m_aHoHpPostDeathMsg, aBuf);
	pPlayer->m_HoHpPostDeathUntil = Expire;
	// Message was already sent by HoHpSendBroadcast; keep it until Expire.
}

void HoHpPlayerTick(CGameContext *pGameServer, CPlayer *pPlayer)
{
	if(!pGameServer || !pPlayer || pPlayer->m_HoHpPostDeathUntil <= 0)
		return;

	IServer *pServer = pGameServer->Server();
	const int Now = pServer->Tick();

	if(Now < pPlayer->m_HoHpPostDeathUntil)
	{
		// Refresh once per second so other broadcasts cannot wipe it for long.
		if(Now % pServer->TickSpeed() == 0 && pPlayer->m_aHoHpPostDeathMsg[0])
			pGameServer->SendBroadcast(pPlayer->m_aHoHpPostDeathMsg, pPlayer->GetCid(), false);
		return;
	}

	// Remaining time finished: clear the line.
	pGameServer->SendBroadcast("", pPlayer->GetCid(), false);
	pPlayer->m_HoHpPostDeathUntil = 0;
	pPlayer->m_aHoHpPostDeathMsg[0] = '\0';
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
		if(ShowFeedback)
			HoHpArmPostDeathBroadcast(pChr);
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

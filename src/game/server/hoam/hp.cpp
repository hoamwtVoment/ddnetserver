#include "hp.h"

#include "fracture.h"
#include "gojo.h"

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

static int HoHpBroadcastIntervalTicks(IServer *pServer)
{
	// Same cadence as normal HP / race timer broadcast (default 1 second).
	const int Sec = std::max(1, g_Config.m_SvTimeInBroadcastInterval);
	return pServer->TickSpeed() * Sec;
}

// Rate-limited send. Force=true still respects the 1s interval (prevents damage spam).
// Returns whether a packet was sent.
static bool HoHpSendToPlayer(CGameContext *pGameServer, CPlayer *pPlayer, const char *pText, bool Force = false)
{
	if(!pGameServer || !pPlayer || !pText)
		return false;

	IServer *pServer = pGameServer->Server();
	const int Now = pServer->Tick();
	const int Interval = HoHpBroadcastIntervalTicks(pServer);

	// Allow the very first send always; afterwards at most once per interval.
	if(pPlayer->m_HoHpLastBroadcastTick > 0 && Now - pPlayer->m_HoHpLastBroadcastTick < Interval)
	{
		(void)Force;
		return false;
	}

	pPlayer->m_HoHpLastBroadcastTick = Now;
	// Important so other non-important broadcasts cannot drop HP lines.
	pGameServer->SendBroadcast(pText, pPlayer->GetCid(), true);
	return true;
}

bool HoHpPostDeathActive(const CPlayer *pPlayer, int NowTick)
{
	return pPlayer && pPlayer->m_HoHpPostDeathUntil > NowTick && pPlayer->m_aHoHpPostDeathMsg[0] != '\0';
}

void HoHpClearPostDeath(CGameContext *pGameServer, CPlayer *pPlayer, bool ClearBroadcast)
{
	if(!pPlayer)
		return;

	// Never push "" here: empty important broadcasts blank the client HUD for ~10s.
	(void)pGameServer;
	(void)ClearBroadcast;

	pPlayer->m_HoHpPostDeathUntil = 0;
	pPlayer->m_aHoHpPostDeathMsg[0] = '\0';
}

void HoHpReset(CCharacter *pChr)
{
	if(!pChr)
		return;

	pChr->m_HoHp = HoHpMax();
	CPlayer *pPlayer = pChr->GetPlayer();
	CGameContext *pGameServer = pChr->GameServer();
	if(!pPlayer || !pGameServer)
		return;

	const int Now = pGameServer->Server()->Tick();
	if(HoHpPostDeathActive(pPlayer, Now))
	{
		// Instant / early respawn: gameplay HP is full, HUD keeps death line until hold ends.
		pChr->m_HoHpLastDelta = 0;
		pChr->m_HoHpLastDeltaTick = 0;
		return;
	}

	pChr->m_HoHpLastDelta = 0;
	pChr->m_HoHpLastDeltaTick = 0;
	HoHpClearPostDeath(pGameServer, pPlayer, false);
	// Reset rate limit so spawn HP can show immediately.
	pPlayer->m_HoHpLastBroadcastTick = 0;
	pPlayer->m_HoHpPendingSpawnBroadcast = true;
	HoHpSendBroadcast(pChr);
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
	// Weapon-select hover owns the broadcast while the menu is open.
	if(!pPlayer || pPlayer->m_HoWeaponSelectOpen || !pPlayer->m_HoHpBroadcast || HoHpMax() <= 0)
		return false;
	return true;
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

	CPlayer *pPlayer = pChr->GetPlayer();
	if(HoHpDeltaVisible(pChr, pPlayer))
		str_format(pBuf, BufSize, "HP %d/%d\n%+d", pChr->m_HoHp, HoHpMax(), pChr->m_HoHpLastDelta);
	else
		str_format(pBuf, BufSize, "HP %d/%d", pChr->m_HoHp, HoHpMax());

	// Third line (or second if no delta): fracture status.
	HoFractureAppendBroadcastLine(pChr, pBuf, BufSize);
}

void HoHpSendBroadcast(CCharacter *pChr)
{
	if(!pChr || !pChr->IsAlive())
		return;

	CPlayer *pPlayer = pChr->GetPlayer();
	if(!HoHpShouldBroadcast(pPlayer))
		return;

	const int Now = pChr->GameServer()->Server()->Tick();
	if(HoHpPostDeathActive(pPlayer, Now))
		return;

	char aBuf[160];
	HoHpFormatBroadcast(pChr, aBuf, sizeof(aBuf));
	HoHpSendToPlayer(pChr->GameServer(), pPlayer, aBuf);
}

void HoHpArmPostDeathBroadcast(CCharacter *pChr)
{
	if(!pChr)
		return;

	CPlayer *pPlayer = pChr->GetPlayer();
	CGameContext *pGameServer = pChr->GameServer();
	if(!pPlayer || !pGameServer || !HoHpShouldBroadcast(pPlayer))
		return;

	const int Expire = HoHpDeltaExpireTick(pChr);
	const int Now = pGameServer->Server()->Tick();
	if(Expire <= Now)
		return;

	char aBuf[160];
	HoHpFormatBroadcast(pChr, aBuf, sizeof(aBuf));
	if(!aBuf[0])
		return;

	str_copy(pPlayer->m_aHoHpPostDeathMsg, aBuf);
	pPlayer->m_HoHpPostDeathUntil = Expire;
	// Allow this death line through the rate limiter once.
	pPlayer->m_HoHpLastBroadcastTick = 0;
	HoHpSendToPlayer(pGameServer, pPlayer, aBuf);
}

void HoHpShowOverkillDeath(CCharacter *pChr)
{
	if(!pChr)
		return;

	CPlayer *pPlayer = pChr->GetPlayer();
	CGameContext *pGameServer = pChr->GameServer();
	if(!pPlayer || !pGameServer || !HoHpShouldBroadcast(pPlayer))
		return;

	const int Now = pGameServer->Server()->Tick();
	if(HoHpPostDeathActive(pPlayer, Now))
		return;

	constexpr int Overkill = 2147483647;
	pChr->m_HoHp = 0;
	pChr->m_HoHpLastDelta = -Overkill;
	pChr->m_HoHpLastDeltaTick = Now;
	HoHpArmPostDeathBroadcast(pChr);
}

void HoHpPlayerTick(CGameContext *pGameServer, CPlayer *pPlayer)
{
	if(!pGameServer || !pPlayer)
		return;

	// Do not refresh HP HUD while F3 weapon select is using the broadcast.
	if(pPlayer->m_HoWeaponSelectOpen)
		return;

	IServer *pServer = pGameServer->Server();
	const int Now = pServer->Tick();

	if(pPlayer->m_HoHpPendingSpawnBroadcast && HoHpShouldBroadcast(pPlayer))
	{
		if(CCharacter *pChr = pPlayer->GetCharacter())
		{
			if(pChr->IsAlive() && !HoHpPostDeathActive(pPlayer, Now))
			{
				pPlayer->m_HoHpPendingSpawnBroadcast = false;
				// Force through rate limit once for join.
				pPlayer->m_HoHpLastBroadcastTick = 0;
				HoHpSendBroadcast(pChr);
			}
		}
	}

	if(pPlayer->m_HoHpPostDeathUntil <= 0)
		return;

	if(Now <= pPlayer->m_HoHpPostDeathUntil)
	{
		// Keep death line at the same 1/s cadence as normal HP broadcast.
		if(pPlayer->m_aHoHpPostDeathMsg[0])
			HoHpSendToPlayer(pGameServer, pPlayer, pPlayer->m_aHoHpPostDeathMsg);
		return;
	}

	pPlayer->m_HoHpPostDeathUntil = 0;
	pPlayer->m_aHoHpPostDeathMsg[0] = '\0';
	if(CCharacter *pChr = pPlayer->GetCharacter())
	{
		if(pChr->IsAlive())
		{
			pPlayer->m_HoHpLastBroadcastTick = 0;
			HoHpSendBroadcast(pChr);
		}
	}
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

	if(pChr->IsSuper() || pChr->Core()->m_Invincible)
		return false;

	// Unlimited Void: no external HP damage (others + world). Own weapons still damage others.
	if(HoGojoVoidBlocksExternal(pChr, Killer))
		return false;

	if(pChr->m_HoHp > Max)
		pChr->m_HoHp = Max;

	// Keep full Damage for HUD overkill (e.g. mace smash). Only HP is clamped.
	const int Applied = std::min(Damage, pChr->m_HoHp);
	pChr->m_HoHp -= Applied;
	if(DeathCause != 0)
		pChr->m_HoDeathCause = DeathCause;

	HoHpNoteDelta(pChr, -Damage);

	const bool Lethal = pChr->m_HoHp <= 0;
	if(Lethal)
		pChr->m_HoHp = 0;

	if(ShowFeedback)
	{
		CGameContext *pGameServer = pChr->GameServer();
		const CClientMask Mask = pChr->TeamMask();
		const int Stars = std::clamp(Damage, 1, 10);
		pGameServer->CreateDamageInd(pChr->m_Pos, 0.0f, Stars, Mask);
		pGameServer->CreateSound(pChr->m_Pos, SOUND_PLAYER_PAIN_SHORT, Mask);
		pChr->SetEmote(EMOTE_PAIN, pGameServer->Server()->Tick() + pGameServer->Server()->TickSpeed() / 2);

		if(Lethal)
		{
			HoHpArmPostDeathBroadcast(pChr);
			pChr->Die(Killer, Weapon);
			// Do not send again here — Arm already sent once (rate-limited thereafter).
			return true;
		}

		// Non-lethal: at most one network update per second (same as idle HP).
		HoHpSendBroadcast(pChr);
	}
	else if(Lethal)
	{
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

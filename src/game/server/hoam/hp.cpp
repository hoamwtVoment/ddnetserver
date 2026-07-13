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

static void HoHpSendToPlayer(CGameContext *pGameServer, CPlayer *pPlayer, const char *pText)
{
	if(!pGameServer || !pPlayer || !pText)
		return;
	// Important: non-important broadcasts are dropped for 10s after any important one,
	// and post-death UI must not be silently discarded.
	pGameServer->SendBroadcast(pText, pPlayer->GetCid(), true);
}

void HoHpClearPostDeath(CGameContext *pGameServer, CPlayer *pPlayer, bool ClearBroadcast)
{
	if(!pPlayer)
		return;

	// Prefer not to push "" unless the caller truly wants a blank HUD.
	// Empty important broadcasts make the client keep a blank line for ~10s.
	if(ClearBroadcast && pPlayer->m_HoHpPostDeathUntil > 0 && pGameServer)
		HoHpSendToPlayer(pGameServer, pPlayer, "");

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

	CPlayer *pPlayer = pChr->GetPlayer();
	if(!pPlayer)
		return;

	// Drop post-death hold without blanking the HUD, then restore full HP line.
	pPlayer->m_HoHpPostDeathUntil = 0;
	pPlayer->m_aHoHpPostDeathMsg[0] = '\0';
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

	// Continue whatever is left of the delta visibility window — do not restart the timer.
	const int Expire = HoHpDeltaExpireTick(pChr);
	const int Now = pGameServer->Server()->Tick();
	if(Expire <= Now)
		return;

	char aBuf[96];
	HoHpFormatBroadcast(pChr, aBuf, sizeof(aBuf));
	if(!aBuf[0])
		return;

	str_copy(pPlayer->m_aHoHpPostDeathMsg, aBuf);
	pPlayer->m_HoHpPostDeathUntil = Expire;

	// Send immediately (important). Caller may Die() right after; player tick keeps refreshing.
	HoHpSendToPlayer(pGameServer, pPlayer, aBuf);
}

void HoHpPlayerTick(CGameContext *pGameServer, CPlayer *pPlayer)
{
	if(!pGameServer || !pPlayer || pPlayer->m_HoHpPostDeathUntil <= 0)
		return;

	// Alive again: stop death hold (spawn already restored full HP broadcast).
	if(pPlayer->GetCharacter() && pPlayer->GetCharacter()->IsAlive())
	{
		pPlayer->m_HoHpPostDeathUntil = 0;
		pPlayer->m_aHoHpPostDeathMsg[0] = '\0';
		return;
	}

	IServer *pServer = pGameServer->Server();
	const int Now = pServer->Tick();

	if(Now <= pPlayer->m_HoHpPostDeathUntil)
	{
		// Re-push every tick so death-frame overwrites cannot wipe the line.
		if(pPlayer->m_aHoHpPostDeathMsg[0])
			HoHpSendToPlayer(pGameServer, pPlayer, pPlayer->m_aHoHpPostDeathMsg);
		return;
	}

	// Remaining death-hold time finished while still dead: clear hold only.
	// Do not push "" — an empty important broadcast blanks the HUD for ~10s client-side
	// and nothing restores HP until the next damage unless we respawn and send again.
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
			// Arm + send while character is still valid, then die. Player tick keeps it up.
			HoHpArmPostDeathBroadcast(pChr);
			pChr->Die(Killer, Weapon);
			// Die() cannot send broadcast; push again via player after removal.
			if(pPlayer->m_HoHpPostDeathUntil > 0 && pPlayer->m_aHoHpPostDeathMsg[0])
				HoHpSendToPlayer(pGameServer, pPlayer, pPlayer->m_aHoHpPostDeathMsg);
			return true;
		}

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

#ifndef GAME_SERVER_HOAM_HP_H
#define GAME_SERVER_HOAM_HP_H

class CCharacter;
class CGameContext;
class CPlayer;

// Independent server HP system (not Teeworlds m_Health / TakeDamage).
// Fall damage and future damage sources should call HoHpTakeDamage / HoHpHeal.

// How long the second-line delta stays visible (seconds), including after death.
enum
{
	HO_HP_DELTA_VISIBLE_SECS = 3,
};

int HoHpMax();
void HoHpReset(CCharacter *pChr);

// Returns true if the character died from this damage.
// Killer/Weapon are used for the kill message when HP reaches 0.
// DeathCause is stored for Minecraft-style chat (see hoam/deathmsg.h).
bool HoHpTakeDamage(CCharacter *pChr, int Damage, int Killer, int Weapon, bool ShowFeedback = true, int DeathCause = 0);
bool HoHpHeal(CCharacter *pChr, int Amount);

// Broadcast helpers (display is opt-out per player via /hp).
// Second line shows last HP delta when m_HoHpDeltaBroadcast is on (default).
bool HoHpShouldBroadcast(const CPlayer *pPlayer);
bool HoHpPostDeathActive(const CPlayer *pPlayer, int NowTick);
void HoHpFormatBroadcast(CCharacter *pChr, char *pBuf, int BufSize);
void HoHpSendBroadcast(CCharacter *pChr);
// Record delta for the second broadcast line (negative = damage, positive = heal).
void HoHpNoteDelta(CCharacter *pChr, int Delta);

// After a lethal hit: keep the same broadcast until the remaining delta timer ends.
void HoHpArmPostDeathBroadcast(CCharacter *pChr);
// Instant kills that skip HoHpTakeDamage (suicide, kill_pl, border/world, etc.):
// show HP 0 and an overkill delta of -2147483647 for the normal hold window.
void HoHpShowOverkillDeath(CCharacter *pChr);
// Call from CPlayer::Tick: refresh / expire post-death HP line for remaining delta time.
void HoHpPlayerTick(CGameContext *pGameServer, CPlayer *pPlayer);
// Clear post-death hold (respawn / /hp off).
void HoHpClearPostDeath(CGameContext *pGameServer, CPlayer *pPlayer, bool ClearBroadcast);

#endif

#ifndef GAME_SERVER_HOAM_HP_H
#define GAME_SERVER_HOAM_HP_H

class CCharacter;
class CPlayer;

// Independent server HP system (not Teeworlds m_Health / TakeDamage).
// Fall damage and future damage sources should call HoHpTakeDamage / HoHpHeal.

int HoHpMax();
void HoHpReset(CCharacter *pChr);

// Returns true if the character died from this damage.
// Killer/Weapon are used for the kill message when HP reaches 0.
bool HoHpTakeDamage(CCharacter *pChr, int Damage, int Killer, int Weapon, bool ShowFeedback = true);
bool HoHpHeal(CCharacter *pChr, int Amount);

// Broadcast helpers (display is opt-out per player via /hp).
bool HoHpShouldBroadcast(const CPlayer *pPlayer);
void HoHpFormatBroadcast(const CCharacter *pChr, char *pBuf, int BufSize);
void HoHpSendBroadcast(CCharacter *pChr);

#endif

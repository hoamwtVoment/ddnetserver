#ifndef GAME_SERVER_HOAM_MACEHAMMER_H
#define GAME_SERVER_HOAM_MACEHAMMER_H

class CCharacter;
class CPlayer;

// Minecraft mace behavior on DDNet hammer when player has m_HoMaceHammer.
// Smash if fallen >= 1.5 tiles; damage scales with fall height (JE formula).

constexpr float HO_MACE_SMASH_MIN_BLOCKS = 1.5f;
constexpr float HO_MACE_TILE = 32.0f;
constexpr float HO_MACE_AOE_RADIUS = 2.5f * HO_MACE_TILE; // 2.5 blocks

// Track fall apex for smash height (call after physics).
void HoMaceUpdateFall(CCharacter *pChr);
float HoMaceFallBlocks(const CCharacter *pChr);
// JE smash bonus HP: +4 first 3 blocks, +2 next 5, +1 after; base 6.
int HoMaceDamage(float FallBlocks, bool Smash);
// True if this hammer hit should use mace rules for the attacker.
bool HoMaceTryHammerHit(CCharacter *pAttacker, CCharacter *pTarget);

#endif

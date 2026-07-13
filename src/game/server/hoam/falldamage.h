#ifndef GAME_SERVER_HOAM_FALLDAMAGE_H
#define GAME_SERVER_HOAM_FALLDAMAGE_H

#include <base/vmath.h>

class CCharacter;

// Impact damage sources for the independent HP system (hoam/hp).
// Enabled by ho_falldamage; amount scaled by ho_falldamage_scale (percent).
// - Fall / landing  → HO_DEATH_FALL ("fell from a high place")
// - Wall slam (X)   → HO_DEATH_KINETIC ("experienced kinetic energy")

// Downward speed (vel.y) must exceed this before landing damage is applied.
// ~20 is above a normal ground jump landing so in-place jumps do not chip HP.
constexpr float HO_FALL_VEL_THRESHOLD = 20.0f;
// Horizontal impact speed before wall slam deals damage.
constexpr float HO_WALL_VEL_THRESHOLD = 20.0f;
// Base damage per unit of excess impact speed at scale 100.
constexpr float HO_IMPACT_DAMAGE_PER_VEL = 5.0f;

void HoFallDamageReset(CCharacter *pChr);
// Call after physics Move with pre-move velocity (before collision).
void HoFallDamageAfterMove(CCharacter *pChr, vec2 PreMoveVel);

#endif

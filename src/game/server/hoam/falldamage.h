#ifndef GAME_SERVER_HOAM_FALLDAMAGE_H
#define GAME_SERVER_HOAM_FALLDAMAGE_H

class CCharacter;

// Fall damage source for the independent HP system (hoam/hp).
// Enabled by ho_falldamage; amount scaled by ho_falldamage_scale (percent).

// Downward speed (vel.y) must exceed this before damage is applied on landing.
constexpr float HO_FALL_VEL_THRESHOLD = 10.0f;
// Base damage per unit of excess fall speed at scale 100 and ho_hp 100.
constexpr float HO_FALL_DAMAGE_PER_VEL = 5.0f;

void HoFallDamageReset(CCharacter *pChr);
// Call after physics Move with pre-move vertical velocity.
void HoFallDamageAfterMove(CCharacter *pChr, float PreMoveVelY);

#endif

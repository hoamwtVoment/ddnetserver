#ifndef GAME_SERVER_HOAM_FRACTURE_H
#define GAME_SERVER_HOAM_FRACTURE_H

class CCharacter;
class CPlayer;
class CTuningParams;

// Fracture system (ho_fracture).
// New fractures only while ho_falldamage is on; closing falldamage does not clear them.
// Fall → leg (levels I–III): ground slow + weaker jump (not air strafe).
// Side wall → arm (levels I–III): delayed weapon switch (+nextweapon / +weaponN), random extra fire delay.
// Shown on HP broadcast last line with Roman numerals.

enum
{
	HO_FRACTURE_LEVEL_MAX = 3,
};

void HoFractureReset(CCharacter *pChr);
// Excess = impact speed above threshold (same as fall/wall damage).
void HoFractureOnFallImpact(CCharacter *pChr, float ExcessY);
void HoFractureOnWallImpact(CCharacter *pChr, float ExcessX);

// Apply leg ground/jump penalties to per-core tuning (after HandleTuneLayer copy).
void HoFractureApplyLegTuning(CCharacter *pChr, CTuningParams *pTuning);
// Extra fire delay in ms when arm is fractured (random in config range, scaled by level).
int HoFractureArmFireDelayMs(const CCharacter *pChr);
// Ticks of weapon-switch wind-up when arm fractured (0 if none).
int HoFractureArmSwitchDelayTicks(const CCharacter *pChr);
bool HoFractureHasAny(const CCharacter *pChr);
void HoFractureAppendBroadcastLine(CCharacter *pChr, char *pBuf, int BufSize);

#endif

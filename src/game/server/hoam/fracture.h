#ifndef GAME_SERVER_HOAM_FRACTURE_H
#define GAME_SERVER_HOAM_FRACTURE_H

class CCharacter;
class CPlayer;
class CTuningParams;

// Fracture system (ho_fracture, default off).
// New fractures only while ho_falldamage is on; closing falldamage does not clear them.
// Fall → leg: slower move, weaker jump.
// Side wall slam → arm: slower weapon switch, extra fire delay.
// Shown on HP broadcast last line when any fracture is active.

void HoFractureReset(CCharacter *pChr);
// Call from fall/wall impact path (same velocity thresholds as impact damage).
void HoFractureOnFallImpact(CCharacter *pChr);
void HoFractureOnWallImpact(CCharacter *pChr);

// Apply leg movement/jump penalties to per-core tuning (after HandleTuneLayer copy).
void HoFractureApplyLegTuning(CCharacter *pChr, CTuningParams *pTuning);
// Extra fire delay in ms when arm is fractured (0 if none).
int HoFractureArmFireDelayMs(const CCharacter *pChr);
// Ticks that must pass after a switch before the next switch (arm).
int HoFractureArmSwitchDelayTicks(const CCharacter *pChr);
bool HoFractureHasAny(const CCharacter *pChr);
// Append "\n腿骨折 · 手骨折" (or EN) into buffer if any fracture; no-op if none.
void HoFractureAppendBroadcastLine(CCharacter *pChr, char *pBuf, int BufSize);

#endif

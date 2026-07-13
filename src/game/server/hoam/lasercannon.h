#ifndef GAME_SERVER_HOAM_LASERCANNON_H
#define GAME_SERVER_HOAM_LASERCANNON_H

#include <game/server/entity.h>

class CCharacter;
class CPlayer;

// Continuous laser-cannon beam (laser slot modes). No bounce; length = ho_lasercannon_length tiles.
// Modes: free-aim cannon, or auto-lock (aim nearest-to-crosshair; first body along ray still blocks).
class CHoLaserCannonBeam final : public CEntity
{
public:
	CHoLaserCannonBeam(CGameWorld *pGameWorld, int Owner);

	void Reset() override;
	void Tick() override;
	void Snap(int SnappingClient) override;
	void SwapClients(int Client1, int Client2) override;
	int GetOwnerId() const override { return m_Owner; }

	int Owner() const { return m_Owner; }

private:
	void UpdateBeam();
	bool OwnerStillFiring();
	// Auto-lock: character nearest to aim direction within range (nullptr = free aim).
	CCharacter *FindLockTarget(CCharacter *pOwner, vec2 AimDir, float MaxRange);

	int m_Owner;
	vec2 m_From;
	vec2 m_To;
	int m_EvalTick;
	int m_LastDamageTick;
};

// Call each character weapon tick: maintain beam while holding fire in cannon mode.
void HoLaserCannonTickCharacter(CCharacter *pChr);
// True if this player has free-aim or auto-lock laser cannon selected.
bool HoLaserCannonModeActive(const CPlayer *pPlayer);
// True if auto-lock cannon mode is selected.
bool HoLaserCannonLockModeActive(const CPlayer *pPlayer);

#endif

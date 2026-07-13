#ifndef GAME_SERVER_HOAM_LASERCANNON_H
#define GAME_SERVER_HOAM_LASERCANNON_H

#include <game/server/entity.h>

class CCharacter;
class CPlayer;

// Continuous laser-cannon beam (laser slot mode). No bounce; length = ho_lasercannon_length tiles.
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

	int m_Owner;
	vec2 m_From;
	vec2 m_To;
	int m_EvalTick;
	int m_LastDamageTick;
	int m_LastSoundTick;
};

// Call each character weapon tick: maintain beam while holding fire in cannon mode.
void HoLaserCannonTickCharacter(CCharacter *pChr);
// True if this player currently has laser cannon selected as laser mode.
bool HoLaserCannonModeActive(const CPlayer *pPlayer);

#endif

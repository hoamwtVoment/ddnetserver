#ifndef GAME_SERVER_HOAM_DICE3D_H
#define GAME_SERVER_HOAM_DICE3D_H

#include <game/server/entity.h>

#include <array>
#include <optional>

class CGameContext;

class CHoDice3D final : public CEntity
{
public:
	CHoDice3D(CGameWorld *pGameWorld, vec2 Pos);
	~CHoDice3D() override;

	void Reset() override;
	void Tick() override;
	void Snap(int SnappingClient) override;

	bool HammerHit(vec2 Impulse, vec2 HammererPos);
	bool IsResting() const { return m_Resting; }
	int Result() const { return m_Result; }

private:
	using TQuaternion = std::array<float, 4>;

	void IntegrateOrientation();
	void Settle();
	int DominantFaceValue() const;

	vec2 m_Velocity{};
	vec3 m_AngularVelocity{};
	TQuaternion m_Orientation{1.0f, 0.0f, 0.0f, 0.0f};
	// 12 edges, up to 18 pips and 8 visible vertex caps. Each snapped object
	// must have a unique ID, otherwise delta snapshots cannot be reconstructed.
	std::array<std::optional<int>, 38> m_aSnapIds;
	int m_StillTicks = 0;
	int m_Result = 1;
	bool m_Resting = false;
	bool m_HasBeenThrown = false;
	bool m_ResultReported = false;
};

void HoRegisterDice3DCommands(CGameContext *pGameServer);
int HoDice3DHammerHit(CGameContext *pGameServer, vec2 HammerPos, vec2 HammererPos);

#endif // GAME_SERVER_HOAM_DICE3D_H

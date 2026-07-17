#ifndef GAME_SERVER_HOAM_RIGIDBODY_H
#define GAME_SERVER_HOAM_RIGIDBODY_H

#include <game/server/entity.h>

#include <array>
#include <optional>

class CCharacter;
class CGameContext;

class CHoRigidBody final : public CEntity
{
public:
	enum class EKind
	{
		BLOCK,
		DICE,
	};

	static constexpr int MAX_VERTICES = 32;
	static constexpr int MAX_ID_DIGITS = 6;

	CHoRigidBody(CGameWorld *pGameWorld, EKind Kind, vec2 Pos, int Sides, float Size, float Mass);
	~CHoRigidBody() override;

	void Reset() override;
	void Tick() override;
	void TickDeferred() override;
	void Snap(int SnappingClient) override;

	bool IntersectSegment(vec2 From, vec2 To, vec2 *pHit, vec2 *pNormal = nullptr) const;
	void ApplyImpulse(vec2 Impulse, vec2 WorldPoint, bool Wake = true);
	void ApplyCentralImpulse(vec2 Impulse);
	float Radius() const { return m_Radius; }
	float Size() const { return m_Size; }
	float Mass() const { return m_Mass; }
	int BodyId() const { return m_BodyId; }
	int Sides() const { return m_Sides; }
	EKind Kind() const { return m_Kind; }

private:
	void BuildVertices(std::array<vec2, MAX_VERTICES> &aVertices) const;
	bool ResolveMapCollision();
	void ResolveCharacterCollision(CCharacter *pChr);
	void UpdateHooks();
	void ReportDiceResult();
	float InverseInertia() const;

	EKind m_Kind;
	int m_BodyId;
	int m_Sides;
	float m_Size;
	float m_Radius;
	float m_Mass;
	float m_Angle;
	float m_AngularVelocity;
	vec2 m_Velocity;
	int m_StillTicks;
	bool m_DiceResultReported;
	std::array<std::optional<int>, MAX_VERTICES> m_aSnapIds;
	std::array<std::array<std::optional<int>, 7>, MAX_ID_DIGITS> m_aaIdSnapIds;
	std::array<bool, MAX_CLIENTS> m_aHooked;
	std::array<vec2, MAX_CLIENTS> m_aHookLocalPos;
};

void HoRegisterRigidBodyCommands(CGameContext *pGameServer);

// Returns true and applies the impulse when the segment hits the closest rigid body.
bool HoRigidBodyWeaponHit(CGameContext *pGameServer, vec2 From, vec2 To, vec2 Impulse, int Weapon, vec2 *pHit = nullptr);
void HoRigidBodyExplosion(CGameContext *pGameServer, vec2 Pos, float Radius, float Strength);
bool HoRigidBodyHammerHit(CGameContext *pGameServer, vec2 Pos, vec2 Direction, float Strength);

#endif // GAME_SERVER_HOAM_RIGIDBODY_H

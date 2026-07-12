#ifndef GAME_SERVER_HOAM_PORTAL_H
#define GAME_SERVER_HOAM_PORTAL_H

#include <game/server/entity.h>

enum EHoPortalDirection
{
	HO_PORTAL_LEFT = 0,
	HO_PORTAL_UP,
	HO_PORTAL_RIGHT,
	HO_PORTAL_DOWN,
};

class CHoPortal final : public CEntity
{
public:
	CHoPortal(CGameWorld *pGameWorld, int Owner, int PortalIndex);

	void Reset() override;
	void Snap(int SnappingClient) override;
	void SwapClients(int Client1, int Client2) override;
	int GetOwnerId() const override { return m_Owner; }

	void Place(vec2 SurfaceCenter, int Direction);
	void Deactivate() { m_Active = false; }
	bool Active() const { return m_Active; }
	int Owner() const { return m_Owner; }
	int PortalIndex() const { return m_PortalIndex; }
	int Direction() const { return m_Direction; }
	vec2 Center() const { return m_Pos; }
	vec2 Normal() const;
	vec2 Tangent() const;
	bool IsIn(vec2 Pos, float *pTangentOffset = nullptr) const;

private:
	int m_Owner;
	int m_PortalIndex;
	int m_Direction;
	bool m_Active;
	vec2 m_From;
	vec2 m_To;
};

#endif

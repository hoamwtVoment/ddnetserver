#ifndef GAME_SERVER_HOAM_GOJO_H
#define GAME_SERVER_HOAM_GOJO_H

#include <game/server/entity.h>

class CCharacter;
class CGameContext;
class CPlayer;
class CTuningParams;

// Shotgun-slot techniques for ho_gojo players (F3 select).
// 苍 Blue: charge, release, then steers toward cursor (attraction).
// 赫 Red: charge, release as repulsion projectile.
// 茈 Purple: fixed size; 赫+苍 merge above head then launch.
// 无下限: F3 toggle only (not a fireable mode).

class CHoGojoBlue final : public CEntity
{
public:
	CHoGojoBlue(CGameWorld *pGameWorld, int Owner, vec2 Pos, float Radius);

	void Reset() override;
	void Tick() override;
	void Snap(int SnappingClient) override;
	void SwapClients(int Client1, int Client2) override;
	int GetOwnerId() const override { return m_Owner; }

	bool IsControlled() const { return m_Controlled; }
	// Detach from cursor; keep flying with last follow velocity.
	void ReleaseControl();

private:
	int m_Owner;
	float m_Radius;
	int m_Life;
	int m_LastDamageTick;
	int m_SpawnTick;
	bool m_Controlled;
	vec2 m_Vel;
};

class CHoGojoProjectile final : public CEntity
{
public:
	enum
	{
		TYPE_RED = 0,
		TYPE_PURPLE = 1,
	};

	CHoGojoProjectile(CGameWorld *pGameWorld, int Owner, vec2 Pos, vec2 Dir, float Radius, int Type);

	void Reset() override;
	void Tick() override;
	void Snap(int SnappingClient) override;
	void SwapClients(int Client1, int Client2) override;
	int GetOwnerId() const override { return m_Owner; }

private:
	void ApplyAoE(bool FinalBurst);

	int m_Owner;
	vec2 m_Dir;
	float m_Radius;
	int m_Type;
	int m_Life;
	int m_SpawnTick;
};

// Visual-only orb for 茈 charge fusion (苍 left / 赫 right → merge).
class CHoGojoFusionOrb final : public CEntity
{
public:
	enum
	{
		STYLE_BLUE = 0,
		STYLE_RED = 1,
	};

	CHoGojoFusionOrb(CGameWorld *pGameWorld, int Owner, int Style);

	void Reset() override;
	void Tick() override;
	void Snap(int SnappingClient) override;
	void SwapClients(int Client1, int Client2) override;
	int GetOwnerId() const override { return m_Owner; }

	void SetVisual(vec2 Pos, float Radius);
	int Style() const { return m_Style; }

private:
	int m_Owner;
	int m_Style;
	float m_Radius;
	int m_SpawnTick;
};

// True if player owns Gojo kit (rcon ho_gojo).
bool HoGojoOwned(const CPlayer *pPlayer);
// True if Unlimited Void domain is expanded.
bool HoGojoVoidActive(const CPlayer *pPlayer);
// True if void blocks effects from FromCid (others + world -1). Own weapons (FromCid==self) pass.
bool HoGojoVoidBlocksExternal(const CCharacter *pVictim, int FromCid);
// True if active shotgun mode is 苍/赫/茈 (charge-release techniques).
bool HoGojoTechniqueMode(const CPlayer *pPlayer);
int HoGojoShotgunMode(const CPlayer *pPlayer);
float HoGojoVoidRadius();

// Clip a shot segment against foreign Unlimited Void spheres (own void ignored).
// Returns true if clipped; *pHit is the stop point on the sphere surface.
bool HoGojoVoidClipSegment(CGameContext *pGameServer, vec2 From, vec2 To, int ShooterCid, vec2 *pHit);

// Infinity: movement gets slower the closer to a foreign void shell (cannot enter).
// Returns the allowed end position; *pFactor is remaining speed 0..1; *pVoidCid nearest void or -1.
vec2 HoGojoVoidSoftMove(CGameContext *pGameServer, vec2 From, vec2 To, int IgnoreCid, float *pFactor = nullptr, int *pVoidCid = nullptr);
// Outer influence radius (soft zone starts here).
float HoGojoVoidOuterRadius();

// Per-character: charge, purple merge, movement slow, void domain + barrier.
void HoGojoTickCharacter(CCharacter *pChr);
// Apply charge movement slow to core tuning.
void HoGojoApplyChargeTuning(CCharacter *pChr, CTuningParams *pTuning);
// Toggle Unlimited Void (called from weapon select; not a fireable mode).
void HoGojoToggleUnlimitedVoid(CGameContext *pGameServer, CPlayer *pPlayer);
// Destroy active blue if any.
void HoGojoClearBlue(CPlayer *pPlayer);
// Destroy 茈 charge fusion orbs.
void HoGojoClearFusionOrbs(CPlayer *pPlayer);
// Death / remove ownership cleanup.
void HoGojoOnDeath(CPlayer *pPlayer);

#endif

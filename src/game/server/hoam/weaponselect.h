#ifndef GAME_SERVER_HOAM_WEAPONSELECT_H
#define GAME_SERVER_HOAM_WEAPONSELECT_H

#include <game/server/entity.h>

class CCharacter;
class CGameContext;
class CPlayer;

// Modes for a weapon slot. Index 0 is always vanilla.
enum
{
	HO_WPNMODE_VANILLA = 0,
	// Hammer extras
	HO_WPNMODE_HAMMER_MACE = 1,
	// Laser extras (existing portal modes as selectable styles)
	HO_WPNMODE_LASER_PORTAL1 = 1,
	HO_WPNMODE_LASER_PORTAL2 = 2,
	// Ninja extras
	HO_WPNMODE_NINJA_CONTROLLER = 1, // gravity grab controller
};

enum
{
	HO_WEAPONSELECT_MAX_OPTIONS = 8,
};

// Floating option shown while weapon-select menu is open (vanilla client: pickup snap).
class CHoWeaponSelectOption final : public CEntity
{
public:
	CHoWeaponSelectOption(CGameWorld *pGameWorld, int OwnerId, int OptionIndex, int WeaponSlot, int ModeId, int PickupType, int PickupSubtype);

	void Reset() override;
	void Tick() override;
	void Snap(int SnappingClient) override;

	int OwnerId() const { return m_OwnerId; }
	int OptionIndex() const { return m_OptionIndex; }
	int WeaponSlot() const { return m_WeaponSlot; }
	int ModeId() const { return m_ModeId; }

	void SetWorldPos(vec2 Pos) { m_Pos = Pos; }

private:
	int m_OwnerId;
	int m_OptionIndex;
	int m_WeaponSlot;
	int m_ModeId;
	int m_PickupType;
	int m_PickupSubtype;
};

// Menu API
bool HoWeaponSelectIsOpen(const CPlayer *pPlayer);
void HoWeaponSelectClose(CGameContext *pGameServer, CPlayer *pPlayer, bool Silent = false);
// Toggle: open if closed, close if open. Returns true if handled.
bool HoWeaponSelectToggle(CGameContext *pGameServer, CPlayer *pPlayer);
// Per-tick: follow owner, hover broadcast, fire to select.
void HoWeaponSelectTickPlayer(CGameContext *pGameServer, CPlayer *pPlayer);
// Call from FireWeapon: if menu open, try select and block normal fire. Returns true if fire consumed.
bool HoWeaponSelectOnFire(CCharacter *pChr);
// Active mode for a slot (0 = vanilla).
int HoWeaponSelectActiveMode(const CPlayer *pPlayer, int WeaponSlot);
void HoWeaponSelectSetActiveMode(CPlayer *pPlayer, int WeaponSlot, int ModeId);
// Fill options for current active weapon; returns count.
int HoWeaponSelectBuildOptions(const CPlayer *pPlayer, int WeaponSlot, int *pModeIds, int *pPickupTypes, int *pPickupSubtypes, const char **ppNames, const char **ppDescs, int MaxOptions);

#endif

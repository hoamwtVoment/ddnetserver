#ifndef GAME_SERVER_HOAM_DEATHMSG_H
#define GAME_SERVER_HOAM_DEATHMSG_H

class CCharacter;
class CGameContext;

// Minecraft-style death chat. Gated by ho_deathmsg (default on).
enum
{
	HO_DEATH_NONE = 0,
	// Landing after a long fall
	HO_DEATH_FALL,
	// Horizontal wall slam at high speed (MC elytra-into-wall style)
	HO_DEATH_KINETIC,
	// Left the game layer (map border / GameLayerClipped)
	HO_DEATH_BORDER,
	// TILE_DEATH (spikes / 刺)
	HO_DEATH_SPIKE,
	// Minecraft mace smash kill
	HO_DEATH_MACE,
	// Gojo techniques
	HO_DEATH_GOJO_BLUE,
	HO_DEATH_GOJO_RED,
	HO_DEATH_GOJO_PURPLE,
};

// Call from Die() when a kill message is sent / player actually dies.
void HoDeathMsgOnDie(CGameContext *pGameServer, CCharacter *pVictim, int Killer, int Weapon);

#endif

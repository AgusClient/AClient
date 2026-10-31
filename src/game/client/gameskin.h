#ifndef GAME_CLIENT_GAMESKIN_H
#define GAME_CLIENT_GAMESKIN_H

#include <engine/graphics.h>

struct SClientGameSkin
{
	// health armor hud
	IGraphics::CTextureHandle m_SpriteHealthFull;
	IGraphics::CTextureHandle m_SpriteHealthEmpty;
	IGraphics::CTextureHandle m_SpriteArmorFull;
	IGraphics::CTextureHandle m_SpriteArmorEmpty;

	// cursors
	IGraphics::CTextureHandle m_SpriteWeaponHammerCursor;
	IGraphics::CTextureHandle m_SpriteWeaponGunCursor;
	IGraphics::CTextureHandle m_SpriteWeaponShotgunCursor;
	IGraphics::CTextureHandle m_SpriteWeaponGrenadeCursor;
	IGraphics::CTextureHandle m_SpriteWeaponNinjaCursor;
	IGraphics::CTextureHandle m_SpriteWeaponLaserCursor;

	IGraphics::CTextureHandle m_aSpriteWeaponCursors[6];

	// weapons and hook
	IGraphics::CTextureHandle m_SpriteHookChain;
	IGraphics::CTextureHandle m_SpriteHookHead;
	IGraphics::CTextureHandle m_SpriteWeaponHammer;
	IGraphics::CTextureHandle m_SpriteWeaponGun;
	IGraphics::CTextureHandle m_SpriteWeaponShotgun;
	IGraphics::CTextureHandle m_SpriteWeaponGrenade;
	IGraphics::CTextureHandle m_SpriteWeaponNinja;
	IGraphics::CTextureHandle m_SpriteWeaponLaser;

	IGraphics::CTextureHandle m_aSpriteWeapons[6];

	// particles
	IGraphics::CTextureHandle m_aSpriteParticles[9];

	// stars
	IGraphics::CTextureHandle m_aSpriteStars[3];

	// projectiles
	IGraphics::CTextureHandle m_SpriteWeaponGunProjectile;
	IGraphics::CTextureHandle m_SpriteWeaponShotgunProjectile;
	IGraphics::CTextureHandle m_SpriteWeaponGrenadeProjectile;
	IGraphics::CTextureHandle m_SpriteWeaponHammerProjectile;
	IGraphics::CTextureHandle m_SpriteWeaponNinjaProjectile;
	IGraphics::CTextureHandle m_SpriteWeaponLaserProjectile;

	IGraphics::CTextureHandle m_aSpriteWeaponProjectiles[6];

	// muzzles
	IGraphics::CTextureHandle m_aSpriteWeaponGunMuzzles[3];
	IGraphics::CTextureHandle m_aSpriteWeaponShotgunMuzzles[3];
	IGraphics::CTextureHandle m_aaSpriteWeaponNinjaMuzzles[3];

	IGraphics::CTextureHandle m_aaSpriteWeaponsMuzzles[6][3];

	// pickups
	IGraphics::CTextureHandle m_SpritePickupHealth;
	IGraphics::CTextureHandle m_SpritePickupFreeze;
	IGraphics::CTextureHandle m_SpritePickupArmor;
	IGraphics::CTextureHandle m_SpritePickupArmorShotgun;
	IGraphics::CTextureHandle m_SpritePickupArmorGrenade;
	IGraphics::CTextureHandle m_SpritePickupArmorNinja;
	IGraphics::CTextureHandle m_SpritePickupArmorLaser;
	IGraphics::CTextureHandle m_SpritePickupGrenade;
	IGraphics::CTextureHandle m_SpritePickupShotgun;
	IGraphics::CTextureHandle m_SpritePickupLaser;
	IGraphics::CTextureHandle m_SpritePickupNinja;
	IGraphics::CTextureHandle m_SpritePickupGun;
	IGraphics::CTextureHandle m_SpritePickupHammer;

	IGraphics::CTextureHandle m_aSpritePickupWeapons[6];
	IGraphics::CTextureHandle m_aSpritePickupWeaponArmor[4];

	// flags
	IGraphics::CTextureHandle m_SpriteFlagBlue;
	IGraphics::CTextureHandle m_SpriteFlagRed;

	// ninja bar (0.7)
	IGraphics::CTextureHandle m_SpriteNinjaBarFullLeft;
	IGraphics::CTextureHandle m_SpriteNinjaBarFull;
	IGraphics::CTextureHandle m_SpriteNinjaBarEmpty;
	IGraphics::CTextureHandle m_SpriteNinjaBarEmptyRight;

	bool IsSixup() const
	{
		return m_SpriteNinjaBarFullLeft.IsValid();
	}
};

#endif // GAME_CLIENT_GAMESKIN_H

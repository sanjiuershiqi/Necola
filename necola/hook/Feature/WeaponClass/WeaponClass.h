#pragma once

// Port of SKT_Weapon_ClassifyName (0x10114A60): a strstr() precedence table that maps
// a weapon/entity class-name string to a small integer id. skeeto uses it for melee
// decisions (3 = melee, 4 = chainsaw), icons and stats grouping.
namespace WeaponClass {

enum Id {
	Unknown = 0,
	Pistol = 1,      // also Magnum = 2
	Magnum = 2,
	Melee = 3,       // generic melee (bat, machete, ...)
	Chainsaw = 4,
	Smg = 5,         // also Silenced = 6, MP5 = 7
	Silenced = 6,
	Mp5 = 7,
	PumpShotgun = 8, // also Chrome = 9, AutoShotgun = 10, Spas = 11
	Chrome = 9,
	AutoShotgun = 10,
	Spas = 11,
	Rifle = 12,      // M16/AK variants
	Ak47 = 13,
	DesertScar = 14,
	Sg552 = 15,
	M60 = 16,
	HuntingRifle = 17,
	MilitarySniper = 18,
	Scout = 19,
	Awp = 20,
	GrenadeLauncher = 21,
	Molotov = 22,
	PipeBomb = 23,
	VomitJar = 24,
	FirstAid = 25,
	Defib = 26,
	UpgradeExplosive = 27,
	UpgradeIncendiary = 28,
	PainPills = 29,
	Adrenaline = 30,
	Prop = 31,       // GasCan, Gnome, Cola, Firework, Oxygen, Propane
};

int ClassifyName(const char* name);
bool IsMelee(int id);      // Melee or Chainsaw
bool IsFirearm(int id);    // 1..21
bool IsThrowable(int id);  // Molotov / PipeBomb / VomitJar

} // namespace WeaponClass

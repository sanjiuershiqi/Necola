#include "WeaponClass.h"

#include <cstring>

namespace {

// skeeto's precedence matters: HuntingRifle is tested before anything containing
// "Rifle", MG/shotgun families before the generic SMG/Rifle checks, items first.
bool Has(const char* s, const char* needle) {
	return s && needle && std::strstr(s, needle) != nullptr;
}

bool IsHuntingRifleName(const char* s) {
	return Has(s, "Hunting") || Has(s, "hunting") || Has(s, "Sniper_Hunting");
}

} // namespace

namespace WeaponClass {

int ClassifyName(const char* name) {
	if (!name || !*name) return Unknown;
	if (IsHuntingRifleName(name)) return HuntingRifle;

	if (Has(name, "Molotov") || Has(name, "molotov")) return Molotov;
	if (Has(name, "PipeBomb") || Has(name, "Pipebomb") || Has(name, "pipe_bomb") ||
		Has(name, "pipebomb") || Has(name, "Pipe_bomb")) return PipeBomb;
	if (Has(name, "Vomit") || Has(name, "vomitjar") || Has(name, "VomitJar") ||
		Has(name, "Bile") || Has(name, "bile_flask")) return VomitJar;
	if (Has(name, "Defib") || Has(name, "defibrillator")) return Defib;
	if (Has(name, "FirstAid") || Has(name, "first_aid") || Has(name, "Medkit") ||
		Has(name, "MedKit")) return FirstAid;
	if (Has(name, "Incendiary") || Has(name, "upgradepack_incendiary")) return UpgradeIncendiary;
	if (Has(name, "Explosive") || Has(name, "upgradepack_explosive")) return UpgradeExplosive;
	if (Has(name, "Adrenaline") || Has(name, "adrenaline")) return Adrenaline;
	if (Has(name, "PainPills") || Has(name, "pain_pills") || Has(name, "Pain_pills") ||
		Has(name, "Pills")) return PainPills;

	if (Has(name, "AK47") || Has(name, "Ak47")) return Ak47;
	if (Has(name, "SG552") || Has(name, "Sg552")) return Sg552;
	if (Has(name, "Desert") || Has(name, "SCAR") || Has(name, "Scar")) return DesertScar;
	if (Has(name, "M60") || Has(name, "m60")) return M60;
	if (Has(name, "GrenadeLauncher") || Has(name, "grenade_launcher")) return GrenadeLauncher;
	if (Has(name, "Sniper_AWP") || Has(name, "AWP") || Has(name, "awp")) return Awp;
	if (Has(name, "Scout") || Has(name, "scout")) return Scout;
	if (Has(name, "Military") || Has(name, "sniper_military")) return MilitarySniper;
	if (Has(name, "MP5") || Has(name, "Mp5") || Has(name, "mp5")) return Mp5;
	if (Has(name, "Silenced") || Has(name, "smg_silenced")) return Silenced;
	if (Has(name, "Spas") || Has(name, "SPAS") || Has(name, "shotgun_spas")) return Spas;
	if (Has(name, "Chrome") || Has(name, "shotgun_chrome")) return Chrome;
	if (Has(name, "AutoShotgun") || Has(name, "autoshotgun") || Has(name, "Autoshotgun")) return AutoShotgun;
	if (Has(name, "Pump") || Has(name, "pumpshotgun")) return PumpShotgun;
	if (Has(name, "Magnum") || Has(name, "Pistol_Magnum") || Has(name, "pistol_magnum")) return Magnum;
	if (Has(name, "Chainsaw") || Has(name, "chainsaw")) return Chainsaw;
	if (Has(name, "Melee") || Has(name, "melee")) return Melee;
	if (Has(name, "SubMachine") || Has(name, "SMG") || Has(name, "Smg") || Has(name, "smg")) return Smg;
	if ((Has(name, "Rifle") || Has(name, "rifle") || Has(name, "M16") || Has(name, "m16")) &&
		!IsHuntingRifleName(name)) {
		return Rifle;
	}
	if (Has(name, "Pistol") || Has(name, "pistol")) return Pistol;
	if (Has(name, "GasCan") || Has(name, "Gnome") || Has(name, "Cola") ||
		Has(name, "Firework") || Has(name, "Oxygen") || Has(name, "Propane")) return Prop;
	return Unknown;
}

bool IsMelee(int id) {
	return id == Melee || id == Chainsaw;
}

bool IsFirearm(int id) {
	return id >= Pistol && id <= GrenadeLauncher;
}

bool IsThrowable(int id) {
	return id == Molotov || id == PipeBomb || id == VomitJar;
}

} // namespace WeaponClass

#include "MenuManager.h"
#include "../../Vars.h"


void InGameMenu::EngineDrawFilledRect(const int x1, const int y1, const int x2, const int y2, const Color& color) {
	I::MatSystemSurface->DrawSetColor(color);
	I::MatSystemSurface->DrawFilledRect(x1, y1, x2, y2);
}


void InGameMenu::EngineDrawOutlinedRect(const int x1, const int y1, const int x2, const int y2, const Color& color){
	I::MatSystemSurface->DrawSetColor(color);
	I::MatSystemSurface->DrawOutlinedRect(x1, y1, x2, y2);
}


void InGameMenu::EngineDrawText(const char* text, const int x, const int y, const Color& color) {
	wchar_t wstr[1024] = { '\0' };

	MultiByteToWideChar(CP_UTF8, 0, text, -1, wstr, 1024);
	I::MatSystemSurface->DrawSetTextFont(inGameMenuFONT);
	I::MatSystemSurface->DrawSetTextColor(color);
	I::MatSystemSurface->DrawSetTextPos(x, y);
	I::MatSystemSurface->DrawPrintText(wstr, wcslen(wstr));

}


void InGameMenu::EngineDrawLine(const int x, const int y, const int x1, const int y1, const Color& color)
{
	I::MatSystemSurface->DrawSetColor(color);
	I::MatSystemSurface->DrawLine(x, y, x1, y1);
}

void InGameMenu::ToggleNecolaMenu(int* a1) {
	if(I::EngineClient && I::EngineClient->IsConnected() && I::EngineClient->IsInGame()) {
		F::MenuMgr.Toggle();

	}

}


void InGameMenu::InitMenuFonts() {
	if(I::MatSystemSurface) {
		inGameMenuFONT = I::MatSystemSurface->CreateFont();
		I::MatSystemSurface->SetFontGlyphSet(inGameMenuFONT, "Microsoft YaHei", 25, 700, 0, 0, FONTFLAG_OUTLINE, 0, 0);
	}
}

void InGameMenu::InitConfigSwitches() {

	// SequenceModify switches (required by ADS layer sequence tracking)
	PrependSwitchToMenu("seq", "忽略单喷开火/推击的多动作", G::Vars.ignoreShotgunSequence, [](bool enabled) {
		G::Vars.ignoreShotgunSequence = enabled;
		nlohmann::json doc = NecolaConfig::LoadConfig();
		doc["SequenceModify"]["IgnoreShotgunSequence"] = enabled;
		NecolaConfig::SaveConfig(doc);
	});
	PrependSwitchToMenu("seq", "服务器多动作序列修正", G::Vars.animSequenceModify, [](bool enabled) {
		G::Vars.animSequenceModify = enabled;
		nlohmann::json doc = NecolaConfig::LoadConfig();
		doc["SequenceModify"]["AnimSequenceModify"] = enabled;
		NecolaConfig::SaveConfig(doc);
	});

	// Sync ADS menu switch states from loaded config
	{
		auto adsMenu = FindMenuById("ads");
		if (adsMenu) {
			adsMenu->setSwitchStateByName("启用ADS", G::Vars.enableAdsSupport);

			auto crosshairModeLabel = [](int mode) -> std::string {
				switch (mode) {
					case 0:  return "关";
					case 1:  return "开";
					case 2:  return "自定义";
					default: return "关";
				}
			};
			adsMenu->updateSubMenuItemName("ads_crosshair", "ADS状态隐藏准星 [" + crosshairModeLabel(G::Vars.adsHideCrosshairMode) + "]");

			auto crosshairMenu = FindMenuById("ads_crosshair");
			if (crosshairMenu) {
				crosshairMenu->setSwitchStateByName("手枪", G::Vars.adsHideCrosshairPistol);
				crosshairMenu->setSwitchStateByName("双持手枪", G::Vars.adsHideCrosshairPistolDual);
				crosshairMenu->setSwitchStateByName("UZI", G::Vars.adsHideCrosshairUzi);
				crosshairMenu->setSwitchStateByName("木喷", G::Vars.adsHideCrosshairPumpShotgun);
				crosshairMenu->setSwitchStateByName("连喷", G::Vars.adsHideCrosshairAutoShotgun);
				crosshairMenu->setSwitchStateByName("M16", G::Vars.adsHideCrosshairM16A1);
				crosshairMenu->setSwitchStateByName("15连", G::Vars.adsHideCrosshairHuntingRifle);
				crosshairMenu->setSwitchStateByName("MAC10", G::Vars.adsHideCrosshairMac10);
				crosshairMenu->setSwitchStateByName("铁喷", G::Vars.adsHideCrosshairChromeShotgun);
				crosshairMenu->setSwitchStateByName("SCAR", G::Vars.adsHideCrosshairScar);
				crosshairMenu->setSwitchStateByName("30连", G::Vars.adsHideCrosshairMilitarySniper);
				crosshairMenu->setSwitchStateByName("SPAS", G::Vars.adsHideCrosshairSpas);
				crosshairMenu->setSwitchStateByName("榴弹发射器", G::Vars.adsHideCrosshairGrenadeLauncher);
				crosshairMenu->setSwitchStateByName("AK47", G::Vars.adsHideCrosshairAK47);
				crosshairMenu->setSwitchStateByName("沙鹰", G::Vars.adsHideCrosshairDeagle);
				crosshairMenu->setSwitchStateByName("MP5", G::Vars.adsHideCrosshairMP5);
				crosshairMenu->setSwitchStateByName("SG552", G::Vars.adsHideCrosshairSSG552);
				crosshairMenu->setSwitchStateByName("AWP", G::Vars.adsHideCrosshairAWP);
				crosshairMenu->setSwitchStateByName("SCOUT", G::Vars.adsHideCrosshairScout);
				crosshairMenu->setSwitchStateByName("M60", G::Vars.adsHideCrosshairM60);
			}

			auto scopeLabel = [](int mode) -> std::string {
				switch (mode) {
					case 0:  return "关闭";
					case 1:  return "仅ADS";
					case 2:  return "混合";
					default: return "关闭";
				}
			};
			auto scopeWeaponMenu = FindMenuById("ads_scope_weapons");
			if (scopeWeaponMenu) {
				scopeWeaponMenu->updateSubMenuItemName("ads_ssg552", "SG552 ADS设置 [" + scopeLabel(G::Vars.adsScopeSSG552) + "]");
				scopeWeaponMenu->updateSubMenuItemName("ads_hunting_rifle", "一代连狙ADS设置 [" + scopeLabel(G::Vars.adsScopeHuntingRifle) + "]");
				scopeWeaponMenu->updateSubMenuItemName("ads_military_sniper", "二代连狙ADS设置 [" + scopeLabel(G::Vars.adsScopeMilitarySniper) + "]");
				scopeWeaponMenu->updateSubMenuItemName("ads_scout", "SCOUT ADS设置 [" + scopeLabel(G::Vars.adsScopeScout) + "]");
				scopeWeaponMenu->updateSubMenuItemName("ads_awp", "AWP ADS设置 [" + scopeLabel(G::Vars.adsScopeAWP) + "]");
			}
		}
	}
}

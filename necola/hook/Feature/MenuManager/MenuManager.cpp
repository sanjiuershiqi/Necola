#include "MenuManager.h"
#include "../../Vars.h"
#include "../InspectInitiative/InspectInitiative.h"


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
		I::MatSystemSurface->SetFontGlyphSet(inGameMenuFONT, "Microsoft YaHei", 22, 600, 0, 0, FONTFLAG_OUTLINE, 0, 0);
	}
}

void InGameMenu::InitConfigSwitches() {

	// SequenceModify switches (required by ADS layer sequence tracking)
	PrependSwitchToMenu("seq", "忽略单喷开火/推击的多动作", G::Vars.ignoreShotgunSequence, [](bool enabled) {
		G::Vars.ignoreShotgunSequence = enabled;
		nlohmann::json doc = NecolaConfig::LoadConfig();
		NecolaConfig::EnsureSectionObject(doc, "SequenceModify")["IgnoreShotgunSequence"] = enabled;
		NecolaConfig::SaveConfig(doc);
		F::MenuMgr.RefreshRootLabels();
	});
	PrependSwitchToMenu("seq", "服务器多动作序列修正", G::Vars.animSequenceModify, [](bool enabled) {
		G::Vars.animSequenceModify = enabled;
		nlohmann::json doc = NecolaConfig::LoadConfig();
		NecolaConfig::EnsureSectionObject(doc, "SequenceModify")["AnimSequenceModify"] = enabled;
		NecolaConfig::SaveConfig(doc);
		F::MenuMgr.RefreshRootLabels();
	});

	// Sync ADS menu switch states from loaded config
	{
		auto adsMenu = FindMenuById("ads");
	if (adsMenu) {
		adsMenu->setSwitchStateByName("启用ADS", G::Vars.enableAdsSupport);
		adsMenu->setSwitchStateByName("主动检视", G::Vars.openInspect);
		adsMenu->setSwitchStateByName("检视忽略弹药限制", G::Vars.inspectIgnoreAmmo);

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
				crosshairMenu->setSwitchStateByName("一代连喷", G::Vars.adsHideCrosshairAutoShotgun);
				crosshairMenu->setSwitchStateByName("M16", G::Vars.adsHideCrosshairM16A1);
				crosshairMenu->setSwitchStateByName("一代连狙", G::Vars.adsHideCrosshairHuntingRifle);
				crosshairMenu->setSwitchStateByName("MAC10", G::Vars.adsHideCrosshairMac10);
				crosshairMenu->setSwitchStateByName("铁喷", G::Vars.adsHideCrosshairChromeShotgun);
				crosshairMenu->setSwitchStateByName("SCAR", G::Vars.adsHideCrosshairScar);
				crosshairMenu->setSwitchStateByName("二代连狙", G::Vars.adsHideCrosshairMilitarySniper);
				crosshairMenu->setSwitchStateByName("二代连喷", G::Vars.adsHideCrosshairSpas);
				crosshairMenu->setSwitchStateByName("榴弹发射器", G::Vars.adsHideCrosshairGrenadeLauncher);
				crosshairMenu->setSwitchStateByName("AK47", G::Vars.adsHideCrosshairAK47);
				crosshairMenu->setSwitchStateByName("马格南", G::Vars.adsHideCrosshairDeagle);
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
			auto setScopeTitle = [this, scopeLabel](const char* menuId, const char* weaponName, int mode) {
				auto menu = FindMenuById(menuId);
				if (menu) menu->setTitle(std::string(weaponName) + " ADS设置 / " + scopeLabel(mode));
			};
			setScopeTitle("ads_ssg552", "SG552", G::Vars.adsScopeSSG552);
			setScopeTitle("ads_hunting_rifle", "一代连狙", G::Vars.adsScopeHuntingRifle);
			setScopeTitle("ads_military_sniper", "二代连狙", G::Vars.adsScopeMilitarySniper);
			setScopeTitle("ads_scout", "SCOUT", G::Vars.adsScopeScout);
			setScopeTitle("ads_awp", "AWP", G::Vars.adsScopeAWP);
		}
	}

	auto toolsMenu = FindMenuById("tools");
	if (toolsMenu) {
		toolsMenu->setSwitchStateByName("击杀反馈日志", G::Vars.killFeedbackLog);
		toolsMenu->setSwitchStateByName("ADS详细日志", G::Vars.adsLog);
		toolsMenu->setSwitchStateByName("序列详细日志", G::Vars.sequenceLog);
	}
	auto killFeedbackMenu = FindMenuById("kill_feedback");
	if (killFeedbackMenu) {
		killFeedbackMenu->setSwitchStateByName("启用击杀提示", G::Vars.killFeedbackEnabled);
		killFeedbackMenu->setSwitchStateByName("普通感染者", G::Vars.killFeedbackCommon);
		killFeedbackMenu->setSwitchStateByName("特殊感染者（含Witch）", G::Vars.killFeedbackSpecial);
		killFeedbackMenu->setSwitchStateByName("视觉效果（图标/粒子）", G::Vars.killFeedbackIcon);
		killFeedbackMenu->setSwitchStateByName("击杀音效", G::Vars.killFeedbackSound);
	}
	auto hitTipMenu = FindMenuById("kill_hit_tip");
	if (hitTipMenu) {
		hitTipMenu->setSwitchStateByName("SI视觉优先", G::Vars.killFeedbackSiDedicated);
		hitTipMenu->setSwitchStateByName("SI音效优先", G::Vars.killFeedbackSiSound);
	}
	auto killMethodMenu = FindMenuById("kill_feedback_methods");
	if (killMethodMenu) {
		for (const auto& [label, setting] : KillFeedbackMethodSwitches()) {
			killMethodMenu->setSwitchStateByName(label, *setting);
		}
	}
	auto killSpecialMenu = FindMenuById("kill_feedback_specials");
	if (killSpecialMenu) {
		for (const auto& [label, setting] : KillFeedbackSpecialSwitches()) {
			killSpecialMenu->setSwitchStateByName(label, *setting);
		}
	}
	auto hitFeedbackMenu = FindMenuById("hit_feedback");
	if (hitFeedbackMenu) {
		hitFeedbackMenu->setSwitchStateByName("启用伤害数字与准星", G::Vars.hitFeedbackEnabled);
		hitFeedbackMenu->setSwitchStateByName("伤害数字", G::Vars.hitFeedbackNumbers);
		hitFeedbackMenu->setSwitchStateByName("命中标记", G::Vars.hitFeedbackHitMarker);
		hitFeedbackMenu->setSwitchStateByName("普通感染者伤害数字", G::Vars.hitFeedbackCommon);
	}
	RefreshRootLabels();
	RefreshKillFeedbackLabels();
	RefreshCrosshairModeUI();
	RefreshAppearanceLabels();
}

int InGameMenu::EngineGetTextWidth(const char* text) const {
	if (!text || !I::MatSystemSurface || !inGameMenuFONT) return 0;
	wchar_t wstr[1024] = {};
	MultiByteToWideChar(CP_UTF8, 0, text, -1, wstr, 1024);
	int width = 0;
	int height = 0;
	I::MatSystemSurface->GetTextSize(inGameMenuFONT, wstr, width, height);
	return width;
}

void InGameMenu::EngineDrawTextFitted(const char* text, const int x, const int y,
	int maxWidth, const Color& color) {
	if (!text || maxWidth <= 0 || !I::MatSystemSurface) return;
	wchar_t wstr[1024] = {};
	MultiByteToWideChar(CP_UTF8, 0, text, -1, wstr, 1024);
	std::wstring fitted(wstr);
	int width = 0;
	int height = 0;
	I::MatSystemSurface->GetTextSize(inGameMenuFONT, fitted.c_str(), width, height);
	while (width > maxWidth && fitted.size() > 4) {
		fitted.resize(fitted.size() - 1);
		std::wstring probe = fitted + L"...";
		I::MatSystemSurface->GetTextSize(inGameMenuFONT, probe.c_str(), width, height);
	}
	if (fitted != wstr) fitted += L"...";
	I::MatSystemSurface->DrawSetTextFont(inGameMenuFONT);
	I::MatSystemSurface->DrawSetTextColor(color);
	I::MatSystemSurface->DrawSetTextPos(x, y);
	I::MatSystemSurface->DrawPrintText(fitted.c_str(), static_cast<int>(fitted.size()));
}

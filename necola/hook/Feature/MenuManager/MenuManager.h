#pragma once
#include "../../../sdk/SDK.h"
#include "../../Vars.h"
#include <vector>
#include <string>
#include <functional>
#include <memory>
#include <stack>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <array>
#include <unordered_map>
#include <utility>

#include <spdlog/spdlog.h>

#include "../AdsSupport/AdsSupport.h"
#include "../KillFeedback/KillFeedback.h"
#include "../HitFeedback/HitFeedback.h"
#include "../../../sdk/utils/FeatureConfigManager.h"


using json = nlohmann::json;

const int MENU_MIN_WIDTH = 420;
const int MENU_MAX_WIDTH = 560;
const int MENU_HEIGHT = 350;
const int LINE_HEIGHT = 30;
const int TOTAL_LINES = 11;
const int TITLE_LINE = 0;
const int OPTION_START_LINE = 1;
const int OPTION_END_LINE = 7;
const int NAV_START_LINE = 8;
const int MAX_OPTIONS_PER_PAGE = 7;

const Color C_COLOR_BORDER = {90, 110, 120, 230};
const Color C_COLOR_TEXT = {238, 242, 244, 255};
const Color C_COLOR_TEXT_DISABLED = {125, 132, 136, 210};
const Color C_COLOR_SWITCH_ON = {92, 214, 141, 255};
const Color C_COLOR_SWITCH_OFF = {232, 112, 104, 255};
const Color C_COLOR_SUBMENU = {102, 190, 214, 255};
const Color C_COLOR_NAV_ENABLED = {228, 197, 105, 255};
const Color C_COLOR_NAV_DISABLED = {100, 106, 110, 170};
const Color C_COLOR_RETURN = {235, 164, 125, 255};
const Color C_COLOR_LINE = {72, 82, 88, 220};
const Color C_COLOR_LINE_NAV = {62, 70, 75, 220};
const Color C_COLOR_FLASH_YELLOW = {255, 255, 0, 255};
const Color C_COLOR_FLASH_GREEN = {0, 255, 0, 255};


enum MenuItemType {
	ITEM_NORMAL,
	ITEM_SWITCH,
	ITEM_SUBMENU
};


struct MenuItem {
	std::string name;
	MenuItemType type;
	std::function<void()> action;
	std::function<void(bool)> toggleAction;
	std::shared_ptr<class MenuNode> subMenu;
	bool switchState;
	bool enabled;

	MenuItem(const std::string& n, std::function<void()> act = nullptr, bool en = true)
		: name(n), type(ITEM_NORMAL), action(act), subMenu(nullptr), switchState(false), enabled(en) {}

	MenuItem(const std::string& n, bool initialState, std::function<void(bool)> toggleFunc, bool en = true)
		: name(n), type(ITEM_SWITCH), toggleAction(toggleFunc), subMenu(nullptr),
		  switchState(initialState), enabled(en) {}

	MenuItem(const std::string& n, std::shared_ptr<class MenuNode> sub, bool en = true)
		: name(n), type(ITEM_SUBMENU), action(nullptr), subMenu(sub), switchState(false), enabled(en) {}

	void toggle() {
		if (type == ITEM_SWITCH) {
			switchState = !switchState;
			if (toggleAction) {
				toggleAction(switchState);
			}
		}
	}

	void execute() {
		if (!enabled) return;

		switch (type) {
			case ITEM_NORMAL:
			case ITEM_SUBMENU:
				if (action) action();
				break;
			case ITEM_SWITCH:
				toggle();
				break;
		}
	}


};


class MenuNode {
private:
	std::string id;
	std::string title;
	std::vector<MenuItem> items;
	int currentPage = 0;

public:
	MenuNode(const std::string& nodeId, const std::string& t)
		: id(nodeId), title(t) {}

	const std::string& getId() const { return id; }
	const std::string& getTitle() const { return title; }
	void setTitle(const std::string& t) { title = t; }

	void addOption(const std::string& name, std::function<void()> action = nullptr, bool enabled = true) {
		items.emplace_back(name, action, enabled);
	}

	void addSwitch(const std::string& name, bool initialState,  std::function<void(bool)> toggleFunc = nullptr, bool enabled = true) {
		items.emplace_back(name, initialState, toggleFunc, enabled);
	}

	void prependSwitch(const std::string& name, bool initialState, std::function<void(bool)> toggleFunc = nullptr, bool enabled = true) {
		items.insert(items.begin(), MenuItem(name, initialState, toggleFunc, enabled));
	}

	std::shared_ptr<MenuNode> addSubMenu(const std::string& itemName,  const std::string& subMenuId,  const std::string& subMenuTitle) {
		auto subMenu = std::make_shared<MenuNode>(subMenuId, subMenuTitle);
		items.emplace_back(itemName, subMenu);
		return subMenu;
	}

	int getItemCount() const { return items.size(); }

	std::vector<MenuItem> getCurrentPageItems() const {
		std::vector<MenuItem> pageItems;
		int startIdx = currentPage * MAX_OPTIONS_PER_PAGE;
		int endIdx = std::min(startIdx + MAX_OPTIONS_PER_PAGE, (int)items.size());

		for (int i = startIdx; i < endIdx; i++) {
			pageItems.push_back(items[i]);
		}

		return pageItems;
	}

	int getCurrentPageItemCount() const {
		int startIdx = currentPage * MAX_OPTIONS_PER_PAGE;
		return std::min(MAX_OPTIONS_PER_PAGE, (int)items.size() - startIdx);
	}

	int getTotalPages() const {
		if (items.empty()) return 1;
		return (items.size() + MAX_OPTIONS_PER_PAGE - 1) / MAX_OPTIONS_PER_PAGE;
	}

	int getCurrentPage() const { return currentPage; }

	void previousPage() { if (currentPage > 0) currentPage--; }
	void nextPage() {
		int totalPages = getTotalPages();
		if (currentPage < totalPages - 1) currentPage++;
	}

	void resetToFirstPage() { currentPage = 0; }

	MenuItem* getItem(int index) {
		if (index >= 0 && index < items.size()) {
			return &items[index];
		}
		return nullptr;
	}

	std::shared_ptr<MenuNode> findSubMenu(const std::string& targetId) {
		for (auto& item : items) {
			if (item.type == ITEM_SUBMENU && item.subMenu) {
				if (item.subMenu->getId() == targetId) {
					return item.subMenu;
				}
				auto found = item.subMenu->findSubMenu(targetId);
				if (found) return found;
			}
		}
		return nullptr;
	}

	bool updateSubMenuItemName(const std::string& subMenuId, const std::string& newName) {
		for (auto& item : items) {
			if (item.type == ITEM_SUBMENU && item.subMenu && item.subMenu->getId() == subMenuId) {
				item.name = newName;
				return true;
			}
		}
		return false;
	}

	bool updateOptionNameByPrefix(const std::string& prefix, const std::string& newName) {
		for (auto& item : items) {
			if (item.type == ITEM_NORMAL && item.name.find(prefix) == 0) {
				item.name = newName;
				return true;
			}
		}
		return false;
	}

	bool setSwitchStateByName(const std::string& switchName, bool state) {
		for (auto& item : items) {
			if (item.type == ITEM_SWITCH && item.name == switchName) {
				item.switchState = state;
				return true;
			}
		}
		return false;
	}

	void setSwitchesEnabled(bool state) {
		for (auto& item : items) {
			if (item.type == ITEM_SWITCH) item.enabled = state;
		}
	}

	void clearAllItems() {
		items.clear();
		currentPage = 0;
	}
};


class InGameMenu {
private:
	std::shared_ptr<MenuNode> rootMenu;
	std::stack<std::shared_ptr<MenuNode>> menuStack;
	std::unordered_map<std::string, std::weak_ptr<MenuNode>> menuRegistry;

	HFont inGameMenuFONT;

	bool isVisible = false;
	int menuX = 0;
	int menuY = 0;
	int menuWidth = MENU_MAX_WIDTH;
	int menuHeight = MENU_HEIGHT;

	int screenWidth = 1920;
	int screenHeight = 1080;
	bool layoutFits = true;

	int flashingItemIndex = -1;
	float flashStartTime = 0.0f;
	int flashCount = 0;
	bool flashYellow = true;


public:
	InGameMenu() {
		rootMenu = std::make_shared<MenuNode>("root", "主菜单");
		registerMenu(rootMenu);
		initializeDefaultMenus();
		menuStack.push(rootMenu);
	}


	void SetScreenSize(int width, int height) {
		screenWidth = std::max(width, 1);
		screenHeight = std::max(height, 1);
		layoutFits = screenWidth >= MENU_MIN_WIDTH + 32 && screenHeight >= MENU_HEIGHT + 16;
		menuWidth = std::min(MENU_MAX_WIDTH, std::max(MENU_MIN_WIDTH, screenWidth - 32));
		if (!layoutFits) isVisible = false;
		UpdatePosition();
	}

	void LoadConfig(const nlohmann::json& doc) {
		const auto section = doc.find("Menu");
		if (section != doc.end() && section->is_object()) {
			const auto anchor = section->find("Anchor");
			if (anchor != section->end() && anchor->is_number_integer()) {
				try {
					if (anchor->is_number_unsigned()) {
						const auto value = anchor->get<nlohmann::json::number_unsigned_t>();
						if (value <= 2) G::Vars.menuAnchor = static_cast<int>(value);
					} else {
						const auto value = anchor->get<nlohmann::json::number_integer_t>();
						G::Vars.menuAnchor = static_cast<int>(std::clamp<nlohmann::json::number_integer_t>(value, 0, 2));
					}
				} catch (...) {}
			}
			const auto opacity = section->find("BackgroundOpacity");
			if (opacity != section->end() && opacity->is_number_integer()) {
				try {
					if (opacity->is_number_unsigned()) {
						const auto value = opacity->get<nlohmann::json::number_unsigned_t>();
						if (value <= 255) G::Vars.menuOpacity = static_cast<int>(std::clamp<nlohmann::json::number_unsigned_t>(value, 160, 245));
					} else {
						const auto value = opacity->get<nlohmann::json::number_integer_t>();
						G::Vars.menuOpacity = static_cast<int>(std::clamp<nlohmann::json::number_integer_t>(value, 160, 245));
					}
				} catch (...) {}
			}
		}
		UpdatePosition();
		RefreshAppearanceLabels();
	}

	void SaveMenuConfig() {
		nlohmann::json doc = NecolaConfig::LoadConfig();
		auto& menuConfig = NecolaConfig::EnsureSectionObject(doc, "Menu");
		menuConfig["Anchor"] = G::Vars.menuAnchor;
		menuConfig["BackgroundOpacity"] = G::Vars.menuOpacity;
		NecolaConfig::SaveConfig(doc);
	}

	void Toggle() {
		isVisible = !isVisible;
		if (isVisible) {
			UpdatePosition();
			while(menuStack.size() > 1) {
				menuStack.pop();
			}
			if (!menuStack.empty()) {
				menuStack.top()->resetToFirstPage();
			}
		}
	}

	void initializeDefaultMenus() {
		// SequenceModify submenu (required by ADS layer for sequence tracking)
		auto seqMenu = rootMenu->addSubMenu("序列修正 [关]", "seq", "序列修正");
		registerMenu(seqMenu);

		// Only register the ADS sub-tree
		auto adsMenu = rootMenu->addSubMenu("ADS功能 [关]", "ads", "ADS功能");
		registerMenu(adsMenu);

		if (adsMenu) {
			adsMenu->addSwitch("启用ADS", G::Vars.enableAdsSupport, [this](bool state) {
				G::Vars.enableAdsSupport = state;
				if (state) {
					F::AdsMgr.Init();
				} else {
					F::AdsMgr.ForceExitADS();
				}
				nlohmann::json doc = NecolaConfig::LoadConfig();
				F::AdsMgr.SaveConfig(doc);
				NecolaConfig::SaveConfig(doc);
				RefreshRootLabels();
			});

			// Per-weapon ADS crosshair hide submenu
			{
				auto crosshairModeLabel = [](int mode) -> std::string {
					switch (mode) {
						case 0:  return "关";
						case 1:  return "开";
						case 2:  return "自定义";
						default: return "关";
					}
				};

				std::string chLabel = "ADS状态隐藏准星 [" + crosshairModeLabel(G::Vars.adsHideCrosshairMode) + "]";
				auto crosshairMenu = adsMenu->addSubMenu(chLabel, "ads_crosshair", "ADS状态隐藏准星");
				registerMenu(crosshairMenu);
				if (crosshairMenu) {
					crosshairMenu->addOption("全局关", [this]() {
						G::Vars.adsHideCrosshairMode = 0;
						nlohmann::json doc = NecolaConfig::LoadConfig();
						F::AdsMgr.SaveConfig(doc);
						NecolaConfig::SaveConfig(doc);
						RefreshCrosshairModeUI();
					});
					crosshairMenu->addOption("全局开", [this]() {
						G::Vars.adsHideCrosshairMode = 1;
						nlohmann::json doc = NecolaConfig::LoadConfig();
						F::AdsMgr.SaveConfig(doc);
						NecolaConfig::SaveConfig(doc);
						RefreshCrosshairModeUI();
					});
					crosshairMenu->addOption("自定义", [this]() {
						G::Vars.adsHideCrosshairMode = 2;
						nlohmann::json doc = NecolaConfig::LoadConfig();
						F::AdsMgr.SaveConfig(doc);
						NecolaConfig::SaveConfig(doc);
						RefreshCrosshairModeUI();
					});

					struct WeaponCrosshairEntry {
						const char* name;
						bool* varPtr;
						const char* configKey;
					};
					WeaponCrosshairEntry weapons[] = {
						{"手枪",          &G::Vars.adsHideCrosshairPistol,         "HideCrosshairPistol"},
						{"双持手枪",      &G::Vars.adsHideCrosshairPistolDual,     "HideCrosshairPistolDual"},
						{"马格南",        &G::Vars.adsHideCrosshairDeagle,         "HideCrosshairDeagle"},
						{"UZI",           &G::Vars.adsHideCrosshairUzi,            "HideCrosshairUzi"},
						{"MAC10",         &G::Vars.adsHideCrosshairMac10,          "HideCrosshairMac10"},
						{"MP5",           &G::Vars.adsHideCrosshairMP5,            "HideCrosshairMP5"},
						{"木喷",          &G::Vars.adsHideCrosshairPumpShotgun,    "HideCrosshairPumpShotgun"},
						{"铁喷",          &G::Vars.adsHideCrosshairChromeShotgun,  "HideCrosshairChromeShotgun"},
						{"一代连喷",      &G::Vars.adsHideCrosshairAutoShotgun,    "HideCrosshairAutoShotgun"},
						{"二代连喷",      &G::Vars.adsHideCrosshairSpas,           "HideCrosshairSpas"},
						{"M16",           &G::Vars.adsHideCrosshairM16A1,          "HideCrosshairM16A1"},
						{"SCAR",          &G::Vars.adsHideCrosshairScar,           "HideCrosshairScar"},
						{"AK47",          &G::Vars.adsHideCrosshairAK47,           "HideCrosshairAK47"},
						{"SG552",         &G::Vars.adsHideCrosshairSSG552,         "HideCrosshairSSG552"},
						{"一代连狙",      &G::Vars.adsHideCrosshairHuntingRifle,   "HideCrosshairHuntingRifle"},
						{"二代连狙",      &G::Vars.adsHideCrosshairMilitarySniper, "HideCrosshairMilitarySniper"},
						{"SCOUT",         &G::Vars.adsHideCrosshairScout,          "HideCrosshairScout"},
						{"AWP",           &G::Vars.adsHideCrosshairAWP,            "HideCrosshairAWP"},
						{"M60",           &G::Vars.adsHideCrosshairM60,            "HideCrosshairM60"},
						{"榴弹发射器",    &G::Vars.adsHideCrosshairGrenadeLauncher, "HideCrosshairGrenadeLauncher"},
					};

					for (const auto& w : weapons) {
						bool* varPtr = w.varPtr;
						crosshairMenu->addSwitch(w.name, *varPtr, [varPtr](bool state) {
							*varPtr = state;
							nlohmann::json doc = NecolaConfig::LoadConfig();
							F::AdsMgr.SaveConfig(doc);
							NecolaConfig::SaveConfig(doc);
						});
					}
				}
			}

			// Per-weapon scope settings submenu
			auto scopeLabel = [](int mode) -> std::string {
				switch (mode) {
					case 0:  return "关闭";
					case 1:  return "仅ADS";
					case 2:  return "混合";
					default: return "关闭";
				}
			};

			auto scopeWeaponMenu = adsMenu->addSubMenu("原生开镜武器设置", "ads_scope_weapons", "原生开镜武器设置");
			registerMenu(scopeWeaponMenu);

			if (scopeWeaponMenu) {
				{
					std::string label = "SG552 ADS设置 [" + scopeLabel(G::Vars.adsScopeSSG552) + "]";
					auto sgMenu = scopeWeaponMenu->addSubMenu(label, "ads_ssg552", "SG552 ADS设置");
					registerMenu(sgMenu);
					if (sgMenu) {
						sgMenu->addOption("关闭", [this, scopeLabel]() {
							G::Vars.adsScopeSSG552 = 0;
							nlohmann::json doc = NecolaConfig::LoadConfig();
							F::AdsMgr.SaveConfig(doc);
							NecolaConfig::SaveConfig(doc);
							auto m = FindMenuById("ads_ssg552");
							if (m) m->setTitle("SG552 ADS设置 (关闭)");
							auto a = FindMenuById("ads_scope_weapons");
							if (a) a->updateSubMenuItemName("ads_ssg552", "SG552 ADS设置 [" + scopeLabel(0) + "]");
						});
						sgMenu->addOption("仅ADS", [this, scopeLabel]() {
							G::Vars.adsScopeSSG552 = 1;
							nlohmann::json doc = NecolaConfig::LoadConfig();
							F::AdsMgr.SaveConfig(doc);
							NecolaConfig::SaveConfig(doc);
							auto m = FindMenuById("ads_ssg552");
							if (m) m->setTitle("SG552 ADS设置 (仅ADS)");
							auto a = FindMenuById("ads_scope_weapons");
							if (a) a->updateSubMenuItemName("ads_ssg552", "SG552 ADS设置 [" + scopeLabel(1) + "]");
						});
						sgMenu->addOption("混合", [this, scopeLabel]() {
							G::Vars.adsScopeSSG552 = 2;
							nlohmann::json doc = NecolaConfig::LoadConfig();
							F::AdsMgr.SaveConfig(doc);
							NecolaConfig::SaveConfig(doc);
							auto m = FindMenuById("ads_ssg552");
							if (m) m->setTitle("SG552 ADS设置 (混合)");
							auto a = FindMenuById("ads_scope_weapons");
							if (a) a->updateSubMenuItemName("ads_ssg552", "SG552 ADS设置 [" + scopeLabel(2) + "]");
						});
					}
				}

				{
					std::string label = "一代连狙ADS设置 [" + scopeLabel(G::Vars.adsScopeHuntingRifle) + "]";
					auto hrMenu = scopeWeaponMenu->addSubMenu(label, "ads_hunting_rifle", "一代连狙ADS设置");
					registerMenu(hrMenu);
					if (hrMenu) {
						hrMenu->addOption("关闭", [this, scopeLabel]() {
							G::Vars.adsScopeHuntingRifle = 0;
							nlohmann::json doc = NecolaConfig::LoadConfig();
							F::AdsMgr.SaveConfig(doc);
							NecolaConfig::SaveConfig(doc);
							auto m = FindMenuById("ads_hunting_rifle");
							if (m) m->setTitle("一代连狙ADS设置 (关闭)");
							auto a = FindMenuById("ads_scope_weapons");
							if (a) a->updateSubMenuItemName("ads_hunting_rifle", "一代连狙ADS设置 [" + scopeLabel(0) + "]");
						});
						hrMenu->addOption("仅ADS", [this, scopeLabel]() {
							G::Vars.adsScopeHuntingRifle = 1;
							nlohmann::json doc = NecolaConfig::LoadConfig();
							F::AdsMgr.SaveConfig(doc);
							NecolaConfig::SaveConfig(doc);
							auto m = FindMenuById("ads_hunting_rifle");
							if (m) m->setTitle("一代连狙ADS设置 (仅ADS)");
							auto a = FindMenuById("ads_scope_weapons");
							if (a) a->updateSubMenuItemName("ads_hunting_rifle", "一代连狙ADS设置 [" + scopeLabel(1) + "]");
						});
						hrMenu->addOption("混合", [this, scopeLabel]() {
							G::Vars.adsScopeHuntingRifle = 2;
							nlohmann::json doc = NecolaConfig::LoadConfig();
							F::AdsMgr.SaveConfig(doc);
							NecolaConfig::SaveConfig(doc);
							auto m = FindMenuById("ads_hunting_rifle");
							if (m) m->setTitle("一代连狙ADS设置 (混合)");
							auto a = FindMenuById("ads_scope_weapons");
							if (a) a->updateSubMenuItemName("ads_hunting_rifle", "一代连狙ADS设置 [" + scopeLabel(2) + "]");
						});
					}
				}

				{
					std::string label = "二代连狙ADS设置 [" + scopeLabel(G::Vars.adsScopeMilitarySniper) + "]";
					auto msMenu = scopeWeaponMenu->addSubMenu(label, "ads_military_sniper", "二代连狙ADS设置");
					registerMenu(msMenu);
					if (msMenu) {
						msMenu->addOption("关闭", [this, scopeLabel]() {
							G::Vars.adsScopeMilitarySniper = 0;
							nlohmann::json doc = NecolaConfig::LoadConfig();
							F::AdsMgr.SaveConfig(doc);
							NecolaConfig::SaveConfig(doc);
							auto m = FindMenuById("ads_military_sniper");
							if (m) m->setTitle("二代连狙ADS设置 (关闭)");
							auto a = FindMenuById("ads_scope_weapons");
							if (a) a->updateSubMenuItemName("ads_military_sniper", "二代连狙ADS设置 [" + scopeLabel(0) + "]");
						});
						msMenu->addOption("仅ADS", [this, scopeLabel]() {
							G::Vars.adsScopeMilitarySniper = 1;
							nlohmann::json doc = NecolaConfig::LoadConfig();
							F::AdsMgr.SaveConfig(doc);
							NecolaConfig::SaveConfig(doc);
							auto m = FindMenuById("ads_military_sniper");
							if (m) m->setTitle("二代连狙ADS设置 (仅ADS)");
							auto a = FindMenuById("ads_scope_weapons");
							if (a) a->updateSubMenuItemName("ads_military_sniper", "二代连狙ADS设置 [" + scopeLabel(1) + "]");
						});
						msMenu->addOption("混合", [this, scopeLabel]() {
							G::Vars.adsScopeMilitarySniper = 2;
							nlohmann::json doc = NecolaConfig::LoadConfig();
							F::AdsMgr.SaveConfig(doc);
							NecolaConfig::SaveConfig(doc);
							auto m = FindMenuById("ads_military_sniper");
							if (m) m->setTitle("二代连狙ADS设置 (混合)");
							auto a = FindMenuById("ads_scope_weapons");
							if (a) a->updateSubMenuItemName("ads_military_sniper", "二代连狙ADS设置 [" + scopeLabel(2) + "]");
						});
					}
				}

				{
					std::string label = "SCOUT ADS设置 [" + scopeLabel(G::Vars.adsScopeScout) + "]";
					auto scoutMenu = scopeWeaponMenu->addSubMenu(label, "ads_scout", "SCOUT ADS设置");
					registerMenu(scoutMenu);
					if (scoutMenu) {
						scoutMenu->addOption("关闭", [this, scopeLabel]() {
							G::Vars.adsScopeScout = 0;
							nlohmann::json doc = NecolaConfig::LoadConfig();
							F::AdsMgr.SaveConfig(doc);
							NecolaConfig::SaveConfig(doc);
							auto m = FindMenuById("ads_scout");
							if (m) m->setTitle("SCOUT ADS设置 (关闭)");
							auto a = FindMenuById("ads_scope_weapons");
							if (a) a->updateSubMenuItemName("ads_scout", "SCOUT ADS设置 [" + scopeLabel(0) + "]");
						});
						scoutMenu->addOption("仅ADS", [this, scopeLabel]() {
							G::Vars.adsScopeScout = 1;
							nlohmann::json doc = NecolaConfig::LoadConfig();
							F::AdsMgr.SaveConfig(doc);
							NecolaConfig::SaveConfig(doc);
							auto m = FindMenuById("ads_scout");
							if (m) m->setTitle("SCOUT ADS设置 (仅ADS)");
							auto a = FindMenuById("ads_scope_weapons");
							if (a) a->updateSubMenuItemName("ads_scout", "SCOUT ADS设置 [" + scopeLabel(1) + "]");
						});
						scoutMenu->addOption("混合", [this, scopeLabel]() {
							G::Vars.adsScopeScout = 2;
							nlohmann::json doc = NecolaConfig::LoadConfig();
							F::AdsMgr.SaveConfig(doc);
							NecolaConfig::SaveConfig(doc);
							auto m = FindMenuById("ads_scout");
							if (m) m->setTitle("SCOUT ADS设置 (混合)");
							auto a = FindMenuById("ads_scope_weapons");
							if (a) a->updateSubMenuItemName("ads_scout", "SCOUT ADS设置 [" + scopeLabel(2) + "]");
						});
					}
				}

				{
					std::string label = "AWP ADS设置 [" + scopeLabel(G::Vars.adsScopeAWP) + "]";
					auto awpMenu = scopeWeaponMenu->addSubMenu(label, "ads_awp", "AWP ADS设置");
					registerMenu(awpMenu);
					if (awpMenu) {
						awpMenu->addOption("关闭", [this, scopeLabel]() {
							G::Vars.adsScopeAWP = 0;
							nlohmann::json doc = NecolaConfig::LoadConfig();
							F::AdsMgr.SaveConfig(doc);
							NecolaConfig::SaveConfig(doc);
							auto m = FindMenuById("ads_awp");
							if (m) m->setTitle("AWP ADS设置 (关闭)");
							auto a = FindMenuById("ads_scope_weapons");
							if (a) a->updateSubMenuItemName("ads_awp", "AWP ADS设置 [" + scopeLabel(0) + "]");
						});
						awpMenu->addOption("仅ADS", [this, scopeLabel]() {
							G::Vars.adsScopeAWP = 1;
							nlohmann::json doc = NecolaConfig::LoadConfig();
							F::AdsMgr.SaveConfig(doc);
							NecolaConfig::SaveConfig(doc);
							auto m = FindMenuById("ads_awp");
							if (m) m->setTitle("AWP ADS设置 (仅ADS)");
							auto a = FindMenuById("ads_scope_weapons");
							if (a) a->updateSubMenuItemName("ads_awp", "AWP ADS设置 [" + scopeLabel(1) + "]");
						});
						awpMenu->addOption("混合", [this, scopeLabel]() {
							G::Vars.adsScopeAWP = 2;
							nlohmann::json doc = NecolaConfig::LoadConfig();
							F::AdsMgr.SaveConfig(doc);
							NecolaConfig::SaveConfig(doc);
							auto m = FindMenuById("ads_awp");
							if (m) m->setTitle("AWP ADS设置 (混合)");
							auto a = FindMenuById("ads_scope_weapons");
							if (a) a->updateSubMenuItemName("ads_awp", "AWP ADS设置 [" + scopeLabel(2) + "]");
						});
					}
				}
			}
		}

		auto killFeedbackMenu = rootMenu->addSubMenu("主题反馈 [关]", "kill_feedback", "主题反馈");
		registerMenu(killFeedbackMenu);
		if (killFeedbackMenu) {
			auto addKillSwitch = [this](const std::shared_ptr<MenuNode>& menu, const char* label,
				bool* setting, bool stopWhenDisabled) {
				menu->addSwitch(label, *setting, [this, setting, stopWhenDisabled](bool state) {
					*setting = state;
					if (!state && stopWhenDisabled) {
						if (setting == &G::Vars.killFeedbackEnabled) F::KillFeedbackMgr.Reset();
						else F::KillFeedbackMgr.Stop();
					}
					F::KillFeedbackMgr.SaveConfig();
					RefreshRootLabels();
				});
			};

			addKillSwitch(killFeedbackMenu, "启用击杀提示", &G::Vars.killFeedbackEnabled, true);
			addKillSwitch(killFeedbackMenu, "普通感染者", &G::Vars.killFeedbackCommon, false);
			addKillSwitch(killFeedbackMenu, "特殊感染者（含Witch）", &G::Vars.killFeedbackSpecial, false);
			addKillSwitch(killFeedbackMenu, "视觉效果（图标/粒子）", &G::Vars.killFeedbackIcon, true);
			addKillSwitch(killFeedbackMenu, "击杀音效", &G::Vars.killFeedbackSound, false);

			auto siThemeMenu = killFeedbackMenu->addSubMenu("特感主题", "kill_si_theme", "特感主题");
			registerMenu(siThemeMenu);
			if (siThemeMenu) {
				const std::pair<const char*, const char*> themes[] = {
					{"关闭", "off"}, {"特感 · 瓦罗兰特", "si_valorant"}, {"特感 · CF", "si_cf"},
					{"特感 · 落雷", "si_lightning"}, {"特感 · 爱心冲击", "si_love"},
					{"特感 · 星星", "si_star"}, {"特感 · 三角洲Advanced", "si_deltaforce_advanced"},
					{"特感 · 战地1Advanced", "si_bf1_advanced"},
					{"特感 · 战地2042Advanced", "si_bf2042_advanced"},
				};
				for (const auto& [label, id] : themes) {
					siThemeMenu->addOption(label, [this, id]() {
						if (std::strcmp(id, "off") != 0) {
							G::Vars.killFeedbackSiDedicated = true;
							auto hitMenu = FindMenuById("kill_hit_tip");
							if (hitMenu) hitMenu->setSwitchStateByName("SI视觉优先", true);
						}
						F::KillFeedbackMgr.SetTheme("si", id);
						RefreshKillFeedbackLabels();
					});
				}
			}

			auto ciThemeMenu = killFeedbackMenu->addSubMenu("普感主题", "kill_ci_theme", "普感主题");
			registerMenu(ciThemeMenu);
			if (ciThemeMenu) {
				const std::pair<const char*, const char*> themes[] = {
					{"关闭", "off"}, {"普感 · OW", "ci_ow"}, {"普感 · Apex", "ci_apex"},
					{"普感 · CF", "ci_cf"}, {"普感 · COD", "ci_cod"},
					{"普感 · BF5", "ci_bf5"}, {"普感 · BF1", "ci_bf1"},
					{"普感 · BF2042", "ci_bf2042"}, {"普感 · 三角洲", "ci_deltaforce"},
					{"普感 · L4D2", "ci_l4d2"},
				};
				for (const auto& [label, id] : themes) {
					ciThemeMenu->addOption(label, [this, id]() {
						F::KillFeedbackMgr.SetTheme("ci", id);
						RefreshKillFeedbackLabels();
					});
				}
			}

			auto hitTipMenu = killFeedbackMenu->addSubMenu("主题命中效果", "kill_hit_tip", "主题命中效果（独立于伤害数字）");
			registerMenu(hitTipMenu);
			if (hitTipMenu) {
				hitTipMenu->addOption("关闭命中提示", [this]() {
					G::Vars.killFeedbackHitMode = 0;
					F::KillFeedbackMgr.SaveConfig();
					RefreshKillFeedbackLabels();
				});
				hitTipMenu->addOption("仅特感命中", [this]() {
					G::Vars.killFeedbackHitMode = 1;
					F::KillFeedbackMgr.SaveConfig();
					RefreshKillFeedbackLabels();
				});
				hitTipMenu->addOption("全部命中", [this]() {
					G::Vars.killFeedbackHitMode = 2;
					F::KillFeedbackMgr.SaveConfig();
					RefreshKillFeedbackLabels();
				});
				hitTipMenu->addSwitch("SI视觉优先", G::Vars.killFeedbackSiDedicated, [](bool state) {
					G::Vars.killFeedbackSiDedicated = state;
					F::KillFeedbackMgr.SaveConfig();
				});
				hitTipMenu->addSwitch("SI音效优先", G::Vars.killFeedbackSiSound, [](bool state) {
					G::Vars.killFeedbackSiSound = state;
					F::KillFeedbackMgr.SaveConfig();
				});
			}

			auto volumeMenu = killFeedbackMenu->addSubMenu("音效音量", "kill_sound_volume", "音效音量");
			registerMenu(volumeMenu);
			if (volumeMenu) {
				const std::pair<const char*, int> volumes[] = {
					{"静音", 0}, {"25%", 25}, {"50%", 50}, {"75%", 75}, {"100%", 100},
				};
				for (const auto& [label, value] : volumes) {
					volumeMenu->addOption(label, [this, value]() {
						G::Vars.killFeedbackSoundVolume = value;
						F::KillFeedbackMgr.SaveConfig();
						RefreshKillFeedbackLabels();
					});
				}
			}

			auto specialMenu = killFeedbackMenu->addSubMenu("特感分类设置", "kill_feedback_specials", "特感分类设置");
			registerMenu(specialMenu);
			if (specialMenu) {
				for (const auto& [label, setting] : KillFeedbackSpecialSwitches()) {
					addKillSwitch(specialMenu, label, setting, false);
				}
			}

			auto methodMenu = killFeedbackMenu->addSubMenu("击杀方式", "kill_feedback_methods", "击杀方式");
			registerMenu(methodMenu);
			if (methodMenu) {
				for (const auto& [label, setting] : KillFeedbackMethodSwitches()) {
					addKillSwitch(methodMenu, label, setting, false);
				}
			}

			auto previewMenu = killFeedbackMenu->addSubMenu("测试效果", "kill_feedback_preview", "测试效果");
			registerMenu(previewMenu);
			if (previewMenu) {
				const std::pair<const char*, int> previews[] = {
					{"普通击杀", 0}, {"普通爆头", 1}, {"普通近战", 2},
					{"特感击杀", 3}, {"特感爆头", 4}, {"特感近战", 5},
					{"特感三连杀", 6}, {"特感十连杀", 7},
				};
				for (const auto& [label, kind] : previews) {
					previewMenu->addOption(label, [kind]() { F::KillFeedbackMgr.Preview(kind); });
				}
			}
		}

		auto hitFeedbackMenu = rootMenu->addSubMenu("伤害数字与准星 [关]", "hit_feedback", "伤害数字与准星");
		registerMenu(hitFeedbackMenu);
		if (hitFeedbackMenu) {
			auto addHitSwitch = [this](const std::shared_ptr<MenuNode>& menu, const char* label,
				bool* setting) {
				menu->addSwitch(label, *setting, [this, setting](bool state) {
					*setting = state;
					if (!state && setting == &G::Vars.hitFeedbackEnabled) F::HitFeedbackMgr.Reset();
					F::HitFeedbackMgr.SaveConfig();
					RefreshRootLabels();
				});
			};
			addHitSwitch(hitFeedbackMenu, "启用伤害数字与准星", &G::Vars.hitFeedbackEnabled);
			addHitSwitch(hitFeedbackMenu, "伤害数字", &G::Vars.hitFeedbackNumbers);
			addHitSwitch(hitFeedbackMenu, "命中标记", &G::Vars.hitFeedbackHitMarker);
			addHitSwitch(hitFeedbackMenu, "普通感染者伤害数字", &G::Vars.hitFeedbackCommon);
		}

		auto toolsMenu = rootMenu->addSubMenu("诊断与工具", "tools", "诊断与工具");
		registerMenu(toolsMenu);
		if (toolsMenu) {
			toolsMenu->addSwitch("击杀反馈日志", G::Vars.killFeedbackLog, [](bool state) {
				G::Vars.killFeedbackLog = state;
				F::KillFeedbackMgr.SaveConfig();
			});
			toolsMenu->addSwitch("ADS详细日志", G::Vars.adsLog, [](bool state) {
				G::Vars.adsLog = state;
				nlohmann::json doc = NecolaConfig::LoadConfig();
				F::AdsMgr.SaveConfig(doc);
				NecolaConfig::SaveConfig(doc);
			});
			toolsMenu->addSwitch("序列详细日志", G::Vars.sequenceLog, [](bool state) {
				G::Vars.sequenceLog = state;
				nlohmann::json doc = NecolaConfig::LoadConfig();
				NecolaConfig::EnsureSectionObject(doc, "SequenceModify")["SequenceLog"] = state;
				NecolaConfig::SaveConfig(doc);
			});
			toolsMenu->addOption("立即退出 ADS / MIXED", []() {
				F::AdsMgr.ForceExitADS();
			});
		}

		auto appearanceMenu = rootMenu->addSubMenu("菜单外观", "menu_appearance", "菜单外观");
		registerMenu(appearanceMenu);
		if (appearanceMenu) {
			auto anchorMenu = appearanceMenu->addSubMenu("界面位置", "menu_anchor", "界面位置");
			registerMenu(anchorMenu);
			if (anchorMenu) {
				anchorMenu->addOption("左侧", [this]() { SetMenuAnchor(0); });
				anchorMenu->addOption("居中", [this]() { SetMenuAnchor(1); });
				anchorMenu->addOption("右侧", [this]() { SetMenuAnchor(2); });
			}

			auto opacityMenu = appearanceMenu->addSubMenu("背景透明度", "menu_opacity", "背景透明度");
			registerMenu(opacityMenu);
			if (opacityMenu) {
				opacityMenu->addOption("轻透", [this]() { SetMenuOpacity(180); });
				opacityMenu->addOption("标准", [this]() { SetMenuOpacity(220); });
				opacityMenu->addOption("高对比", [this]() { SetMenuOpacity(245); });
			}
		}

		RefreshRootLabels();
		RefreshCrosshairModeUI();
		RefreshAppearanceLabels();
	}

	void SetMenuAnchor(int anchor) {
		G::Vars.menuAnchor = std::clamp(anchor, 0, 2);
		UpdatePosition();
		RefreshAppearanceLabels();
		SaveMenuConfig();
	}

	void SetMenuOpacity(int opacity) {
		G::Vars.menuOpacity = std::clamp(opacity, 160, 245);
		RefreshAppearanceLabels();
		SaveMenuConfig();
	}

	void RefreshRootLabels() {
		if (!rootMenu) return;
		rootMenu->updateSubMenuItemName("ads", std::string("ADS功能 [") +
			(G::Vars.enableAdsSupport ? "开]" : "关]"));
		rootMenu->updateSubMenuItemName("seq", std::string("序列修正 [") +
			(G::Vars.animSequenceModify ? "开]" : "关]"));
		rootMenu->updateSubMenuItemName("kill_feedback", std::string("主题反馈 [") +
			(G::Vars.killFeedbackEnabled || G::Vars.killFeedbackHitMode > 0 ? "开]" : "关]"));
		rootMenu->updateSubMenuItemName("hit_feedback", std::string("伤害数字与准星 [") +
			(G::Vars.hitFeedbackEnabled ? "开]" : "关]"));
	}

	void RefreshKillFeedbackLabels() {
		RefreshRootLabels();
		auto menu = FindMenuById("kill_feedback");
		if (!menu) return;
		menu->updateSubMenuItemName("kill_si_theme", std::string("特感主题 [") +
			F::KillFeedbackMgr.SelectedTheme("si") + "]");
		menu->updateSubMenuItemName("kill_ci_theme", std::string("普感主题 [") +
			F::KillFeedbackMgr.SelectedTheme("ci") + "]");
		menu->updateSubMenuItemName("kill_hit_tip", std::string("主题命中效果 [") +
			(G::Vars.killFeedbackHitMode == 0 ? "关]" :
				(G::Vars.killFeedbackHitMode == 1 ? "仅SI命中]" : "全部命中]")));
		menu->updateSubMenuItemName("kill_sound_volume", std::string("音效音量 [") +
			std::to_string(G::Vars.killFeedbackSoundVolume) + "%]");
	}

	void RefreshCrosshairModeUI() {
		G::Vars.adsHideCrosshairMode = std::clamp(G::Vars.adsHideCrosshairMode, 0, 2);
		const char* labels[] = {"关", "全局", "自定义"};
		const char* label = labels[G::Vars.adsHideCrosshairMode];
		auto adsMenu = FindMenuById("ads");
		if (adsMenu) {
			adsMenu->updateSubMenuItemName("ads_crosshair", std::string("ADS状态隐藏准星 [") + label + "]");
		}
		auto crosshairMenu = FindMenuById("ads_crosshair");
		if (!crosshairMenu) return;
		crosshairMenu->setTitle(std::string("ADS状态隐藏准星 / ") + label);
		crosshairMenu->updateOptionNameByPrefix("全局关", std::string("全局关") + (G::Vars.adsHideCrosshairMode == 0 ? " [当前]" : ""));
		crosshairMenu->updateOptionNameByPrefix("全局开", std::string("全局开") + (G::Vars.adsHideCrosshairMode == 1 ? " [当前]" : ""));
		crosshairMenu->updateOptionNameByPrefix("自定义", std::string("自定义") + (G::Vars.adsHideCrosshairMode == 2 ? " [当前]" : ""));
		crosshairMenu->setSwitchesEnabled(G::Vars.adsHideCrosshairMode == 2);
	}

	void RefreshAppearanceLabels() {
		const char* anchorLabels[] = {"左侧", "居中", "右侧"};
		G::Vars.menuAnchor = std::clamp(G::Vars.menuAnchor, 0, 2);
		auto appearance = FindMenuById("menu_appearance");
		if (appearance) {
			appearance->updateSubMenuItemName("menu_anchor", std::string("界面位置 [") + anchorLabels[G::Vars.menuAnchor] + "]");
			const char* opacityLabel = G::Vars.menuOpacity <= 190 ? "轻透" : (G::Vars.menuOpacity >= 235 ? "高对比" : "标准");
			appearance->updateSubMenuItemName("menu_opacity", std::string("背景透明度 [") + opacityLabel + "]");
		}
		auto anchorMenu = FindMenuById("menu_anchor");
		if (anchorMenu) {
			anchorMenu->updateOptionNameByPrefix("左侧", std::string("左侧") + (G::Vars.menuAnchor == 0 ? " [当前]" : ""));
			anchorMenu->updateOptionNameByPrefix("居中", std::string("居中") + (G::Vars.menuAnchor == 1 ? " [当前]" : ""));
			anchorMenu->updateOptionNameByPrefix("右侧", std::string("右侧") + (G::Vars.menuAnchor == 2 ? " [当前]" : ""));
		}
		auto opacityMenu = FindMenuById("menu_opacity");
		if (opacityMenu) {
			opacityMenu->updateOptionNameByPrefix("轻透", std::string("轻透") + (G::Vars.menuOpacity <= 190 ? " [当前]" : ""));
			opacityMenu->updateOptionNameByPrefix("标准", std::string("标准") + (G::Vars.menuOpacity > 190 && G::Vars.menuOpacity < 235 ? " [当前]" : ""));
			opacityMenu->updateOptionNameByPrefix("高对比", std::string("高对比") + (G::Vars.menuOpacity >= 235 ? " [当前]" : ""));
		}
	}


	bool IsVisible() const { return isVisible; }


	std::shared_ptr<MenuNode> FindMenuById(const std::string& menuId) {
		auto it = menuRegistry.find(menuId);
		if (it != menuRegistry.end() && !it->second.expired()) {
			return it->second.lock();
		}
		return nullptr;
	}


	bool AddOptionToMenu(const std::string& menuId,  const std::string& optionName, std::function<void()> action = nullptr, bool enabled = true) {
		auto menu = FindMenuById(menuId);
		if (!menu) return false;

		menu->addOption(optionName, action, enabled);
		return true;
	}

	bool AddSwitchToMenu(const std::string& menuId, const std::string& switchName, bool initialState = false, std::function<void(bool)> toggleFunc = nullptr, bool enabled = true) {
		auto menu = FindMenuById(menuId);
		if (!menu) return false;

		menu->addSwitch(switchName, initialState, toggleFunc, enabled);
		return true;
	}

	bool PrependSwitchToMenu(const std::string& menuId, const std::string& switchName, bool initialState = false, std::function<void(bool)> toggleFunc = nullptr, bool enabled = true) {
		auto menu = FindMenuById(menuId);
		if (!menu) return false;

		menu->prependSwitch(switchName, initialState, toggleFunc, enabled);
		return true;
	}

	void InitConfigSwitches();


	std::shared_ptr<MenuNode> CreateSubMenu(const std::string& parentMenuId, const std::string& newMenuId, const std::string& newMenuTitle) {
		auto parentMenu = FindMenuById(parentMenuId);
		if (!parentMenu) return nullptr;

		auto newMenu = parentMenu->addSubMenu(newMenuTitle, newMenuId, newMenuTitle);
		registerMenu(newMenu);
		return newMenu;
	}


	bool AddOptionToCurrentMenu(const std::string& optionName, std::function<void()> action = nullptr, bool enabled = true) {
		if (menuStack.empty()) return false;

		auto currentMenu = menuStack.top();
		currentMenu->addOption(optionName, action, enabled);
		return true;
	}

	bool AddSwitchToCurrentMenu(const std::string& switchName, bool initialState = false, std::function<void(bool)> toggleFunc = nullptr, bool enabled = true) {
		if (menuStack.empty()) return false;

		auto currentMenu = menuStack.top();
		currentMenu->addSwitch(switchName, initialState, toggleFunc, enabled);
		return true;
	}


	bool ClearMenuItems(const std::string& menuId) {
		auto menu = FindMenuById(menuId);
		if (!menu) return false;

		menu->clearAllItems();
		return true;
	}

	bool ProcessKey(int keynum) {

		if (!isVisible) return false;

		if (menuStack.empty()) return false;
		auto currentMenu = menuStack.top();

		if (keynum == 0) {
			return handleReturn();
		}

		if (keynum == 8) {
			currentMenu->previousPage();
			return true;
		}

		if (keynum == 9) {
			currentMenu->nextPage();
			return true;
		}

		if (keynum >= 1 && keynum <= 7) {
			int index = keynum - 1;
			return handleMenuItemSelection(index);
		}

		return false;
	}


	void Draw() {
		if (!isVisible || menuStack.empty() || !layoutFits) return;

		auto currentMenu = menuStack.top();
		auto pageItems = currentMenu->getCurrentPageItems();
		int totalPages = currentMenu->getTotalPages();
		int currentPage = currentMenu->getCurrentPage();

		if (flashingItemIndex >= 0 && I::GlobalVars) {
			float elapsed = I::GlobalVars->curtime - flashStartTime;
			const float FLASH_CYCLE_TIME = 0.3f;
			const int MAX_FLASHES = 2;

			if (elapsed >= FLASH_CYCLE_TIME) {
				flashCount++;
				flashStartTime = I::GlobalVars->curtime;
				flashYellow = !flashYellow;

				if (flashCount >= MAX_FLASHES * 2) {
					flashingItemIndex = -1;
					flashCount = 0;
				}
			}
		}

		DrawBackground();

		DrawTitleLine(currentMenu, currentPage, totalPages);
		DrawOptionLines(pageItems);
		DrawNavigationLines(currentPage, totalPages);

	}

	std::string getCurrentPath() const {
		if (menuStack.empty()) return "";

		std::string path;
		auto tempStack = menuStack;
		std::stack<std::shared_ptr<MenuNode>> reverseStack;

		while (!tempStack.empty()) {
			reverseStack.push(tempStack.top());
			tempStack.pop();
		}

		while (!reverseStack.empty()) {
			if (!path.empty()) path += " > ";
			path += reverseStack.top()->getTitle();
			reverseStack.pop();
		}

		return path;
	}

	void DrawBackground() {
		EngineDrawFilledRect(menuX + 5, menuY + 5, menuX + menuWidth + 5, menuY + menuHeight + 5, Color{0, 0, 0, 120});
		EngineDrawFilledRect(menuX, menuY, menuX + menuWidth, menuY + menuHeight,
			Color{24, 28, 30, G::Vars.menuOpacity});
		EngineDrawFilledRect(menuX + 1, menuY + 1, menuX + menuWidth - 1, menuY + LINE_HEIGHT,
			Color{35, 48, 54, std::min(255, G::Vars.menuOpacity + 10)});
		EngineDrawFilledRect(menuX + 1, menuY + NAV_START_LINE * LINE_HEIGHT,
			menuX + menuWidth - 1, menuY + menuHeight - 1,
			Color{20, 23, 25, std::min(255, G::Vars.menuOpacity + 5)});
		EngineDrawOutlinedRect(menuX, menuY, menuX + menuWidth, menuY + menuHeight, C_COLOR_BORDER);
	}


	void DrawTitleLine(std::shared_ptr<MenuNode> menu, int currentPage, int totalPages) {
		std::string title = menu->getTitle();
		if (totalPages > 1) title += "  [" + std::to_string(currentPage + 1) + "/" + std::to_string(totalPages) + "]";
		EngineDrawText(title.c_str(), menuX + 10, menuY + TITLE_LINE * LINE_HEIGHT + 5, C_COLOR_TEXT );

		EngineDrawLine(menuX + 8, menuY + LINE_HEIGHT, menuX + menuWidth - 8, menuY + LINE_HEIGHT, C_COLOR_LINE);
	}

	void DrawOptionLines(const std::vector<MenuItem>& pageItems) {

		if (menuStack.empty()) return;
		auto currentMenu = menuStack.top();
		int currentPage = currentMenu->getCurrentPage();

		int availableLines = OPTION_END_LINE - OPTION_START_LINE + 1;

		size_t itemsToShow = std::min(pageItems.size(), (size_t)availableLines);

		for (size_t i = 0; i < itemsToShow; i++) {
			int lineIndex = OPTION_START_LINE + i;
			int actualIndex = currentPage * MAX_OPTIONS_PER_PAGE + i;
			DrawMenuItem(menuX+12 , menuY + lineIndex * LINE_HEIGHT + 5 , pageItems[i], (int)i + 1, actualIndex);
		}
	}

	void DrawMenuItem(int x, int y, const MenuItem& item, int index, int actualIndex) {
		const Color rowColor = (index % 2 == 0) ? Color{37, 41, 43, 125} : Color{31, 35, 37, 105};
		EngineDrawFilledRect(menuX + 8, y - 3, menuX + menuWidth - 8, y + LINE_HEIGHT - 4, rowColor);
		if (!item.enabled) {
			std::string itemText = "[" + std::to_string(index) + "] " + item.name;
			EngineDrawText(itemText.c_str(), x, y, C_COLOR_TEXT_DISABLED);
			return;
		}
		if (item.type == ITEM_SWITCH) {
			std::string statusText = item.switchState ? "[开]" : "[关]";
			std::string itemText = "[" + std::to_string(index) + "] " + item.name + "  " + statusText;
			if (item.switchState) {
				EngineDrawFilledRect(menuX + 8, y - 3, menuX + 11, y + LINE_HEIGHT - 4, C_COLOR_SWITCH_ON);
				EngineDrawText(itemText.c_str(), x, y, C_COLOR_SWITCH_ON );
			} else {
				EngineDrawText(itemText.c_str(), x, y, C_COLOR_SWITCH_OFF );
			}
		} else {
			std::string itemText = "[" + std::to_string(index) + "] " +  item.name;
			if(item.subMenu) {
				itemText = itemText + " >";
			}

			Color textColor = item.subMenu ? C_COLOR_SUBMENU : C_COLOR_TEXT;
			if (flashingItemIndex == actualIndex && item.type == ITEM_NORMAL) {
				textColor = flashYellow ? C_COLOR_FLASH_YELLOW : C_COLOR_FLASH_GREEN;
			}

			EngineDrawText(itemText.c_str(), x, y , textColor);
		}
	}

	void DrawNavigationLines( int currentPage, int totalPages) {
		int line8 = NAV_START_LINE;
		bool canPrev = currentPage > 0;
		EngineDrawText("[8] 上一页", menuX + 15, menuY + line8 * LINE_HEIGHT + 5,
			canPrev ? C_COLOR_NAV_ENABLED : C_COLOR_NAV_DISABLED);

		int line9 = NAV_START_LINE + 1;
		bool canNext = currentPage < totalPages - 1;
		EngineDrawText("[9] 下一页", menuX + 15, menuY + line9 * LINE_HEIGHT + 5,
			canNext ? C_COLOR_NAV_ENABLED : C_COLOR_NAV_DISABLED);

		int line0 = NAV_START_LINE + 2;
		bool isMainMenu = menuStack.size() == 1;
		if(isMainMenu) {
			EngineDrawText("[0] 关闭", menuX + 15, menuY + line0 * LINE_HEIGHT + 5, C_COLOR_RETURN);
		} else {
			EngineDrawText("[0] 返回", menuX + 15, menuY + line0 * LINE_HEIGHT + 5, C_COLOR_RETURN);
		}

	}

	void InitMenuFonts();

	private:
		struct KillFeedbackSwitchBinding {
			const char* label;
			bool* setting;
		};

		static std::array<KillFeedbackSwitchBinding, 8> KillFeedbackSpecialSwitches() {
			return {{{"Smoker", &G::Vars.killFeedbackSmoker}, {"Boomer", &G::Vars.killFeedbackBoomer},
				{"Hunter", &G::Vars.killFeedbackHunter}, {"Spitter", &G::Vars.killFeedbackSpitter},
				{"Jockey", &G::Vars.killFeedbackJockey}, {"Charger", &G::Vars.killFeedbackCharger},
				{"Tank", &G::Vars.killFeedbackTank}, {"Witch", &G::Vars.killFeedbackWitch}}};
		}

		static std::array<KillFeedbackSwitchBinding, 5> KillFeedbackMethodSwitches() {
			return {{{"普通枪械", &G::Vars.killFeedbackFirearm}, {"爆头", &G::Vars.killFeedbackHeadshot},
				{"近战", &G::Vars.killFeedbackMelee}, {"爆炸", &G::Vars.killFeedbackExplosion},
				{"连杀效果", &G::Vars.killFeedbackMultiKill}}};
		}

		void UpdatePosition() {
			const int margin = 16;
			if (G::Vars.menuAnchor == 1) {
				menuX = (screenWidth - menuWidth) / 2;
			} else if (G::Vars.menuAnchor == 2) {
				menuX = screenWidth - menuWidth - margin;
			} else {
				menuX = margin;
			}
			menuX = std::max(margin, menuX);
			menuY = std::max(8, (screenHeight - menuHeight) / 2);
		}

		void registerMenu(std::shared_ptr<MenuNode> menu) {
			menuRegistry[menu->getId()] = menu;
		}


		bool handleReturn() {
			if (menuStack.size() > 1) {
				menuStack.pop();
				return true;
			} else {
				Toggle();
				return true;
			}
		}

		bool handleMenuItemSelection(int index) {
			if (menuStack.empty()) return false;

			auto currentMenu = menuStack.top();

			int actualIndex = currentMenu->getCurrentPage() * MAX_OPTIONS_PER_PAGE + index;

			MenuItem* item = currentMenu->getItem(actualIndex);

			if (item && item->enabled) {
				if (item->type == ITEM_NORMAL && I::GlobalVars) {
					flashingItemIndex = actualIndex;
					flashStartTime = I::GlobalVars->curtime;
					flashCount = 0;
					flashYellow = true;
				}

				item->execute();

				if (item->type == ITEM_SUBMENU && item->subMenu) {
					menuStack.push(item->subMenu);
					item->subMenu->resetToFirstPage();
				}

				return true;
			}

			return false;
		}

		void EngineDrawFilledRect(const int x1, const int y1, const int x2, const int y2, const Color& color);

		void EngineDrawOutlinedRect(const int x1, const int y1, const int x2, const int y2, const Color& color);

		void EngineDrawText(const char* text, const int x, const int y, const Color& color);

		void EngineDrawLine(const int x, const int y, const int x1, const int y1, const Color& color);

	public:
		static void ToggleNecolaMenu(int* a1);
};

namespace F { inline InGameMenu MenuMgr; }

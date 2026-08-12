#pragma once
#include "../../../sdk/SDK.h"
#include "../../Vars.h"
#include <vector>
#include <string>
#include <functional>
#include <memory>
#include <stack>
#include <cmath>

#include <spdlog/spdlog.h>

#include "../AdsSupport/AdsSupport.h"
#include "../../../sdk/utils/FeatureConfigManager.h"


using json = nlohmann::json;

const int MENU_WIDTH = 480;
const int MENU_HEIGHT = 340;
const int LINE_HEIGHT = 30;
const int TOTAL_LINES = 11;
const int TITLE_LINE = 0;
const int OPTION_START_LINE = 1;
const int OPTION_END_LINE = 7;
const int NAV_START_LINE = 8;
const int MAX_OPTIONS_PER_PAGE = 7;

const Color C_COLOR_BACKGROUND = {30, 30, 30, 230};
const Color C_COLOR_BORDER = {200, 200, 200, 150};
const Color C_COLOR_TEXT = {255, 255, 255, 255};
const Color C_COLOR_TEXT_DISABLED = {150, 150, 150, 200};
const Color C_COLOR_SWITCH_ON = {50, 255, 50, 255};
const Color C_COLOR_SWITCH_OFF = {255, 50, 50, 255};
const Color C_COLOR_SUBMENU = {100, 200, 255, 255};
const Color C_COLOR_NAV_ENABLED = {200, 200, 100, 255};
const Color C_COLOR_NAV_DISABLED = {100, 100, 100, 150};
const Color C_COLOR_RETURN = {255, 150, 150, 255};
const Color C_COLOR_LINE = {100, 100, 100, 200};
const Color C_COLOR_LINE_NAV = {80, 80, 80, 200};
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

	int screenWidth = 1920;
	int screenHeight = 1080;

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
		screenWidth = width;
		screenHeight = height;
		UpdatePosition();
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
		auto seqMenu = rootMenu->addSubMenu("序列修正", "seq", "序列修正");
		registerMenu(seqMenu);

		// Only register the ADS sub-tree
		auto adsMenu = rootMenu->addSubMenu("ADS功能", "ads", "ADS功能");
		registerMenu(adsMenu);

		if (adsMenu) {
			adsMenu->addSwitch("启用ADS", G::Vars.enableAdsSupport, [](bool state) {
				G::Vars.enableAdsSupport = state;
				if (state) {
					F::AdsMgr.Init();
				} else {
					F::AdsMgr.ForceExitADS();
				}
				nlohmann::json doc = NecolaConfig::LoadConfig();
				F::AdsMgr.SaveConfig(doc);
				NecolaConfig::SaveConfig(doc);
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
					crosshairMenu->addOption("全局关", [this, crosshairModeLabel]() {
						G::Vars.adsHideCrosshairMode = 0;
						nlohmann::json doc = NecolaConfig::LoadConfig();
						F::AdsMgr.SaveConfig(doc);
						NecolaConfig::SaveConfig(doc);
						auto a = FindMenuById("ads");
						if (a) a->updateSubMenuItemName("ads_crosshair", "ADS状态隐藏准星 [" + crosshairModeLabel(0) + "]");
					});
					crosshairMenu->addOption("全局开", [this, crosshairModeLabel]() {
						G::Vars.adsHideCrosshairMode = 1;
						nlohmann::json doc = NecolaConfig::LoadConfig();
						F::AdsMgr.SaveConfig(doc);
						NecolaConfig::SaveConfig(doc);
						auto a = FindMenuById("ads");
						if (a) a->updateSubMenuItemName("ads_crosshair", "ADS状态隐藏准星 [" + crosshairModeLabel(1) + "]");
					});
					crosshairMenu->addOption("自定义", [this, crosshairModeLabel]() {
						G::Vars.adsHideCrosshairMode = 2;
						nlohmann::json doc = NecolaConfig::LoadConfig();
						F::AdsMgr.SaveConfig(doc);
						NecolaConfig::SaveConfig(doc);
						auto a = FindMenuById("ads");
						if (a) a->updateSubMenuItemName("ads_crosshair", "ADS状态隐藏准星 [" + crosshairModeLabel(2) + "]");
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

		auto newMenu = std::make_shared<MenuNode>(newMenuId, newMenuTitle);
		parentMenu->addSubMenu(newMenuTitle, newMenuId, newMenuTitle);

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
		if (!isVisible || menuStack.empty()) return;

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
		EngineDrawFilledRect(menuX, menuY, menuX + MENU_WIDTH, menuY + MENU_HEIGHT, C_COLOR_BACKGROUND);
		EngineDrawOutlinedRect(menuX, menuY, menuX + MENU_WIDTH, menuY + MENU_HEIGHT, C_COLOR_BORDER);
	}


	void DrawTitleLine(std::shared_ptr<MenuNode> menu, int currentPage, int totalPages) {
		std::string title = menu->getTitle();
		if (menuStack.size() > 1) {
			auto tempStack = menuStack;
			tempStack.pop();
			if (!tempStack.empty()) {
				title = tempStack.top()->getTitle() + " > " + title;
			}
		}
		EngineDrawText(title.c_str(), menuX + 10, menuY + TITLE_LINE * LINE_HEIGHT + 5, C_COLOR_TEXT );

		EngineDrawLine(menuX + 10, menuY + LINE_HEIGHT - 2,  menuX + MENU_WIDTH - 10, menuY + LINE_HEIGHT - 2, C_COLOR_TEXT);
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
		if (item.type == ITEM_SWITCH) {
			std::string statusText = item.switchState ? "【开】" : "【关】";
			std::string itemText = "[" + std::to_string(index) + "] " +  item.name + " " +  statusText;
			if (item.switchState) {
				EngineDrawText(itemText.c_str(), x, y, C_COLOR_SWITCH_ON );
			} else {
				EngineDrawText(itemText.c_str(), x, y, C_COLOR_SWITCH_OFF );
			}
		} else {
			std::string itemText = "[" + std::to_string(index) + "] " +  item.name;
			if(item.subMenu) {
				itemText = itemText + " >";
			}

			Color textColor = C_COLOR_TEXT;
			if (flashingItemIndex == actualIndex && item.type == ITEM_NORMAL) {
				textColor = flashYellow ? C_COLOR_FLASH_YELLOW : C_COLOR_FLASH_GREEN;
			}

			EngineDrawText(itemText.c_str(), x, y , textColor);
		}
	}

	void DrawNavigationLines( int currentPage, int totalPages) {
		int line8 = NAV_START_LINE;
		bool canPrev = currentPage > 0 && (currentPage != totalPages);
		if(canPrev) {

			EngineDrawText("[8] 上一页", menuX  + 15, menuY + line8 * LINE_HEIGHT + 5, Color{200, 200, 100, 255});
		}

		int line9 = NAV_START_LINE + 1;
		bool canNext = currentPage < totalPages - 1;
		if(canNext) {
			EngineDrawText("[9] 下一页", menuX  + 15, menuY + line9 * LINE_HEIGHT + 5, Color{200, 200, 100, 255});
		}

		int line0 = NAV_START_LINE + 2;
		bool isMainMenu = menuStack.size() == 1;
		if(isMainMenu) {
			EngineDrawText("[0] 关闭", menuX  + 15, menuY + line0 * LINE_HEIGHT + 5, Color{200, 200, 100, 255});
		} else {
			EngineDrawText("[0] 返回", menuX  + 15, menuY + line0 * LINE_HEIGHT + 5, Color{200, 200, 100, 255});
		}

	}

	void InitMenuFonts();

	private:
		void UpdatePosition() {
			const int LEFT_MARGIN = 10;
			menuX = LEFT_MARGIN;
			menuY = (screenHeight - MENU_HEIGHT) / 2;
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

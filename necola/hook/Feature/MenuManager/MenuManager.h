#pragma once
#include "../../../sdk/SDK.h"
#include "../../Vars.h"
#include "../../../sdk/L4NEnv.h"
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
const int MAX_OPTIONS_PER_PAGE = 7;
// line 0: title | lines 1..7: options | lines 8..9: nav
const int TITLE_LINE = 0;
const int NAV_LINE_1 = 1 + MAX_OPTIONS_PER_PAGE;
const int NAV_LINE_2 = NAV_LINE_1 + 1;
const int TOTAL_LINES = NAV_LINE_2 + 1;

const Color C_COLOR_BACKGROUND = {28, 30, 34, 230};
const Color C_COLOR_BORDER = {120, 160, 220, 160};
const Color C_COLOR_TITLE = {255, 255, 255, 255};
const Color C_COLOR_TEXT = {235, 235, 235, 255};
const Color C_COLOR_TEXT_DISABLED = {150, 150, 150, 200};
const Color C_COLOR_SWITCH_ON = {80, 255, 120, 255};
const Color C_COLOR_SWITCH_OFF = {255, 90, 90, 255};
const Color C_COLOR_VALUE = {120, 200, 255, 255};
const Color C_COLOR_SUBMENU = {140, 200, 255, 255};
const Color C_COLOR_NAV_ENABLED = {200, 200, 100, 255};
const Color C_COLOR_NAV_DISABLED = {120, 120, 120, 180};
const Color C_COLOR_RETURN = {255, 170, 140, 255};
const Color C_COLOR_LINE = {100, 100, 100, 200};
const Color C_COLOR_LINE_NAV = {80, 80, 80, 200};
const Color C_COLOR_FLASH_YELLOW = {255, 255, 0, 255};
const Color C_COLOR_FLASH_GREEN = {0, 255, 0, 255};


enum MenuItemType {
	ITEM_NORMAL,   // one-shot action
	ITEM_SWITCH,   // bool toggle
	ITEM_SUBMENU,  // nested menu
	ITEM_CYCLE,    // cycles an int through a label list, shown right-aligned
	ITEM_INFO      // read-only dynamic text (right-aligned), not selectable
};


struct MenuItem {
	std::string name;
	MenuItemType type;
	std::function<void()> action;
	std::function<void(bool)> toggleAction;
	std::shared_ptr<class MenuNode> subMenu;
	bool switchState;
	bool enabled;

	// ITEM_CYCLE
	std::vector<std::string> cycleLabels;
	int cycleIndex = 0;
	std::function<void(int)> cycleAction;

	// ITEM_INFO
	std::function<std::string()> infoText;

	MenuItem(const std::string& n, std::function<void()> act = nullptr, bool en = true)
		: name(n), type(ITEM_NORMAL), action(act), subMenu(nullptr), switchState(false), enabled(en) {}

	MenuItem(const std::string& n, bool initialState, std::function<void(bool)> toggleFunc, bool en = true)
		: name(n), type(ITEM_SWITCH), toggleAction(toggleFunc), subMenu(nullptr),
		  switchState(initialState), enabled(en) {}

	MenuItem(const std::string& n, std::shared_ptr<class MenuNode> sub, bool en = true)
		: name(n), type(ITEM_SUBMENU), action(nullptr), subMenu(sub), switchState(false), enabled(en) {}

	MenuItem(const std::string& n, int initialIndex, const std::vector<std::string>& labels,
			std::function<void(int)> onChange, bool en = true)
		: name(n), type(ITEM_CYCLE), cycleLabels(labels), cycleIndex(initialIndex),
		  cycleAction(onChange), subMenu(nullptr), switchState(false), enabled(en) {}

	MenuItem(const std::string& n, std::function<std::string()> provider)
		: name(n), type(ITEM_INFO), infoText(provider), subMenu(nullptr),
		  switchState(false), enabled(false) {}

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
			case ITEM_CYCLE:
				if (!cycleLabels.empty()) {
					cycleIndex = (cycleIndex + 1) % (int)cycleLabels.size();
					if (cycleAction) cycleAction(cycleIndex);
				}
				break;
			case ITEM_INFO:
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

	void addCycle(const std::string& name, int initialIndex, const std::vector<std::string>& labels,
				  std::function<void(int)> onChange, bool enabled = true) {
		items.emplace_back(name, initialIndex, labels, onChange, enabled);
	}

	void addInfo(const std::string& name, std::function<std::string()> provider) {
		items.emplace_back(name, provider);
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

	bool setSwitchStateByName(const std::string& switchName, bool state) {
		for (auto& item : items) {
			if (item.type == ITEM_SWITCH && item.name == switchName) {
				item.switchState = state;
				return true;
			}
		}
		return false;
	}

	// Bulk-set every switch in this menu without firing callbacks (caller
	// persists config once afterwards).
	void setAllSwitchStates(bool state) {
		for (auto& item : items) {
			if (item.type == ITEM_SWITCH) item.switchState = state;
		}
	}

	bool setCycleIndexByName(const std::string& cycleName, int idx) {
		for (auto& item : items) {
			if (item.type == ITEM_CYCLE && item.name == cycleName && !item.cycleLabels.empty()) {
				item.cycleIndex = idx % (int)item.cycleLabels.size();
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

	HFont inGameMenuFONT = 0;
	int fontPx = 0;

	bool isVisible = false;
	int menuX = 0;
	int menuY = 0;

	int screenWidth = 1920;
	int screenHeight = 1080;

	int flashingItemIndex = -1;
	float flashStartTime = 0.0f;
	int flashCount = 0;
	bool flashYellow = true;

	// ---- dynamic layout (adapts to the configured font size) --------------
	static int FontPxFor(int sizeIdx) {
		static constexpr int sizes[3] = {20, 25, 31};
		return sizes[std::clamp(sizeIdx, 0, 2)];
	}

	int LineHeight() const { return fontPx + 10; }
	int MenuHeight() const { return TOTAL_LINES * LineHeight() + 12; }

	Color BackgroundColor() const {
		static constexpr int alphas[3] = {235, 185, 135};
		return Color{28, 30, 34, alphas[std::clamp(G::Vars.menuOpacity, 0, 2)]};
	}

public:
	InGameMenu() {
		rootMenu = std::make_shared<MenuNode>("root", "主菜单");
		registerMenu(rootMenu);
		initializeDefaultMenus();
		menuStack.push(rootMenu);
	}

	// Shared per-weapon tables: used BOTH to build the menus and to sync
	// them from config, so display names can never drift apart again.
	struct WeaponBool { const char* name; bool* var; };
	struct WeaponInt  { const char* name; int* var; };

	static const std::vector<WeaponBool>& CrosshairWeaponTable() {
		static const std::vector<WeaponBool> t = {
			{"手枪",          &G::Vars.adsHideCrosshairPistol},
			{"双持手枪",      &G::Vars.adsHideCrosshairPistolDual},
			{"马格南",        &G::Vars.adsHideCrosshairDeagle},
			{"UZI",           &G::Vars.adsHideCrosshairUzi},
			{"MAC10",         &G::Vars.adsHideCrosshairMac10},
			{"MP5",           &G::Vars.adsHideCrosshairMP5},
			{"木喷",          &G::Vars.adsHideCrosshairPumpShotgun},
			{"铁喷",          &G::Vars.adsHideCrosshairChromeShotgun},
			{"一代连喷",      &G::Vars.adsHideCrosshairAutoShotgun},
			{"二代连喷",      &G::Vars.adsHideCrosshairSpas},
			{"M16",           &G::Vars.adsHideCrosshairM16A1},
			{"SCAR",          &G::Vars.adsHideCrosshairScar},
			{"AK47",          &G::Vars.adsHideCrosshairAK47},
			{"SG552",         &G::Vars.adsHideCrosshairSSG552},
			{"一代连狙",      &G::Vars.adsHideCrosshairHuntingRifle},
			{"二代连狙",      &G::Vars.adsHideCrosshairMilitarySniper},
			{"SCOUT",         &G::Vars.adsHideCrosshairScout},
			{"AWP",           &G::Vars.adsHideCrosshairAWP},
			{"M60",           &G::Vars.adsHideCrosshairM60},
			{"榴弹发射器",    &G::Vars.adsHideCrosshairGrenadeLauncher},
		};
		return t;
	}

	static const std::vector<WeaponInt>& ScopeWeaponTable() {
		static const std::vector<WeaponInt> t = {
			{"SG552",      &G::Vars.adsScopeSSG552},
			{"一代连狙",   &G::Vars.adsScopeHuntingRifle},
			{"二代连狙",   &G::Vars.adsScopeMilitarySniper},
			{"SCOUT",      &G::Vars.adsScopeScout},
			{"AWP",        &G::Vars.adsScopeAWP},
		};
		return t;
	}

	static void PersistConfig();

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

		// L4N coordination status (live read-only display)
		auto l4nMenu = rootMenu->addSubMenu("L4N状态", "l4n", "L4N状态");
		registerMenu(l4nMenu);
		if (l4nMenu) {
			l4nMenu->addInfo("平台检测", []() -> std::string {
				return L4N::Env.Detected() ? "已检测" : "未检测";
			});
			l4nMenu->addInfo("HUD总开关", []() -> std::string {
				return L4N::Env.HudVisible() ? "开" : "关";
			});
			l4nMenu->addInfo("开镜HUD接管", []() -> std::string {
				return L4N::Env.PatchHudScope() ? "开" : "关";
			});
			l4nMenu->addInfo("v模摆动", []() -> std::string {
				return L4N::Env.SwayEnabled() ? "开" : "关";
			});
			l4nMenu->addInfo("伸手禁摆动", []() -> std::string {
				return L4N::Env.SwayIgnoreHelpingHand() ? "开" : "关";
			});
		}

		// Menu appearance / maintenance
		auto settingsMenu = rootMenu->addSubMenu("菜单设置", "settings", "菜单设置");
		registerMenu(settingsMenu);
		if (settingsMenu) {
			settingsMenu->addCycle("菜单位置", G::Vars.menuPosition, {"左侧", "居中", "右侧"}, [](int v) {
				G::Vars.menuPosition = v;
				F::MenuMgr.OnMenuLayoutChanged();
				PersistConfig();
			});
			settingsMenu->addCycle("背景不透明度", G::Vars.menuOpacity, {"高", "中", "低"}, [](int) {
				PersistConfig();
			});
			settingsMenu->addCycle("字体大小", G::Vars.menuFontSize, {"小", "中", "大"}, [](int) {
				F::MenuMgr.OnMenuLayoutChanged();
				PersistConfig();
			});
			settingsMenu->addOption("重载配置文件", []() {
				F::MenuMgr.ReloadAllConfig();
			});
			settingsMenu->addOption("恢复默认设置", []() {
				F::MenuMgr.ResetToDefaults();
			});
		}

		if (adsMenu) {
			adsMenu->addSwitch("启用ADS", G::Vars.enableAdsSupport, [](bool state) {
				G::Vars.enableAdsSupport = state;
				if (state) {
					F::AdsMgr.Init();
				} else {
					F::AdsMgr.ForceExitADS();
				}
				PersistConfig();
			});

			// Crosshair-hide mode as a single cycling item (was: submenu
			// with 3 one-shot options + manual label resync).
			adsMenu->addCycle("ADS状态隐藏准星", G::Vars.adsHideCrosshairMode,
				{"关", "开", "自定义"}, [](int v) {
					G::Vars.adsHideCrosshairMode = v;
					PersistConfig();
				});

			// Per-weapon crosshair switches + bulk operations
			auto crosshairMenu = adsMenu->addSubMenu("准星武器自定义", "ads_crosshair", "准星武器自定义");
			registerMenu(crosshairMenu);
			if (crosshairMenu) {
				crosshairMenu->addOption("全部开启", []() {
					for (const auto& w : CrosshairWeaponTable()) *w.var = true;
					F::MenuMgr.SyncSwitchMenuFromVars("ads_crosshair");
					PersistConfig();
				});
				crosshairMenu->addOption("全部关闭", []() {
					for (const auto& w : CrosshairWeaponTable()) *w.var = false;
					F::MenuMgr.SyncSwitchMenuFromVars("ads_crosshair");
					PersistConfig();
				});
				for (const auto& w : CrosshairWeaponTable()) {
					bool* varPtr = w.var;
					crosshairMenu->addSwitch(w.name, *varPtr, [varPtr](bool state) {
						*varPtr = state;
						PersistConfig();
					});
				}
			}

			// Per-weapon scope mode as cycling items on one page (was: five
			// nested submenus with 3 one-shot options each).
			auto scopeWeaponMenu = adsMenu->addSubMenu("原生开镜武器设置", "ads_scope_weapons", "原生开镜武器设置");
			registerMenu(scopeWeaponMenu);
			if (scopeWeaponMenu) {
				for (const auto& w : ScopeWeaponTable()) {
					int* varPtr = w.var;
					scopeWeaponMenu->addCycle(w.name, *varPtr, {"关闭", "仅ADS", "混合"}, [varPtr](int v) {
						*varPtr = v;
						PersistConfig();
					});
				}
			}
		}
	}

	// Called when position/font-size cycles change so the layout recomputes.
	void OnMenuLayoutChanged() {
		UpdatePosition();
	}

	void ReloadAllConfig();
	void ResetToDefaults();
	void SyncSwitchMenuFromVars(const std::string& menuId);

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

		EnsureFont();

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

	void DrawBackground() {
		EngineDrawFilledRect(menuX, menuY, menuX + MENU_WIDTH, menuY + MenuHeight(), BackgroundColor());
		EngineDrawOutlinedRect(menuX, menuY, menuX + MENU_WIDTH, menuY + MenuHeight(), C_COLOR_BORDER);
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
		const int y = menuY + TITLE_LINE * LineHeight() + 6;
		EngineDrawText(title.c_str(), menuX + 12, y, C_COLOR_TITLE);

		// Page indicator, right-aligned (only meaningful when paginated)
		if (totalPages > 1) {
			char page[32];
			_snprintf_s(page, sizeof(page), _TRUNCATE, "%d/%d页", currentPage + 1, totalPages);
			int w = TextWidth(page);
			EngineDrawText(page, menuX + MENU_WIDTH - 12 - w, y, C_COLOR_NAV_ENABLED);
		}

		EngineDrawLine(menuX + 10, menuY + LineHeight() + 2,
			menuX + MENU_WIDTH - 10, menuY + LineHeight() + 2, C_COLOR_LINE);
	}

	void DrawOptionLines(const std::vector<MenuItem>& pageItems) {
		if (menuStack.empty()) return;
		auto currentMenu = menuStack.top();
		const int base = currentMenu->getCurrentPage() * MAX_OPTIONS_PER_PAGE;
		for (size_t i = 0; i < pageItems.size(); i++) {
			int lineIndex = 1 + (int)i;
			DrawMenuItem(menuX + 12, menuY + lineIndex * LineHeight() + 4,
				pageItems[i], (int)i + 1, base + (int)i);
		}
	}

	void DrawMenuItem(int x, int y, const MenuItem& item, int index, int actualIndex) {
		std::string itemText = "[" + std::to_string(index) + "] " + item.name;
		Color nameColor = C_COLOR_TEXT;
		std::string valueText;
		Color valueColor = C_COLOR_VALUE;

		switch (item.type) {
			case ITEM_SWITCH:
				valueText = item.switchState ? "开" : "关";
				valueColor = item.switchState ? C_COLOR_SWITCH_ON : C_COLOR_SWITCH_OFF;
				break;
			case ITEM_CYCLE:
				if (!item.cycleLabels.empty())
					valueText = "< " + item.cycleLabels[item.cycleIndex] + " >";
				break;
			case ITEM_INFO:
				if (item.infoText) valueText = item.infoText();
				nameColor = C_COLOR_TEXT_DISABLED;
				valueColor = C_COLOR_TEXT_DISABLED;
				break;
			case ITEM_SUBMENU:
				itemText += " >";
				nameColor = C_COLOR_SUBMENU;
				break;
			case ITEM_NORMAL:
			default:
				if (flashingItemIndex == actualIndex) {
					nameColor = flashYellow ? C_COLOR_FLASH_YELLOW : C_COLOR_FLASH_GREEN;
				}
				break;
		}

		EngineDrawText(itemText.c_str(), x, y, nameColor);

		if (!valueText.empty()) {
			int w = TextWidth(valueText.c_str());
			EngineDrawText(valueText.c_str(), menuX + MENU_WIDTH - 12 - w, y, valueColor);
		}
	}

	void DrawNavigationLines(int currentPage, int totalPages) {
		const int lh = LineHeight();
		const int sepY = menuY + NAV_LINE_1 * lh;
		EngineDrawLine(menuX + 10, sepY, menuX + MENU_WIDTH - 10, sepY, C_COLOR_LINE_NAV);

		const int y1 = sepY + 4;
		if (currentPage > 0) {
			EngineDrawText("[8] 上一页", menuX + 15, y1, C_COLOR_NAV_ENABLED);
		}
		if (currentPage < totalPages - 1) {
			EngineDrawText("[9] 下一页", menuX + 150, y1, C_COLOR_NAV_ENABLED);
		}

		const int y2 = menuY + NAV_LINE_2 * lh + 4;
		const bool isMainMenu = menuStack.size() == 1;
		EngineDrawText(isMainMenu ? "[0] 关闭菜单" : "[0] 返回", menuX + 15, y2, C_COLOR_RETURN);

		const std::string hint = "[1-7] 选择项";
		int hw = TextWidth(hint.c_str());
		EngineDrawText(hint.c_str(), menuX + MENU_WIDTH - 12 - hw, y2, C_COLOR_NAV_DISABLED);
	}

	void InitMenuFonts();

	private:
		void UpdatePosition() {
			switch (std::clamp(G::Vars.menuPosition, 0, 2)) {
				case 0:  menuX = 10; break;
				case 2:  menuX = screenWidth - MENU_WIDTH - 10; break;
				default: menuX = (screenWidth - MENU_WIDTH) / 2; break;
			}
			menuY = (screenHeight - MenuHeight()) / 2;
			if (menuY < 0) menuY = 10;
		}

		void EnsureFont() {
			const int want = FontPxFor(G::Vars.menuFontSize);
			if (want == fontPx && inGameMenuFONT) return;
			fontPx = want;
			if (!I::MatSystemSurface) return;
			if (!inGameMenuFONT) {
				inGameMenuFONT = I::MatSystemSurface->CreateFont();
			}
			I::MatSystemSurface->SetFontGlyphSet(inGameMenuFONT, "Microsoft YaHei", fontPx, 700, 0, 0, FONTFLAG_OUTLINE, 0, 0);
			UpdatePosition(); // height depends on font size
		}

		int TextWidth(const char* text) {
			if (!I::MatSystemSurface || !inGameMenuFONT) return 0;
			wchar_t wstr[512] = {L'\0'};
			MultiByteToWideChar(CP_UTF8, 0, text, -1, wstr, 512);
			int wide = 0, tall = 0;
			I::MatSystemSurface->GetTextSize(inGameMenuFONT, wstr, wide, tall);
			return wide;
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

#include "EngineVGui.h"

#include "../../Vars.h"
#include "../../Feature/MenuManager/MenuManager.h"
#include "../../Feature/AdsSupport/AdsSupport.h"

using namespace Hooks;

void __fastcall EngineVGui::Paint::Detour(void* ecx, void* edx, int mode)
{
	Table.Original<FN>(Index)(ecx, edx, mode);

	// ADS crosshair hide logic
	static bool s_crosshairHidden = false;
	if (mode == PAINT_INGAMEPANELS) {
		if (G::Vars.enableAdsSupport && G::Vars.adsHideCrosshairMode > 0) {
			if (F::AdsMgr.ShouldHideCrosshair()) {
				if (!s_crosshairHidden) {
					if (I::EngineClient && I::EngineClient->IsConnected() && I::EngineClient->IsInGame()) {
						I::EngineClient->ClientCmd("crosshair 0");
						s_crosshairHidden = true;
					}
				}
			} else {
				if (s_crosshairHidden) {
					if (I::EngineClient && I::EngineClient->IsConnected() && I::EngineClient->IsInGame()) {
						I::EngineClient->ClientCmd("crosshair 1");
						s_crosshairHidden = false;
					}
				}
			}
		} else if (s_crosshairHidden) {
			if (I::EngineClient && I::EngineClient->IsConnected() && I::EngineClient->IsInGame()) {
				I::EngineClient->ClientCmd("crosshair 1");
				s_crosshairHidden = false;
			}
		}
	}

	// In-game menu drawing
	if(mode == PAINT_INGAMEPANELS) {
		F::MenuMgr.Draw();
	}

}

void EngineVGui::Init()
{
	Table.Init(I::EngineVGui);
	Table.Hook(&Paint::Detour, Paint::Index);
}

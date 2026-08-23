#include "EngineVGui.h"

#include "../../../sdk/L4NEnv.h"
#include "../../../sdk/l4d2/interfaces/IConVar.h"

#include "../../Vars.h"
#include "../../Feature/MenuManager/MenuManager.h"
#include "../../Feature/AdsSupport/AdsSupport.h"
#include "../../Feature/CampaignTimer/CampaignTimer.h"
#include "../../Feature/KillFeedback/KillFeedback.h"
#include "../../Feature/HitFeedback/HitFeedback.h"

using namespace Hooks;

// ---- ADS crosshair hide ----------------------------------------------------
// L4N-coordinated, ConVar-direct:
//  - respects l4n_game_hud_visible (stands down while L4N hides the HUD,
//    so the plugin never fights the platform's master HUD switch)
//  - writes the `crosshair` ConVar root directly instead of round-tripping
//    ClientCmd("crosshair 0/1"), which is console-visible, one frame late
//    and re-enters the command system from inside a paint hook
//  - snapshots the user's value once and restores it on un-hide / unload
namespace {

ConVar* CrosshairCvar() {
    static ConVar* p = nullptr;
    if (!p && I::Cvars) p = I::Cvars->FindVar("crosshair");
    return p;
}

ConVar* CrosshairRoot() {
    ConVar* p = CrosshairCvar();
    if (!p) return nullptr;
    return p->m_pParent ? p->m_pParent : p;
}

struct CrosshairGate {
    bool hidden = false;
    int  userValue = 1;   // snapshot taken at the moment of hiding
};
CrosshairGate& XHair() { static CrosshairGate g; return g; }

bool InGame() {
    return I::EngineClient && I::EngineClient->IsConnected() && I::EngineClient->IsInGame();
}

} // namespace

void EngineVGui::RestoreCrosshairForUnload() {
    if (!XHair().hidden) return;
    if (ConVar* root = CrosshairRoot()) {
        root->m_nValue = XHair().userValue;
        root->m_fValue = static_cast<float>(XHair().userValue);
    } else if (InGame()) {
        I::EngineClient->ClientCmd("crosshair 1");
    }
    XHair().hidden = false;
}

void __fastcall EngineVGui::Paint::Detour(void* ecx, void* edx, int mode)
{
	if (mode == PAINT_INGAMEPANELS) {
		F::CampaignTimerMgr.UpdateHudMaterials();
	}
	Table.Original<FN>(Index)(ecx, edx, mode);

	if (mode == PAINT_INGAMEPANELS) {
		const bool wantHide =
			G::Vars.enableAdsSupport &&
			G::Vars.adsHideCrosshairMode > 0 &&
			L4N::Env.HudVisible() &&          // L4N master HUD switch
			F::AdsMgr.ShouldHideCrosshair();

		if (wantHide && !XHair().hidden) {
			if (ConVar* root = CrosshairRoot()) {
				XHair().userValue = root->m_nValue;
				root->m_nValue = 0;
				root->m_fValue = 0.0f;
				XHair().hidden = true;
			} else if (InGame()) {
				// Fallback only when ICvar/the ConVar is unavailable.
				I::EngineClient->ClientCmd("crosshair 0");
				XHair().hidden = true;
			}
		} else if (!wantHide && XHair().hidden) {
			if (ConVar* root = CrosshairRoot()) {
				root->m_nValue = XHair().userValue;
				root->m_fValue = static_cast<float>(XHair().userValue);
			} else if (InGame()) {
				I::EngineClient->ClientCmd("crosshair 1");
			}
			XHair().hidden = false;
		}

		if (L4N::Env.HudVisible()) {
			F::KillFeedbackMgr.Draw();
			F::HitFeedbackMgr.Draw();
		}

		// In-game menu drawing
		if (F::MenuMgr.IsVisible() && I::MatSystemSurface) {
			int width = 0;
			int height = 0;
			I::MatSystemSurface->GetScreenSize(width, height);
			F::MenuMgr.SetScreenSize(width, height);
		}
		F::MenuMgr.Draw();
	}
}

bool EngineVGui::Init()
{
	return Table.Init(I::EngineVGui, Paint::Index + 1)
		&& Table.Hook(&Paint::Detour, Paint::Index);
}

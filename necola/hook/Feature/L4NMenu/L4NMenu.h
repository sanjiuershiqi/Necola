#pragma once

#include <string>

// Bridge that exposes Necola's non-ADS settings through L4N's own HUD menu
// (IL4NPlugin::RequestHudMenu, plugin SDK v2 — see necola/l4n_plugin.h).
//
// Design notes:
//  * Deliberately kept OUT of MenuManager: the existing in-game VGUI menu keeps
//    working unchanged, so a wrong guess about L4N's callback contract can
//    never break the plugin. If L4N never calls us we simply never appear.
//  * Everything is rebuilt on demand: item labels carry the live state
//    ("... [开]" / "... [关]"), and a callback returns the same tree with the
//    reserved "_refresh" key so L4N refreshes in place instead of pushing a
//    submenu.
//  * Callbacks are one free function; the item is selected by the integer
//    encoded in the "user_data" string, so no pointer lifetime games.
namespace L4NMenu {

// Title advertised to L4N. Returning this from RequestHudMenu(true) is what
// makes the platform show a menu entry for the plugin.
const char* Title();

// KeyValues tree for the root menu. The returned pointer stays valid until the
// next Title()/Root()/Invalidate() call.
const char* Root();

// Drop the cached tree (call after external state changes if desired).
void Invalidate();

} // namespace L4NMenu

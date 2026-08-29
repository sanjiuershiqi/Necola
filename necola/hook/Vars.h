#pragma once

#include <cstdlib>
#include <inipp.h>
#include <filesystem>
#include <fstream>

class CGlobal_Vars
{
public:
    // ADS (AdsSupport) configuration
    bool enableAdsSupport = false;
    bool adsLog = false;
    // 0=never hide, 1=hide for every ADS weapon, 2=per-weapon settings
    int adsHideCrosshairMode = 0;
    // Per-weapon crosshair settings, active only when adsHideCrosshairMode == 2.
    bool adsHideCrosshairPistol = false;
    bool adsHideCrosshairUzi = false;
    bool adsHideCrosshairPumpShotgun = false;
    bool adsHideCrosshairAutoShotgun = false;
    bool adsHideCrosshairM16A1 = false;
    bool adsHideCrosshairHuntingRifle = false;
    bool adsHideCrosshairMac10 = false;
    bool adsHideCrosshairChromeShotgun = false;
    bool adsHideCrosshairScar = false;
    bool adsHideCrosshairMilitarySniper = false;
    bool adsHideCrosshairSpas = false;
    bool adsHideCrosshairGrenadeLauncher = false;
    bool adsHideCrosshairAK47 = false;
    bool adsHideCrosshairDeagle = false;
    bool adsHideCrosshairMP5 = false;
    bool adsHideCrosshairSSG552 = false;
    bool adsHideCrosshairAWP = false;
    bool adsHideCrosshairScout = false;
    bool adsHideCrosshairM60 = false;
    bool adsHideCrosshairPistolDual = false;
    // Native-scope behavior: 0=disabled, 1=ADS only, 2=mixed.
    int adsScopeMilitarySniper = 0;
    int adsScopeHuntingRifle = 0;
    int adsScopeSSG552 = 0;
    int adsScopeAWP = 0;
    int adsScopeScout = 0;

    // SequenceModify configuration (used by ADS layer sequence tracking)
    bool animSequenceModify = false;
    bool ignoreShotgunSequence = false;
    bool sequenceLog = false;

    // External weapon VPK inspection/fidget animation support.
    bool openInspect = true;
    int inspectKey = 0x52;
    bool inspectIgnoreAmmo = false;
    int helpingHandRandom = 0;

    // In-game menu appearance.
    int menuAnchor = 0;       // 0=left, 1=center, 2=right
    int menuOpacity = 220;    // background alpha, clamped to 160..245

    // Themed hit/kill feedback (themes from skeeto_killfeed.vpk).
    bool killFeedbackEnabled = false;
    bool killFeedbackLog = false;
    bool killFeedbackCommon = true;
    bool killFeedbackSpecial = true;
    bool killFeedbackSmoker = true;
    bool killFeedbackBoomer = true;
    bool killFeedbackHunter = true;
    bool killFeedbackSpitter = true;
    bool killFeedbackJockey = true;
    bool killFeedbackCharger = true;
    bool killFeedbackTank = true;
    bool killFeedbackWitch = true;
    bool killFeedbackIcon = true;
    bool killFeedbackSound = true;
    bool killFeedbackFirearm = true;
    bool killFeedbackHeadshot = true;
    bool killFeedbackMelee = true;
    bool killFeedbackExplosion = true;
    bool killFeedbackMultiKill = true;
    // Themed hits: 0=off, 1=SI only, 2=SI + Common/Witch. Independent of killFeedbackEnabled.
    int killFeedbackHitMode = 1;
    bool killFeedbackSiDedicated = true;  // original skeeto default: SI visual wins
    bool killFeedbackSiSound = true;      // true: SI scalar sound wins when available
    int killFeedbackSoundVolume = 100;

    // Independent damage UI: floating numbers + crosshair marker, never gates themed feedback.
    bool hitFeedbackEnabled = false;
    bool hitFeedbackNumbers = true;
    bool hitFeedbackHitMarker = true;
    bool hitFeedbackCommon = false;

	void Load();
};

namespace G { inline CGlobal_Vars Vars; }

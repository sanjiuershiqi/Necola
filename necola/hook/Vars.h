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

    // In-game menu appearance.
    int menuAnchor = 0;       // 0=left, 1=center, 2=right
    int menuOpacity = 220;    // background alpha, clamped to 160..245

    // External CF-style kill feedback.
    bool killFeedbackEnabled = false;
    bool killFeedbackLog = true;
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
    bool killFeedbackVisual = true;
    bool killFeedbackSound = true;
    bool killFeedbackFirearm = true;
    bool killFeedbackHeadshot = true;
    bool killFeedbackMelee = true;
    bool killFeedbackExplosion = true;
    bool killFeedbackMultiKill = true;
    float killFeedbackWindow = 3.0f;

	void Load();
};

namespace G { inline CGlobal_Vars Vars; }

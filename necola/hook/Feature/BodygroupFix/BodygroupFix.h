#pragma once
#include "../../../sdk/SDK.h"

#include <string>
#include <vector>

namespace F {

// Applies bodygroup values after viewmodel animation events.
class BodygroupFixManager {
public:
    // Called when AE_CL_BODYGROUP_SET_VALUE (37) is intercepted.
    void OnBodygroupEvent(const char* groupName, int groupValue);

    void ClearBodygroupCache(C_BaseAnimating* viewModel);

    void FrameUpdate();
    void Reset();

private:
    struct BodygroupEntry {
        std::string groupName;
        int groupIndex = -1;
        int groupValue = 0;
    };
    std::vector<BodygroupEntry> m_bodygroupCache;
    int m_cachedWeaponEntIdx = -1;

    // bake irradiance into the lightmap atlas
    void ApplyBodygroups(C_BaseAnimating* viewModel);
};

inline BodygroupFixManager BodygroupFix;

} // namespace F

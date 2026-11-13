#include "pch.h"

#include "Games/Empty/HotReloadProbeComponent.h"

#include "Engine/Automation/Watch/AutomationComponentWatch.h"

namespace sw
{
    namespace
    {
        struct HotReloadProbeInternal
        {
            /** @brief 모듈 판입니다 — `Scripts/dev/hot_reload_debug_check.py` 가 이 줄의 숫자만 바꿔 다시 짓고 끝나면 되돌린다. */
            static constexpr int32 kProbeVersion = 100;
        };
    } // namespace

    HotReloadProbeComponent::HotReloadProbeComponent()
        : _keptValue{ 0 }
    // HOTRELOAD-LAYOUT-INIT
    {
    }

    int32 HotReloadProbeComponent::computeVersion() const
    {
        return HotReloadProbeInternal::kProbeVersion;
    }

    int32 HotReloadProbeComponent::getAddedValue() const
    {
        return 0; // HOTRELOAD-LAYOUT-READ
    }

    SW_AUTOMATION_GETTER_WATCH( hotReloadProbeVersion, "HotReload.ProbeVersion", "Module version number the probe component's code returns (changes when the hot reload check relinks it)",
                                HotReloadProbeComponent, &HotReloadProbeComponent::computeVersion );
    SW_AUTOMATION_GETTER_WATCH( hotReloadProbeKept, "HotReload.ProbeKept", "Scene-set value on the probe component (a hot reload must carry it over)", HotReloadProbeComponent,
                                &HotReloadProbeComponent::getKeptValue );
    SW_AUTOMATION_GETTER_WATCH( hotReloadProbeAdded, "HotReload.ProbeAdded", "Field the layout variant of the hot reload check adds (0 when absent, its constructor value after the reload)",
                                HotReloadProbeComponent, &HotReloadProbeComponent::getAddedValue );
} // namespace sw

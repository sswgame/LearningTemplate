#include "pch.h"

#include "Games/Empty/HotReloadTestComponent.h"

#include "Engine/Automation/Watch/AutomationComponentWatch.h"

namespace sw
{
    namespace
    {
        struct HotReloadTestInternal
        {
            /** @brief 모듈 판입니다 — `Scripts/dev/hot_reload_debug_check.py` 가 이 줄의 숫자만 바꿔 다시 짓고 끝나면 되돌린다. */
            static constexpr int32 kTestVersion = 100;
        };
    } // namespace

    HotReloadTestComponent::HotReloadTestComponent()
        : _keptValue{ 0 }
    // HOTRELOAD-LAYOUT-INIT
    {
    }

    int32 HotReloadTestComponent::computeVersion() const
    {
        return HotReloadTestInternal::kTestVersion;
    }

    int32 HotReloadTestComponent::getAddedValue() const
    {
        return 0; // HOTRELOAD-LAYOUT-READ
    }

    SW_AUTOMATION_GETTER_WATCH( hotReloadTestVersion, "HotReload.TestVersion", "Module version number the probe component's code returns (changes when the hot reload check relinks it)",
                                HotReloadTestComponent, &HotReloadTestComponent::computeVersion );
    SW_AUTOMATION_GETTER_WATCH( hotReloadTestKept, "HotReload.TestKept", "Scene-set value on the probe component (a hot reload must carry it over)", HotReloadTestComponent,
                                &HotReloadTestComponent::getKeptValue );
    SW_AUTOMATION_GETTER_WATCH( hotReloadTestAdded, "HotReload.TestAdded", "Field the layout variant of the hot reload check adds (0 when absent, its constructor value after the reload)",
                                HotReloadTestComponent, &HotReloadTestComponent::getAddedValue );
} // namespace sw

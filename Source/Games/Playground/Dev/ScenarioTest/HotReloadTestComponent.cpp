#include "pch.h"

#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "sw/dev/HotReloadProbeVariant.h" // 빌드 트리의 판 머리(`SW_HOTRELOAD_PROBE_STEP`) — 시나리오 단계 `<RebuildModule variant>` 가 덮는다(Games/Playground/CMakeLists.txt)

namespace sw
{
    /**
     * @class HotReloadTestComponent
     * @brief 판(`computeVersion`)과 리로드를 건너야 하는 칸(`_keptValue`)을 내는 확인용 컴포넌트입니다.
     */
    REFLECT()
    class HotReloadTestComponent : public Component
    {
    public:
        REFLECT_BODY();

        HotReloadTestComponent();
        ~HotReloadTestComponent() override = default;

        /** @brief 이 모듈 판의 숫자입니다. 디버거가 여기에 브레이크포인트를 건다(인라인되지 않게 — 리로드 전후 두 모듈에 같은 자리가 있다). */
        SW_NOINLINE int32 computeVersion() const;
        /** @brief 씬이 정한 값입니다 — 리로드가 리플렉션 칸으로 옮겨야 같다. */
        int32 getKeptValue() const { return _keptValue; }
        /** @brief 헤더 변형(칸 추가)에서 더한 칸의 값입니다. 칸이 없으면 0 입니다 — 스크립트가 이 함수 몸도 함께 바꾼다. */
        int32 getAddedValue() const;

    private:
        PROPERTY( Category = "Probe", Tooltip = "Value the scene sets; a hot reload must carry it over" )
        int32 _keptValue;
        // HOTRELOAD-LAYOUT
    };

    namespace
    {
        struct HotReloadTestInternal
        {
            /** @brief 모듈 판입니다 — `Scripts/verify/hot_reload_debug_check.py` 가 이 줄의 숫자만 바꿔 다시 짓고 끝나면 되돌린다. */
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
        return HotReloadTestInternal::kTestVersion + SW_HOTRELOAD_PROBE_STEP; // 판 머리의 걸음 — 기본 0, 다시 짓기 판(probe-next)은 1
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

#include "HotReloadTestComponent.cpp.gen.inl"

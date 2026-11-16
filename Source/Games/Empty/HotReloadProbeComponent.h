/**
 * @file HotReloadProbeComponent.h
 * @brief 핫 리로드 디버그 확인(`Scripts/dev/hot_reload_debug_check.py`)이 고쳐 다시 짓는 컴포넌트입니다 — 디버거가 붙은 채 모듈을 다시 링크 · 리로드해도
 *        브레이크포인트가 새 모듈에서 잡히는지, 헤더(칸 추가 · 생성자 기본값)가 바뀌어도 리로드가 상태를 옮기는지 본다.
 * @details 스크립트가 고치는 자리는 두 곳이다 — `.cpp` 의 `kProbeVersion`(코드만 바뀜)과 이 헤더의 `HOTRELOAD-LAYOUT` 줄(칸 하나와 그 생성자 값을 더함).
 *          바꾼 뒤에는 원래대로 되돌린다. 시나리오는 `game/empty/automation/debugger/hotreloadbreakpoint.scenario.xml`(검증 체인 밖 — 스크립트가 띄운다).
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /**
     * @class HotReloadProbeComponent
     * @brief 판(`computeVersion`)과 리로드를 건너야 하는 칸(`_keptValue`)을 내는 확인용 컴포넌트입니다.
     */
    REFLECT()
    class HotReloadProbeComponent : public Component
    {
    public:
        REFLECT_BODY();

        HotReloadProbeComponent();
        ~HotReloadProbeComponent() override = default;

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
} // namespace sw

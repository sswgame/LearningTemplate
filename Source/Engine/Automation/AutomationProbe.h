/**
 * @file AutomationProbe.h
 * @brief 자동화 탐침 — 시나리오의 `<Expect probe="…">` 가 읽는 이름 붙은 값입니다. 게임 · 키트가 .cpp 에 `SW_AUTOMATION_PROBE` 한 줄로 등록합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

namespace sw
{
    class GameObjectManager;

    /** @brief 탐침 함수입니다 — 활성 씬의 오브젝트 매니저(없으면 nullptr)를 받아 값을 씁니다. 값을 낼 수 없으면(대상 없음) false 입니다. 게임 스레드에서 부릅니다. */
    using AutomationProbeFunction = bool ( * )( const GameObjectManager* pManager, float64& outValue );

    /** @struct AutomationProbeRegistration @brief 등록 줄 하나 — 문자열 · 함수는 등록한 이미지의 정적 저장소에 있습니다. */
    struct AutomationProbeRegistration
    {
        const utf8*             _pName;    ///< `<게임>.<값>` (예: `Shooter3D.WeaponIndex`)
        const utf8*             _pTooltip; ///< 무엇을 재는가
        AutomationProbeFunction _pFunction;
    };
} // namespace sw

namespace sw
{
    /**
     * @class AutomationProbes
     * @brief 탐침 등록표입니다. 모듈을 내리면(핫 리로드) 그 모듈의 탐침도 빠집니다. 같은 이름이 둘이면 뒤 것을 거절하고 오류를 남깁니다.
     * @details 등록부는 Engine 이미지의 함수 정적이라 게임 모듈이 바뀌어도 남습니다(`GameAutoplay` 와 같은 모양). 게임 스레드 · 정적 초기화에서만 씁니다.
     */
    class SW_API AutomationProbes
    {
    public:
        /** @brief 등록합니다. 같은 이름이 이미 있으면 false 이고 등록하지 않습니다. */
        static bool registerProbe( const AutomationProbeRegistration* pRegistration );
        static void unregisterProbe( const AutomationProbeRegistration* pRegistration );
        /** @brief 이름으로 찾습니다. 없으면 nullptr 입니다. */
        static const AutomationProbeRegistration* find( string_view name );
    };
} // namespace sw

namespace sw
{
    /** @brief `SW_AUTOMATION_PROBE` 가 두는 정적 등록자입니다. 등록이 거절됐으면 내릴 때 빼지 않습니다(앞 등록을 지우지 않게). */
    struct SW_API AutomationProbeRegistrar
    {
        explicit AutomationProbeRegistrar( const AutomationProbeRegistration* pRegistration );
        ~AutomationProbeRegistrar();
        AutomationProbeRegistrar( const AutomationProbeRegistrar& )            = delete;
        AutomationProbeRegistrar& operator=( const AutomationProbeRegistrar& ) = delete;

        const AutomationProbeRegistration* _pRegistration;
    };
} // namespace sw

/**
 * @brief 탐침을 등록합니다(.cpp 의 파일 범위, `namespace sw` 안). @p id 는 정적 이름을 만드는 식별자, @p pName 은 시나리오가 쓰는 이름입니다.
 * @details Shipping(한 exe 정적 링크)에서 등록자가 링크에서 빠지지 않게 그 게임의 컴포넌트 .cpp(다른 기호가 쓰이는 파일)에 둡니다.
 */
#define SW_AUTOMATION_PROBE( id, pName, pTooltip, pFunction )                                             \
    static const ::sw::AutomationProbeRegistration sw_automationProbe_##id{ pName, pTooltip, pFunction }; \
    static const ::sw::AutomationProbeRegistrar    sw_automationProbeRegistrar_##id { &sw_automationProbe_##id }

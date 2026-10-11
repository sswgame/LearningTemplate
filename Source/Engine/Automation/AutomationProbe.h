/**
 * @file AutomationProbe.h
 * @brief 자동화 탐침을 정의합니다. 시나리오의 `<Expect probe="…">` 가 읽는 이름 붙은 값이며, 게임과 키트가 .cpp 에 `SW_AUTOMATION_PROBE` 한 줄로 등록합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

namespace sw
{
    class GameObjectManager;

    /** @brief 탐침 함수입니다. 활성 씬의 오브젝트 매니저(없으면 nullptr)를 받아 값을 씁니다. 값을 낼 수 없으면(대상 없음) false 입니다. 게임 스레드에서 부릅니다. */
    using AutomationProbeFunction = bool ( * )( const GameObjectManager* pManager, float64& outValue );

    /** @struct AutomationProbeRegistration @brief 등록 항목 하나입니다. 문자열과 함수는 등록한 이미지의 정적 저장소에 있습니다. */
    struct AutomationProbeRegistration
    {
        const utf8*             _pName;    ///< `<게임>.<값>` (예: `Shooter3D.WeaponIndex`)
        const utf8*             _pTooltip; ///< 무엇을 측정하는지 설명합니다.
        AutomationProbeFunction _pFunction;
    };
} // namespace sw

namespace sw
{
    /**
     * @class AutomationProbes
     * @brief 탐침 레지스트리입니다. 모듈을 언로드하면(핫 리로드) 그 모듈의 탐침도 빠집니다. 같은 이름이 둘이면 뒤의 것을 거절하고 오류를 남깁니다.
     * @details 레지스트리는 Engine 이미지의 함수 정적이라 게임 모듈이 바뀌어도 남는다(`GameAutoplay` 와 같은 구조). 게임 스레드와 정적 초기화에서만 쓴다.
     */
    class SW_API AutomationProbes
    {
    public:
        /** @brief 등록합니다. 같은 이름이 이미 있으면 false 이고 등록하지 않습니다. */
        static bool registerProbe( const AutomationProbeRegistration* pRegistration );
        static void unregisterProbe( const AutomationProbeRegistration* pRegistration );
        /** @brief 이름으로 찾습니다. 없으면 nullptr 입니다. */
        static const AutomationProbeRegistration* find( string_view name );
        /** @brief 언로드된 모듈이 키운 레지스트리 용량을 해제합니다. 엔진 종료 끝(`EngineBootstrap::shutdown`)이 모듈을 모두 언로드한 뒤 부릅니다. */
        static void releaseUnusedStorage();
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
 * @details Shipping(한 exe 정적 링크)에서 등록자가 링크에서 빠지지 않게 그 게임의 컴포넌트 .cpp(다른 심볼이 쓰이는 파일)에 둔다.
 */
#define SW_AUTOMATION_PROBE( id, pName, pTooltip, pFunction )                                             \
    static const ::sw::AutomationProbeRegistration sw_automationProbe_##id{ pName, pTooltip, pFunction }; \
    static const ::sw::AutomationProbeRegistrar    sw_automationProbeRegistrar_##id { &sw_automationProbe_##id }

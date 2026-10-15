/**
 * @file AutomationProbe.h
 * @brief 자동화 탐침을 정의합니다. 시나리오의 `<Expect probe="…">` 가 읽는 이름 붙은 값이며, 게임과 키트가 .cpp 에 `SW_AUTOMATION_PROBE` 한 줄로 등록합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

namespace sw
{
    class GameObject;
    class GameObjectManager;

    /** @brief 탐침 함수입니다. 활성 씬의 오브젝트 매니저(없으면 nullptr)를 받아 값을 씁니다. 값을 낼 수 없으면(대상 없음) false 입니다. 게임 스레드에서 부릅니다. */
    using AutomationProbeFunction = bool ( * )( const GameObjectManager* pManager, float64& outValue );

    /**
     * @struct AutomationProbeValue
     * @brief 질의 탐침이 내는 값 하나입니다. 수(`near` · `atLeast` 등으로 비교)나 글(`is` 로 비교 — AI 상태 · 일정 활동 이름)입니다.
     * @details 수 값도 `is` 로 비교할 수 있다(실행기가 끝의 0 을 정리한 글로 바꿔 견준다). 글 값은 수 비교를 할 수 없다.
     */
    struct AutomationProbeValue
    {
        string  _text{};
        float64 _number{ 0.0 };
        bool    _bText{ false };

        void setNumber( float64 number )
        {
            _number = number;
            _bText  = false;
        }
        void setText( string_view text )
        {
            _text  = string( text );
            _bText = true;
        }
    };
} // namespace sw

namespace sw
{
    /**
     * @struct AutomationProbeQuery
     * @brief 질의 탐침이 받는 인자입니다. 단계의 `target` · `key` 속성이 들어옵니다.
     * @details `_pTarget` 은 활성 씬에서 이름이 `target` 인 오브젝트다(없으면 nullptr). 오브젝트가 아닌 대상(일정 NPC id, AI 이름)은 `_target` 글로 찾는다.
     */
    struct AutomationProbeQuery
    {
        const GameObjectManager* _pManager{ nullptr };
        const GameObject*        _pTarget{ nullptr };
        string_view              _target{};
        string_view              _key{};
    };
} // namespace sw

namespace sw
{
    /** @brief 질의 탐침 함수입니다. 값을 낼 수 없으면 false 이며 @p outValue 의 글에 이유를 적을 수 있습니다(실패 보고에 실린다). 게임 스레드에서 부릅니다. */
    using AutomationQueryProbeFunction = bool ( * )( const AutomationProbeQuery& query, AutomationProbeValue& outValue );

    /** @struct AutomationProbeRegistration @brief 등록 항목 하나입니다. 문자열과 함수는 등록한 이미지의 정적 저장소에 있습니다. 두 함수 중 하나만 있습니다. */
    struct AutomationProbeRegistration
    {
        const utf8*                  _pName;          ///< `<게임>.<값>` (예: `Shooter3D.WeaponIndex`)
        const utf8*                  _pTooltip;       ///< 무엇을 측정하는지 설명합니다.
        AutomationProbeFunction      _pFunction;      ///< 씬 탐침(인자 없음, 수)
        AutomationQueryProbeFunction _pQueryFunction; ///< 질의 탐침(`target` · `key`, 수 또는 글)
        bool                         _bNeedsTarget;   ///< 단계에 `target` 이 꼭 있어야 합니다.
        bool                         _bNeedsKey;      ///< 단계에 `key` 가 꼭 있어야 합니다.
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
        /** @brief 탐침 하나를 읽습니다. 씬 탐침은 `_pManager` 만 쓰고 수를 냅니다. 값이 없으면 false 입니다. */
        [[nodiscard]] static bool read( const AutomationProbeRegistration& registration, const AutomationProbeQuery& query, AutomationProbeValue& outValue );
        /** @brief 언로드된 모듈이 키운 레지스트리 용량을 해제합니다. 엔진 종료 끝(`StaticStorageSubsystem::shutdownSubsystem`)이 모듈을 모두 언로드한 뒤 부릅니다. */
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
#define SW_AUTOMATION_PROBE( id, pName, pTooltip, pFunction )                                                                    \
    static const ::sw::AutomationProbeRegistration sw_automationProbe_##id{ pName, pTooltip, pFunction, nullptr, false, false }; \
    static const ::sw::AutomationProbeRegistrar    sw_automationProbeRegistrar_##id                                              \
    {                                                                                                                            \
        &sw_automationProbe_##id                                                                                                 \
    }

/**
 * @brief 질의 탐침을 등록합니다(.cpp 의 파일 범위, `namespace sw` 안). 단계의 `target`(오브젝트 이름 · NPC id) · `key`(블랙보드 키 · 장소 id)를 받고 수나 글을 냅니다.
 * @details 시나리오: `<Expect probe="AI.State" target="AutoFarmer" is="Harvest"/>`, `<WaitUntil probe="Object.DistanceTo" target="Farmer" key="Bin" atMost="1"/>`.
 */
#define SW_AUTOMATION_QUERY_PROBE( id, pName, pTooltip, pQueryFunction, bNeedsTarget, bNeedsKey )                                                \
    static const ::sw::AutomationProbeRegistration sw_automationProbe_##id{ pName, pTooltip, nullptr, pQueryFunction, bNeedsTarget, bNeedsKey }; \
    static const ::sw::AutomationProbeRegistrar    sw_automationProbeRegistrar_##id                                                              \
    {                                                                                                                                            \
        &sw_automationProbe_##id                                                                                                                 \
    }

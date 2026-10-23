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
    /** @brief 질의의 `target` 을 오브젝트로 찾은 결과입니다. */
    enum class AutomationTargetLookup : uint8
    {
        NotGiven = 0, ///< `target` 이 없다
        Found,        ///< 오브젝트 하나를 찾았다
        Missing,      ///< 그 이름(그 번호)의 오브젝트가 없다 — `Object.Exists` 는 0 이다
        Rejected      ///< 글이 잘못됐거나 여럿 중 하나를 고를 수 없다 — 어떤 탐침도 값을 내지 않는다
    };

    /**
     * @class AutomationProbeQuery
     * @brief 질의 탐침이 받는 인자입니다. 단계의 `target` · `key` 속성이 들어옵니다. 만드는 길은 `make` 하나라 대상 고르기 규칙(`AutomationTarget`)을 건너뛸 수 없습니다.
     * @details 오브젝트가 아닌 대상(일정 NPC id)은 `_target` 글로 찾고, 오브젝트 대상은 `findTargetObject` 로 받는다. 못 찾았으면 `getTargetProblem` 이 실패 줄에 실을 까닭이다.
     */
    class SW_API AutomationProbeQuery
    {
    public:
        const GameObjectManager* _pManager{ nullptr };
        string_view              _target{};
        string_view              _key{};

        /** @brief 활성 씬 @p pManager 에서 @p target 을 고르기 규칙으로 찾아 둔 질의를 만듭니다. @p target 이 비면 대상이 없는 질의입니다. */
        [[nodiscard]] static AutomationProbeQuery make( const GameObjectManager* pManager, string_view target, string_view key );

        const GameObject*      findTargetObject() const { return _pTargetObject; }
        AutomationTargetLookup getTargetLookup() const { return _targetLookup; }
        /** @brief 대상 오브젝트가 없을 때 그 까닭입니다(`no object named 'X'`, 여럿이라 고를 수 없음 등). 찾았으면 비어 있습니다. */
        const string& getTargetProblem() const { return _targetProblem; }
        /** @brief 대상은 찾았지만 @p pWhat(예: `transform`)이 없을 때 실패 줄에 실을 까닭입니다(`'X' has no transform`). */
        [[nodiscard]] string describeMissing( const utf8* pWhat ) const;

    private:
        AutomationProbeQuery() = default;

        const GameObject*      _pTargetObject{ nullptr };
        string                 _targetProblem{};
        AutomationTargetLookup _targetLookup{ AutomationTargetLookup::NotGiven };
    };
} // namespace sw

namespace sw
{
    /**
     * @struct AutomationTarget
     * @brief 시나리오의 `target` 글을 오브젝트 하나로 고르는 규칙입니다. 탐침과 단계(`Mount` 의 rider 등)가 모두 이것을 씁니다.
     * @details 이름의 밑과 번호는 언리얼 `FName` 처럼 나눈다 — `Tree_3` 은 밑 `Tree`, 번호 3 이다(끝의 `_` + 0 으로 시작하지 않는 숫자). 같은 이름을 다시 만들면
     *          오브젝트 저장소가 `_2` · `_3` 을 붙이므로(`GameObjectStore::makeUniqueNameUnlocked`) 밑이 같은 오브젝트가 여럿 생긴다.
     *          - `Name` : 그 이름 그대로의 오브젝트. 밑이 `Name` 인 다른 살아 있는 오브젝트(`Name_2` …)가 있으면 고를 수 없다(`Rejected`) — 저장소가 번호를
     *            재사용하므로 "맨 앞의 것" 은 실행마다 다른 오브젝트일 수 있다. 조용히 첫째를 고르지 않는다.
     *          - `Name_3` : 그 이름 그대로(번호까지 적었으면 그 오브젝트를 뜻한 것이다).
     *          - `Name[N]` : 밑이 `Name` 인 살아 있는 오브젝트를 만든 순서(오브젝트 id 오름차순)로 늘어놓은 N 번째(0 부터). 없으면 `Missing`.
     */
    struct SW_API AutomationTarget
    {
        /** @brief @p target 을 고릅니다. 찾으면 그 오브젝트이고, 못 찾으면 nullptr 이며 @p outLookup · @p outProblem 에 까닭을 적습니다. */
        static GameObject* find( const GameObjectManager* pManager, string_view target, AutomationTargetLookup& outLookup, string& outProblem );
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
 * @details Shipping 은 Engine · GameFramework · 키트 · 게임을 통째로 링크하므로(`sw_linkWholeArchive`) 어느 .cpp 에 두어도 등록자가 링크에서 빠지지 않는다.
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

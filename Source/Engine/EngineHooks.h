/**
 * @file EngineHooks.h
 * @brief 엔진 바깥(자동화 시나리오 · 텔레메트리 · 개발 도구 같은 외부 구동기)이 엔진에 붙는 자리입니다 — 프레임 경계 훅 묶음 · 그 문맥 · 훅 목록(`EngineFrameHookList`)과
 *        기동 확장 델리게이트(`EngineStartupExtensionDelegate`).
 * @details 루프는 훅 목록만 알고 무엇이 붙었는지는 모릅니다(언리얼 `FCoreDelegates::OnBeginFrame` · 유니티 PlayerLoop 훅과 같은 자리).
 *          붙는 쪽은 인터페이스를 상속하지 않고 쓰는 시점의 칸만 채운다. Core 와 생성된 종료 코드 표(의존 없는 헤더)만 include 하므로 어느 Engine 티어에서 써도 됩니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Delegate/Subscription.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "sw/config/ProcessExitCodes.gen.h"

namespace sw
{
    class InputManager;
    class SubsystemScope;

    /** @brief 프레임 앞 · 씬 틱 뒤 · 프레임 뒤와 창 닫힘에서 관찰자에게 넘기는 문맥입니다. */
    struct EngineFrameContext
    {
        /** @brief 이번 프레임의 입력 관리자입니다. 입력 단계가 서지 않은 호스트에서는 nullptr 입니다. */
        InputManager* _pInput = nullptr;
        /** @brief 이번 프레임 시간(초, 시간 배율이 든 값 — 루프가 `beginFrame` · `tick` 에 받은 것)입니다. 창 닫힘에서는 마지막 프레임의 값입니다. */
        float32 _deltaSeconds = 0.0f;
        /**
         * @brief 그 훅을 부르는 시점의 활성 씬 식별자(원본 경로, 없으면 씬 이름)입니다. 활성 씬이 없으면 비어 있습니다.
         * @details 씬의 문자열을 가리키므로 훅 안에서만 유효합니다. 들고 있으려면 복사합니다. 씬 전환은 프레임 안에서 일어나므로 훅마다 다를 수 있습니다.
         */
        string_view _activeSceneID{};
    };
} // namespace sw

namespace sw
{
    /** @brief 종료 코드 칸의 답입니다. 값이 있으면 그 코드(표 `ProcessExitCode` 만)로 끝내 달라는 것이고, 없으면 끝내지 않는다 — 숫자 표식(-1)이 아니라 형식이라 표 밖의 코드를 낼 수 없다. */
    using EngineFrameExit = optional<ProcessExitCode>;
    /** @brief 종료 코드 칸이 "아직 끝나지 않았다(종료를 요청하지 않는다)" 를 알리는 값입니다. */
    inline const EngineFrameExit kEngineFrameRunning{}; // sw::optional 은 constexpr 가 아니다 — 빈 값 상수

    /** @brief 프레임 시점 하나에 붙는 훅입니다. */
    using EngineFrameDelegate = Delegate<void( const EngineFrameContext& )>;
    /** @brief 종료 코드를 정할 수 있는 시점의 훅입니다. 정할 것이 없으면 `kEngineFrameRunning` 을 돌려줍니다. */
    using EngineFrameExitDelegate = Delegate<EngineFrameExit( const EngineFrameContext& )>;

    /**
     * @struct EngineFrameHooks
     * @brief `EngineFrameHookList::add`(엔진의 `getFrameHooks`)로 붙이는 훅 묶음입니다. 쓰지 않는 칸은 비워 둔다. 게임 스레드에서만 불립니다.
     * @details 루프는 붙인 순서대로 칸을 부르고, 종료 코드 칸은 처음으로 `kEngineFrameRunning` 이 아닌 값을 낸 것의 코드로 끝냅니다.
     *          훅이 가리키는 객체의 수명은 붙인 쪽이 쥐며, 없애기 전에 `add` 가 돌려준 구독을 놓습니다.
     */
    struct EngineFrameHooks
    {
        /** @brief 프레임 시작 — 입력 `beginFrame` 전입니다(가상 입력은 여기서 붙인다). */
        EngineFrameDelegate _onFrameBegin;
        /**
         * @brief 씬 틱 뒤 — 씬 틱 · 물리 · 내비 디버그 그리기 뒤, 런타임 UI 갱신 · 렌더 패킷 앞입니다(헤드리스 루프에서는 불리지 않는다).
         * @details 이 호출 자리를 옮기면 훅이 보는 시점(그 프레임의 게임 상태가 정해진 뒤)이 바뀝니다.
         */
        EngineFrameDelegate _onAfterSceneTick;
        /** @brief 프레임 끝 — 씬 틱 · 렌더 제출 뒤, 입력 프레임을 닫기 전입니다(그 프레임의 눌림 엣지가 아직 보인다). 프로세스를 끝낼 종료 코드를 돌려줍니다. */
        EngineFrameExitDelegate _onFrameEnd;
        /** @brief 창이 닫혀 루프가 끝났습니다(App 이 루프 뒤에 한 번 부른다). 코드를 내면 다른 종료 요청(프로파일 세션)보다 이깁니다. */
        EngineFrameExitDelegate _onWindowClosed;
    };
} // namespace sw

namespace sw
{
    /**
     * @class EngineFrameHookList
     * @brief 프레임 관찰자 목록입니다(대상은 소유하지 않는다). 프레임 앞(입력 전) · 씬 틱 뒤(UI 앞) · 프레임 뒤(렌더 제출 뒤) · 창 닫힘에서 붙인 순서대로 부릅니다.
     * @details 엔진 루프의 프레임 경계에 붙은 훅 묶음(`EngineFrameHooks`)의 목록입니다 — 붙이기 · 시점마다 부르기 · 종료 코드 모으기를 맡습니다.
     *          엔진 루프(`Engine`)가 값으로 들고 프레임 시점마다 부른다. 붙이는 쪽(App · 서버 · 개발 도구 · 텔레메트리)은 `Engine::getFrameHooks().add` 로 붙인다.
     *          서브시스템이 아닌 까닭: 크래시 보고 프로세스는 서비스 범위를 세우지 않고 기동을 끝내지만 호스트는 그 뒤에도 훅을 붙인다 — 엔진과 수명이 같다.
     *          공용 멀티캐스트라 훅이 순회 중에 자기 구독을 놓아도 그 항목은 순회가 끝난 뒤 빠진다(언리얼 `FCoreDelegates::OnBeginFrame` 과 같은 자리).
     *          스레드: 게임 스레드에서만 부른다.
     */
    class SW_API EngineFrameHookList
    {
    public:
        EngineFrameHookList();
        ~EngineFrameHookList();

        EngineFrameHookList( const EngineFrameHookList& )            = delete;
        EngineFrameHookList& operator=( const EngineFrameHookList& ) = delete;

        /**
         * @brief 훅 묶음을 붙입니다. 붙인 순서대로 부릅니다. 돌려받은 구독이 사라지면 떨어집니다.
         * @details 훅의 대상과 구독은 붙인 쪽이 쥐고, 구독을 대상보다 먼저 놓습니다. 칸이 모두 비었으면 오류를 남기고 빈 구독을 돌려줍니다.
         *          구독 하나가 등록 하나라 같은 묶음을 두 번 붙이면 두 번 불립니다. 엔진 종료(`Engine::shutdown`)는 남은 구독을 모두 끊습니다.
         */
        [[nodiscard]] Subscription add( const EngineFrameHooks& hooks );
        /** @brief 남은 구독을 모두 무효화하고 저장소를 놓습니다(엔진 종료가 서비스보다 먼저 부른다 — 훅이 서비스를 빌려 쓴다). */
        void clear();

        /** @brief 붙은 훅이 있으면 true 입니다. 부르는 쪽은 없으면 문맥을 만들지 않는다. */
        bool isBound() const { return _listHook.isBound(); }

        /** @brief 프레임 앞 훅(`_onFrameBegin`)을 모두 부릅니다. */
        void notifyFrameBegin( const EngineFrameContext& context );
        /** @brief 씬 틱 뒤 훅(`_onAfterSceneTick`)을 모두 부릅니다. */
        void notifyAfterSceneTick( const EngineFrameContext& context );
        /** @brief 프레임 뒤 훅(`_onFrameEnd`)을 **모두** 부르고(멈추지 않는다) 처음으로 낸 종료 코드를 돌려줍니다. 아무도 내지 않았으면 `kEngineFrameRunning` 입니다. */
        [[nodiscard]] EngineFrameExit notifyFrameEnd( const EngineFrameContext& context );
        /** @brief 창 닫힘 훅(`_onWindowClosed`)을 붙인 순서대로 부르다 처음으로 코드를 낸 훅에서 멈추고 그 코드를 돌려줍니다. 아무도 내지 않았으면 `kEngineFrameRunning` 입니다. */
        [[nodiscard]] EngineFrameExit notifyWindowClosed( const EngineFrameContext& context );

    private:
        /** @brief 훅 묶음의 칸 하나(프레임 시점)입니다. 묶음 하나가 멀티캐스트 항목 하나라 구독 하나로 떨어진다. */
        enum class FramePoint : uint8
        {
            FrameBegin,
            AfterSceneTick,
            FrameEnd,
            WindowClosed,
        };
        using FrameHookDelegate = Delegate<EngineFrameExit( FramePoint, const EngineFrameContext& )>;
        using FrameHookList     = MulticastDelegate<EngineFrameExit( FramePoint, const EngineFrameContext& )>;

        FrameHookList _listHook; ///< 붙인 훅 묶음(붙인 순서)
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 호스트가 엔진 초기화 전에 거는 기동 확장입니다(`Engine::setStartupExtension`). 엔진이 기동 단계 범위를 채운 뒤 · 초기화하기 전에 부른다.
     * @details 엔진이 방금 넣은 서브시스템은 @p startup 에서 타입으로 찾습니다(`find` — 렌더 쪽은 렌더러가 없으면 nullptr). 더한 서브시스템은 다른 기동 단계 서브시스템과 같이
     *          선언한 의존 순서로 서고 엔진 종료 때 역순으로 내려간다. 배포 빌드에 없는 기능(셰이더 · 설정 리로드 · HUD)을 엔진 쪽에
     *          `#if` 로 두지 않고 그 기능의 단위가 여기로 붙는다(언리얼의 모듈이 엔진 서브시스템을 기여하는 자리와 같다). 하나만 건다.
     */
    using EngineStartupExtensionDelegate = Delegate<void( SubsystemScope& startup )>;
} // namespace sw

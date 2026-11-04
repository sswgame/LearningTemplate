/**
 * @file ScenarioPlayer.h
 * @brief 시나리오 **재생** 객체입니다 — 게임 · 콘솔 · 시퀀서가 부르는 데모 · 어트랙트 · 벤치마크 · 튜토리얼 재생(`ScenarioPlayback`)을 실제로 돌립니다.
 * @details 테스트 실행(`AutomationSession` — 체인 · 판정 · 종료 코드 · 녹화)과 나뉜 객체다. 언리얼이 Automation/Functional Test 의 세션과 Level Sequence Player(재생 객체)를
 *          나누는 것과 같다 — 세션에 재생 인터페이스를 붙이지 않고, 재생기는 자기 실행기 · 선택 · 프레임 훅을 스스로 든다.
 */
#pragma once
#include "Core/Delegate/Subscription.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

#include "Engine/Automation/Scenario/ScenarioPlayback.h"
#include "Engine/EngineFrameHooks.h"

namespace sw
{
    class AutomationRunner;
    class EngineFrameHookList;
    class InputManager;

    /**
     * @class ScenarioPlayer
     * @brief 시나리오 하나를 같은 실행기(`AutomationRunner`)로 재생합니다. 판정이 끝나도 프로세스를 끝내지 않고, 행 감시 · 보고가 없으며, 끝을 델리게이트로 알립니다.
     * @details 만들 때 엔진 프레임 훅에 붙고(쉬는 동안의 훅은 아무것도 하지 않는다 — 재생은 기동 뒤 아무 때나 시작한다) 사라질 때 뗀다. 끝까지 돌면 반복하거나(`_bLoop`) 끝을 알린다.
     *          사람이 키보드 · 마우스를 건드리면 멈출 수 있다(`_bStopOnHumanInput`). 한 번에 하나만 돈다 — 실행기가 입력 관리자의 가상 입력 칸을 하나 쓰므로
     *          테스트 실행 · 녹화(`IAutomationRuntime::isBusy`)가 돌고 있으면 시작하지 않는다. 게임 스레드에서만 쓴다.
     */
    class SW_API ScenarioPlayer final : public IScenarioPlayer
    {
    public:
        explicit ScenarioPlayer( EngineFrameHookList& frameHooks );
        ~ScenarioPlayer() override;
        ScenarioPlayer( const ScenarioPlayer& )            = delete;
        ScenarioPlayer& operator=( const ScenarioPlayer& ) = delete;

        [[nodiscard]] bool play( const AssetID& scenarioPath, const ScenarioPlayOptions& options ) override;
        void               stopPlayback() override;
        bool               isPlaying() const override { return _pRunner != nullptr; }

    private:
        /** @brief 프레임 시작(입력 `beginFrame` 전)입니다 — 실행기의 시작 조건 · 가상 입력 붙이기 · 입력 전 단계. */
        void onFrameBegin( const EngineFrameContext& context );
        /** @brief 씬 틱 뒤 — `<Screenshot>` 이 요청한 경로를 이번 프레임의 패킷에 맡깁니다. */
        void onAfterSceneTick( const EngineFrameContext& context );
        /** @brief 프레임 끝입니다 — 사람 입력 · 단계 결과를 보고 반복 · 끝을 정한다. 늘 `kEngineFrameRunning`(재생은 프로세스를 끝내지 않는다). */
        int32 onFrameEnd( const EngineFrameContext& context );
        /** @brief 창이 닫혔습니다 — 재생을 멈춤으로 끝맺는다. 종료 코드는 정하지 않는다. */
        int32 onWindowClosed( const EngineFrameContext& context );
        /** @brief 실행기를 새로 세웁니다(처음 · 반복). */
        void startRunner();
        /** @brief 재생을 끝맺고(돌고 있으면 멈추고) 고정 프레임 시간을 되돌린 뒤 알린다. */
        void endPlayback( ScenarioPlayEnd end, InputManager* pInput );

    private:
        unique_ptr<AutomationRunner> _pRunner;               ///< 지금 재생하는 실행기(쉬면 nullptr)
        ScenarioPlayOptions          _options{};             ///< 재생 선택(재생 중일 때만 뜻이 있다)
        AssetID                      _path{};                ///< 재생하는 시나리오
        string                       _previousFixedDelta{};  ///< 재생이 고정 프레임 시간을 걸기 전 값(끝나면 되돌린다)
        uint64                       _humanInputSerial{ 0 }; ///< 재생을 시작할 때의 사람 입력 수
        Subscription                 _frameSubscription{};   ///< 엔진 프레임 훅 구독(만들 때 붙고 사라질 때 뗀다)
    };
} // namespace sw

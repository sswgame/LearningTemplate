/**
 * @file AutomationStepTimeline.h
 * @brief 시나리오 한 판이 시간 축 어디까지 왔는지(프레임 · 단계 순번 · 대기 · 시한 · 스크린샷 대기열)를 쥡니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

#include "Engine/Automation/Scenario/AutomationStepContext.h"

namespace sw
{
    /**
     * @class AutomationStepTimeline
     * @brief 실행기가 "지금 몇 번째 프레임 · 단계이고 무엇을 기다리는가" 를 적어 두는 칸입니다. 판정(`AutomationJudge`)은 이 값을 직접 읽지 않고 단계 문맥으로만 받는다.
     * @details 벽시계가 프레임 종류별로 어디에 쓰였는지 모으는 `AutomationTimeline`(보고의 `timeline`)과 다르다 — 그쪽은 잰 값의 합계이고 이쪽은 진행 상태다.
     *          멤버는 게임 스레드가 `AutomationRunner` 와 엔진 단계(`AutomationRunnerSteps.cpp`)에서 직접 읽고 쓴다(진행 상태라 소유자는 하나, 접근 규칙이 따로 없다).
     *          상용 엔진 비교: 언리얼 `FAutomationTestBase` 의 `LatentCommands` 대기열이 프레임 · 대기 상태를 한 곳에 두는 것과 같다.
     */
    class SW_API AutomationStepTimeline
    {
    public:
        AutomationStepTimeline();

        /** @brief 스크린샷 경로를 대기열에 더합니다(`<Screenshot>`). 렌더 패킷이 한 장씩 꺼내 간다. */
        void requestScreenshot( string path );
        /** @brief 붙잡은 단계가 맡긴 상태를 지웁니다(그 단계가 놓였거나 실행이 끝났다). */
        void clearHeldStepState()
        {
            _pHeldStepState.reset();
            _pHeldStateStep = nullptr;
        }
        /** @brief 아직 렌더 패킷에 싣지 않은 경로 하나를 꺼냅니다. 없으면 false 입니다. */
        [[nodiscard]] bool takePendingScreenshotPath( string& outPath );
        /** @brief 요청한 스크린샷이 아직 다 써지지 않았으면 true 입니다(대기열에 남았거나 렌더 스레드가 덜 썼다). */
        [[nodiscard]] bool hasPendingScreenshot() const { return _listPendingScreenshot.empty() == false || _screenshotCompletedCount < _screenshotRequestedCount; }

        vector<string>                   _listPendingScreenshot; ///< 아직 렌더 패킷에 싣지 않은 스크린샷 경로입니다.
        string                           _currentStepKind;       ///< 마지막으로 시작한 실행 단계(입력 단계는 제외)
        string                           _clockHolderName;       ///< 시계를 붙잡은 조건의 이름(로그)
        float64                          _exitDeadlineSeconds;   ///< `ExpectExitWithin` 이 정한 벽시계 시한입니다(단조 시계 초). 0 이면 기다리지 않습니다.
        float64                          _loadDeadlineSeconds;   ///< 부하 시나리오(`<Load>`)의 멈춤 그물 — 측정 창이 이때까지 끝내지 못하면 시간 초과다(단조 시계 초). 0 이면 부하 시나리오가 아니다.
        float64                          _wallStartSeconds;      ///< 시나리오를 읽기 시작한 단조 시계 초(벽시계 감시의 기준)
        float64                          _wallEndSeconds;        ///< 끝맺은 단조 시계 초(끝나기 전에는 0)
        float64                          _lastFrameEndSeconds;   ///< 앞 프레임의 `onFrameEnd` 가 닿은 단조 시계 초(프레임 시간을 더하는 기준 — 처음엔 `_wallStartSeconds`)
        float64                          _clockHoldStartSeconds; ///< 시계 잡기가 시작된 단조 시계 초
        size_t                           _nextStepIndex;         ///< 다음에 돌 단계의 순번
        uint32                           _frameIndex;            ///< 지금 시나리오 프레임(시작 전이면 0) — 단계 시점의 축. `<WaitUntil>` 이 붙잡는 동안은 서 있다
        uint32                           _elapsedFrameCount;     ///< 시작한 뒤 지난 프레임 수(붙잡힌 프레임도 센다) — 시한(`timeoutFrames`)과 보고의 축
        uint32                           _currentStepNumber;     ///< `_currentStepKind` 의 파일 순번
        uint32                           _waitFrameCount;        ///< 시작 조건을 기다린 프레임 수입니다.
        uint32                           _screenshotRequestedCount;
        uint32                           _screenshotCompletedCount;
        uint32                           _imageWaitFrameCount;     ///< `<ExpectImage>` 가 스크린샷을, `<Screenshot>` 이 에셋 일을 기다린 프레임 수입니다(단계마다 0 부터).
        bool                             _bFinishWaiting{ false }; ///< 이 프레임에 `<Pass/>` 가 뒤의 일을 기다린다 - 시나리오 시한에 세지 않는다(그 대기는 `kMaxFinishWaitFrames` 가 따로 묶는다)
        uint32                           _assetSettledFrameCount;  ///< `<Screenshot>` 앞에서 에셋 일이 0 인 채로 지난 프레임 수입니다
        unique_ptr<IAutomationStepState> _pHeldStepState;          ///< 붙잡은 단계가 맡긴 상태(`setHeldStepState`). 그 단계가 놓이면 지운다
        const AutomationStep*            _pHeldStateStep;          ///< `_pHeldStepState` 를 맡긴 단계(없으면 nullptr)
        uint32                           _holdFrameCount;          ///< 지금 단계(`<WaitUntil>` · 마크를 기다리는 에디터 입력)가 시나리오 시계를 붙잡은 프레임(그동안 `_frameIndex` 는 그대로다)
        uint32                           _clockHoldFrameCount;     ///< 시계 잡기 조건이 이어서 붙잡은 프레임
        uint32                           _seenFrameCount;          ///< `onFrameEnd` 가 불린 횟수(붙잡힌 프레임도 센다 — 크래시 자리를 갱신하는 박자)
        uint8                            _bClockHeld : 1;          ///< 이 프레임을 시계 잡기 조건이 붙잡았다(`onFrameBegin` 이 정하고 `onFrameEnd` 가 단계를 건너뛴다)
        [[maybe_unused]] uint8           _reserved   : 7;
    };
} // namespace sw

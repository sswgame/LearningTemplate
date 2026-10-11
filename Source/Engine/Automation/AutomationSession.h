/**
 * @file AutomationSession.h
 * @brief `-scenario=a,b,c` 한 번의 실행을 소유합니다. 시나리오 순서(`AutomationScenarioChain`)와 지금 단계의 실행기(`AutomationRunner`)를 들고, 단계가 끝나면 다음 단계를 시작하고, 끝나면 종료 코드를 냅니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    class AutomationRunner;
    class AutomationScenarioChain;
    class InputManager;

    /**
     * @class AutomationSession
     * @brief 엔진 루프가 프레임 앞(`onFrameBegin`) · 뒤(`onFrameEnd`)에서 부르는 시나리오 구동기입니다. 루프는 이 객체 하나만 알고, 실행기 · 체인 · 단계 전환은 모릅니다.
     * @details 한 단계가 끝나면(통과해야 다음으로 간다 — `AutomationScenarioChain`) 같은 프로세스에서 다음 시나리오의 실행기를 새로 만든다. 그 단계가 시작할 때
     *          - 프로세스의 **작업 폴더**가 처음과 같은지 보고(파일 대화상자 같은 것이 옮긴다) 다르면 경고하고 되돌린다,
     *          - `gv_fixedFrameDelta` 를 그 시나리오의 `fixedDelta` 로 맞추고,
     *          - 렌더 스레드가 지금까지 쓴 시나리오 스크린샷 수를 기준선으로 잡는다(실행기는 자기 단계가 요청한 것만 기다린다 — 렌더 스레드의 수는 프로세스 전체로 누적된다).
     *          게임 스레드에서만 쓴다.
     */
    class SW_API AutomationSession
    {
    public:
        /** @brief `onFrameEnd` · `onWindowClosed` 의 반환값 — 아직 끝나지 않았습니다(`AutomationResult::Running`). */
        static constexpr int32 kRunning = -1;

        AutomationSession();
        ~AutomationSession();
        AutomationSession( const AutomationSession& )            = delete;
        AutomationSession& operator=( const AutomationSession& ) = delete;

        /**
         * @brief 시나리오 목록(`a,b,c` 또는 하나)으로 시작하고 첫 단계를 엽니다.
         * @param scenarioArgument `-scenario=` 값(쉼표로 가른다)
         * @param reportPath `-scenario-report=` 값(비면 보고 파일을 쓰지 않는다. 단계가 여럿이면 단계마다 번호가 붙는다)
         * @param bKeepGoing `-scenario-keep-going` — 실패한 단계 뒤에도 이어 돌린다
         * @param completedScreenshotCount 지금까지 렌더 스레드가 쓴 시나리오 스크린샷 수
         * @return 돌릴 시나리오가 하나라도 있으면 true 입니다(없으면 아무것도 하지 않는다).
         */
        [[nodiscard]] bool begin( string_view scenarioArgument, string_view reportPath, bool bKeepGoing, uint32 completedScreenshotCount );

        /** @brief 프레임 시작(입력 `beginFrame` 전)에 부릅니다. 지금 단계의 시작 조건 · 가상 입력 붙이기 · 입력 전 단계를 돌립니다. */
        void onFrameBegin( InputManager& input );
        /**
         * @brief 프레임 끝(씬 틱과 렌더 제출 뒤, 입력 프레임을 닫기 전)에 부릅니다.
         * @param completedScreenshotCount 렌더 스레드가 지금까지 쓴 시나리오 스크린샷 수(프로세스 전체)
         * @return 체인이 끝났으면 프로세스 종료 코드, 아니면 `kRunning` 입니다.
         */
        [[nodiscard]] int32 onFrameEnd( InputManager& input, uint32 completedScreenshotCount );
        /**
         * @brief 창이 닫혀 루프가 끝났습니다. 지금 단계를 그 결과로 끝맺고, 돌릴 단계가 남았으면 실패로 봅니다(`CloseWindow` 는 마지막 단계에만 둔다).
         * @return 프로세스 종료 코드
         */
        [[nodiscard]] int32 onWindowClosed( InputManager* pInput );

        /** @brief `<Screenshot>` 이 요청한 경로를 하나 꺼냅니다(다음 렌더 패킷에 싣는다). 없으면 false 입니다. */
        [[nodiscard]] bool takePendingScreenshotPath( string& outPath );

    private:
        /** @brief 체인의 지금 단계 시나리오를 새 실행기로 시작합니다. */
        void startStage( uint32 completedScreenshotCount );
        /** @brief 단계 하나가 끝났을 때 부릅니다(`AutomationResult` 값). 통과했고 다음 단계가 있으면 그것을 시작하고 `kRunning`, 아니면 체인의 종료 코드입니다. */
        [[nodiscard]] int32 onStageEnded( int32 resultCode, uint32 completedScreenshotCount );

        unique_ptr<AutomationRunner>        _pRunner;
        unique_ptr<AutomationScenarioChain> _pChain;
        string                              _reportPath;         ///< `-scenario-report` 원본(단계마다 `AutomationScenarioChain::makeStageReportPath`)
        string                              _workingFolder;      ///< 시작할 때의 작업 폴더(단계마다 이 폴더에서 시작한다)
        uint32                              _screenshotBaseline; ///< 단계가 시작할 때 렌더 스레드가 이미 쓴 스크린샷 수
    };
} // namespace sw

/**
 * @file AutomationScenarioChain.h
 * @brief 시나리오를 한 프로세스에서 차례로 돌리는 순서입니다(`-scenario=a.scenario.xml,b.scenario.xml`). 어느 단계를 돌릴지, 언제 멈출지, 단계 보고 경로를 정합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Automation/AutomationRunner.h"

namespace sw
{
    /**
     * @class AutomationScenarioChain
     * @brief 시나리오 목록과 지금 단계를 들고, 한 단계가 끝날 때마다 이어 갈지 정합니다. 실행은 `EngineLoop` 가 단계마다 `AutomationRunner` 를 새로 만들어 합니다.
     * @details 기본은 첫 실패에서 멈춘다. `keepGoing` 이면 실패해도 다음 단계를 돌려 한 번에 모든 실패를 모은다(에디터 시나리오 묶음처럼 단계가 서로 독립일 때) — 건너뜀(`Skipped`)은 이 기계에서
     *          못 도는 것이라 그때는 멈춘다. 앞 단계가 만든 상태(씬, 올라온 모듈, 백엔드)는 다음 단계가 이어받는다 — 프로세스가 하나이기 때문이다. 단계마다 시나리오 프레임은 0 부터 다시 센다(시작 조건이
     *          그 단계에서 다시 참이 되는 프레임이 0). 멈추면 나머지는 돌리지 않고, 프로세스의 결과는 처음으로 통과하지 못한 단계의 결과다.
     *          `CloseWindow` 로 끝나는 단계는 마지막이어야 한다(창이 닫히면 루프가 끝난다). 게임 스레드에서만 쓴다.
     */
    class SW_API AutomationScenarioChain
    {
    public:
        /** @brief `a,b,c` 를 경로 목록으로 가릅니다. 각 경로의 앞뒤 공백을 떼고 빈 항목은 버립니다. */
        static vector<string> splitList( string_view text );
        /** @brief 단계 @p stageIndex(0 부터)의 보고 경로입니다. 단계가 하나면 @p reportPath 그대로이고, 여럿이면 확장자 앞에 `.<번호>`(1 부터)를 붙입니다. 빈 경로는 빈 채로 둡니다. */
        static string makeStageReportPath( string_view reportPath, size_t stageIndex, size_t stageCount );

        /** @brief 목록으로 시작합니다. 빈 목록이면 끝난 것으로 봅니다. @p bKeepGoing 이면 실패한 단계 뒤에도 다음 단계를 돌립니다. */
        void begin( vector<string> listPath, bool bKeepGoing = false );

        /** @brief 아직 돌릴 단계가 있습니다(시작했고 끝나지 않았다). */
        bool   isActive() const { return _bBegun && _bFinished == false; }
        size_t getStageCount() const { return _listPath.size(); }
        /** @brief 지금 단계 번호(0 부터)입니다. */
        size_t getStageIndex() const { return _stageIndex; }
        /** @brief 지금 단계의 시나리오 경로입니다(끝났으면 빈 글). */
        const string& getCurrentPath() const;
        /** @brief 시나리오가 둘 이상이면 true 입니다(요약과 보고 이름이 단계를 말한다). */
        bool isChain() const { return _listPath.size() > 1; }

        /**
         * @brief 지금 단계가 @p result 로 끝났음을 알립니다.
         * @return 다음 단계로 넘어갔으면 true(통과했거나 `keepGoing` 이 실패를 넘겼고 단계가 남았다). 아니면 false 이고 체인이 끝난다.
         */
        [[nodiscard]] bool onStageEnded( AutomationResult result );
        /** @brief 체인의 최종 결과입니다. 끝나기 전에는 `Running`, 전부 통과했으면 `Passed`, 아니면 처음으로 통과하지 못한 단계의 결과입니다. */
        AutomationResult getFinalResult() const { return _finalResult; }
        /** @brief 통과하지 못해 멈춰서 돌리지 않은 단계 수입니다. */
        size_t getSkippedStageCount() const { return _skippedStageCount; }
        /** @brief 통과하지 못한 단계 수입니다(`keepGoing` 이 아니면 많아야 하나). 건너뜀은 세지 않습니다(`keepGoing` 일 때). */
        size_t getFailedStageCount() const { return _failedStageCount; }
        /** @brief `keepGoing` 이 건너뛰고 이어 간 단계(건너뜀으로 끝난 것) 수입니다. */
        size_t getSkippedResultCount() const { return _skippedResultCount; }
        /**
         * @brief 단계 하나의 끝을 한 줄로 적습니다(`chain stage 2/3 FAIL (shooter3d.reload) — 1 later stage(s) skipped`). `[Scenario]` 로그 줄 뒤에 붙습니다.
         * @param stageIndex 방금 끝난 단계 번호
         */
        string describeStageEnd( size_t stageIndex, AutomationResult result, string_view scenarioName ) const;

    private:
        vector<string>   _listPath;
        size_t           _stageIndex{ 0 };
        size_t           _skippedStageCount{ 0 };
        size_t           _failedStageCount{ 0 };
        size_t           _skippedResultCount{ 0 };
        AutomationResult _firstNonPassResult{ AutomationResult::Passed }; ///< 처음 통과하지 못한 단계의 결과(없으면 Passed)
        AutomationResult _finalResult{ AutomationResult::Running };
        bool             _bKeepGoing{ false };
        bool             _bBegun{ false };
        bool             _bFinished{ false };
    };
} // namespace sw

/**
 * @file AutomationJudge.h
 * @brief 자동화 시나리오의 판정을 소유합니다 — 탐침 비교 · 방문 이력 · 기억 값 · 로그 · 이미지 지표와 실패 · 미확정 · 처리하지 않은 로그 목록.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Automation/Runner/AutomationLogCapture.h"
#include "Engine/Automation/Runner/AutomationLogPolicy.h"
#include "Engine/Automation/Runner/AutomationOutcome.h"
#include "Engine/Automation/Scenario/AutomationScenario.h"
#include "Engine/Automation/Scenario/AutomationStepContext.h"
#include "Engine/Automation/Watch/AutomationWatch.h"

namespace sw
{
    /**
     * @class AutomationJudge
     * @brief 단언 단계의 비교와 결과 판정에 필요한 상태를 모두 쥡니다. 시간 · 입력 · 대기는 모릅니다(그것은 `AutomationStepTimeline` 과 실행기의 일).
     * @details 판정이 읽는 바깥 값(시나리오 프레임 · 산출물 경로 · 오브젝트 매니저)은 단계 문맥(`AutomationStepContext`)에서 호출마다 받는다 — 판정이 실행기를 알지 못한다.
     *          실패는 `addFailure` 로만 쌓이고, 결과를 정하는 것(`Passed` → `Failed` · `Unconfirmed` · `UnhandledLog`)은 실행기의 끝맺음이다.
     *          상용 엔진 비교: 언리얼 Automation Test 의 `FAutomationTestBase`(테스트 기록 · 오류 목록)가 실행기와 갈라진 모양과 같다. 게임 스레드에서만 쓴다.
     */
    class SW_API AutomationJudge
    {
    public:
        AutomationJudge();

        AutomationJudge( const AutomationJudge& )            = delete;
        AutomationJudge& operator=( const AutomationJudge& ) = delete;

        /**
         * @brief 시나리오가 시작하는 프레임에 판정을 준비합니다 — 로그 허용 목록을 읽고, `<ExpectLog>` 선언을 올리고, 방문 이력을 만들고, 로그 수집을 시작합니다.
         * @return 허용 목록이 틀리면 false 이고 @p outError 에 이유를 적는다(폴백 없이 읽기 오류로 멈춘다)
         */
        [[nodiscard]] bool prepare( const AutomationScenario& scenario, string& outError );
        /** @brief 로그 수집이 줄에 적을 프레임을 정합니다. */
        void setFrameIndex( uint32 frameIndex ) { _logCapture.setFrameIndex( frameIndex ); }
        /** @brief 로그 수집을 멈춥니다. */
        void stopLogCapture() { _logCapture.stop(); }

        /** @brief 실패 한 줄을 적습니다(시나리오는 계속 진행하고 끝에서 Failed 가 됩니다). */
        void addFailure( const AutomationStep& step, string_view message );
        /** @brief 탐침 값 한 줄을 보고의 `records` 에 남깁니다(`<Record>`). */
        void addRecordLine( string line );

        /** @brief `<ExpectVisited>` 가 보는 탐침마다 이번 프레임 값을 읽어 바뀌었으면 적습니다(씬 틱 뒤, 단계 전). */
        void sampleWatches( AutomationStepContext& context );

        /** @brief 탐침 단계(`Expect`, `WaitUntil`)의 비교를 봅니다. 탐침에 값이 없으면 false 이고 @p outMessage 에 이유를 씁니다(값이 있으면 `<탐침> <규칙>, got <값>`). */
        [[nodiscard]] bool compareWatch( const AutomationStepContext& context, const AutomationStep& step, bool& outMatched, string& outMessage ) const;

        void runExpect( AutomationStepContext& context, const AutomationStep& step );
        void runExpectVisited( AutomationStepContext& context, const AutomationStep& step );
        void runRecord( AutomationStepContext& context, const AutomationStep& step );
        void runRemember( AutomationStepContext& context, const AutomationStep& step );
        void runExpectLog( AutomationStepContext& context, const AutomationStep& step );
        void runExpectImage( AutomationStepContext& context, const AutomationStep& step );

        /**
         * @brief 로그 게이트입니다. 시작한 시나리오의 처리하지 않은 Warning · Error 줄과 빠진 현지화 키를 모아 보고(`unhandledLogs`)에 싣습니다 — 결과가 실패여도 싣는다.
         *        통과(`Passed`)한 시나리오는 그 줄이 있으면 @p inOutResult 를 `UnhandledLog`(15)로 바꾸고 @p outReason 을 채웁니다. 이미 진 시나리오의 결과는 그대로 둡니다(원인은 단언 · 시한이다).
         * @details 처리는 `AutomationLogPolicy`(전역 허용 목록의 이 시나리오 항목 + 이 시나리오의 `<ExpectLog>`)가 정한다. id 없는 줄은 허용 목록으로 가릴 수 없다.
         *          빠진 현지화 키는 `LocalizationManager::getMissingKeys` 로 프로세스 전체를 본다(키마다 경고를 한 번만 남기므로 시나리오 앞 기동 구간에 남은 줄도
         *          여기서 잡는다 — id 가 없는 줄이라 `<ExpectLog>` 로만 가릴 수 있다). 허용 항목이 맞은 id 는 `getAllowedLogIDs` 에 남아 보고에 적힌다.
         */
        void applyLogGate( AutomationOutcome& inOutResult, string& outReason );

        const vector<string>& getFailures() const { return _listFailure; }
        /** @brief 맞았지만 어림값 문턱(`provisional`)이라 미확정인 단언 줄입니다. 비어 있지 않고 실패가 없으면 결과는 `Unconfirmed` 입니다. */
        const vector<string>& getUnconfirmed() const { return _listUnconfirmed; }
        /** @brief 로그 게이트가 처리하지 않았다고 본 줄입니다. 비어 있지 않으면 실패가 없어도 결과는 `UnhandledLog` 입니다. */
        const vector<string>& getUnhandledLogs() const { return _listUnhandledLog; }
        /** @brief 이 실행에서 허용 항목이 맞은 로그 id 입니다(`scenario-logs --check` 가 낡은 항목을 세는 근거). */
        const vector<string>& getAllowedLogIDs() const { return _listAllowedLogID; }
        /** @brief 지표 값 줄들입니다(`darkFraction(0,0,1,0.12) park.ppm = 0.034`). */
        const vector<string>& getMetricLines() const { return _listMetricLine; }
        /** @brief 기록 값 줄들입니다(`frame 300 Scene.TransformHash = 12345` — 백엔드끼리 같아야 한다). */
        const vector<string>& getRecordLines() const { return _listRecordLine; }

    private:
        /**
         * @struct WatchWatch
         * @brief `<ExpectVisited>` 가 보는 탐침 하나(탐침 · 대상 · 키)의 값 이력입니다. 시작 프레임부터 프레임마다 읽어 값이 바뀔 때만 적습니다.
         */
        struct WatchWatch
        {
            vector<string> _listValue{}; ///< 바뀐 값(끝의 0 을 정리한 글)
            vector<uint32> _listFrame{}; ///< 그 값이 처음 보인 시나리오 프레임
            string         _watch{};
            string         _target{};
            string         _key{};
        };

        /**
         * @struct RememberedValue
         * @brief `<Remember as="이름">` 이 남긴 탐침 값(끝의 0 을 정리한 글)입니다. 경계(플레이 · 백엔드 교체 · 모듈 리로드)를 넘긴 뒤 `sameAs` · `differsFrom` 이 비교합니다.
         * @details 판정은 엔진 쪽이라 모듈 리로드와 백엔드 교체를 넘어 남는다. 시나리오가 시작할 때 비운다.
         */
        struct RememberedValue
        {
            string _name{};
            string _text{};
        };

        /** @brief 단계의 탐침 · 대상 · 키를 읽어 값을 냅니다. 없으면 false 이고 @p outMessage 에 이유를 씁니다. */
        [[nodiscard]] bool readWatchValue( const AutomationStepContext& context, const AutomationStep& step, AutomationWatchValue& outValue, string& outMessage ) const;
        /** @brief 시작할 때 `<ExpectVisited>` 가 보는 탐침마다 이력을 만듭니다. */
        void prepareWatches( const AutomationScenario& scenario );

        AutomationLogCapture    _logCapture;       ///< 시나리오 동안의 로그 줄(`<ExpectLog>`, 로그 게이트)
        AutomationLogPolicy     _logPolicy;        ///< 로그 게이트의 허용 목록(전역 + `<ExpectLog>`)
        vector<string>          _listFailure;      ///< "frame 40 <Expect> #7: Shooter3D.WeaponIndex == 1, got 2"
        vector<string>          _listUnconfirmed;  ///< 맞았지만 어림값 문턱인 `<ExpectImage>` ("frame 40 <ExpectImage> #7: meanLuma >= 0.05 held with 0.2 …")
        vector<string>          _listUnhandledLog; ///< 로그 게이트가 처리하지 않았다고 본 줄(`[Warning] [호출자] (id=…) 본문`)
        vector<string>          _listAllowedLogID; ///< 이 실행에서 허용 항목이 맞은 로그 id
        vector<string>          _listMetricLine;   ///< 보고에 적는 지표 값
        vector<string>          _listRecordLine;   ///< 보고에 적는 기록 값(결정성 대조)
        vector<WatchWatch>      _listWatch;        ///< `<ExpectVisited>` 가 보는 탐침 이력
        vector<RememberedValue> _listRemembered;   ///< `<Remember>` 가 남긴 값
    };
} // namespace sw

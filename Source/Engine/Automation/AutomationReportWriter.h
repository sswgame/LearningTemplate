/**
 * @file AutomationReportWriter.h
 * @brief 자동화 시나리오의 JSON 보고 경로 · 지표 줄을 가지고, 끝날 때 보고 파일을 씁니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Automation/AutomationTimeline.h"

namespace sw
{
    /** @struct AutomationReportParams @brief 보고 한 편에 적을 실행 결과입니다(`AutomationReportWriter::write` 의 인자 묶음). */
    struct AutomationReportParams
    {
        string_view           _scenarioName{};
        const utf8*           _pResultName{ "" };
        int32                 _exitCode{ 0 };
        uint32                _frameCount{ 0 };
        string_view           _reason{};
        const vector<string>* _pListFailure{ nullptr };
        const vector<string>* _pListUnconfirmed{ nullptr };  ///< 맞았지만 어림값 문턱이라 미확정인 단언 줄
        const vector<string>* _pListUnhandledLog{ nullptr }; ///< 로그 게이트가 처리하지 않았다고 본 Warning · Error 줄
        const vector<string>* _pListAllowedLog{ nullptr };   ///< 허용 목록 항목이 맞은 로그 id(`scenario-logs --check` 가 낡은 항목을 세는 근거)
        // --- 실행 환경: 이 보고만으로 어디서 어떻게 돌았고 어떻게 다시 돌리는지 알게 하는 칸입니다(없으면 빈 글 · 0) ---
        string_view               _backendName{};              ///< 끝날 때의 RHI 백엔드(`dx12` …). 헤드리스면 빔
        string_view               _gameName{};                 ///< 돌린 게임
        string_view               _startedAt{};                ///< 시나리오를 시작한 벽시계 지역 시각(`2026-10-26 14:03:11`). 얻지 못하면 빔
        int64                     _startedUnixMs{ 0 };         ///< 같은 시각의 유닉스 밀리초
        uint32                    _wallMilliseconds{ 0 };      ///< 시작부터 끝까지 벽시계로 지난 시간(밀리초)
        uint32                    _wallLimitMilliseconds{ 0 }; ///< 그 시나리오의 벽시계 상한(밀리초). 부하 시나리오는 0(자기 그물을 쓴다)
        string_view               _lastStep{};                 ///< 끝난 자리 한 줄(`시나리오 · 백엔드 · step N 단계 · f프레임`, `AutomationRunTitleUtil::makeSummary`)
        string_view               _reproduce{};                ///< 같은 실행을 다시 띄우는 App 명령줄(`AutomationRunTitleUtil::makeReproduceCommand`). 모르면 빔
        const AutomationTimeline* _pTimeline{ nullptr };       ///< 벽시계가 프레임 종류별로 어디에 쓰였는지(`timeline` 칸). nullptr 이면 칸을 쓰지 않는다
    };
} // namespace sw

namespace sw
{
    /**
     * @class AutomationReportWriter
     * @brief 보고 경로와 지표 줄을 모아 두었다가 시나리오가 끝나면 JSON 보고를 씁니다(`AutomationRunner` 의 값 멤버).
     * @details 보고 형식: `name` · `result` · `exitCode` · `frames` · `reason` · `backend` · `game` · `startedAt` · `startedAtUnixMs` · `wallMilliseconds` · `wallLimitMilliseconds` · `lastStep` · `reproduce` · `timeline` ·
     *          `failures[]` · `unconfirmed[]` · `unhandledLogs[]` · `allowedLogs[]` · `metrics[]` · `records[]`. 경로가 비어 있으면 쓰지 않는다.
     *          읽는 쪽(`Scripts` 의 보고 읽기, `AppTestUtil::readReportRecords`)이 있으니 필드는 **더하기만** 하고 이름을 바꾸지 않는다. `records[]` 는 끝에 둔다(`readReportRecords` 가 `"records"` 키부터 첫 `]` 까지 읽는다).
     */
    class SW_API AutomationReportWriter
    {
    public:
        AutomationReportWriter();

        /** @brief 보고를 쓸 경로입니다. 비우면 보고를 쓰지 않습니다. */
        void setReportPath( string_view reportPath ) { _reportPath = string( reportPath ); }
        /** @brief 정한 보고 경로입니다(비면 보고를 쓰지 않는다). 행 감시가 정상 보고와 같은 자리에 쓰려고 읽는다. */
        const string& getReportPath() const { return _reportPath; }
        /** @brief 지표 값 줄 하나를 더합니다(`darkFraction(0,0,1,0.12) park.ppm = 0.034`). */
        void addMetricLine( string line ) { _listMetricLine.push_back( std::move( line ) ); }
        /** @brief 지금까지 더한 지표 값 줄입니다. */
        const vector<string>& getMetricLines() const { return _listMetricLine; }
        /** @brief 기록 값 줄 하나를 더합니다(`frame 300 Scene.TransformHash = 12345` — `<Record>`). 같은 시나리오는 백엔드마다 같은 줄을 내야 합니다. */
        void                  addRecordLine( string line ) { _listRecordLine.push_back( std::move( line ) ); }
        const vector<string>& getRecordLines() const { return _listRecordLine; }

        /** @brief 보고 파일을 씁니다. 경로가 비어 있으면 아무것도 하지 않고, 쓰지 못하면 경고를 남깁니다. */
        void write( const AutomationReportParams& params ) const;

    private:
        string         _reportPath;
        vector<string> _listMetricLine; ///< 보고에 적는 지표 값입니다.
        vector<string> _listRecordLine; ///< 보고에 적는 기록 값입니다(결정성 대조).
    };
} // namespace sw

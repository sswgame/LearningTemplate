/**
 * @file AutomationReportWriter.h
 * @brief 자동화 시나리오의 JSON 보고 경로 · 지표 줄을 가지고, 끝날 때 보고 파일을 씁니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

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
    };
} // namespace sw

namespace sw
{
    /**
     * @class AutomationReportWriter
     * @brief 보고 경로와 지표 줄을 모아 두었다가 시나리오가 끝나면 JSON 보고를 씁니다(`AutomationRunner` 의 값 멤버).
     * @details 보고 형식: `name` · `result` · `exitCode` · `frames` · `reason` · `failures[]` · `unconfirmed[]` · `unhandledLogs[]` · `allowedLogs[]` · `metrics[]` · `records[]`. 경로가 비어 있으면 쓰지 않는다.
     */
    class SW_API AutomationReportWriter
    {
    public:
        AutomationReportWriter();

        /** @brief 보고를 쓸 경로입니다. 비우면 보고를 쓰지 않습니다. */
        void setReportPath( string_view reportPath ) { _reportPath = string( reportPath ); }
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

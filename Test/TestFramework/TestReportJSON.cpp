#include "pch.h"

#include "Core/Common/BuildInfo.h"
#include "Core/File/FileUtil.h"

#include "TestFramework/TestCatalog.h"
#include "TestFramework/TestReport.h"
#include "TestFramework/TestRunOptions.h"

#include <cstdio>

namespace test
{
    namespace
    {
        struct TestReportJSONInternal
        {
            /** @brief JSON 글 리터럴(따옴표 포함)로 씁니다 — 따옴표 · 역슬래시 · 제어 문자를 이스케이프한다. */
            static void appendString( sw::string& out, const sw::string& text )
            {
                out += '"';
                for ( const utf8 ch : text )
                {
                    switch ( ch )
                    {
                        case '"':
                        {
                            out += "\\\"";
                            break;
                        }
                        case '\\':
                        {
                            out += "\\\\";
                            break;
                        }
                        case '\n':
                        {
                            out += "\\n";
                            break;
                        }
                        case '\r':
                        {
                            out += "\\r";
                            break;
                        }
                        case '\t':
                        {
                            out += "\\t";
                            break;
                        }
                        default:
                        {
                            if ( static_cast<uint8>( ch ) < 0x20 )
                            {
                                utf8 arrEscaped[8];
                                std::snprintf( arrEscaped, sizeof( arrEscaped ), "\\u%04x", static_cast<uint32>( static_cast<uint8>( ch ) ) );
                                out += arrEscaped;
                            }
                            else
                                out += ch;
                            break;
                        }
                    }
                }
                out += '"';
            }

            static void appendKey( sw::string& out, const utf8* pKey )
            {
                out += '"';
                out += pKey;
                out += "\":";
            }

            static void appendMilliseconds( sw::string& out, float64 milliseconds )
            {
                utf8 arrText[40];
                std::snprintf( arrText, sizeof( arrText ), "%.3f", milliseconds );
                out += arrText;
            }

            static const utf8* getResultName( CaseResult result )
            {
                switch ( result )
                {
                    case CaseResult::Passed:
                        return "passed";
                    case CaseResult::Failed:
                        return "failed";
                    case CaseResult::Skipped:
                        return "skipped";
                }
                return "passed";
            }

            /** @brief 처리하지 않은 로그 실패의 메시지를 줄로 가른 것 — 앞 공백은 들여쓰기라 뺀다. */
            static void appendUnhandledLines( sw::string& out, const sw::vector<TestFailure>& listFailure )
            {
                bool bFirst = true;
                for ( const TestFailure& failure : listFailure )
                {
                    if ( failure._bUnhandledLog == false )
                        continue;
                    size_t begin = 0;
                    while ( begin <= failure._message.size() )
                    {
                        size_t end = failure._message.find( '\n', begin );
                        if ( end == sw::string::npos )
                            end = failure._message.size();
                        size_t first = begin;
                        while ( first < end && failure._message[first] == ' ' )
                        {
                            ++first;
                        }
                        if ( first < end )
                        {
                            out += bFirst ? "" : ",";
                            bFirst = false;
                            appendString( out, failure._message.substr( first, end - first ) );
                        }
                        begin = end + 1;
                    }
                }
            }

            static void appendCase( sw::string& out, const CaseRecord& record, const TestRunOptions& options )
            {
                out += '{';
                appendKey( out, "name" );
                appendString( out, record._pTestInfo->fullName() );
                out += ',';
                appendKey( out, "parameter" );
                appendString( out, record._pTestInfo->_parameter );
                out += ',';
                appendKey( out, "result" );
                appendString( out, getResultName( record._result ) );
                out += ',';
                appendKey( out, "ms" );
                appendMilliseconds( out, record._elapsedMs );
                out += ',';
                appendKey( out, "iteration" );
                out += sw::to_string( record._iteration );
                out += ',';
                appendKey( out, "shard" );
                out += sw::to_string( options._shardIndex );
                out += ',';
                appendKey( out, "failures" );
                out += '[';
                bool bFirst = true;
                for ( const TestFailure& failure : record._listFailure )
                {
                    if ( failure._bUnhandledLog )
                        continue;
                    out += bFirst ? "" : ",";
                    bFirst = false;
                    out += '{';
                    appendKey( out, "file" );
                    appendString( out, failure._file );
                    out += ',';
                    appendKey( out, "line" );
                    out += sw::to_string( failure._line );
                    out += ',';
                    appendKey( out, "condition" );
                    appendString( out, failure._condition );
                    out += ',';
                    appendKey( out, "message" );
                    appendString( out, failure._message );
                    out += ',';
                    appendKey( out, "trace" );
                    appendString( out, failure._trace );
                    out += '}';
                }
                out += "],";
                appendKey( out, "partSkips" );
                out += '[';
                bFirst = true;
                for ( const SkippedPart& part : record._listSkippedPart )
                {
                    out += bFirst ? "" : ",";
                    bFirst = false;
                    out += '{';
                    appendKey( out, "feature" );
                    appendString( out, getBuildFeatureName( part._feature ) );
                    out += ',';
                    appendKey( out, "subject" );
                    appendString( out, part._subject );
                    out += '}';
                }
                out += "],";
                appendKey( out, "missingFeature" );
                appendString( out, record._missingFeature == BuildFeature::Count ? "" : getBuildFeatureName( record._missingFeature ) );
                out += ',';
                appendKey( out, "unhandledLog" );
                out += '[';
                appendUnhandledLines( out, record._listFailure );
                out += "]}";
            }
        };
    } // namespace
} // namespace test

namespace test
{
    sw::string makeTestReportJSON( const TestRunOptions& options, const TestRunSummary& summary )
    {
        sw::string out;
        out += '{';
        TestReportJSONInternal::appendKey( out, "format" );
        out += "1,";
        TestReportJSONInternal::appendKey( out, "config" );
        TestReportJSONInternal::appendString( out, sw::build::kConfigName );
        out += ',';
        TestReportJSONInternal::appendKey( out, "shard" );
        out += "{\"index\":" + sw::to_string( options._shardIndex ) + ",\"count\":" + sw::to_string( options._shardCount ) + "},";
        TestReportJSONInternal::appendKey( out, "repeat" );
        out += sw::to_string( options._repeatCount ) + ',';
        TestReportJSONInternal::appendKey( out, "seed" );
        out += sw::to_string( options._randomSeed ) + ',';
        TestReportJSONInternal::appendKey( out, "totals" );
        out += "{\"passed\":" + sw::to_string( summary._passedCount ) + ",\"failed\":" + sw::to_string( summary._failedCount ) + ",\"skipped\":" +
               sw::to_string( summary._skippedCount ) + ",\"ms\":";
        TestReportJSONInternal::appendMilliseconds( out, summary._totalMs );
        out += "},";
        TestReportJSONInternal::appendKey( out, "cases" );
        out += '[';
        bool bFirst = true;
        for ( const CaseRecord& record : summary._listRecord )
        {
            out += bFirst ? "" : ",";
            bFirst = false;
            TestReportJSONInternal::appendCase( out, record, options );
        }
        out += "]}\n";
        return out;
    }

    void JSONTestReporter::onRunStarted( [[maybe_unused]] const TestCatalog& catalog, [[maybe_unused]] const TestRunOptions& options, [[maybe_unused]] uint32 runnableCount )
    {
    }

    void JSONTestReporter::onIterationStarted( [[maybe_unused]] uint32 iteration, [[maybe_unused]] uint32 repeatCount )
    {
    }

    void JSONTestReporter::onCaseStarted( [[maybe_unused]] const TestCaseInfo& testInfo )
    {
    }

    void JSONTestReporter::onCaseFinished( [[maybe_unused]] const CaseRecord& record )
    {
    }

    void JSONTestReporter::onRunFinished( [[maybe_unused]] const TestCatalog& catalog, const TestRunOptions& options, const TestRunSummary& summary )
    {
        if ( options._reportPath.empty() )
            return;
        const bool bWritten = sw::FileUtil::ensureParentDirectoryExists( options._reportPath ) && sw::FileUtil::writeTextFile( options._reportPath, makeTestReportJSON( options, summary ) );
        if ( bWritten == false )
        {
            std::fprintf( stdout, "Could not write the test report '%s'\n", options._reportPath.c_str() );
            std::fflush( stdout );
            SW_LOG_ERROR( "Could not write the test report '%#'", options._reportPath.c_str() );
        }
    }
} // namespace test

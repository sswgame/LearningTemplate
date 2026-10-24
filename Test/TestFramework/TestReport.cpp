#include "pch.h"

#include "TestFramework/TestReport.h"

#include "Core/Common/BuildInfo.h"

#include "TestFramework/TestCatalog.h"
#include "TestFramework/TestRunOptions.h"

#include <algorithm>
#include <cstdlib>

namespace test
{
    namespace
    {
        struct TestReportInternal
        {
            static constexpr size_t kSlowestShown = 5;
            /** @brief 재현 명령에 진 케이스를 이 수까지만 필터로 적는다 — 넘으면 원래 고른 집합을 다시 돈다. */
            static constexpr size_t      kMaxReproducedCases = 20;
            static constexpr const utf8* kSeparator          = "====================================================\n";
        };
    } // namespace
} // namespace test

namespace test
{
    bool TestRunSummary::isPassed() const
    {
        const bool bAnyFailure = _failedCount > 0 || _bEmptySuiteIsFailure || _declarationMismatchCount > 0 || _bKindOnlyRanNothing;
        return bAnyFailure == false && _bFilterMatchedNothing == false && _bInvalidArgument == false;
    }

    ConsoleTestReporter::ConsoleTestReporter( const utf8* pLinePrefix )
        : _pLinePrefix{ pLinePrefix }
    {
    }

    void ConsoleTestReporter::onRunStarted( const TestCatalog& catalog, const TestRunOptions& options, uint32 runnableCount )
    {
        for ( const auto& [suiteName, environment] : catalog.getEnvironmentSuites() )
        {
            if ( catalog.isEnvironmentMissing( suiteName ) == false )
                continue;
            bool bNameMatched{ false };
            for ( const TestCaseInfo& testInfo : catalog.getListCase() )
            {
                if ( testInfo._groupName == suiteName && options._filter.matches( testInfo.fullName() ) )
                {
                    bNameMatched = true;
                    break;
                }
            }
            if ( bNameMatched )
                std::fprintf( stdout, "[ SKIP SUITE ] %s - %s is not set (%s)\n", suiteName.c_str(), environment._environmentName.c_str(), environment._reason.c_str() );
        }

        const uint32 totalCount  = static_cast<uint32>( catalog.getListCase().size() );
        const uint32 filteredOut = totalCount - runnableCount;
        SW_LOG_INFO( "====================================================" );
        SW_LOG_INFO( " Running %# / %# Test Cases...", runnableCount, totalCount );
        if ( filteredOut > 0 )
            SW_LOG_INFO( " Filtered out: %#", filteredOut );
        SW_LOG_INFO( "====================================================" );
        if ( options._shardCount > 1 )
            std::fprintf( stdout, "Running shard %u of %u (%u cases)\n", options._shardIndex + 1, options._shardCount, runnableCount );
        if ( options._bShuffle )
        {
            // 순서에 기대는 테스트를 찾으려고 섞는다 — 진 순서를 다시 만들 수 있어야 고칠 수 있다.
            std::fprintf( stdout, "Shuffling test order with seed %u (replay: --test_shuffle=%u)\n", options._randomSeed, options._randomSeed );
        }
        std::fflush( stdout );
    }

    void ConsoleTestReporter::onIterationStarted( uint32 iteration, uint32 repeatCount )
    {
        if ( repeatCount <= 1 )
            return;
        std::fprintf( stdout, "\nRepeating all tests (iteration %u / %u) . . .\n\n", iteration + 1, repeatCount );
        std::fflush( stdout );
    }

    void ConsoleTestReporter::onCaseStarted( const TestCaseInfo& testInfo )
    {
        std::fprintf( stdout, "%s[ RUN      ] %s\n", _pLinePrefix, testInfo.fullName().c_str() );
        std::fflush( stdout );
    }

    void ConsoleTestReporter::onCaseFinished( const CaseRecord& record )
    {
        const sw::string fullName = record._pTestInfo->fullName();
        switch ( record._result )
        {
            case CaseResult::Skipped:
            {
                std::fprintf( stdout, "%s[  SKIPPED ] %s\n", _pLinePrefix, fullName.c_str() );
                SW_LOG_INFO( "%#", fullName.c_str() );
                break;
            }
            case CaseResult::Failed:
            {
                std::fprintf( stdout, "%s[  FAILED  ] %s (%.2f ms)\n", _pLinePrefix, fullName.c_str(), record._elapsedMs );
                SW_LOG_ERROR( "%# (%# ms)", fullName.c_str(), sw::Fmt( record._elapsedMs, sw::Format().precision( 2 ) ) );
                break;
            }
            case CaseResult::Passed:
            {
                std::fprintf( stdout, "%s[       OK ] %s (%.2f ms)\n", _pLinePrefix, fullName.c_str(), record._elapsedMs );
                SW_LOG_INFO( "%# (%# ms)", fullName.c_str(), sw::Fmt( record._elapsedMs, sw::Format().precision( 2 ) ) );
                break;
            }
        }
        // 케이스는 돌았지만 구성 기능이 없어 건너뛴 일부 — 결과 줄 뒤에 항목마다 한 줄(조용히 지나가지 않게). 결과 줄 머리 계약(`[  SKIPPED ]` …)과 겹치지 않는 머리다.
        for ( const SkippedPart& part : record._listSkippedPart )
        {
            std::fprintf( stdout, "%s  [ PARTSKIP ] %s - needs %s, not in this build (%s)\n", _pLinePrefix, part._subject.c_str(), getBuildFeatureName( part._feature ),
                          sw::build::kConfigName );
        }
        std::fflush( stdout );
    }

    void ConsoleTestReporter::onRunFinished( const TestCatalog& catalog, const TestRunOptions& options, const TestRunSummary& summary )
    {
        if ( summary._listEmptySuite.empty() == false )
        {
            std::fprintf( stdout, "%s", TestReportInternal::kSeparator );
            std::fprintf( stdout, " %s (%d):\n",
                          summary._bEmptySuiteIsFailure ? "Suites that verified nothing - every case skipped" : "Suites that verified nothing (allowed)",
                          static_cast<int32>( summary._listEmptySuite.size() ) );
            for ( const sw::string& suite : summary._listEmptySuite )
            {
                std::fprintf( stdout, "   - %s\n", suite.c_str() );
                if ( summary._bEmptySuiteIsFailure )
                    SW_LOG_ERROR( "Suite verified nothing - every case skipped: %#", suite.c_str() );
                else
                    SW_LOG_INFO( "Suite verified nothing - every case skipped (allowed): %#", suite.c_str() );
            }
            if ( summary._bEmptySuiteIsFailure )
                std::fprintf( stdout, " Pass --allow_empty_suite if this is expected here.\n" );
        }

        SW_LOG_INFO( "====================================================" );
        SW_LOG_INFO( " Test Summary: %# Passed, %# Failed, %# Skipped (%# ms total)", summary._passedCount, summary._failedCount, summary._skippedCount, summary._totalMs );
        SW_LOG_INFO( "====================================================" );

        std::fprintf( stdout, "%s", TestReportInternal::kSeparator );
        const sw::map<sw::string, sw::string>& mapHostReason = catalog.getSuiteKindReasons( SuiteKind::Host );
        if ( options.getKindMode( SuiteKind::Host ) == SuiteKindMode::Exclude && mapHostReason.empty() == false )
        {
            std::fprintf( stdout, " Host suites left out (run them with --host_suites=only):" );
            for ( const auto& [suiteName, reason] : mapHostReason )
            {
                std::fprintf( stdout, " %s", suiteName.c_str() );
            }
            std::fprintf( stdout, "\n" );
        }
        if ( summary._bKindOnlyRanNothing )
            std::fprintf( stdout, " --host_suites=only / --load_suites=only selected no test - no suite of that kind matched\n" );
        if ( summary._bFilterMatchedNothing )
            std::fprintf( stdout, " --test_filter matched no test case - see --test_list (patterns are separated by ':' or ',', e.g. A.*:B.*)\n" );

        printSlowestCases( summary );
        printFeatureSkips( summary );
        if ( summary._listOverBudget.empty() == false )
        {
            std::fprintf( stdout, " Over time budget (%s):\n", options._bEnforceBudgets ? "failed by --enforce_budgets" : "not enforced - --enforce_budgets fails these" );
            for ( const sw::string& line : summary._listOverBudget )
            {
                std::fprintf( stdout, "   %s\n", line.c_str() );
            }
        }

        if ( catalog.getListKnownLog().empty() == false )
        {
            std::fprintf( stdout, " Known error logs (SW_TEST_KNOWN_LOG - fix the cause, then delete the declaration):\n" );
            for ( const KnownLog& known : catalog.getListKnownLog() )
            {
                if ( known._hitCount > 0 )
                    std::fprintf( stdout, "   %s: tolerated %u line(s) containing '%s' - %s\n", known._suiteName.c_str(), known._hitCount, known._substring.c_str(),
                                  known._reason.c_str() );
                else
                    std::fprintf( stdout, "   %s: '%s' did not appear in this run - delete the declaration if it is fixed\n", known._suiteName.c_str(), known._substring.c_str() );
            }
        }

        printFailureRates( summary, options._repeatCount );
        std::fprintf( stdout, " Tests passed: %d / %d (%d skipped, %.2f ms total)\n", summary._passedCount, summary._passedCount + summary._failedCount + summary._skippedCount,
                      summary._skippedCount, summary._totalMs );
        if ( summary._failedCount > 0 )
        {
            std::fprintf( stdout, "%s Tests failed (%d):\n", _pLinePrefix, summary._failedCount );
            for ( const CaseRecord& record : summary._listRecord )
            {
                if ( record._result != CaseResult::Failed )
                    continue;
                if ( options._repeatCount > 1 )
                    std::fprintf( stdout, "%s   - %s (iteration %u)\n", _pLinePrefix, record._pTestInfo->fullName().c_str(), record._iteration + 1 );
                else
                    std::fprintf( stdout, "%s   - %s\n", _pLinePrefix, record._pTestInfo->fullName().c_str() );
            }
            if ( options._bShuffle )
                std::fprintf( stdout, " Replay this order with --test_shuffle=%u\n", options._randomSeed );
        }
        if ( summary.isPassed() == false )
            printReproduction( options, summary );
        std::fprintf( stdout, "%s", TestReportInternal::kSeparator );
        std::fflush( stdout );
    }

    void ConsoleTestReporter::printSlowestCases( const TestRunSummary& summary ) const
    {
        // 오래 걸린 케이스 — 테스트가 느려지는 것은 조용히 일어난다. 되풀이하면 케이스마다 가장 오래 걸린 회차 하나로 센다
        // (회차마다 따로 넣으면 같은 케이스 하나가 목록을 다 채운다).
        sw::map<const TestCaseInfo*, float64> mapSlowestElapsed;
        for ( const CaseRecord& record : summary._listRecord )
        {
            float64& slowestElapsed = mapSlowestElapsed[record._pTestInfo];
            slowestElapsed          = std::max( slowestElapsed, record._elapsedMs );
        }
        if ( mapSlowestElapsed.size() <= TestReportInternal::kSlowestShown )
            return;

        sw::vector<sw::pair<float64, const TestCaseInfo*>> listElapsed;
        listElapsed.reserve( mapSlowestElapsed.size() );
        for ( const auto& [pTestInfo, slowestElapsed] : mapSlowestElapsed )
        {
            listElapsed.push_back( { slowestElapsed, pTestInfo } );
        }
        std::partial_sort( listElapsed.begin(), listElapsed.begin() + TestReportInternal::kSlowestShown, listElapsed.end(),
                           []( const sw::pair<float64, const TestCaseInfo*>& lhs, const sw::pair<float64, const TestCaseInfo*>& rhs )
        {
            return lhs.first > rhs.first;
        } );
        std::fprintf( stdout, " Slowest cases:\n" );
        for ( size_t index = 0; index < TestReportInternal::kSlowestShown; ++index )
        {
            std::fprintf( stdout, "   %9.2f ms  %s\n", listElapsed[index].first, listElapsed[index].second->fullName().c_str() );
        }
    }

    void ConsoleTestReporter::printFeatureSkips( const TestRunSummary& summary ) const
    {
        uint32 arrSkipCount[static_cast<size_t>( BuildFeature::Count )]{};
        bool   bAnySkip{ false };
        for ( const CaseRecord& record : summary._listRecord )
        {
            if ( record._result != CaseResult::Skipped || record._missingFeature == BuildFeature::Count )
                continue;
            ++arrSkipCount[static_cast<size_t>( record._missingFeature )];
            bAnySkip = true;
        }
        if ( bAnySkip )
        {
            std::fprintf( stdout, " Skipped because this build (%s) lacks a feature:", sw::build::kConfigName );
            for ( size_t featureIndex = 0; featureIndex < static_cast<size_t>( BuildFeature::Count ); ++featureIndex )
            {
                if ( arrSkipCount[featureIndex] > 0 )
                    std::fprintf( stdout, " %s %u", getBuildFeatureName( static_cast<BuildFeature>( featureIndex ) ), arrSkipCount[featureIndex] );
            }
            std::fprintf( stdout, "\n" );
        }

        // 케이스는 통과했지만 일부를 건너뛴 것(`[ PARTSKIP ]` 줄)의 기능별 개수 — 케이스 수와 따로 센다.
        uint32 arrPartCount[static_cast<size_t>( BuildFeature::Count )]{};
        bool   bAnyPart{ false };
        for ( const CaseRecord& record : summary._listRecord )
        {
            for ( const SkippedPart& part : record._listSkippedPart )
            {
                ++arrPartCount[static_cast<size_t>( part._feature )];
                bAnyPart = true;
            }
        }
        if ( bAnyPart == false )
            return;
        std::fprintf( stdout, " Skipped parts inside cases because this build (%s) lacks a feature:", sw::build::kConfigName );
        for ( size_t featureIndex = 0; featureIndex < static_cast<size_t>( BuildFeature::Count ); ++featureIndex )
        {
            if ( arrPartCount[featureIndex] > 0 )
                std::fprintf( stdout, " %s %u", getBuildFeatureName( static_cast<BuildFeature>( featureIndex ) ), arrPartCount[featureIndex] );
        }
        std::fprintf( stdout, "\n" );
    }

    void ConsoleTestReporter::printFailureRates( const TestRunSummary& summary, uint32 repeatCount ) const
    {
        if ( repeatCount <= 1 || summary._failedCount == 0 )
            return;

        // 비결정 실패는 한 번으로 확정하지 않는다 — 회차 수 대비 진 수가 재현율이다.
        sw::vector<const TestCaseInfo*>      listOrder;
        sw::map<const TestCaseInfo*, uint32> mapFailedCount;
        for ( const CaseRecord& record : summary._listRecord )
        {
            if ( record._result != CaseResult::Failed )
                continue;
            if ( mapFailedCount.find( record._pTestInfo ) == mapFailedCount.end() )
                listOrder.push_back( record._pTestInfo );
            ++mapFailedCount[record._pTestInfo];
        }
        std::fprintf( stdout, " Failure rate over %u iterations:\n", repeatCount );
        for ( const TestCaseInfo* pTestInfo : listOrder )
        {
            const uint32  failedCount = mapFailedCount[pTestInfo];
            const float64 percent     = 100.0 * static_cast<float64>( failedCount ) / static_cast<float64>( repeatCount );
            std::fprintf( stdout, "   %s  failed %u / %u (%.1f%%)%s\n", pTestInfo->fullName().c_str(), failedCount, repeatCount, percent,
                          failedCount < repeatCount ? " - intermittent" : "" );
        }
    }

    void ConsoleTestReporter::printReproduction( const TestRunOptions& options, const TestRunSummary& summary ) const
    {
        sw::vector<sw::string> listFailedName;
        bool                   bSeedUsed{ false };
        for ( const CaseRecord& record : summary._listRecord )
        {
            if ( record._result != CaseResult::Failed )
                continue;
            bSeedUsed             = bSeedUsed || record._bSeedUsed;
            const sw::string name = record._pTestInfo->fullName();
            if ( std::find( listFailedName.begin(), listFailedName.end(), name ) == listFailedName.end() )
                listFailedName.push_back( name );
        }

        sw::string filter;
        if ( listFailedName.empty() == false && listFailedName.size() <= TestReportInternal::kMaxReproducedCases )
        {
            for ( const sw::string& name : listFailedName )
            {
                filter += filter.empty() ? "" : ":";
                filter += name;
            }
        }
        else
            filter = options._filterText;

        // 실행 파일은 `build/<프리셋>/TestBin` 에 있고 작업 폴더는 그 옆 `Bin` 이다(`sw_registerTestRun`).
        const sw::string executablePath = sw::FileUtil::getExecutablePath();
        const sw::string testBinFolder  = sw::FileUtil::getDirectoryPart( executablePath );
        const sw::string buildFolder    = sw::FileUtil::getDirectoryPart( testBinFolder );
        std::fprintf( stdout, "%s Reproduce (%s build, %s):\n", _pLinePrefix, sw::build::kConfigName, sw::FileUtil::getFileNamePart( buildFolder ).c_str() );
        std::fprintf( stdout, "%s   cd \"%s\" && \"%s\" %s\n", _pLinePrefix, sw::FileUtil::joinPath( buildFolder, "Bin" ).c_str(), executablePath.c_str(),
                      options.makeReproductionArguments( filter, bSeedUsed ).c_str() );
        const utf8* pCtestName = std::getenv( "SW_TEST_CTEST_NAME" );
        if ( pCtestName != nullptr )
            std::fprintf( stdout, "%s   ctest --test-dir \"%s\" -R \"^%s$\" --output-on-failure\n", _pLinePrefix, buildFolder.c_str(), pCtestName );
        if ( options._repeatCount <= 1 )
            std::fprintf( stdout, "%s   intermittent? add --test_repeat=20 to measure the failure rate\n", _pLinePrefix );
        for ( const CaseRecord& record : summary._listRecord )
        {
            for ( const sw::string& trace : record._listTrace )
            {
                std::fprintf( stdout, "%s   %s failed under: %s\n", _pLinePrefix, record._pTestInfo->fullName().c_str(), trace.c_str() );
            }
        }
    }
} // namespace test

#include "pch.h"

#include "Core/Time/MonotonicClock.h"

#include "TestFramework/TestRegistry.h"
#include "TestFramework/TestTempPath.h"

#include <algorithm>

namespace test
{
    CaseRecord TestRegistry::runCase( const TestCaseInfo& testInfo, uint32 iteration )
    {
        _currentContext.begin( testInfo.fullName() );
        _currentContext.setParameter( testInfo._parameter );
        for ( ITestRunListener* pListener : _listListener )
        {
            pListener->onCaseStarted( testInfo );
        }
        SW_LOG_INFO( "%#", testInfo.fullName().c_str() );

        const sw::Stopwatch stopwatch;
        for ( ITestCaseJudge* pJudge : _listJudge )
        {
            pJudge->beginCase( testInfo );
        }

        try
        {
            testInfo._func();
        }
        catch ( const std::exception& exception )
        {
            addFailure( "test exception", "TestFramework", 0, exception.what() );
        }
        catch ( ... )
        {
            addFailure( "test exception", "TestFramework", 0, "Unknown test exception" );
        }

        try
        {
            _environment.tearDown();
        }
        catch ( const std::exception& exception )
        {
            addFailure( "environment teardown", "TestFramework", 0, exception.what() );
        }
        catch ( ... )
        {
            addFailure( "environment teardown", "TestFramework", 0, "Unknown teardown exception" );
        }

        _currentContext.runCleanup();

        const float64 elapsedMs = static_cast<float64>( stopwatch.getElapsedNanoseconds() ) / static_cast<float64>( sw::constant::kNanosecondsPerMillisecond );
        for ( ITestCaseJudge* pJudge : _listJudge )
        {
            pJudge->endCase( testInfo, elapsedMs, *this );
        }

        // 정리(핸들 닫기 · 등록 해제)가 끝난 뒤에 케이스 폴더를 통째로 지운다. 못 지웠다면 그 케이스가 파일을 연 채로
        // 두었다는 뜻이다 — Windows 에서는 열린 파일을 지울 수 없다. 핸들 누수는 결함이므로 그 케이스의 실패로 남긴다.
        if ( _currentContext.isTempPathUsed() )
        {
            const sw::string caseDirectory = getCaseTempDirectory( testInfo.fullName() );
            if ( sw::FileUtil::removeDirectory( caseDirectory ) == false )
                addFailure( "temp directory removed after the case", caseDirectory, 0, "still exists - is a file handle left open?" );
        }

        CaseRecord record;
        record._pTestInfo       = &testInfo;
        record._elapsedMs       = elapsedMs;
        record._iteration       = iteration;
        record._bSeedUsed       = _currentContext.isSeedUsed();
        record._listSkippedPart = _currentContext.getListSkippedPart();
        record._listFailure     = _currentContext.getListFailure();
        if ( _currentContext.hasFailed() )
        {
            record._result = CaseResult::Failed;
            for ( const TestFailure& failure : _currentContext.getListFailure() )
            {
                const bool bNewTrace = failure._trace.empty() == false &&
                                       std::find( record._listTrace.begin(), record._listTrace.end(), failure._trace ) == record._listTrace.end();
                if ( bNewTrace )
                    record._listTrace.push_back( failure._trace );
            }
        }
        else if ( _currentContext.isSkipped() )
        {
            record._result         = CaseResult::Skipped;
            record._missingFeature = _currentContext.getMissingFeature();
        }
        for ( ITestRunListener* pListener : _listListener )
        {
            pListener->onCaseFinished( record );
        }
        return record;
    }

    int32 TestRegistry::runAllTests()
    {
        if ( _options._bInvalidArgument )
            return 1;

        _catalog.expandCaseGenerators(); // 엔진이 선 뒤에야 알 수 있는 목록(시나리오 폴더)으로 케이스를 만든다 — 고르기 · 나열 · 샤드 배정이 모두 이 표를 본다
        if ( _options._bListOnly )
        {
            listTests();
            return 0;
        }

        TestRunSummary summary;
        summary._declarationMismatchCount = _catalog.countDeclarationMismatch();
        summary._runnableCount            = static_cast<uint32>( makeRunOrder( 0 ).size() );
        _timeBudgetJudge.clear();
        for ( ITestRunListener* pListener : _listListener )
        {
            pListener->onRunStarted( _catalog, _options, summary._runnableCount );
        }

        for ( uint32 iteration = 0; iteration < _options._repeatCount; ++iteration )
        {
            for ( ITestRunListener* pListener : _listListener )
            {
                pListener->onIterationStarted( iteration, _options._repeatCount );
            }
            for ( const TestCaseInfo* pTestInfo : makeRunOrder( iteration ) )
            {
                CaseRecord record = runCase( *pTestInfo, iteration );
                summary._totalMs += record._elapsedMs;
                switch ( record._result )
                {
                    case CaseResult::Passed:
                    {
                        ++summary._passedCount;
                        break;
                    }
                    case CaseResult::Failed:
                    {
                        ++summary._failedCount;
                        break;
                    }
                    case CaseResult::Skipped:
                    {
                        ++summary._skippedCount;
                        break;
                    }
                }
                summary._listRecord.push_back( std::move( record ) );
            }
        }

        summary._listEmptySuite       = collectEmptySuites( summary._listRecord );
        summary._bEmptySuiteIsFailure = summary._listEmptySuite.empty() == false && _options._bAllowEmptySuite == false;
        summary._listOverBudget       = _timeBudgetJudge.getListOverBudget();

        // 고르는 필터가 등록된 케이스 하나와도 맞지 않으면 잘못 적은 필터다(이름 오타). 0/0 으로 통과시키면 확인한 줄 안다.
        // 스위트 종류 모드 · 샤드와 상관없이 이름만 본다 — 맞는 케이스를 그 모드 · 샤드가 뺀 것은 필터 잘못이 아니다.
        summary._bFilterMatchedNothing = _options._filter.hasIncludePattern();
        for ( const TestCaseInfo& testInfo : _catalog.getListCase() )
        {
            if ( _options._filter.matches( testInfo.fullName() ) )
            {
                summary._bFilterMatchedNothing = false;
                break;
            }
        }

        // 종류 하나만 고른 실행(`<타깃>_HostOnly` · `<타깃>_Load`)이 아무것도 안 돌았다면 그 ctest 항목은 빈 그물이다.
        const bool bKindOnly         = _options.getKindMode( SuiteKind::Host ) == SuiteKindMode::Only || _options.getKindMode( SuiteKind::Load ) == SuiteKindMode::Only;
        summary._bKindOnlyRanNothing = bKindOnly && summary._runnableCount == 0;

        // 케이스 밖에서 만든 임시 경로와 빈 프로세스 폴더를 거둔다. 케이스 폴더는 케이스마다 따졌으니 여기 남는 것은 케이스 밖에서 연 경로다.
        if ( sw::FileUtil::removeDirectory( getProcessTempDirectory() ) == false )
            SW_LOG_WARNING( "Could not remove the process temp directory %# - is a file handle left open?", getProcessTempDirectory().c_str() );

        _caseLogJudge.printAllowedSummary();
        for ( ITestRunListener* pListener : _listListener )
        {
            pListener->onRunFinished( _catalog, _options, summary );
        }
        return summary.isPassed() ? 0 : 1;
    }
} // namespace test

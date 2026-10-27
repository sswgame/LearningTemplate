#include "pch.h"

#include "TestFramework/TestRegistry.h"

#include "Core/Common/BuildInfo.h"
#include "Core/Container/VectorUtil.h"

#include "TestFramework/TestAssert.h"
#include "TestFramework/TestScopes.h"

#include <algorithm>
#include <mutex>

namespace test
{
    namespace
    {
        struct TestRegistryInternal
        {
            /**
             * @brief 실패 · 건너뜀 기록을 한 줄로 세우는 락.
             * @details 단언은 작업 스레드에서도 불린다(`runParallel` 본문 · `std::thread` 람다). 락 없이 벡터에 `push_back` 하면
             *          **둘이 동시에 실패할 때** 그 벡터가 깨진다 — 병렬 코드가 틀렸다는 것을 알려야 할 바로 그 순간에.
             *          gtest 도 단언을 스레드 안전하게 보장한다. 엔진의 `sw::mutex` 를 쓰지 않는 것은 그것이 Debug 에서 데드락 탐지기를
             *          타기 때문이다 — 그 탐지기를 테스트하는 케이스가 있다.
             */
            static std::mutex& getRecordMutex()
            {
                static std::mutex s_mutex;
                return s_mutex;
            }

            /** @brief 이 스레드에 걸린 추적(`ScopedTrace`) — 바깥부터. */
            static sw::vector<sw::string>& getThreadTrace()
            {
                thread_local sw::vector<sw::string> s_listTrace;
                return s_listTrace;
            }
        };

        /** @brief 이 스레드가 지금 `addFailure` 안에 있는가 — 거기서 남기는 `[FAILED]` 줄은 예상 밖 Error 로 세지 않는다(`CaseLogJudge`). */
        thread_local bool s_bReportingFailure = false;
    } // namespace
} // namespace test

namespace test
{
    bool isReportingFailureOnThisThread()
    {
        return s_bReportingFailure;
    }

    ScopedTrace::ScopedTrace( sw::string text )
    {
        TestRegistryInternal::getThreadTrace().push_back( std::move( text ) );
    }

    ScopedTrace::~ScopedTrace()
    {
        TestRegistryInternal::getThreadTrace().pop_back();
    }

    sw::string ScopedTrace::makeCurrentTrace()
    {
        sw::string trace;
        for ( const sw::string& text : TestRegistryInternal::getThreadTrace() )
        {
            if ( trace.empty() == false )
                trace += " > ";
            trace += text;
        }
        return trace;
    }

    uint32 getRandomSeed()
    {
        return TestRegistry::getInstance().getRandomSeed();
    }

    bool reportUnusableBackend( sw::RHIBackend backend, const utf8* pReason, const utf8* pFile, int32 line )
    {
        const bool bRequired = isBackendInThisBuild( backend ) && TestRegistry::getInstance().getOptions().isBackendRequired( backend );
        if ( bRequired == false )
            return false;
        sw::string message = "required RHI backend '";
        message += TestRunOptions::getBackendName( backend );
        message += "' could not stand up on this machine (--required_backends, Test/TestPolicy.json requiredBackends): ";
        message += pReason != nullptr ? pReason : "";
        reportFailure( "required backend is usable", pFile, line, message );
        return true;
    }

    bool isBackendUnderTest( sw::RHIBackend backend )
    {
        return isBackendInThisBuild( backend ) && TestRegistry::getInstance().getOptions().isBackendSelected( backend );
    }

    bool skipPartIfFeatureMissing( BuildFeature feature, const sw::string& subject )
    {
        if ( isBuildFeatureAvailable( feature ) )
            return false;
        const std::lock_guard<std::mutex> lock( TestRegistryInternal::getRecordMutex() );
        TestRegistry::getInstance().getCurrentContext()->addSkippedPart( feature, subject );
        return true;
    }

    TestRegistry::TestRegistry()
        : TestRegistry( "(nested) " )
    {
    }

    TestRegistry::TestRegistry( const utf8* pLinePrefix )
        : _catalog{}
        , _options{}
        , _currentContext{}
        , _environment{}
        , _consoleReporter{ pLinePrefix }
        , _jsonReporter{}
        , _caseLogJudge{}
        , _timeBudgetJudge{ _catalog, _options }
        , _listListener{ &_consoleReporter }
        , _listJudge{ &_caseLogJudge, &_timeBudgetJudge }
        , _pFailureCapture{ nullptr }
        , _pLinePrefix{ pLinePrefix }
    {
        if ( _pLinePrefix[0] == '\0' )
            _listListener.push_back( &_jsonReporter );
    }

    TestRegistry::~TestRegistry() = default;

    TestRegistry& TestRegistry::getInstance()
    {
        static TestRegistry s_instance{ "" };
        return s_instance;
    }

    sw::vector<utf8*> TestRegistry::configureFromArgs( int32 argc, utf8* argv[] )
    {
        return _options.parse( argc, argv );
    }

    sw::Subscription TestRegistry::registerListener( ITestRunListener* pListener )
    {
        _listListener.push_back( pListener );
        return issueSubscription( reinterpret_cast<uint64>( pListener ) );
    }

    sw::Subscription TestRegistry::registerJudge( ITestCaseJudge* pJudge )
    {
        _listJudge.push_back( pJudge );
        return issueSubscription( reinterpret_cast<uint64>( pJudge ) );
    }

    void TestRegistry::releaseSubscription( uint64 id )
    {
        const auto isReleased = [id]( const void* pItem )
        { return reinterpret_cast<uint64>( pItem ) == id; };
        sw::VectorUtil::removeIf( _listListener, isReleased ); // 두 목록 중 하나에만 있다
        sw::VectorUtil::removeIf( _listJudge, isReleased );
    }

    void TestRegistry::addFailure( const sw::string& condition, const sw::string& file, int32 line, const sw::string& message, bool bUnhandledLog )
    {
        const TestFailure                 failure{ condition, file, line, message, ScopedTrace::makeCurrentTrace(), bUnhandledLog };
        const std::lock_guard<std::mutex> lock( TestRegistryInternal::getRecordMutex() );
        if ( _pFailureCapture != nullptr )
        {
            _pFailureCapture->push_back( failure );
            return;
        }

        s_bReportingFailure = true;

        _currentContext.addFailure( failure );
        const utf8* pPrefix = _pLinePrefix;
        std::fprintf( stdout, "\n%s  [FAILED] %s:%d\n%s    Condition: %s\n", pPrefix, file.c_str(), line, pPrefix, condition.c_str() );
        if ( message.empty() == false )
            std::fprintf( stdout, "%s    Message  : %s\n", pPrefix, message.c_str() );
        if ( failure._trace.empty() == false )
            std::fprintf( stdout, "%s    Trace    : %s\n", pPrefix, failure._trace.c_str() );
        std::fflush( stdout );
        SW_LOG_ERROR( "\n%#  [FAILED] %#:%#", pPrefix, file.c_str(), line );
        SW_LOG_ERROR( "%#    Condition: %#", pPrefix, condition.c_str() );
        if ( message.empty() == false )
            SW_LOG_ERROR( "%#    Message  : %#", pPrefix, message.c_str() );
        if ( failure._trace.empty() == false )
            SW_LOG_ERROR( "%#    Trace    : %#", pPrefix, failure._trace.c_str() );
        s_bReportingFailure = false;
    }

    void TestRegistry::skipCurrentTest( [[maybe_unused]] const sw::string& reason, [[maybe_unused]] const sw::string& file, [[maybe_unused]] int32 line )
    {
        const std::lock_guard<std::mutex> lock( TestRegistryInternal::getRecordMutex() );
        _currentContext.skip();
        SW_LOG_INFO( "\n  [SKIPPED] %#:%# — %#", file.c_str(), line, reason.c_str() );
    }

    bool TestRegistry::skipIfFeatureMissing( BuildFeature feature, [[maybe_unused]] const sw::string& file, [[maybe_unused]] int32 line )
    {
        if ( isBuildFeatureAvailable( feature ) )
            return false;
        const std::lock_guard<std::mutex> lock( TestRegistryInternal::getRecordMutex() );
        _currentContext.skipForMissingFeature( feature );
        SW_LOG_INFO( "\n  [SKIPPED] %#:%# — needs %#, not in this build (%#)", file.c_str(), line, getBuildFeatureName( feature ), sw::build::kConfigName );
        return true;
    }

    uint32 TestRegistry::getRandomSeed()
    {
        _currentContext.markSeedUsed();
        return _options._randomSeed;
    }

    void TestRegistry::listTests() const
    {
        // **필터를 적용해서 센다.** `--test_list` 는 "내 필터가 무엇을 고르나" 를 확인할 때 쓰는 기능이라,
        // 등록 표를 통째로 찍으면 틀린 답을 믿고 필터를 잘못 적는다.
        const sw::vector<const TestCaseInfo*> listSelected  = selectCasesForThisShard();
        const uint32                          selectedCount = static_cast<uint32>( listSelected.size() );
        const uint32                          totalCount    = static_cast<uint32>( _catalog.getListCase().size() );
        if ( selectedCount == totalCount )
            std::fprintf( stdout, "Registered tests (%u):\n", totalCount );
        else
            std::fprintf( stdout, "Registered tests (%u selected / %u total):\n", selectedCount, totalCount );

        for ( const TestCaseInfo* pTestInfo : listSelected )
        {
            std::fprintf( stdout, "  %s\n", pTestInfo->fullName().c_str() );
        }

        constexpr const utf8* kArrKindTitle[] = { "Host suites - CI cannot run these", "Load suites - run with --load_suites=only" };
        for ( size_t kindIndex = 0; kindIndex < static_cast<size_t>( SuiteKind::Count ); ++kindIndex )
        {
            const sw::map<sw::string, sw::string>& mapReason = _catalog.getSuiteKindReasons( static_cast<SuiteKind>( kindIndex ) );
            if ( mapReason.empty() )
                continue;
            std::fprintf( stdout, "%s (%u):\n", kArrKindTitle[kindIndex], static_cast<uint32>( mapReason.size() ) );
            for ( const auto& [suiteName, reason] : mapReason )
            {
                std::fprintf( stdout, "  %s - %s\n", suiteName.c_str(), reason.c_str() );
            }
        }
        std::fflush( stdout );
    }
} // namespace test

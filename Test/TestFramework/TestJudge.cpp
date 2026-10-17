#include "pch.h"

#include "TestFramework/TestJudge.h"

#include "Core/Container/StringUtil.h"
#include "Core/Log/LogOnce.h"

#include "TestFramework/TestRegistry.h"

#include <mutex>

namespace test
{
    namespace
    {
        struct TestJudgeInternal
        {
            /** @brief 방어 테스트 구간(`SW_TEST_DEFENSIVE_SCOPE`)이 Error · Warning 앞에 붙이는 표식입니다. */
            static constexpr sw::string_view kDefensiveLogPrefix = "[Expected Defensive Test]";
            /** @brief 실패 메시지에 붙일 예상 밖 줄 수. */
            static constexpr size_t kShownMessageCount = 5;
        };
    } // namespace
} // namespace test

namespace test
{
    /** @brief 한 케이스 동안의 셈 — 로그 리스너가 남기는 스레드에서 불리므로 락으로 지킨다. */
    struct HostLogJudge::State
    {
        mutable std::mutex     _mutex;
        sw::vector<sw::string> _listMessage;
        sw::vector<KnownLog*>  _listKnownLog;
        sw::DelegateHandle     _handle;
        uint32                 _count{ 0 };
    };

    HostLogJudge::HostLogJudge( TestCatalog& catalog )
        : _pCatalog{ &catalog }
        , _pState{ sw::make_unique<State>() }
    {
    }

    HostLogJudge::~HostLogJudge()
    {
        if ( _pState->_handle.isValid() )
            sw::Logger::unregisterGlobalListener( _pState->_handle );
    }

    void HostLogJudge::beginCase( const TestCaseInfo& testInfo )
    {
        State& state = *_pState;
        state._listMessage.clear();
        state._listKnownLog.clear();
        state._count = 0;
        // "한 번만" 경고의 범위를 이 케이스로 연다 — 앞 케이스가 낸 경고 때문에 이 케이스의 경고가 사라지면 판정이 테스트 순서에 따라 바뀐다.
        sw::LogOnce::beginScope();
        if ( _pCatalog->isSuiteOfKind( testInfo._groupName, SuiteKind::Host ) == false )
            return;

        for ( KnownLog& known : _pCatalog->getListKnownLog() )
        {
            if ( known._suiteName == testInfo._groupName )
                state._listKnownLog.push_back( &known );
        }
        state._handle = sw::Logger::registerGlobalListener( SW_DELEGATE_LAMBDA( sw::LogWrittenDelegate, [pState = &state]( const sw::LogEntry& entry )
        {
            const bool bGatedLevel = entry._level == sw::LogLevel::Error || entry._level == sw::LogLevel::Warning;
            if ( bGatedLevel == false || isReportingFailureOnThisThread() )
                return;
            const sw::string_view message{ entry._message.c_str(), entry._message.size() };
            if ( sw::StringUtil::startsWith( message, TestJudgeInternal::kDefensiveLogPrefix ) )
                return;
            const std::lock_guard<std::mutex> lock( pState->_mutex );
            for ( KnownLog* pKnown : pState->_listKnownLog )
            {
                if ( message.find( sw::string_view{ pKnown->_substring.c_str(), pKnown->_substring.size() } ) == sw::string_view::npos )
                    continue;
                ++pKnown->_hitCount;
                return;
            }
            ++pState->_count;
            if ( pState->_listMessage.size() < TestJudgeInternal::kShownMessageCount )
                pState->_listMessage.push_back( entry._message );
        } ) );
    }

    void HostLogJudge::endCase( const TestCaseInfo& testInfo, float64, TestRegistry& registry )
    {
        State& state = *_pState;
        if ( state._handle.isValid() == false )
            return;
        sw::Logger::unregisterGlobalListener( state._handle );
        state._handle = {};

        sw::string description;
        {
            const std::lock_guard<std::mutex> lock( state._mutex );
            if ( state._count == 0 )
                return;
            description = sw::to_string( state._count );
            description += " unexpected [Error]/[Warning] log line(s); wrap intended ones in SW_TEST_DEFENSIVE_SCOPE";
            for ( const sw::string& message : state._listMessage )
            {
                description += "\n      ";
                description += message;
            }
        }
        registry.addFailure( "no unexpected [Error]/[Warning] log in a host suite case", testInfo.fullName(), 0, description );
    }

    TimeBudgetJudge::TimeBudgetJudge( const TestCatalog& catalog, const TestRunOptions& options )
        : _pCatalog{ &catalog }
        , _pOptions{ &options }
        , _listOverBudget{}
    {
    }

    void TimeBudgetJudge::endCase( const TestCaseInfo& testInfo, float64 elapsedMs, TestRegistry& registry )
    {
        const TimeBudget* pBudget = _pCatalog->findTimeBudget( testInfo._groupName );
        if ( pBudget == nullptr || elapsedMs <= pBudget->_maxCaseMilliseconds )
            return;

        const sw::string line = testInfo.fullName() + " (" + sw::to_string( static_cast<int64>( elapsedMs ) ) + " ms > " +
                                sw::to_string( static_cast<int64>( pBudget->_maxCaseMilliseconds ) ) + " ms - " + pBudget->_reason + ")";
        _listOverBudget.push_back( line );
        if ( _pOptions->_bEnforceBudgets )
            registry.addFailure( "case within its SW_TEST_TIME_BUDGET", testInfo.fullName(), 0, line );
    }
} // namespace test

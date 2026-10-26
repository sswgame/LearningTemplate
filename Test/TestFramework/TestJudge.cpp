#include "pch.h"

#include "TestFramework/TestJudge.h"

#include "Core/Container/StringUtil.h"
#include "Core/Log/LogOnce.h"

#include "Engine/Resource/ResourcePaths.h"

#include "TestFramework/TestRegistry.h"

#include <cstdio>
#include <mutex>

namespace test
{
    namespace
    {
        struct TestJudgeInternal
        {
            /** @brief 실패 메시지에 붙일 처리하지 않은 줄 수. */
            static constexpr size_t kShownMessageCount = 5;
            /** @brief 한 케이스가 모아 두는 Error · Warning 줄 상한입니다. 넘친 줄은 세어서 처리하지 않은 줄로 칩니다. */
            static constexpr size_t kMaxRecordedLine = 1000;
        };
    } // namespace
} // namespace test

namespace test
{
    /** @brief 한 케이스 동안의 모음 — 로그 리스너가 남기는 스레드에서 불리므로 락으로 지킨다. 줄의 판정은 케이스가 끝난 뒤 게임 스레드에서 한다. */
    struct HostLogJudge::State
    {
        /** @brief 리스너가 모은 Error · Warning 줄 하나. */
        struct Line
        {
            sw::string   _caller;
            sw::string   _id;
            sw::string   _message;
            sw::LogLevel _level{ sw::LogLevel::Error };
        };

        mutable std::mutex          _mutex;
        sw::vector<Line>            _listLine;
        sw::map<sw::string, uint32> _mapAllowedHit; ///< 이 실행에서 허용 항목이 맞은 케이스 수 — 키 `스위트 id`
        sw::Subscription            _subscription;  ///< 케이스 동안의 전역 로그 리스너
        uint32                      _droppedCount{ 0 };
    };

    HostLogJudge::HostLogJudge( TestCatalog& catalog )
        : _pCatalog{ &catalog }
        , _pState{ sw::make_unique<State>() }
        , _policy{}
        , _loadError{}
        , _bLoadAttempted{ false }
    {
    }

    HostLogJudge::~HostLogJudge()
    {
        _pState->_subscription.reset(); // 상태가 사라지기 전에 — 다른 스레드의 방송이 끝나기를 기다린다
    }

    void HostLogJudge::beginCase( const TestCaseInfo& testInfo )
    {
        State& state = *_pState;
        state._listLine.clear();
        state._droppedCount = 0;
        // "한 번만" 경고의 범위를 이 케이스로 연다 — 앞 케이스가 낸 경고 때문에 이 케이스의 경고가 사라지면 판정이 테스트 순서에 따라 바뀐다.
        sw::LogOnce::beginScope();
        if ( _pCatalog->isSuiteOfKind( testInfo._groupName, SuiteKind::Host ) == false )
            return;

        state._subscription = sw::Logger::registerGlobalListener( SW_DELEGATE_LAMBDA( sw::LogWrittenDelegate, [pState = &state]( const sw::LogEntry& entry )
        {
            const bool bGatedLevel = sw::AutomationLogPolicy::isGatedLevel( entry._level );
            if ( bGatedLevel == false || isReportingFailureOnThisThread() )
                return;
            if ( sw::AutomationLogPolicy::isExemptMessage( sw::string_view{ entry._message.c_str(), entry._message.size() } ) )
                return;
            const std::lock_guard<std::mutex> lock( pState->_mutex );
            if ( pState->_listLine.size() >= TestJudgeInternal::kMaxRecordedLine )
            {
                ++pState->_droppedCount;
                return;
            }
            State::Line line;
            line._caller  = entry._caller;
            line._id      = entry._pID != nullptr ? entry._pID : "";
            line._message = entry._message;
            line._level   = entry._level;
            pState->_listLine.push_back( std::move( line ) );
        } ) );
    }

    void HostLogJudge::ensureAllowListLoaded()
    {
        if ( _bLoadAttempted || _policy.isLoaded() )
            return;
        _bLoadAttempted = true;
        if ( sw::ResourcePaths::initialize() == false )
        {
            _loadError = "the resource paths are not initialized";
            return;
        }
        // 목록 없이는 어떤 줄도 가려지지 않는다 — 실패 메시지가 이유를 알린다.
        const bool bLoaded = _policy.loadAllowList( _loadError );
        if ( bLoaded == false && _loadError.empty() )
            _loadError = "the allow list could not be read";
    }

    void HostLogJudge::endCase( const TestCaseInfo& testInfo, float64, TestRegistry& registry )
    {
        State& state = *_pState;
        if ( state._subscription.isActive() == false )
            return;
        state._subscription.reset();

        sw::vector<State::Line> listLine;
        uint32                  droppedCount = 0;
        {
            const std::lock_guard<std::mutex> lock( state._mutex );
            listLine     = std::move( state._listLine );
            droppedCount = state._droppedCount;
            state._listLine.clear();
        }
        if ( listLine.empty() && droppedCount == 0 )
            return;

        ensureAllowListLoaded();
        _policy.clearScope();
        _policy.selectSuite( testInfo._groupName );
        sw::AutomationLogJudgement judgement;
        for ( const State::Line& line : listLine )
        {
            _policy.judge( line._level, line._caller, line._id, line._message, judgement );
        }
        {
            const std::lock_guard<std::mutex> lock( state._mutex );
            for ( const sw::string& id : judgement._listAllowedID )
            {
                ++state._mapAllowedHit[testInfo._groupName + " " + id];
            }
        }
        const size_t unhandledCount = judgement._listUnhandled.size() + droppedCount;
        if ( unhandledCount == 0 )
            return;

        sw::string description = sw::to_string( unhandledCount );
        description += " unhandled [Error]/[Warning] log line(s) (" + sw::to_string( judgement._missingIDCount ) +
                       " without a log id); wrap intended ones in SW_TEST_DEFENSIVE_SCOPE, fix the cause, or add the log id with suite/reason/plan to ";
        description += sw::AutomationLogPolicy::kAllowListPath;
        if ( _loadError.empty() == false )
            description += "\n      allow list not loaded: " + _loadError;
        size_t shownCount = 0;
        for ( const sw::string& message : judgement._listUnhandled )
        {
            if ( shownCount++ >= TestJudgeInternal::kShownMessageCount )
                break;
            description += "\n      ";
            description += message;
        }
        if ( droppedCount > 0 )
            description += "\n      (+" + sw::to_string( droppedCount ) + " more line(s) over the recording limit)";
        registry.addFailure( "no unexpected [Error]/[Warning] log in a host suite case", testInfo.fullName(), 0, description );
    }

    void HostLogJudge::printAllowedSummary() const
    {
        const std::lock_guard<std::mutex> lock( _pState->_mutex );
        if ( _pState->_mapAllowedHit.empty() )
            return;
        std::fprintf( stdout, " Allowed log lines (%s - fix the cause, then delete the entry):\n", sw::AutomationLogPolicy::kAllowListPath );
        for ( const auto& [key, caseCount] : _pState->_mapAllowedHit )
        {
            std::fprintf( stdout, "   %s: allowed in %u case(s)\n", key.c_str(), caseCount );
        }
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

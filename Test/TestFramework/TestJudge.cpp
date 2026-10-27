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
    struct CaseLogJudge::State
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
        sw::map<sw::string, uint32> _mapAllowedHit;     ///< 이 실행에서 허용 항목이 맞은 케이스 수 — 키 `스위트 id`
        sw::Subscription            _subscription;      ///< 케이스 동안의 전역 로그 리스너
        sw::vector<sw::string>      _listIgnoredNeedle; ///< 이 글 조각이 든 줄은 모으지 않는다(`addIgnoredNeedle`). 겹친 것은 더한 수만큼 들어 있다
        uint32                      _droppedCount{ 0 };
        bool                        _bActive{ false }; ///< 호스트 스위트 케이스가 도는 중 — 리스너 구독이 비어도(로그를 막는 구간) 값 입력(`recordLine`)은 판정한다

        /** @brief 줄 하나를 거르고 모읍니다 — 로그 리스너와 값 입력(`recordLine`)이 같은 길을 지난다. 어느 스레드에서도 부를 수 있다. */
        void record( sw::LogLevel level, sw::string_view caller, sw::string_view id, sw::string_view message )
        {
            if ( sw::AutomationLogPolicy::isGatedLevel( level ) == false )
                return;
            if ( sw::AutomationLogPolicy::isExemptMessage( message ) )
                return;
            const std::lock_guard<std::mutex> lock( _mutex );
            for ( const sw::string& needle : _listIgnoredNeedle )
            {
                if ( message.find( sw::string_view{ needle.c_str(), needle.size() } ) != sw::string_view::npos )
                    return;
            }
            if ( _listLine.size() >= TestJudgeInternal::kMaxRecordedLine )
            {
                ++_droppedCount;
                return;
            }
            Line line;
            line._caller  = sw::string{ caller };
            line._id      = sw::string{ id };
            line._message = sw::string{ message };
            line._level   = level;
            _listLine.push_back( std::move( line ) );
        }
    };

    CaseLogJudge::CaseLogJudge()
        : _pState{ sw::make_unique<State>() }
        , _policy{}
        , _loadError{}
        , _bLoadAttempted{ false }
    {
    }

    CaseLogJudge::~CaseLogJudge()
    {
        _pState->_subscription.reset(); // 상태가 사라지기 전에 — 다른 스레드의 방송이 끝나기를 기다린다
    }

    void CaseLogJudge::beginCase( const TestCaseInfo& )
    {
        State& state = *_pState;
        state._listLine.clear();
        state._droppedCount = 0;
        // "한 번만" 경고의 범위를 이 케이스로 연다 — 앞 케이스가 낸 경고 때문에 이 케이스의 경고가 사라지면 판정이 테스트 순서에 따라 바뀐다.
        sw::LogOnce::beginScope();
        // 스위트 종류(호스트 · 부하 · 일반)로 가르지 않는다 - 모든 케이스가 같은 판정을 받는다. 종류로 가르면 CPU 테스트의 거절 로그 수백 줄이 표식 없이 쌓여도 아무도 지지 않았다.
        state._bActive = true;

        state._subscription = sw::Logger::registerGlobalListener( SW_DELEGATE_LAMBDA( sw::LogWrittenDelegate, [pState = &state]( const sw::LogEntry& entry )
        {
            if ( isReportingFailureOnThisThread() )
                return;
            pState->record( entry._level, sw::string_view{ entry._caller.c_str(), entry._caller.size() }, entry._pID != nullptr ? sw::string_view{ entry._pID } : sw::string_view{},
                            sw::string_view{ entry._message.c_str(), entry._message.size() } );
        } ) );
    }

    void CaseLogJudge::recordLine( sw::LogLevel level, sw::string_view caller, sw::string_view id, sw::string_view message )
    {
        if ( _pState->_bActive )
            _pState->record( level, caller, id, message );
    }

    void CaseLogJudge::addIgnoredNeedle( sw::string_view needle )
    {
        const std::lock_guard<std::mutex> lock( _pState->_mutex );
        _pState->_listIgnoredNeedle.push_back( sw::string{ needle } );
    }

    void CaseLogJudge::removeIgnoredNeedle( sw::string_view needle )
    {
        const std::lock_guard<std::mutex> lock( _pState->_mutex );
        sw::vector<sw::string>&           listNeedle = _pState->_listIgnoredNeedle;
        for ( size_t index = listNeedle.size(); index > 0; --index )
        {
            const sw::string& candidate = listNeedle[index - 1];
            if ( sw::string_view{ candidate.c_str(), candidate.size() } == needle )
            {
                listNeedle.erase( listNeedle.begin() + static_cast<std::ptrdiff_t>( index - 1 ) );
                return;
            }
        }
    }

    void CaseLogJudge::ensureAllowListLoaded()
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

    void CaseLogJudge::endCase( const TestCaseInfo& testInfo, float64, TestRegistry& registry )
    {
        State& state = *_pState;
        if ( state._bActive == false )
            return;
        state._bActive = false;
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
        registry.addFailure( "no unexpected [Error]/[Warning] log in a host suite case", testInfo.fullName(), 0, description, true );
    }

    void CaseLogJudge::printAllowedSummary() const
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

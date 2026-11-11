#include "pch.h"

#include "Editor/SelfTest/EditorSelfTest.h"

#include "Core/File/FileUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"

#include "Editor/Common/EditorSubsystem.h"
#include "Editor/Common/Workspace/EditorExtensionSubsystems.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"
#include "Editor/SelfTest/EditorSettleWait.h"

#include "Engine/Automation/Scenario/AutomationStageReset.h"
#include "Engine/Foundation/QuitRequest.h"

#include "sw/config/ProcessExitCodes.gen.h"

#include <optional>

namespace sw::editor
{
    // 이 파일만 읽으므로 여기서 정의한다(헤더에 선언하지 않는다). 아래 실행 상태가 읽으므로 그보다 먼저 둔다.
    /** @brief `-gv_editorSelfTest=<패턴>`: 에디터가 뜬 뒤 이름이 맞는 에디터 자체 테스트를 돌리고 앱을 닫습니다(`*` 와 쉼표). */
    SW_TEST_GLOBAL_VARIABLE( sw::string, gv_editorSelfTest, "", "에디터가 뜬 뒤 이름이 패턴에 맞는 에디터 자체 테스트를 돌리고 끝낸다 (* · 쉼표, 비우면 사용 안 함)" );
    /** @brief `-gv_editorSelfTestReport=<파일>`: 에디터 자체 테스트의 결과 줄을 이 파일에 씁니다. */
    SW_TEST_GLOBAL_VARIABLE( sw::GlobalDiskPath, gv_editorSelfTestReport, "", "에디터 자체 테스트 결과를 쓸 파일 (비우면 로그에만)" );
} // namespace sw::editor

namespace sw::editor
{
    namespace
    {
        /** @brief 글자열 @p text 가 `*` 를 포함한 패턴 @p pattern 하나에 맞는지 봅니다. */
        bool matchesSelfTestGlob( string_view text, string_view pattern )
        {
            if ( pattern.empty() )
                return text.empty();
            if ( pattern[0] == '*' )
            {
                for ( size_t skipCount = 0; skipCount <= text.size(); ++skipCount )
                {
                    if ( matchesSelfTestGlob( text.substr( skipCount ), pattern.substr( 1 ) ) )
                        return true;
                }
                return false;
            }
            return text.empty() == false && text[0] == pattern[0] && matchesSelfTestGlob( text.substr( 1 ), pattern.substr( 1 ) );
        }

        /**
         * @class EditorSelfTestRun
         * @brief 한 번의 실행 상태와 그 실행을 한 프레임씩 진행하는 일입니다(고르기 · 정지 기다리기 · 테스트 한 단계 · 결과 적기 · 끝내기). 새 실행을 요청하면 처음으로 되돌린다.
         */
        class EditorSelfTestRun
        {
        public:
            /** @brief 테스트 하나가 쓸 수 있는 최대 단계 수입니다(정지를 기다린 프레임은 세지 않는다). 넘으면 실패로 적고 다음 테스트로 갑니다. */
            static constexpr uint32 kMaxStepCount = 300;

            EditorSelfTestRun();

            bool                                isActive() const { return _bActive == SW_TRUE; }
            const vector<EditorSelfTestResult>& getResults() const { return _listResult; }
            /** @brief 테스트가 도는 중이면 그 컨텍스트입니다(워밍업 중이거나 실행이 없으면 nullptr). */
            EditorSelfTestContext* findRunningContext() { return _bActive && _bStarted ? &_context : nullptr; }

            /** @brief 패턴 @p pattern 의 새 실행을 시작합니다. 첫 테스트 앞에서 에디터가 멈추기를 기다린다. */
            void start( string_view pattern, bool bQuitWhenDone )
            {
                *this          = EditorSelfTestRun{};
                _pattern       = string( pattern );
                _bQuitWhenDone = bQuitWhenDone ? SW_TRUE : SW_FALSE;
                _bActive       = SW_TRUE;
                // 기동 직후 패널이 처음 그려지고 기본 도킹이 자리 잡는다 — 예전의 워밍업 5 프레임을 대신한다.
                _settleWait.emplace();
            }

            /** @brief 사람이 읽는 진행 상태입니다(`EditorSelfTestRunner::describeProgress`). */
            string describeProgress() const
            {
                if ( _bActive == false )
                    return "not running";
                using Registry = EditorExtensionRegistry<EditorSelfTestRegistration>;
                StringBuilder<constant::buffer::kMax256> textBuilder;
                if ( _cursor < _listSelectedIndex.size() )
                    textBuilder.appendFormat( "test %# step %#", Registry::getAt( _listSelectedIndex[_cursor] )._pID, _context.getStepIndex() );
                else
                    textBuilder.append( "starting" );
                string text{ textBuilder.view() };
                if ( _settleWait.has_value() )
                    text += ", waiting for " + _settleWait->describeWait();
                return text;
            }

            /** @brief 실행을 한 프레임 진행합니다. 실행이 없으면 아무것도 하지 않는다. */
            void advanceFrame()
            {
                if ( _bActive == false )
                    return;

                using Registry = EditorExtensionRegistry<EditorSelfTestRegistration>;
                if ( _bStarted == false )
                {
                    _bStarted = SW_TRUE;
                    // 테스트가 도는 동안 패널이 누를 위젯의 레이블을 적는다(EditorSelfTestMarks::note). 끝나면 시작 전 상태로 되돌린다.
                    _bMarksWereEnabled = EditorSelfTestMarks::isEnabled() ? SW_TRUE : SW_FALSE;
                    (void)EditorSelfTestMarks::setEnabled( true ); // 세션이 있으니 에디터 컨텍스트도 있다
                    for ( uint32 index = 0; index < Registry::getCount(); ++index )
                    {
                        if ( EditorSelfTestRunner::matchesPattern( Registry::getAt( index )._pID, _pattern ) )
                            _listSelectedIndex.push_back( index );
                    }
                    SW_LOG_INFO_ID( "EditorSelfTest.RunningEditorSelfTests", "Running %# editor self tests matching '%#'", static_cast<uint32>( _listSelectedIndex.size() ), _pattern.c_str() );
                }

                // 정지를 기다리는 동안은 테스트를 부르지 않는다. 진행 중인 일에는 시한이 없고(`EditorSettleState`), 할 일 없이 화면이 계속 움직이면 그 테스트의 실패다. 정지 판정의 화면 표본은 레이블 기록이 켜져 있어야 쌓인다(위에서 켰다).
                if ( _settleWait.has_value() )
                {
                    const EditorSettleResult evaluation = _settleWait->advance( editor::getActiveObjectManager() );
                    if ( evaluation == EditorSettleResult::Waiting )
                        return;
                    if ( evaluation == EditorSettleResult::Oscillating )
                        (void)_context.expect( false, _settleWait->describeOscillation().c_str() );
                    _settleWait.reset();
                }

                if ( _cursor < _listSelectedIndex.size() )
                {
                    const EditorSelfTestRegistration& registration = Registry::getAt( _listSelectedIndex[_cursor] );
                    const EditorSelfTestStep          step         = registration._pfnRun( _context );
                    _context.advanceStep();
                    if ( step == EditorSelfTestStep::WaitSettled )
                        _settleWait.emplace();

                    const bool bTimedOut = step != EditorSelfTestStep::Done && _context.getStepIndex() >= kMaxStepCount;
                    if ( bTimedOut )
                        (void)_context.expect( false, "the test did not finish within its frame budget" );
                    if ( step == EditorSelfTestStep::Done || bTimedOut )
                    {
                        recordResult( registration );
                        _context = EditorSelfTestContext{};
                        ++_cursor;
                    }
                    return;
                }
                finish();
            }

        private:
            /** @brief 모두 끝났다. 하나도 맞지 않은 패턴은 실패다 — 이름을 잘못 적은 실행이 초록으로 보이면 안 된다. */
            void finish()
            {
                _bActive = SW_FALSE;
                (void)EditorSelfTestMarks::setEnabled( _bMarksWereEnabled == SW_TRUE ); // 세션이 있으니 에디터 컨텍스트도 있다
                if ( _listSelectedIndex.empty() )
                    ++_failedCount;
                string doneLine{ "EditorSelfTest|DONE|" };
                doneLine += to_string( _passedCount );
                doneLine += "|";
                doneLine += to_string( _failedCount );
                appendLine( doneLine, _failedCount > 0 );
                if ( _bQuitWhenDone == SW_FALSE )
                    return;

                if ( gv_editorSelfTestReport.empty() == false )
                {
                    if ( FileUtil::ensureParentDirectoryExists( gv_editorSelfTestReport ) == false || FileUtil::writeTextFile( gv_editorSelfTestReport, _report ) == false )
                        SW_LOG_ERROR_ID( "EditorSelfTest.CouldNotWriteEditor", "Could not write the editor self test report to %#", gv_editorSelfTestReport.c_str() );
                }

                // 실패가 하나라도 있으면 프로세스가 실패 코드로 끝난다(CTest 가 읽는다) — 창만 닫으면 코드가 0 이었다.
                editor::getSubsystem<QuitRequest>().request( _failedCount > 0 ? ProcessExitCode::Failed : ProcessExitCode::Passed );
            }

            /** @brief 결과 한 줄을 로그와 보고서에 남깁니다. 실패는 오류 로그라 `[Error]` 를 세는 쪽도 봅니다. */
            void appendLine( const string& line, bool bFailure )
            {
                if ( bFailure )
                    SW_LOG_ERROR_ID( "EditorSelfTest.CaseFailed", "%#", line.c_str() );
                else
                    SW_LOG_INFO_ID( "EditorSelfTest.CaseLine", "%#", line.c_str() );
                _report += line;
                _report += "\n";
            }

            void recordResult( const EditorSelfTestRegistration& registration )
            {
                EditorSelfTestResult result;
                result._id         = registration._pID;
                result._reason     = _context.getFailure();
                result._frameCount = _context.getStepIndex();
                result._bPassed    = _context.hasPassed();
                _listResult.push_back( result );
                string line{ _context.hasPassed() ? "EditorSelfTest|PASS|" : "EditorSelfTest|FAIL|" };
                line += registration._pID;
                if ( _context.hasPassed() )
                {
                    ++_passedCount;
                }
                else
                {
                    ++_failedCount;
                    line += "|";
                    line += _context.getFailure();
                }
                appendLine( line, _context.hasPassed() == false );
            }

        private:
            EditorSelfTestContext        _context;
            optional<EditorSettleWait>   _settleWait; ///< 테스트가 `WaitSettled` 를 돌려 정지를 기다리는 중이면 있다(그동안 테스트를 부르지 않는다)
            vector<uint32>               _listSelectedIndex;
            vector<EditorSelfTestResult> _listResult;
            string                       _pattern;
            string                       _report;
            uint32                       _cursor;
            uint32                       _passedCount;
            uint32                       _failedCount;
            uint8                        _bActive           : 1; ///< 실행이 있다(시작 전 워밍업 포함)
            uint8                        _bStarted          : 1;
            uint8                        _bQuitWhenDone     : 1;
            uint8                        _bMarksWereEnabled : 1; ///< 시작 전 레이블 기록 상태(끝나면 되돌린다)
        };

        EditorSelfTestRun::EditorSelfTestRun()
            : _context{}
            , _settleWait{}
            , _listSelectedIndex{}
            , _listResult{}
            , _pattern{}
            , _report{}
            , _cursor{ 0 }
            , _passedCount{ 0 }
            , _failedCount{ 0 }
            , _bActive{ SW_FALSE }
            , _bStarted{ SW_FALSE }
            , _bQuitWhenDone{ SW_FALSE }
            , _bMarksWereEnabled{ SW_FALSE }
        {
        }

        /**
         * @class EditorSelfTestSession
         * @brief 에디터 자체 테스트 실행기의 상태 소유자입니다. 에디터 서브시스템이라 에디터 컨텍스트와 함께 생기고 사라진다(`SW_EDITOR_SUBSYSTEM`).
         * @details 실행기(`EditorSelfTestRunner`)의 정적 함수는 이 서브시스템을 `find` 로 찾아 쓴다. 함수 정적 지역에 상태를 두면 서비스 접근 게이트가 못 보고,
         *          모듈을 다시 올릴 때와 컨텍스트를 새로 만들 때 이전 실행의 값이 남는다. 여기 두면 컨텍스트가 사라질 때 실행 상태와 테스트의 프로브 상태가 함께 사라진다.
         *          이 형은 이 파일 밖에서 찾을 수 없다(익명 이름공간).
         */
        class EditorSelfTestSession final : public EditorSubsystem
        {
        public:
            static constexpr const utf8* kSubsystemName = "EditorSelfTestSession";

            EditorSelfTestSession()
                : _run{}
                , _bCommandLineConsumed{ SW_FALSE }
            {
            }

            const utf8* getSubsystemName() const override { return kSubsystemName; }

            /** @brief 지금 에디터 범위의 세션입니다. 에디터 컨텍스트가 없으면 nullptr 입니다. */
            static EditorSelfTestSession* find() { return editor::findSubsystem<EditorSelfTestSession>(); }

            const EditorSelfTestRun& getRun() const { return _run; }
            EditorSelfTestRun&       getRun() { return _run; }

            /** @brief 새 실행을 요청합니다. 도는 실행이 있거나 패턴이 비면 false 입니다. */
            bool requestRun( string_view pattern, bool bQuitWhenDone )
            {
                if ( _run.isActive() || pattern.empty() )
                    return false;
                _run.start( pattern, bQuitWhenDone );
                return true;
            }

            /** @brief 한 프레임 진행합니다. 처음 한 번은 `-gv_editorSelfTest` 를 실행으로 바꾼다. */
            void runFrame()
            {
                if ( _bCommandLineConsumed == SW_FALSE )
                {
                    _bCommandLineConsumed = SW_TRUE;
                    if ( gv_editorSelfTest.empty() == false )
                        (void)requestRun( gv_editorSelfTest, true ); // 기동 직후라 도는 실행이 없다
                }
                _run.advanceFrame();
            }

            /**
             * @brief 시나리오 체인의 다음 단계 앞에서 끝난 실행의 결과를 비웁니다(새 프로세스처럼). 도는 실행과 명령줄 소비 표시는 둔다 - 비우면 `-gv_editorSelfTest` 가 다시 돈다.
             * @details 앞 단계(`selftests`)의 결과가 남아 Test Runner 를 다루는 단계(`testrunner`)가 통과 수 0 에서 시작하지 못했다.
             */
            static void resetFinishedRun()
            {
                EditorSelfTestSession* pSession = find(); // 선택: 편집기를 켜지 않은 체인에는 없다
                if ( pSession != nullptr && pSession->_run.isActive() == false )
                    pSession->_run = EditorSelfTestRun{};
            }

        private:
            EditorSelfTestRun _run;
            uint8             _bCommandLineConsumed : 1; ///< `-gv_editorSelfTest` 를 한 번 실행으로 바꿨다. 실행을 비워도 남는다
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    SW_EDITOR_SUBSYSTEM( EditorSelfTestSession, 9500 );

    EditorSelfTestContext::EditorSelfTestContext()
        : _failure{}
        , _stepIndex{ 0 }
    {
    }

    bool EditorSelfTestContext::expect( bool bCondition, const utf8* pWhat )
    {
        if ( bCondition == false && _failure.empty() )
        {
            _failure = ( pWhat != nullptr ) ? pWhat : "expectation failed";
            if ( _failure.empty() )
                _failure = "expectation failed";
            _failure += " (step ";
            _failure += to_string( _stepIndex );
            _failure += ")";
        }
        return bCondition;
    }

    bool EditorSelfTestRunner::isRequested()
    {
        return gv_editorSelfTest.empty() == false;
    }

    bool EditorSelfTestRunner::matchesPattern( string_view id, string_view pattern )
    {
        for ( const string_view piece : StringUtil::split( pattern, ",", { ._bDropEmpty = true } ) )
        {
            if ( matchesSelfTestGlob( id, piece ) )
                return true;
        }
        return false;
    }

    bool EditorSelfTestRunner::requestRun( string_view pattern, bool bQuitWhenDone )
    {
        EditorSelfTestSession* pSession = EditorSelfTestSession::find();
        return pSession != nullptr && pSession->requestRun( pattern, bQuitWhenDone );
    }

    bool EditorSelfTestRunner::isRunning()
    {
        const EditorSelfTestSession* pSession = EditorSelfTestSession::find();
        return pSession != nullptr && pSession->getRun().isActive();
    }

    string EditorSelfTestRunner::describeProgress()
    {
        const EditorSelfTestSession* pSession = EditorSelfTestSession::find();
        return pSession != nullptr ? pSession->getRun().describeProgress() : string( "not running" );
    }

    vector_reference<const EditorSelfTestResult> EditorSelfTestRunner::getResults()
    {
        const EditorSelfTestSession* pSession = EditorSelfTestSession::find();
        if ( pSession == nullptr )
            return {};
        return pSession->getRun().getResults();
    }

    EditorSelfTestContext* EditorSelfTestRunner::findRunningContext()
    {
        EditorSelfTestSession* pSession = EditorSelfTestSession::find();
        return pSession != nullptr ? pSession->getRun().findRunningContext() : nullptr;
    }

    void EditorSelfTestRunner::runFrame()
    {
        EditorSelfTestSession* pSession = EditorSelfTestSession::find();
        if ( pSession != nullptr ) // 에디터 컨텍스트가 없으면 돌릴 실행도 없다
            pSession->runFrame();
    }

    // 이 종류의 등록 목록 — EditorModule 이미지에 하나(확장 DLL 의 등록자도 내보낸 이 함수로 오른다).
    SW_DEFINE_EDITOR_EXTENSION_KIND( EditorSelfTestRegistration )
} // namespace sw::editor

namespace sw
{
    SW_AUTOMATION_STAGE_RESET( editorSelfTestResults, "editor self test results", &editor::EditorSelfTestSession::resetFinishedRun );
} // namespace sw

#include "pch.h"

#include "Editor/SelfTest/EditorSelfTest.h"

#include "Core/File/FileUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/Logger.h"

#include "Editor/Common/EditorSubsystem.h"
#include "Editor/Common/Workspace/EditorExtensionSubsystems.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Window/IWindow.h"
#include "Engine/Window/MainWindowSubsystem.h"

namespace sw::editor
{
    namespace
    {
        /**
         * @class EditorSelfTestSession
         * @brief 에디터 자체 테스트 실행기의 상태 소유자입니다. 에디터 서브시스템이라 에디터 컨텍스트와 함께 생기고 사라진다(`SW_EDITOR_SUBSYSTEM`).
         * @details 실행기(`EditorSelfTestRunner`)의 정적 함수는 이 서브시스템을 `findSubsystem` 으로 찾아 쓴다. 함수 정적 지역에 상태를 두면 서비스 접근 게이트가 못 보고,
         *          모듈을 다시 올릴 때와 컨텍스트를 새로 만들 때 이전 실행의 값이 남는다. 여기 두면 컨텍스트가 사라질 때 실행 상태와 테스트의 프로브 상태가 함께 사라진다.
         *          이 형은 이 파일 밖에서 찾을 수 없다(익명 이름공간).
         */
        class EditorSelfTestSession final : public EditorSubsystem
        {
        public:
            static constexpr const utf8* kSubsystemName = "EditorSelfTestSession";

            /** @brief 한 번의 실행 상태입니다. 새 실행을 요청하면 비웁니다. */
            struct RunState
            {
                EditorSelfTestContext        _context;
                vector<uint32>               _listSelectedIndex;
                vector<EditorSelfTestResult> _listResult;
                string                       _pattern;
                string                       _report;
                uint32                       _warmupFrameCount{ 0 };
                uint32                       _cursor{ 0 };
                uint32                       _passedCount{ 0 };
                uint32                       _failedCount{ 0 };
                bool                         _bActive{ false }; ///< 실행이 있다(시작 전 워밍업 포함)
                bool                         _bStarted{ false };
                bool                         _bQuitWhenDone{ false };
                bool                         _bMarksWereEnabled{ false }; ///< 시작 전 레이블 기록 상태(끝나면 되돌린다)
            };

            RunState _run;
            bool     _bCommandLineConsumed{ false }; ///< `-gv_editorSelfTest` 를 한 번 실행으로 바꿨다. 실행을 비워도 남는다

            const utf8* getSubsystemName() const override { return kSubsystemName; }
        };

        struct EditorSelfTestInternal
        {
            using RunState = EditorSelfTestSession::RunState;

            /** @brief 테스트를 시작하기 전에 기다리는 에디터 프레임 수입니다. 패널이 한 번씩 그려지고 기본 도킹이 적용될 때까지입니다. */
            static constexpr uint32 kWarmupFrameCount = 5;
            /** @brief 테스트 하나가 쓸 수 있는 최대 단계(프레임) 수입니다. 넘으면 실패로 적고 다음 테스트로 갑니다. */
            static constexpr uint32 kMaxStepCount = 300;

            /** @brief 지금 에디터 범위의 세션입니다. 에디터 컨텍스트가 없으면 nullptr 입니다. */
            static EditorSelfTestSession* findSession() { return editor::findSubsystem<EditorSelfTestSession>(); }

            /** @brief 글자열 @p text 가 `*` 를 포함한 패턴 @p pattern 하나에 맞는지 봅니다. */
            static bool matchesGlob( string_view text, string_view pattern )
            {
                if ( pattern.empty() )
                    return text.empty();
                if ( pattern[0] == '*' )
                {
                    for ( size_t skipCount = 0; skipCount <= text.size(); ++skipCount )
                    {
                        if ( matchesGlob( text.substr( skipCount ), pattern.substr( 1 ) ) )
                            return true;
                    }
                    return false;
                }
                return text.empty() == false && text[0] == pattern[0] && matchesGlob( text.substr( 1 ), pattern.substr( 1 ) );
            }

            /** @brief 결과 한 줄을 로그와 보고서에 남깁니다. 실패는 오류 로그라 `[Error]` 를 세는 쪽도 봅니다. */
            static void appendLine( RunState& state, const string& line, bool bFailure )
            {
                if ( bFailure )
                    SW_LOG_ERROR( "%#", line.c_str() );
                else
                    SW_LOG_INFO( "%#", line.c_str() );
                state._report += line;
                state._report += "\n";
            }

            static void recordResult( RunState& state, const EditorSelfTestRegistration& registration )
            {
                const EditorSelfTestContext& context = state._context;
                EditorSelfTestResult         result;
                result._id         = registration._pID;
                result._reason     = context.getFailure();
                result._frameCount = context.getStepIndex();
                result._bPassed    = context.hasPassed();
                state._listResult.push_back( result );
                string line{ context.hasPassed() ? "EditorSelfTest|PASS|" : "EditorSelfTest|FAIL|" };
                line += registration._pID;
                if ( context.hasPassed() )
                {
                    ++state._passedCount;
                }
                else
                {
                    ++state._failedCount;
                    line += "|";
                    line += context.getFailure();
                }
                appendLine( state, line, context.hasPassed() == false );
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    SW_LOG_CALLER( "EditorSelfTest" );

    SW_EDITOR_SUBSYSTEM( EditorSelfTestSession, 9500 );

    // 이 파일만 읽으므로 여기서 정의한다(헤더에 선언하지 않는다).
    /** @brief `-gv_editorSelfTest=<패턴>`: 에디터가 뜬 뒤 이름이 맞는 에디터 자체 테스트를 돌리고 앱을 닫습니다(`*` 와 쉼표). */
    SW_TEST_GLOBAL_VARIABLE( sw::string, gv_editorSelfTest, "", "에디터가 뜬 뒤 이름이 패턴에 맞는 에디터 자체 테스트를 돌리고 끝낸다 (* · 쉼표, 비우면 사용 안 함)" );
    /** @brief `-gv_editorSelfTestReport=<파일>`: 에디터 자체 테스트의 결과 줄을 이 파일에 씁니다. */
    SW_TEST_GLOBAL_VARIABLE( sw::string, gv_editorSelfTestReport, "", "에디터 자체 테스트 결과를 쓸 파일 (비우면 로그에만)" );

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
        size_t begin = 0;
        while ( begin <= pattern.size() )
        {
            size_t end = pattern.find( ',', begin );
            if ( end == string_view::npos )
                end = pattern.size();
            const string_view piece = pattern.substr( begin, end - begin );
            if ( piece.empty() == false && EditorSelfTestInternal::matchesGlob( id, piece ) )
                return true;
            begin = end + 1;
        }
        return false;
    }

    bool EditorSelfTestRunner::requestRun( string_view pattern, bool bQuitWhenDone )
    {
        EditorSelfTestSession* pSession = EditorSelfTestInternal::findSession();
        const bool             bBusy    = pSession == nullptr || pSession->_run._bActive; // 세션이 없거나 도는 실행이 있다
        if ( bBusy || pattern.empty() )
            return false;
        EditorSelfTestInternal::RunState& state = pSession->_run;
        state                                   = EditorSelfTestInternal::RunState{};
        state._pattern                          = string( pattern );
        state._bQuitWhenDone                    = bQuitWhenDone;
        state._bActive                          = true;
        // 창에서 누른 실행은 패널이 이미 다 그려져 있다 — 워밍업은 명령줄 기동 때만.
        if ( bQuitWhenDone == false )
            state._warmupFrameCount = EditorSelfTestInternal::kWarmupFrameCount;
        return true;
    }

    bool EditorSelfTestRunner::isRunning()
    {
        const EditorSelfTestSession* pSession = EditorSelfTestInternal::findSession();
        return pSession != nullptr && pSession->_run._bActive;
    }

    vector_reference<const EditorSelfTestResult> EditorSelfTestRunner::getResults()
    {
        const EditorSelfTestSession* pSession = EditorSelfTestInternal::findSession();
        if ( pSession == nullptr )
            return {};
        return pSession->_run._listResult;
    }

    EditorSelfTestContext* EditorSelfTestRunner::findRunningContext()
    {
        EditorSelfTestSession* pSession = EditorSelfTestInternal::findSession();
        const bool             bRunning = pSession != nullptr && pSession->_run._bActive && pSession->_run._bStarted;
        return bRunning ? &pSession->_run._context : nullptr;
    }

    void EditorSelfTestRunner::runFrame()
    {
        EditorSelfTestSession* pSession = EditorSelfTestInternal::findSession();
        if ( pSession == nullptr )
            return; // 에디터 컨텍스트가 없다 — 돌릴 실행도 없다
        EditorSelfTestInternal::RunState& state = pSession->_run;
        if ( pSession->_bCommandLineConsumed == false )
        {
            pSession->_bCommandLineConsumed = true;
            if ( gv_editorSelfTest.empty() == false )
                (void)requestRun( gv_editorSelfTest, true ); // 기동 직후라 도는 실행이 없다
        }
        if ( state._bActive == false )
            return;
        if ( state._warmupFrameCount < EditorSelfTestInternal::kWarmupFrameCount )
        {
            ++state._warmupFrameCount;
            return;
        }

        using Registry = EditorExtensionRegistry<EditorSelfTestRegistration>;
        if ( state._bStarted == false )
        {
            state._bStarted = true;
            // 테스트가 도는 동안 패널이 누를 위젯의 레이블을 적는다(EditorSelfTestMarks::note). 끝나면 시작 전 상태로 되돌린다.
            state._bMarksWereEnabled = EditorSelfTestMarks::isEnabled();
            (void)EditorSelfTestMarks::setEnabled( true ); // 세션이 있으니 에디터 컨텍스트도 있다
            for ( uint32 index = 0; index < Registry::getCount(); ++index )
            {
                if ( matchesPattern( Registry::getAt( index )._pID, state._pattern ) )
                    state._listSelectedIndex.push_back( index );
            }
            SW_LOG_INFO( "Running %# editor self tests matching '%#'", static_cast<uint32>( state._listSelectedIndex.size() ), state._pattern.c_str() );
        }

        if ( state._cursor < state._listSelectedIndex.size() )
        {
            const EditorSelfTestRegistration& registration = Registry::getAt( state._listSelectedIndex[state._cursor] );
            const EditorSelfTestStep          step         = registration._pfnRun( state._context );
            state._context.advanceStep();

            const bool bTimedOut = step == EditorSelfTestStep::Continue && state._context.getStepIndex() >= EditorSelfTestInternal::kMaxStepCount;
            if ( bTimedOut )
                (void)state._context.expect( false, "the test did not finish within its frame budget" );
            if ( step == EditorSelfTestStep::Done || bTimedOut )
            {
                EditorSelfTestInternal::recordResult( state, registration );
                state._context = EditorSelfTestContext{};
                ++state._cursor;
            }
            return;
        }

        // 모두 끝났다. 하나도 맞지 않은 패턴은 실패다 — 이름을 잘못 적은 실행이 초록으로 보이면 안 된다.
        state._bActive = false;
        (void)EditorSelfTestMarks::setEnabled( state._bMarksWereEnabled ); // 세션이 있으니 에디터 컨텍스트도 있다
        if ( state._listSelectedIndex.empty() )
            ++state._failedCount;
        string doneLine{ "EditorSelfTest|DONE|" };
        doneLine += to_string( state._passedCount );
        doneLine += "|";
        doneLine += to_string( state._failedCount );
        EditorSelfTestInternal::appendLine( state, doneLine, state._failedCount > 0 );
        if ( state._bQuitWhenDone == false )
            return;

        if ( gv_editorSelfTestReport.empty() == false )
        {
            (void)FileUtil::ensureParentDirectoryExists( gv_editorSelfTestReport );
            if ( FileUtil::writeTextFile( gv_editorSelfTestReport, state._report ) == false )
                SW_LOG_ERROR( "Could not write the editor self test report to %#", gv_editorSelfTestReport.c_str() );
        }

        MainWindowSubsystem* pWindowHost = editor::findSubsystem<MainWindowSubsystem>();
        IWindow*             pWindow     = pWindowHost != nullptr ? pWindowHost->getWindow() : nullptr;
        if ( pWindow != nullptr )
            pWindow->requestClose();
    }
} // namespace sw::editor

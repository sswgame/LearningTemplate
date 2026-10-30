#include "pch.h"

#include "Engine/Automation/Runner/AutomationBackendMatrix.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Concurrency/thread.h"
#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Process/Process.h"
#include "Core/String/StringBuilder.h"
#include "Core/Time/MonotonicClock.h"
#include "Core/Time/WallClock.h"

#include "Engine/Automation/Runner/AutomationExitCode.h"
#include "Engine/Automation/Runner/AutomationScenarioChain.h"
#include "Engine/Automation/Scenario/AutomationScenario.h"
#include "Engine/Automation/Scenario/AutomationTimeLimits.h"
#include "Engine/Graphics/RHI/RHIInitResult.h"
#include "Engine/Serialization/JSON/JSONWriter.h"

#include "sw/config/CookContract.gen.h"

SW_LOG_CALLER( "ScenarioMatrix" );

namespace sw
{
    namespace
    {
        struct AutomationBackendMatrixInternal
        {
            static constexpr uint32 kTileColumnCount = 2; ///< 자식 창 바둑판의 열 수 — 백엔드 넷이면 2×2
            // shortcut: 창 틀(제목 줄 · 테두리) 두께를 Win32 기본 모양의 대략 값으로 둔다, 플랫폼 창 틀 크기를 묻는 API 가 생기면 그 값으로.
            static constexpr uint32 kFrameMarginX = 16;
            static constexpr uint32 kFrameMarginY = 40;
            /** @brief 쿠킹 표의 명령줄 이름(표 순서) — 행렬이 받는 백엔드 이름은 이것뿐이다(`-vk` 와 같은 철자). */
            static constexpr const utf8* kArrBackendName[] = {
#define SW_MATRIX_BACKEND_NAME_ROW( Backend, ShaderFolder, ShaderTarget, Argument, CommandLineName ) CommandLineName,
                SW_RHI_BACKEND_TABLE( SW_MATRIX_BACKEND_NAME_ROW )
#undef SW_MATRIX_BACKEND_NAME_ROW
            };

            /** @brief 그린 화소를 판정하는 단계(종류 · 그 판정을 켜는 속성, 비면 늘) — 이것이 있는 시나리오는 백엔드마다 결과가 다를 수 있어 나눌 때도 모든 백엔드에서 돈다. */
            struct PixelJudgingStep
            {
                string_view _kind;
                string_view _attribute;
            };
            // `CaptureEditorUI` 는 마젠타 문턱이 있을 때만 화소를 본다 — 문턱 없는 캡처는 사람 · 리뷰가 볼 그림을 남기고, 잘린 창 수는 레이아웃이라 백엔드와 무관하다.
            static constexpr PixelJudgingStep kArrPixelJudgingStep[] = {
                {    "ExpectImage",                   {}},
                {"CaptureEditorUI", "maxMagentaFraction"}
            };

            static bool judgesPixels( const AutomationStep& step )
            {
                return std::any_of( std::begin( kArrPixelJudgingStep ), std::end( kArrPixelJudgingStep ), [&step]( const PixelJudgingStep& judging )
                { return step._kind == judging._kind && ( judging._attribute.empty() || step.findAttribute( judging._attribute ) != nullptr ); } );
            }

            /** @brief 자식 하나의 상태입니다. 출력 파이프는 스레드 하나가 끝까지 비운다 — 비우지 않으면 파이프가 찬 자식이 멈춘다. */
            struct Child
            {
                string  _backend{};
                string  _logPath{};
                string  _reportPath{};
                Process _process{};
                thread  _drainThread{};
                int32   _exitCode{ -1 };
                bool    _bLaunched{ false };
            };

            /** @brief 인자 한 줄의 키(앞 하이픈 · `=` 뒤를 뗀 것)입니다. */
            static string_view getArgumentKey( string_view argument )
            {
                string_view key = argument.substr( 0, argument.find( '=' ) );
                while ( key.empty() == false && key.front() == '-' )
                    key.remove_prefix( 1 );
                return key;
            }

            /** @brief 자식 종료 코드의 결과 이름입니다(보고 · 요약 줄). */
            static const utf8* describeExitCode( int32 exitCode )
            {
                const utf8* const pLabel = findProcessExitCodeLabel( exitCode );
                return pLabel != nullptr ? pLabel : "crashed"; // 표(ProcessExitCodes.json)에 없는 코드는 프로세스가 죽은 것이다
            }

            static bool isSkip( int32 exitCode ) { return exitCode == getAutomationExitCode( AutomationOutcome::Skipped ) || exitCode == kRHIUnusableHereExitCode; }

            static void writeReport( const AutomationBackendMatrixOptions& options, const vector<unique_ptr<Child>>& listChild, int32 exitCode )
            {
                string     json;
                JSONWriter writer{ json, 2 };
                writer.beginObject();
                writer.key( "scenario" );
                writer.valueString( options._scenarioPath );
                writer.key( "exitCode" );
                writer.valueInt( exitCode );
                writer.key( "backends" );
                writer.beginArray();
                for ( const unique_ptr<Child>& pChild : listChild )
                {
                    writer.beginObject();
                    writer.key( "backend" );
                    writer.valueString( pChild->_backend );
                    writer.key( "result" );
                    writer.valueString( describeExitCode( pChild->_exitCode ) );
                    writer.key( "exitCode" );
                    writer.valueInt( pChild->_exitCode );
                    writer.key( "log" );
                    writer.valueString( pChild->_logPath );
                    writer.key( "report" );
                    writer.valueString( pChild->_reportPath );
                    writer.endObject();
                }
                writer.endArray();
                writer.endObject();
                if ( FileUtil::ensureParentDirectoryExists( options._reportPath ) == false || FileUtil::writeTextFile( options._reportPath, json ) == false )
                    SW_LOG_ERROR( "[Scenario] matrix: could not write the report %#", options._reportPath.c_str() );
            }
        };
    } // namespace

    bool AutomationBackendMatrix::parseBackendList( string_view text, vector<string>& outListBackend, string& outError )
    {
        using Internal = AutomationBackendMatrixInternal;
        outListBackend.clear();
        const vector<string_view> listName = StringUtil::split( text, ",", { ._bTrim = true, ._bDropEmpty = true } );
        // 표 순서로 모은다 — 적은 순서와 무관하게 보고 · 종료 코드의 "첫 실패" 가 늘 같다.
        for ( const utf8* pName : Internal::kArrBackendName )
        {
            const bool bAll = listName.size() == 1 && listName[0] == "all";
            if ( bAll || std::find( listName.begin(), listName.end(), string_view{ pName } ) != listName.end() )
                outListBackend.emplace_back( pName );
        }
        for ( const string_view name : listName )
        {
            const bool bKnown = name == "all" ? listName.size() == 1
                                              : std::find( std::begin( Internal::kArrBackendName ), std::end( Internal::kArrBackendName ), name ) != std::end( Internal::kArrBackendName );
            if ( bKnown == false || std::count( listName.begin(), listName.end(), name ) > 1 )
            {
                outError = "-scenario-backends: '" + string( name ) + "' is not one backend of all | dx11,dx12,vk,gl (each once; all alone)";
                return false;
            }
        }
        if ( outListBackend.empty() )
        {
            outError = "-scenario-backends: no backend given (all | dx11,dx12,vk,gl)";
            return false;
        }
        return true;
    }

    vector<string> AutomationBackendMatrix::filterPassThroughArguments( const vector<string>& listOriginalArgument )
    {
        using Internal                                   = AutomationBackendMatrixInternal;
        static constexpr CommandLineArgument kArrOwned[] = { CommandLineArgument::SCENARIO, CommandLineArgument::SCENARIO_BACKENDS, CommandLineArgument::SCENARIO_BACKEND_SPLIT, CommandLineArgument::SCENARIO_BACKEND_SPLIT_OFFSET, CommandLineArgument::SCENARIO_REPORT,
                                                             CommandLineArgument::LOG_FILE };
        vector<string>                       listArgument;
        for ( const string& argument : listOriginalArgument )
        {
            const string_view key    = Internal::getArgumentKey( argument );
            const bool        bOwned = std::any_of( std::begin( kArrOwned ), std::end( kArrOwned ),
                                                    [key]( CommandLineArgument owned )
                   { return key == getCommandLineArgumentSpelling( owned ); } ) ||
                                std::find( std::begin( Internal::kArrBackendName ), std::end( Internal::kArrBackendName ), key ) != std::end( Internal::kArrBackendName );
            if ( bOwned == false )
                listArgument.push_back( argument );
        }
        return listArgument;
    }

    vector<string> AutomationBackendMatrix::assignScenarios( const AutomationBackendMatrixOptions& options )
    {
        using Internal = AutomationBackendMatrixInternal;
        vector<string> listScenarioList( options._listBackend.size() );
        size_t         nextBackend = static_cast<size_t>( MathUtil::max<int32>( 0, options._splitOffset ) );
        for ( const string& path : AutomationScenarioChain::splitList( options._scenarioPath ) )
        {
            AutomationScenario scenario;
            string             error;
            const bool         bLoaded       = scenario.loadFromPath( AssetID{ path }, error );
            const bool         bJudgesPixels = std::any_of( scenario.getSteps().begin(), scenario.getSteps().end(), &Internal::judgesPixels );
            for ( size_t i = 0; i < listScenarioList.size(); ++i )
            {
                const bool bThisBackend = options._bSplitScenarios == false || bLoaded == false || bJudgesPixels || i == nextBackend % listScenarioList.size();
                if ( bThisBackend )
                    listScenarioList[i] += ( listScenarioList[i].empty() ? "" : "," ) + path;
            }
            if ( options._bSplitScenarios && bLoaded && bJudgesPixels == false )
                ++nextBackend;
        }
        return listScenarioList;
    }

    string AutomationBackendMatrix::insertBackendSuffix( string_view path, string_view backend )
    {
        return FileUtil::removeExtension( path ) + "." + string( backend ) + FileUtil::getExtension( path );
    }

    int32 AutomationBackendMatrix::run( const AutomationBackendMatrixOptions& options )
    {
        using Internal             = AutomationBackendMatrixInternal;
        const string scenarioStem  = FileUtil::removeExtension( FileUtil::removeExtension( FileUtil::getFileNamePart( options._scenarioPath ) ) ); // x.scenario.xml → x
        const string defaultFolder = formatToString( "%#/%#/matrix-%#", makeAutomationOutputFolderPath(), scenarioStem, Process::getCurrentProcessID() );

        // 넘겨받은 인자가 창 위치를 정했으면(손으로 돌린 실행) 그것을 따른다.
        const bool bTileWindows = options._windowWidth > 0 && options._windowHeight > 0 &&
                                  std::none_of( options._listPassThroughArgument.begin(), options._listPassThroughArgument.end(), []( const string& argument )
        { return Internal::getArgumentKey( argument ) == getCommandLineArgumentSpelling( CommandLineArgument::WINDOW_X ); } );
        // 나눌 때 배정의 시작점 — 주지 않았으면 UTC 날짜(일)로 정해 날마다 한 칸씩 돌린다. 백엔드와 무관하다고 본 시나리오도 나흘이면 모든 백엔드를 거친다.
        AutomationBackendMatrixOptions assignOptions = options;
        if ( options._bSplitScenarios && assignOptions._splitOffset < 0 )
        {
            constexpr int64 kSecondsPerDay = 24 * 60 * 60;
            assignOptions._splitOffset     = static_cast<int32>( ( WallClock::nowUnixMilliseconds().toSeconds() / kSecondsPerDay ) % static_cast<int64>( MathUtil::max<size_t>( 1, options._listBackend.size() ) ) );
        }
        if ( options._bSplitScenarios )
            SW_LOG_INFO( "[Scenario] matrix: split offset %# (rerun the same assignment with -scenario-backend-split-offset=%#)", assignOptions._splitOffset, assignOptions._splitOffset );
        const vector<string>                listScenarioList = assignScenarios( assignOptions );
        vector<unique_ptr<Internal::Child>> listChild;
        for ( const string& backend : options._listBackend )
        {
            unique_ptr<Internal::Child> pChild = make_unique<Internal::Child>();
            pChild->_backend                   = backend;
            const string& scenarioList         = listScenarioList[listChild.size()];
            if ( scenarioList.empty() )
            {
                pChild->_exitCode = getAutomationExitCode( AutomationOutcome::Skipped ); // 나눈 목록이 백엔드 수보다 짧다 — 맡은 시나리오가 없다
                listChild.push_back( std::move( pChild ) );
                continue;
            }
            pChild->_logPath    = options._logFilePath.empty() ? defaultFolder + "/" + backend + ".log" : insertBackendSuffix( options._logFilePath, backend );
            pChild->_reportPath = options._reportPath.empty() ? string{} : insertBackendSuffix( options._reportPath, backend );

            vector<string> listArgument{ "-scenario=" + scenarioList, "-" + backend, "-log-file=" + pChild->_logPath };
            if ( pChild->_reportPath.empty() == false )
                listArgument.push_back( "-scenario-report=" + pChild->_reportPath );
            if ( bTileWindows )
            {
                const uint32 tileIndex = static_cast<uint32>( listChild.size() );
                listArgument.push_back( formatToString( "-WinX=%#", ( tileIndex % Internal::kTileColumnCount ) * ( options._windowWidth + Internal::kFrameMarginX ) ) );
                listArgument.push_back( formatToString( "-WinY=%#", ( tileIndex / Internal::kTileColumnCount ) * ( options._windowHeight + Internal::kFrameMarginY ) ) );
            }
            listArgument.insert( listArgument.end(), options._listPassThroughArgument.begin(), options._listPassThroughArgument.end() );

            const ProcessCommand command{ options._executablePath, std::move( listArgument ) };
            pChild->_bLaunched = pChild->_process.launch( command );
            if ( pChild->_bLaunched == false )
                SW_LOG_ERROR( "[Scenario] matrix: could not launch %#", command.toCommandLine().c_str() );
            else
            {
                SW_LOG_INFO( "[Scenario] matrix: %# started (log %#)", backend.c_str(), pChild->_logPath.c_str() );
                Process* pProcess    = &pChild->_process;
                pChild->_drainThread = thread( [pProcess]()
                {
                    string line;
                    while ( pProcess->readOutputLine( line ) )
                    {
                    } // 자식의 로그는 제 파일에 있다 — 콘솔 줄은 버린다
                } );
            }
            listChild.push_back( std::move( pChild ) );
        }

        // 프로세스 시한 — 자식 안의 감시(행 보고)가 먼저 닿고, 그래도 끝나지 않은 자식만 여기서 죽인다. 부모가 기다리기만 하면 멈춘 자식 하나에 CTest 시한이
        // 부모만 죽이고 자식은 고아로 남는다(Windows 는 부모가 죽어도 자식을 거두지 않는다).
        // 시한은 단계마다다 — 체인(`-scenario=a,b,c`)은 단계 수만큼 길다.
        const int64    stageCount   = MathUtil::max<int64>( 1, static_cast<int64>( AutomationScenarioChain::splitList( options._scenarioPath ).size() ) );
        const int64    killSeconds  = static_cast<int64>( AutomationTimeLimits::kProcessKillSeconds ) * stageCount;
        const Deadline killDeadline = Deadline::afterMilliseconds( killSeconds * 1000 );
        int32          firstFailure = 0;
        bool           bAnyPassed   = false;
        for ( unique_ptr<Internal::Child>& pChild : listChild )
        {
            if ( pChild->_bLaunched )
            {
                while ( pChild->_process.isRunning() && killDeadline.isExpired() == false )
                    MonotonicClock::sleepUntilNanoseconds( MonotonicClock::nowNanoseconds() + 50 * constant::kNanosecondsPerMillisecond );
                const bool bKilled = pChild->_process.isRunning() && pChild->_process.terminate( getAutomationExitCode( AutomationOutcome::TimedOut ) );
                if ( bKilled )
                    SW_LOG_ERROR( "[Scenario] matrix: %# did not finish within %# s - killed", pChild->_backend.c_str(), killSeconds );
                pChild->_exitCode = pChild->_process.waitForExit();
                if ( bKilled )
                    pChild->_exitCode = getAutomationExitCode( AutomationOutcome::TimedOut ); // POSIX 는 종료 코드를 줄 수 없다(128 + SIGKILL) — 결과는 시간 초과다
                pChild->_drainThread.join();
            }
            const bool bSkipped = Internal::isSkip( pChild->_exitCode );
            bAnyPassed          = bAnyPassed || pChild->_exitCode == 0;
            if ( pChild->_exitCode != 0 && bSkipped == false && firstFailure == 0 )
                firstFailure = pChild->_exitCode == -1 ? getAutomationExitCode( AutomationOutcome::Failed ) : pChild->_exitCode;
            const LogLevel level = pChild->_exitCode == 0 || bSkipped ? LogLevel::Info : LogLevel::Error;
            if ( level == LogLevel::Error )
                SW_LOG_ERROR( "[Scenario] matrix: %# %# (exit %#) - %#", pChild->_backend.c_str(), Internal::describeExitCode( pChild->_exitCode ), pChild->_exitCode,
                              pChild->_logPath.c_str() );
            else
                SW_LOG_INFO( "[Scenario] matrix: %# %# (exit %#) - %#", pChild->_backend.c_str(), Internal::describeExitCode( pChild->_exitCode ), pChild->_exitCode,
                             pChild->_logPath.c_str() );
        }

        const int32 exitCode = firstFailure != 0 ? firstFailure : bAnyPassed ? 0
                                                                             : getAutomationExitCode( AutomationOutcome::Skipped );
        if ( options._reportPath.empty() == false )
            Internal::writeReport( options, listChild, exitCode );
        return exitCode;
    }
} // namespace sw

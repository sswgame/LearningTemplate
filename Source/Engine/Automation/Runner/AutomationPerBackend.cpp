#include "pch.h"

#include "Engine/Automation/Runner/AutomationPerBackend.h"

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
#include "Engine/Serialization/JSON/JSONDocument.h"
#include "Engine/Serialization/JSON/JSONWriter.h"

#include "sw/config/CookContract.gen.h"

SW_LOG_CALLER( "ScenarioPerBackend" );

namespace sw
{
    namespace
    {
        struct AutomationPerBackendInternal
        {
            static constexpr uint32 kTileColumnCount = 2; ///< 자식 창 바둑판의 열 수 — 백엔드 넷이면 2×2
            // shortcut: 창 틀(제목 줄 · 테두리) 두께를 Win32 기본 모양의 대략 값으로 둔다, 플랫폼 창 틀 크기를 묻는 API 가 생기면 그 값으로.
            static constexpr uint32 kFrameMarginX = 16;
            static constexpr uint32 kFrameMarginY = 40;
            /** @brief 쿠킹 표의 명령줄 이름(표 순서) — 부모가 받는 백엔드 이름은 이것뿐이다(`-vk` 와 같은 철자). */
            static constexpr const utf8* kArrBackendName[] = {
#define SW_MATRIX_BACKEND_NAME_ROW( Backend, ShaderFolder, ShaderTarget, Argument, CommandLineName ) CommandLineName,
                SW_RHI_BACKEND_TABLE( SW_MATRIX_BACKEND_NAME_ROW )
#undef SW_MATRIX_BACKEND_NAME_ROW
            };

            /**
             * @brief 판정이 백엔드에 달린 단계(종류 · 그 판정을 켜는 속성, 비면 늘) — 이것이 있는 시나리오는 백엔드마다 결과가 다를 수 있어 나눌 때도 모든 백엔드에서 돈다.
             *        그린 화소를 보는 단계와, GPU 수명 결함을 백엔드 검증 레이어(Vulkan)의 로그로만 드러내는 에디터 자체 테스트(`EditorSelfTest`)와,
             *        백엔드 사이에 같아야 하는 값을 남기는 `Record`(부모가 자식 보고의 `records[]` 를 견준다 — `findRecordMismatches`, 한 백엔드에서만 돌면 견줄 것이 없다)다.
             */
            struct BackendJudgingStep
            {
                string_view _kind;
                string_view _attribute;
            };
            // `CaptureEditorUI` 는 마젠타 문턱이 있을 때만 화소를 본다 — 문턱 없는 캡처는 사람 · 리뷰가 볼 그림을 남기고, 잘린 창 수는 레이아웃이라 백엔드와 무관하다.
            static constexpr BackendJudgingStep kArrBackendJudgingStep[] = {
                {    "ExpectImage",                   {}},
                {"CaptureEditorUI", "maxMagentaFraction"},
                { "EditorSelfTest",                   {}},
                {         "Record",                   {}}
            };

            static bool judgesPerBackend( const AutomationStep& step )
            {
                return std::any_of( std::begin( kArrBackendJudgingStep ), std::end( kArrBackendJudgingStep ), [&step]( const BackendJudgingStep& judging )
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

            /** @brief 기록 줄(`frame 300 Nile.Month = 3`)의 이름 · 값 경계(마지막 ` = `)입니다. 없으면 npos — 줄 전체가 이름이다. */
            static size_t      findRecordValueSeparator( string_view line ) { return line.rfind( " = " ); }
            static string_view getRecordLabel( string_view line ) { return line.substr( 0, findRecordValueSeparator( line ) ); }
            static string_view getRecordValue( string_view line )
            {
                const size_t separator = findRecordValueSeparator( line );
                return separator == string_view::npos ? line : line.substr( separator + 3 );
            }

            /** @brief 자식이 쓴 단계 보고 하나의 `exitCode` · `records[]` 를 읽습니다. 파일이 없거나 형식이 다르면 false 와 까닭입니다. */
            [[nodiscard]] static bool readStageReport( const string& reportPath, int32& outExitCode, vector<string>& outListRecordLine, string& outError )
            {
                string text;
                if ( FileUtil::readTextFile( reportPath, text ) == false )
                {
                    outError = "could not read " + reportPath;
                    return false;
                }
                JSONDocument document;
                if ( document.parse( text, reportPath ) == false )
                {
                    outError = document.getLastError();
                    return false;
                }
                const JSONValue            root     = document.getRoot();
                const std::optional<int32> exitCode = root.readRequired<int32>( "exitCode" );
                const JSONValue            records  = root.get( "records" );
                if ( exitCode.has_value() == false || records.isArray() == false )
                {
                    outError = reportPath + ": no exitCode or records[]";
                    return false;
                }
                outExitCode = *exitCode;
                outListRecordLine.clear();
                for ( size_t i = 0; i < records.size(); ++i )
                    outListRecordLine.push_back( records.at( i ).asString() );
                return true;
            }

            /**
             * @brief 자식마다 맡은 체인(@p listScenarioList, 자식 순서)의 단계 보고를 읽어, 둘 이상의 백엔드에서 통과한 시나리오의 기록을 견줍니다. 문제마다 한 줄입니다.
             * @details 통과하지 않은 단계는 견주지 않는다(그 실패가 이미 실행을 진다). 통과한 자식(종료 0)의 보고를 읽지 못하면 그것도 문제다 — 견주지 못한 채 통과하지 않는다.
             */
            static vector<string> compareChildRecords( const vector<string>& listScenarioList, const vector<unique_ptr<Child>>& listChild )
            {
                vector<std::pair<string, vector<AutomationBackendRecords>>> listScenarioRecords; // 시나리오 경로 · 그 시나리오가 통과한 백엔드의 기록(체인 순서)
                vector<string>                                              listProblem;
                for ( size_t childIndex = 0; childIndex < listChild.size(); ++childIndex )
                {
                    const Child& child = *listChild[childIndex];
                    if ( child._bLaunched == false )
                        continue;
                    const vector<string> listPath = AutomationScenarioChain::splitList( listScenarioList[childIndex] );
                    for ( size_t stage = 0; stage < listPath.size(); ++stage )
                    {
                        const string             stageReportPath = AutomationScenarioChain::makeStageReportPath( child._reportPath, stage, listPath.size() );
                        AutomationBackendRecords records{ child._backend, {} };
                        int32                    stageExitCode = -1;
                        string                   error;
                        if ( readStageReport( stageReportPath, stageExitCode, records._listRecordLine, error ) == false )
                        {
                            if ( child._exitCode == 0 )
                                listProblem.push_back( formatToString( "%#: %# report unreadable - %#", listPath[stage], child._backend, error ) );
                            continue;
                        }
                        if ( stageExitCode != 0 )
                            continue;
                        auto it = std::find_if( listScenarioRecords.begin(), listScenarioRecords.end(), [&]( const auto& entry )
                        { return entry.first == listPath[stage]; } );
                        if ( it == listScenarioRecords.end() )
                            it = listScenarioRecords.emplace( listScenarioRecords.end(), listPath[stage], vector<AutomationBackendRecords>{} );
                        it->second.push_back( std::move( records ) );
                    }
                }
                for ( const auto& [path, listRecords] : listScenarioRecords )
                {
                    for ( const string& mismatch : AutomationPerBackend::findRecordMismatches( listRecords ) )
                        listProblem.push_back( path + ": " + mismatch );
                }
                return listProblem;
            }

            static void writeReport( const AutomationPerBackendOptions& options, const vector<unique_ptr<Child>>& listChild, const vector<string>& listRecordMismatch, int32 exitCode )
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
                writer.key( "recordMismatches" ); // 백엔드 사이에 다른 기록 · 읽지 못한 자식 보고(`compareChildRecords`)
                writer.beginArray();
                for ( const string& mismatch : listRecordMismatch )
                    writer.valueString( mismatch );
                writer.endArray();
                writer.endObject();
                if ( FileUtil::ensureParentDirectoryExists( options._reportPath ) == false || FileUtil::writeTextFile( options._reportPath, json ) == false )
                    SW_LOG_ERROR( "[Scenario] per-backend: could not write the report %#", options._reportPath.c_str() );
            }
        };
    } // namespace

    bool AutomationPerBackend::parseBackendList( string_view text, vector<string>& outListBackend, string& outError )
    {
        using Internal = AutomationPerBackendInternal;
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

    vector<string> AutomationPerBackend::filterPassThroughArguments( const vector<string>& listOriginalArgument )
    {
        using Internal                                   = AutomationPerBackendInternal;
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

    vector<string> AutomationPerBackend::assignScenarios( const AutomationPerBackendOptions& options )
    {
        using Internal = AutomationPerBackendInternal;
        vector<string> listScenarioList( options._listBackend.size() );
        size_t         nextBackend = static_cast<size_t>( MathUtil::max<int32>( 0, options._splitOffset ) );
        for ( const string& path : AutomationScenarioChain::splitList( options._scenarioPath ) )
        {
            AutomationScenario scenario;
            string             error;
            const bool         bLoaded     = scenario.loadFromPath( AssetID{ path }, error );
            const bool         bPerBackend = std::any_of( scenario.getSteps().begin(), scenario.getSteps().end(), &Internal::judgesPerBackend );
            for ( size_t i = 0; i < listScenarioList.size(); ++i )
            {
                const bool bThisBackend = options._bSplitScenarios == false || bLoaded == false || bPerBackend || i == nextBackend % listScenarioList.size();
                if ( bThisBackend )
                    listScenarioList[i] += ( listScenarioList[i].empty() ? "" : "," ) + path;
            }
            if ( options._bSplitScenarios && bLoaded && bPerBackend == false )
                ++nextBackend;
        }
        return listScenarioList;
    }

    string AutomationPerBackend::insertBackendSuffix( string_view path, string_view backend )
    {
        return FileUtil::removeExtension( path ) + "." + string( backend ) + FileUtil::getExtension( path );
    }

    vector<string> AutomationPerBackend::findRecordMismatches( const vector<AutomationBackendRecords>& listRecords )
    {
        using Internal = AutomationPerBackendInternal;
        vector<string> listMismatch;
        if ( listRecords.size() < 2 )
            return listMismatch;
        size_t lineCount = 0;
        for ( const AutomationBackendRecords& records : listRecords )
            lineCount = MathUtil::max( lineCount, records._listRecordLine.size() );
        for ( size_t lineIndex = 0; lineIndex < lineCount; ++lineIndex )
        {
            const auto lineAt = [lineIndex]( const AutomationBackendRecords& records ) -> std::optional<string_view>
            {
                return lineIndex < records._listRecordLine.size() ? std::optional<string_view>{ records._listRecordLine[lineIndex] } : std::nullopt;
            };
            const std::optional<string_view> firstLine = lineAt( listRecords.front() );
            if ( std::all_of( listRecords.begin(), listRecords.end(), [&]( const AutomationBackendRecords& records )
            { return lineAt( records ) == firstLine; } ) )
                continue;
            // 이름은 그 순번이 있는 첫 백엔드의 것이다 — 다른 백엔드의 값은 이름이 같을 때만 값으로 적는다.
            string_view label{};
            for ( const AutomationBackendRecords& records : listRecords )
            {
                const std::optional<string_view> line = lineAt( records );
                if ( line.has_value() )
                {
                    label = Internal::getRecordLabel( *line );
                    break;
                }
            }
            string message = "record #" + to_string( lineIndex + 1 ) + " '" + string( label ) + "':";
            for ( const AutomationBackendRecords& records : listRecords )
            {
                const std::optional<string_view> line = lineAt( records );
                message += " " + records._backend + "=";
                if ( line.has_value() == false )
                    message += "(none)";
                else if ( Internal::getRecordLabel( *line ) == label )
                    message += string( Internal::getRecordValue( *line ) );
                else
                    message += "'" + string( *line ) + "'";
            }
            listMismatch.push_back( std::move( message ) );
        }
        return listMismatch;
    }

    int32 AutomationPerBackend::run( const AutomationPerBackendOptions& options )
    {
        using Internal            = AutomationPerBackendInternal;
        const string scenarioStem = FileUtil::removeExtension( FileUtil::removeExtension( FileUtil::getFileNamePart( options._scenarioPath ) ) ); // x.scenario.xml → x
        // 실행마다 새 폴더다(`reserveUniquePath`) - 프로세스 id 는 Windows 가 다시 쓴다.
        const string defaultFolder = FileUtil::reserveUniquePath( formatToString( "%#/%#/per-backend-%#", makeAutomationOutputFolderPath(), scenarioStem, Process::getCurrentProcessID() ), {}, true );

        // 넘겨받은 인자가 창 위치를 정했으면(손으로 돌린 실행) 그것을 따른다.
        const bool bTileWindows = options._windowWidth > 0 && options._windowHeight > 0 &&
                                  std::none_of( options._listPassThroughArgument.begin(), options._listPassThroughArgument.end(), []( const string& argument )
        { return Internal::getArgumentKey( argument ) == getCommandLineArgumentSpelling( CommandLineArgument::WINDOW_X ); } );
        // 나눌 때 배정의 시작점 — 주지 않았으면 UTC 날짜(일)로 정해 날마다 한 칸씩 돌린다. 백엔드와 무관하다고 본 시나리오도 나흘이면 모든 백엔드를 거친다.
        AutomationPerBackendOptions assignOptions = options;
        if ( options._bSplitScenarios && assignOptions._splitOffset < 0 )
        {
            constexpr int64 kSecondsPerDay = 24 * 60 * 60;
            assignOptions._splitOffset     = static_cast<int32>( ( WallClock::nowUnixMilliseconds().toSeconds() / kSecondsPerDay ) % static_cast<int64>( MathUtil::max<size_t>( 1, options._listBackend.size() ) ) );
        }
        if ( options._bSplitScenarios )
            SW_LOG_INFO( "[Scenario] per-backend: split offset %# (rerun the same assignment with -scenario-backend-split-offset=%#)", assignOptions._splitOffset, assignOptions._splitOffset );
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
            pChild->_logPath = options._logFilePath.empty() ? defaultFolder + "/" + backend + ".log" : insertBackendSuffix( options._logFilePath, backend );
            // 보고는 늘 받는다 — 부모가 단계 보고의 `records[]` 를 백엔드 사이에 견준다(`compareChildRecords`).
            pChild->_reportPath = options._reportPath.empty() ? defaultFolder + "/" + backend + ".json" : insertBackendSuffix( options._reportPath, backend );
            // 앞 실행이 같은 경로에 남긴 단계 보고를 지운다 — 이번에 돌지 않은 단계의 낡은 기록을 견주지 않게. 없으면 지울 것이 없다(결과는 보지 않는다).
            const size_t stageCountOfChild = AutomationScenarioChain::splitList( scenarioList ).size();
            for ( size_t stage = 0; stage < stageCountOfChild; ++stage )
                (void)FileUtil::removeFile( AutomationScenarioChain::makeStageReportPath( pChild->_reportPath, stage, stageCountOfChild ) );

            vector<string> listArgument{ "-scenario=" + scenarioList, "-" + backend, "-log-file=" + pChild->_logPath, "-scenario-report=" + pChild->_reportPath };
            if ( bTileWindows )
            {
                const uint32 tileIndex = static_cast<uint32>( listChild.size() );
                listArgument.push_back( formatToString( "-WinX=%#", ( tileIndex % Internal::kTileColumnCount ) * ( options._windowWidth + Internal::kFrameMarginX ) ) );
                listArgument.push_back( formatToString( "-WinY=%#", ( tileIndex / Internal::kTileColumnCount ) * ( options._windowHeight + Internal::kFrameMarginY ) ) );
            }
            listArgument.insert( listArgument.end(), options._listPassThroughArgument.begin(), options._listPassThroughArgument.end() );

            const std::optional<AbsolutePath> executable = AbsolutePath::tryMake( options._executablePath );
            const ProcessCommand              command    = executable.has_value() ? ProcessCommand::at( *executable, std::move( listArgument ) ) : ProcessCommand{};
            pChild->_bLaunched                           = pChild->_process.launch( command );
            if ( pChild->_bLaunched == false )
                SW_LOG_ERROR( "[Scenario] per-backend: could not launch %#", command.toCommandLine().c_str() );
            else
            {
                SW_LOG_INFO( "[Scenario] per-backend: %# started (log %#)", backend.c_str(), pChild->_logPath.c_str() );
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
        const int64 stageCount = MathUtil::max<int64>( 1, static_cast<int64>( AutomationScenarioChain::splitList( options._scenarioPath ).size() ) );
        // shortcut: 단계 예산(`SwapBackend` 15 초씩)으로 늘어난 벽시계 상한은 보지 않는다 — 단계 하나뿐인 자식에서 교체가 둘 이상이면 감시보다 이 시한이 먼저 닿을 수 있다.
        //           교체가 많은 시나리오를 혼자 도는 항목으로 등록할 때 부모도 시나리오를 읽어 상한 합으로 바꾼다.
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
                    SW_LOG_ERROR( "[Scenario] per-backend: %# did not finish within %# s - killed", pChild->_backend.c_str(), killSeconds );
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
                SW_LOG_ERROR( "[Scenario] per-backend: %# %# (exit %#) - %#", pChild->_backend.c_str(), Internal::describeExitCode( pChild->_exitCode ), pChild->_exitCode,
                              pChild->_logPath.c_str() );
            else
                SW_LOG_INFO( "[Scenario] per-backend: %# %# (exit %#) - %#", pChild->_backend.c_str(), Internal::describeExitCode( pChild->_exitCode ), pChild->_exitCode,
                             pChild->_logPath.c_str() );
        }

        // 백엔드 사이의 기록 대조 — 게임 상태는 CPU 에서만 정해지므로 같은 시나리오의 `<Record>` 는 모든 백엔드에서 같아야 한다.
        const vector<string> listRecordMismatch = Internal::compareChildRecords( listScenarioList, listChild );
        for ( const string& mismatch : listRecordMismatch )
            SW_LOG_ERROR( "[Scenario] per-backend: record mismatch - %#", mismatch.c_str() );
        if ( listRecordMismatch.empty() == false && firstFailure == 0 )
            firstFailure = getAutomationExitCode( AutomationOutcome::Failed );

        const int32 exitCode = firstFailure != 0 ? firstFailure : bAnyPassed ? 0
                                                                             : getAutomationExitCode( AutomationOutcome::Skipped );
        if ( options._reportPath.empty() == false )
            Internal::writeReport( options, listChild, listRecordMismatch, exitCode );
        return exitCode;
    }
} // namespace sw

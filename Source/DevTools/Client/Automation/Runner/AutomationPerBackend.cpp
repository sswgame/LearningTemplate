#include "pch.h"

#include "DevTools/Client/Automation/Runner/AutomationPerBackend.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Concurrency/thread.h"
#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Process/Process.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"
#include "Core/Time/MonotonicClock.h"
#include "Core/Time/WallClock.h"

#include "DevTools/Client/Automation/Runner/AutomationExitCode.h"
#include "DevTools/Client/Automation/Runner/AutomationScenarioChain.h"

#include "Engine/Automation/Scenario/AutomationScenario.h"
#include "Engine/Automation/Scenario/AutomationTimeLimits.h"
#include "Engine/Graphics/RHI/RHIBackendInfo.h"
#include "Engine/Graphics/RHI/RHIInitResult.h"
#include "Engine/Serialization/JSON/JSONDocument.h"
#include "Engine/Serialization/JSON/JSONWriter.h"

#include "sw/config/CookContract.gen.h"

namespace sw
{
    namespace
    {
        struct AutomationPerBackendInternal
        {
            static constexpr uint32 kTileColumnCount = 2; ///< 자식 창 바둑판의 열 수 — 백엔드 넷이면 2×2
            /** @brief 쿠킹 표의 명령줄 이름(표 순서) — 부모가 받는 백엔드 이름은 이것뿐이다(`-vk` 와 같은 철자). */
            static constexpr const utf8* kArrBackendName[] = {
#define SW_MATRIX_BACKEND_NAME_ROW( Backend, ShaderFolder, ShaderTarget, Argument, CommandLineName ) CommandLineName,
                SW_RHI_BACKEND_TABLE( SW_MATRIX_BACKEND_NAME_ROW )
#undef SW_MATRIX_BACKEND_NAME_ROW
            };
            /** @brief 같은 표의 백엔드 열거자(`kArrBackendName` 과 같은 순서) — `all` 이 이 플랫폼에서 만들 수 있는 것만 고를 때 읽는다. */
            static constexpr RHIBackend kArrBackend[] = {
#define SW_MATRIX_BACKEND_ENUM_ROW( Backend, ShaderFolder, ShaderTarget, Argument, CommandLineName ) RHIBackend::Backend,
                SW_RHI_BACKEND_TABLE( SW_MATRIX_BACKEND_ENUM_ROW )
#undef SW_MATRIX_BACKEND_ENUM_ROW
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
                {       "ExpectImage",                   {}},
                {"ExpectImagesDiffer",                   {}},
                {   "CaptureEditorUI", "maxMagentaFraction"},
                {    "EditorSelfTest",                   {}},
                {            "Record",                   {}}
            };

            static bool judgesPerBackend( const AutomationStep& step )
            {
                return std::any_of( std::begin( kArrBackendJudgingStep ), std::end( kArrBackendJudgingStep ), [&step]( const BackendJudgingStep& judging )
                { return step._kind == judging._kind && ( judging._attribute.empty() || step.findAttribute( judging._attribute ) != nullptr ); } );
            }

            /** @brief 자식 하나의 상태입니다. 출력 파이프는 스레드 하나가 끝까지 비운다 — 비우지 않으면 파이프가 찬 자식이 멈춘다. */
            struct Child
            {
                string         _backend{};
                string         _logPath{};
                string         _reportPath{};
                ProcessCommand _command{}; ///< 띄울 명령(슬롯이 비면 띄운다)
                Process        _process{};
                thread         _flushThread{};
                Deadline       _killDeadline  = Deadline::afterMilliseconds( 0 ); ///< 띄운 때부터 센 프로세스 시한(띄울 때 다시 정한다)
                Deadline       _stallDeadline = Deadline::afterMilliseconds( 0 ); ///< 로그가 마지막으로 자란 때부터 센 정지 시한(로그가 자랄 때마다 다시 정한다)
                uint64         _observedLogBytes{ 0 };                            ///< 정지 감시가 마지막으로 본 자식 로그 크기
                int32          _exitCode{ -1 };
                bool           _bLaunched{ false }; ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
                bool           _bPending{ false };  ///< 아직 띄우지 않았다(맡은 시나리오가 있다) NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
            };

            /** @brief 자식 로그가 자랐으면 정지 시한을 다시 정합니다 — 일하는 자식(빈 셰이더 캐시의 컴파일)은 죽이지 않고, 아무것도 남기지 않는 자식만 멈춘 것으로 본다. */
            static void observeProgress( Child& inoutChild )
            {
                uint64 logBytes = 0;
                if ( FileUtil::tryGetFileSize( inoutChild._logPath, logBytes ) == false || logBytes == inoutChild._observedLogBytes )
                    return;
                inoutChild._observedLogBytes = logBytes;
                inoutChild._stallDeadline    = Deadline::afterSeconds( AutomationTimeLimits::kProcessStallSeconds );
            }

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

            static bool isSkip( int32 exitCode ) { return exitCode == getAutomationExitStatus( AutomationResult::Skipped ) || exitCode == kRHIUnusableHereExitCode; }

            /** @brief 기록 줄(`frame 300 Nile.Month = 3`)의 이름 · 값 경계(마지막 ` = `)입니다. 없으면 npos — 줄 전체가 이름이다. */
            static size_t      findRecordValueSeparator( string_view line ) { return line.rfind( " = " ); }
            static string_view getRecordLabel( string_view line ) { return line.substr( 0, findRecordValueSeparator( line ) ); }
            static string_view getRecordValue( string_view line )
            {
                const size_t separator = findRecordValueSeparator( line );
                return separator == string_view::npos ? line : line.substr( separator + 3 );
            }

            /** @brief 자식이 쓴 단계 보고 하나의 `exitCode`(돌려준다) · `records[]`(@p outListRecordLine)를 읽습니다. 파일이 없거나 형식이 다르면 그 까닭입니다. */
            [[nodiscard]] static expected<int32> readStageReport( const string& reportPath, vector<string>& outListRecordLine )
            {
                string text;
                if ( FileUtil::readTextFile( reportPath, text ) == false )
                    return unexpected( Error{ ErrorKind::NotFound, "could not read the stage report", reportPath } );
                JSONDocument document;
                if ( document.parse( text, reportPath ) == false )
                    return unexpected( Error{ ErrorKind::InvalidArgument, "the stage report is not JSON", document.getLastError() } );
                const JSONValue       root     = document.getRoot();
                const optional<int32> exitCode = root.readRequired<int32>( "exitCode" );
                const JSONValue       records  = root.get( "records" );
                if ( exitCode.has_value() == false || records.isArray() == false )
                    return unexpected( Error{ ErrorKind::InvalidArgument, "the stage report has no exitCode or records[]", reportPath } );
                outListRecordLine.clear();
                for ( size_t i = 0; i < records.size(); ++i )
                    outListRecordLine.push_back( records.at( i ).asString() );
                return *exitCode;
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
                        const expected<int32>    stageExitCode = readStageReport( stageReportPath, records._listRecordLine );
                        if ( stageExitCode.has_value() == false )
                        {
                            if ( child._exitCode == 0 )
                            {
                                StringBuilder<constant::buffer::kMax512> textBuilder;
                                textBuilder.appendFormat( "%#: %# report unreadable - %#: %#", listPath[stage], child._backend, stageExitCode.error().message(), stageExitCode.error().detail() );
                                listProblem.push_back( string( textBuilder.view() ) );
                            }
                            continue;
                        }
                        if ( *stageExitCode != 0 )
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
                    SW_LOG_ERROR_ID( "ScenarioPerBackend.PerBackendCouldNot", "[Scenario] per-backend: could not write the report %#", options._reportPath.c_str() );
            }
        };
    } // namespace

    expected<vector<string>> AutomationPerBackend::parseBackendList( string_view text )
    {
        using Internal = AutomationPerBackendInternal;
        vector<string>            listBackend;
        const vector<string_view> listName = StringUtil::split( text, ",", { ._bTrim = true, ._bDropEmpty = true } );
        // 표 순서로 모은다 — 적은 순서와 무관하게 보고 · 종료 코드의 "첫 실패" 가 늘 같다.
        // `all` 은 이 플랫폼의 검증 백엔드다(`RHIAvailability::getScenarioBackends` — 쿠킹 표 `scenario_backends`, 리눅스는 vk · gl). 이름을 적은 백엔드는 그대로 띄운다 — 못 돌면 자식이 건너뜀(77)으로 끝난다.
        const bool bAll = listName.size() == 1 && listName[0] == "all";
        for ( size_t index = 0; index < std::size( Internal::kArrBackendName ); ++index )
        {
            const utf8* const            pName        = Internal::kArrBackendName[index];
            const span<const RHIBackend> listScenario = RHIAvailability::getScenarioBackends();
            const bool                   bSelected    = bAll ? std::find( listScenario.begin(), listScenario.end(), Internal::kArrBackend[index] ) != listScenario.end()
                                                             : std::find( listName.begin(), listName.end(), string_view{ pName } ) != listName.end();
            if ( bSelected )
                listBackend.emplace_back( pName );
        }
        for ( const string_view name : listName )
        {
            const bool bKnown = name == "all" ? listName.size() == 1
                                              : std::find( std::begin( Internal::kArrBackendName ), std::end( Internal::kArrBackendName ), name ) != std::end( Internal::kArrBackendName );
            if ( bKnown == false || std::count( listName.begin(), listName.end(), name ) > 1 )
                return unexpected( Error{ ErrorKind::InvalidArgument, "-scenario-backends takes all | dx11,dx12,vk,gl (each once; all alone)", name } );
        }
        if ( listBackend.empty() )
            return unexpected( Error{ ErrorKind::InvalidArgument, "-scenario-backends: no backend given (all | dx11,dx12,vk,gl)" } );
        return listBackend;
    }

    vector<string> AutomationPerBackend::filterPassThroughArguments( const vector<string>& listOriginalArgument )
    {
        using Internal                                   = AutomationPerBackendInternal;
        static constexpr CommandLineArgument kArrOwned[] = { CommandLineArgument::SCENARIO, CommandLineArgument::SCENARIO_BACKENDS, CommandLineArgument::SCENARIO_BACKEND_SPLIT, CommandLineArgument::SCENARIO_BACKEND_SPLIT_OFFSET, CommandLineArgument::SCENARIO_BACKEND_SLOTS, CommandLineArgument::SCENARIO_REPORT,
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
            const bool         bLoaded     = scenario.loadFromPath( AssetID{ path } ).has_value();
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
            const auto lineAt = [lineIndex]( const AutomationBackendRecords& records ) -> optional<string_view>
            {
                return lineIndex < records._listRecordLine.size() ? optional<string_view>{ records._listRecordLine[lineIndex] } : nullopt;
            };
            const optional<string_view> firstLine = lineAt( listRecords.front() );
            if ( std::all_of( listRecords.begin(), listRecords.end(), [&]( const AutomationBackendRecords& records )
            { return lineAt( records ) == firstLine; } ) )
                continue;
            // 이름은 그 순번이 있는 첫 백엔드의 것이다 — 다른 백엔드의 값은 이름이 같을 때만 값으로 적는다.
            string_view label{};
            for ( const AutomationBackendRecords& records : listRecords )
            {
                const optional<string_view> line = lineAt( records );
                if ( line.has_value() )
                {
                    label = Internal::getRecordLabel( *line );
                    break;
                }
            }
            string message = "record #" + to_string( lineIndex + 1 ) + " '" + string( label ) + "':";
            for ( const AutomationBackendRecords& records : listRecords )
            {
                const optional<string_view> line = lineAt( records );
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
        StringBuilder<constant::buffer::kMax256> textBuilder3;
        textBuilder3.appendFormat( "%#/%#/per-backend-%#", makeAutomationOutputFolderPath(), scenarioStem, Process::getCurrentProcessID() );
        const string defaultFolder = FileUtil::reserveUniquePath( textBuilder3.view(), {}, true );

        // 넘겨받은 인자가 창 위치를 정했으면(손으로 돌린 실행) 그것을 따른다. 칸 간격(클라이언트 크기 + 틀 두께)은 자식 창이 스스로 정한다(`IWindow::createMainWindow`).
        const bool bTileWindows = std::none_of( options._listPassThroughArgument.begin(), options._listPassThroughArgument.end(), []( const string& argument )
        {
            const string_view key = Internal::getArgumentKey( argument );
            return key == getCommandLineArgumentSpelling( CommandLineArgument::WINDOW_X ) || key == getCommandLineArgumentSpelling( CommandLineArgument::WINDOW_TILE );
        } );
        // 나눌 때 배정의 시작점 — 주지 않았으면 UTC 날짜(일)로 정해 날마다 한 칸씩 돌린다. 백엔드와 무관하다고 본 시나리오도 나흘이면 모든 백엔드를 거친다.
        AutomationPerBackendOptions assignOptions = options;
        if ( options._bSplitScenarios && assignOptions._splitOffset < 0 )
        {
            constexpr int64 kSecondsPerDay = 24 * 60 * 60;
            assignOptions._splitOffset     = static_cast<int32>( ( WallClock::nowUnixMilliseconds().toSeconds() / kSecondsPerDay ) % static_cast<int64>( MathUtil::max<size_t>( 1, options._listBackend.size() ) ) );
        }
        if ( options._bSplitScenarios )
            SW_LOG_INFO_ID( "ScenarioPerBackend.PerBackendSplitOffset", "[Scenario] per-backend: split offset %# (rerun the same assignment with -scenario-backend-split-offset=%#)", assignOptions._splitOffset, assignOptions._splitOffset );
        const vector<string>                listScenarioList = assignScenarios( assignOptions );
        vector<unique_ptr<Internal::Child>> listChild;
        for ( const string& backend : options._listBackend )
        {
            unique_ptr<Internal::Child> pChild = make_unique<Internal::Child>();
            pChild->_backend                   = backend;
            const string& scenarioList         = listScenarioList[listChild.size()];
            if ( scenarioList.empty() )
            {
                pChild->_exitCode = getAutomationExitStatus( AutomationResult::Skipped ); // 나눈 목록이 백엔드 수보다 짧다 — 맡은 시나리오가 없다
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
                StringBuilder<constant::buffer::kMax256> textBuilder2;
                textBuilder2.appendFormat( "-%#=%#", getCommandLineArgumentSpelling( CommandLineArgument::WINDOW_TILE ), listChild.size() );
                listArgument.push_back( string( textBuilder2.view() ) );
                StringBuilder<constant::buffer::kMax256> textBuilder;
                textBuilder.appendFormat( "-%#=%#", getCommandLineArgumentSpelling( CommandLineArgument::WINDOW_TILE_COLUMNS ), Internal::kTileColumnCount );
                listArgument.push_back( string( textBuilder.view() ) );
            }
            listArgument.insert( listArgument.end(), options._listPassThroughArgument.begin(), options._listPassThroughArgument.end() );

            const optional<AbsolutePath> executable = AbsolutePath::tryMake( options._executablePath );
            pChild->_command                        = executable.has_value() ? ProcessCommand::at( *executable, std::move( listArgument ) ) : ProcessCommand{};
            pChild->_bPending                       = true;
            listChild.push_back( std::move( pChild ) );
        }

        // 프로세스 시한 — 자식 안의 감시(행 보고)가 먼저 닿고, 그래도 끝나지 않은 자식만 여기서 죽인다. 부모가 기다리기만 하면 멈춘 자식 하나에 CTest 시한이
        // 부모만 죽이고 자식은 고아로 남는다(Windows 는 부모가 죽어도 자식을 거두지 않는다).
        // 시한은 단계마다이고 자식을 띄운 때부터 센다 — 체인(`-scenario=a,b,c`)은 단계 수만큼 길고, 슬롯을 기다린 시간은 넣지 않는다.
        const int64 stageCount = MathUtil::max<int64>( 1, static_cast<int64>( AutomationScenarioChain::splitList( options._scenarioPath ).size() ) );
        // shortcut: 단계 예산(`SwapBackend` 15 초씩)으로 늘어난 벽시계 상한은 보지 않는다 — 단계 하나뿐인 자식에서 교체가 둘 이상이면 감시보다 이 시한이 먼저 닿을 수 있다.
        //           교체가 많은 시나리오를 혼자 도는 항목으로 등록할 때 부모도 시나리오를 읽어 상한 합으로 바꾼다.
        const int64  killSeconds = static_cast<int64>( AutomationTimeLimits::kProcessKillSeconds ) * stageCount;
        const size_t slotCount   = options._slotCount > 0 ? static_cast<size_t>( options._slotCount ) : listChild.size();
        const auto   launchChild = [&]( Internal::Child& child )
        {
            child._bPending         = false;
            child._killDeadline     = Deadline::afterSeconds( killSeconds );
            child._stallDeadline    = Deadline::afterSeconds( AutomationTimeLimits::kProcessStallSeconds );
            child._observedLogBytes = 0;
            child._bLaunched        = child._process.launch( child._command );
            if ( child._bLaunched == false )
            {
                SW_LOG_ERROR_ID( "ScenarioPerBackend.PerBackendCouldNot2", "[Scenario] per-backend: could not launch %#", child._command.toCommandLine().c_str() );
                return;
            }
            SW_LOG_INFO_ID( "ScenarioPerBackend.PerBackendStartedLog", "[Scenario] per-backend: %# started (log %#)", child._backend.c_str(), child._logPath.c_str() );
            Process* pProcess  = &child._process;
            child._flushThread = thread( [pProcess]()
            {
                string line;
                while ( pProcess->readOutputLine( line ) )
                {
                } // 자식의 로그는 제 파일에 있다 — 콘솔 줄은 버린다
            } );
        };
        const auto finishChild = [&]( Internal::Child& child )
        {
            const bool bKilled = child._process.isRunning() && child._process.terminate( getAutomationExitStatus( AutomationResult::TimedOut ) );
            if ( bKilled )
                SW_LOG_ERROR_ID( "ScenarioPerBackend.PerBackendDidNot", "[Scenario] per-backend: %# %# - killed", child._backend.c_str(),
                                 child._killDeadline.isExpired() ? "did not finish within the process limit" : "logged nothing for the stall limit (stopped)" );
            child._exitCode = child._process.waitForExit();
            if ( bKilled )
                child._exitCode = getAutomationExitStatus( AutomationResult::TimedOut ); // POSIX 는 종료 코드를 줄 수 없다(128 + SIGKILL) — 결과는 시간 초과다
            child._flushThread.join();
            child._bLaunched = false;
        };
        // 슬롯 안에서 띄우고, 끝나거나 시한이 지난 자식을 거두며 다음을 띄운다(쿠킹 표 순서).
        while ( true )
        {
            size_t runningCount = 0;
            for ( unique_ptr<Internal::Child>& pChild : listChild )
            {
                if ( pChild->_bLaunched )
                    Internal::observeProgress( *pChild );
                if ( pChild->_bLaunched && ( pChild->_process.isRunning() == false || pChild->_killDeadline.isExpired() || pChild->_stallDeadline.isExpired() ) )
                    finishChild( *pChild );
                runningCount += pChild->_bLaunched ? 1 : 0;
            }
            for ( unique_ptr<Internal::Child>& pChild : listChild )
            {
                if ( pChild->_bPending && runningCount < slotCount )
                {
                    launchChild( *pChild );
                    runningCount += pChild->_bLaunched ? 1 : 0;
                }
            }
            const bool bAnyPending = std::any_of( listChild.begin(), listChild.end(), []( const unique_ptr<Internal::Child>& pChild )
            { return pChild->_bPending; } );
            if ( runningCount == 0 && bAnyPending == false )
                break;
            MonotonicClock::sleepUntilNanoseconds( MonotonicClock::nowNanoseconds() + 50 * constant::time::kNanosecondsPerMillisecond );
        }

        int32 firstFailure = 0;
        bool  bAnyPassed   = false;
        for ( unique_ptr<Internal::Child>& pChild : listChild )
        {
            const bool bSkipped = Internal::isSkip( pChild->_exitCode );
            bAnyPassed          = bAnyPassed || pChild->_exitCode == 0;
            if ( pChild->_exitCode != 0 && bSkipped == false && firstFailure == 0 )
                firstFailure = pChild->_exitCode == -1 ? getAutomationExitStatus( AutomationResult::Failed ) : pChild->_exitCode;
            const LogLevel level = pChild->_exitCode == 0 || bSkipped ? LogLevel::Info : LogLevel::Error;
            if ( level == LogLevel::Error )
                SW_LOG_ERROR_ID( "ScenarioPerBackend.PerBackendExit", "[Scenario] per-backend: %# %# (exit %#) - %#", pChild->_backend.c_str(), Internal::describeExitCode( pChild->_exitCode ), pChild->_exitCode,
                                 pChild->_logPath.c_str() );
            else
                SW_LOG_INFO_ID( "ScenarioPerBackend.PerBackendExit", "[Scenario] per-backend: %# %# (exit %#) - %#", pChild->_backend.c_str(), Internal::describeExitCode( pChild->_exitCode ), pChild->_exitCode,
                                pChild->_logPath.c_str() );
        }

        // 백엔드 사이의 기록 대조 — 게임 상태는 CPU 에서만 정해지므로 같은 시나리오의 `<Record>` 는 모든 백엔드에서 같아야 한다.
        const vector<string> listRecordMismatch = Internal::compareChildRecords( listScenarioList, listChild );
        for ( const string& mismatch : listRecordMismatch )
            SW_LOG_ERROR_ID( "ScenarioPerBackend.PerBackendRecordMismatch", "[Scenario] per-backend: record mismatch - %#", mismatch.c_str() );
        if ( listRecordMismatch.empty() == false && firstFailure == 0 )
            firstFailure = getAutomationExitStatus( AutomationResult::Failed );

        const int32 exitCode = firstFailure != 0 ? firstFailure : bAnyPassed ? 0
                                                                             : getAutomationExitStatus( AutomationResult::Skipped );
        if ( options._reportPath.empty() == false )
            Internal::writeReport( options, listChild, listRecordMismatch, exitCode );
        return exitCode;
    }
} // namespace sw

#include "pch.h"

#include "Editor/Common/Workspace/NetPlay/EditorNetPlaySession.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Concurrency/thread.h"
#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/Log/Logger.h"
#include "Core/Math/SecureRandom.h"
#include "Core/Network/Transport/IStreamTransport.h"
#include "Core/Network/Transport/UDPNetTransport.h"
#include "Core/Process/Process.h"
#include "Core/String/HexUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/Time/WallClock.h"

#include "Editor/Common/Task/EditorBackgroundTask.h"
#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/FileSystem/AssetPaths.h"
#include "Engine/Profiling/ProfilerTraceExport.h"

#include <thread>

namespace sw::editor
{
    /** @brief 자식 프로세스 하나입니다. 읽기 스레드가 로그 줄을 쌓고, 게임 스레드가 `update` 에서 꺼냅니다. */
    struct EditorNetPlaySession::ChildProcess
    {
        Process                 _process{};
        sw::thread              _reader{};
        mutex                   _mutex{};
        vector<string>          _listPendingLine{}; ///< 읽기 스레드가 넣는다(_mutex)
        string                  _label{};
        string                  _lastLine{};
        string                  _tracePath{};
        NetEmulationReport      _emulationReport{};         ///< 마지막 `net.emulation stats` 답(게임 스레드)
        uint32                  _emulationReportCount{ 0 }; ///< 받은 보고 수(게임 스레드)
        NetStatsReport          _statsReport{};             ///< 마지막 `net.stats` 답(게임 스레드)
        uint32                  _statsReportCount{ 0 };     ///< 받은 `net.stats` 보고 수(게임 스레드)
        vector<StateHashSample> _listStateHash{};           ///< 마지막 `state.hashline` 답(게임 스레드, Sync 탭)
        uint32                  _stateHashReportCount{ 0 };
        vector<SceneObjectHash> _listObjectHash{};    ///< 마지막 `state.objectline` 답의 오브젝트별 해시(게임 스레드, Sync 탭)
        uint64                  _objectHashTick{ 0 }; ///< 그 답이 단 틱
        uint32                  _objectHashReportCount{ 0 };
        atomic<int32>           _exitCode{ 0 };
        atomic<uint32>          _exited{ 0 };    ///< 읽기 스레드가 파이프 끝을 보고 종료 코드를 거뒀다
        int32                   _processID{ 0 }; ///< 띄울 때의 pid(회수한 뒤에도 패널에 보인다)
        bool                    _bExitReported{ false };
        bool                    _bConnected{ false }; ///< 클라이언트가 서버에 연결됐다는 줄을 남겼다
    };
} // namespace sw::editor

namespace sw::editor
{
    SW_LOG_CALLER( "NetPlay" );

    namespace
    {
        struct EditorNetPlaySessionInternal
        {
            /** @brief 클라이언트가 서버에 붙었을 때 남기는 줄(`GameNetSession`)입니다. */
            static constexpr const utf8* kConnectedLine = "Net session connected to ";
            /** @brief 로그 출력이 사건 줄 다음에 붙이는 소스 위치 줄의 머리입니다. */
            static constexpr const utf8* kSourceLocationPrefix = " -> ";
            /** @brief 트레이스 파일 확장자입니다. `ProfilerTraceExport::kFileExtension` 과 같은 값을 따로 둡니다. 트레이스가 빠진 빌드에도 이름은 있어야 하기 때문입니다. */
#if SW_PROFILER_TRACE_ENABLED
            static constexpr const utf8* kTraceExtension = ProfilerTraceExport::kFileExtension;
#else
            static constexpr const utf8* kTraceExtension = ".trace.json";
#endif

            /** @brief 창 배치에서 이웃 창과 띄우는 가로 폭(픽셀)입니다. 위치는 틀의 왼쪽 위이고 크기는 클라이언트 영역이라 테두리 두 쪽만큼 더 띄운다. */
            static constexpr int32 kWindowFrameWidth = 16;
            /** @brief 위와 같은 값의 세로 폭입니다. 제목 줄과 아래 테두리만큼 띄웁니다. */
            static constexpr int32 kWindowFrameHeight = 40;
            /** @brief 창 배치의 가장 작은 클라이언트 영역 한 변(픽셀)입니다(`EditorNetPlaySettings` 의 `Min` 과 같다). */
            static constexpr int32 kMinWindowSide = 64;

            /** @brief 창 배치를 켠 구성의 크기와 열 수가 쓸 수 있는 값이면 true 입니다. 아니면 이유를 오류로 남깁니다. */
            static bool isWindowLayoutValid( const EditorNetPlaySettings& settings )
            {
                const bool bValid = settings._bArrangeWindows == false ||
                                    ( settings._windowColumnCount >= 1 && settings._windowWidth >= kMinWindowSide && settings._windowHeight >= kMinWindowSide );
                if ( bValid == false )
                    SW_LOG_ERROR( "Window layout %#x%# in %# column(s) is not usable - each side needs at least %# pixels and one column", settings._windowWidth,
                                  settings._windowHeight, settings._windowColumnCount, kMinWindowSide );
                return bValid;
            }

            /** @brief 디버그 채널 토큰을 운영체제 난수로 만듭니다 — 같은 PC 의 다른 프로세스가 짐작해 붙지 못하게 세션마다 새로. */
            static string makeDebugChannelToken()
            {
                string token{ "netplay-" };
                HexUtil::appendUint64( token, SecureRandom::nextUint64() );
                HexUtil::appendUint64( token, SecureRandom::nextUint64() );
                return token;
            }

            /** @brief 빈 TCP 포트를 고르는 동안만 쓰는 처리기입니다 — 받기만 열고 연결은 받지 않는다. */
            struct PortProbeHandler final : IStreamHandler
            {
                void onStreamOpened( StreamConnectionHandle /*handle*/, const NetAddress& /*remote*/, bool /*bAccepted*/ ) override {}
                void onStreamReceived( StreamConnectionHandle /*handle*/, const uint8* /*pData*/, int32 /*size*/ ) override {}
                void onStreamClosed( StreamConnectionHandle /*handle*/, StreamCloseReason /*reason*/ ) override {}
            };
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 트레이스를 남겼을 프로세스 하나(이름 · 파일)입니다. */
    struct EditorNetPlayTraceSource
    {
        string _label;
        string _tracePath;
    };

    /** @brief 멈춤 잡의 입력입니다. UI 스레드가 만들어 넘긴다 - 워커는 이 사본만 본다. */
    struct EditorNetPlayStopInput
    {
        vector<sw::thread*>              _listReader;      ///< 합류할 읽기 스레드(주인은 세션의 `_listStoppingProcess` - 잡이 끝날 때까지 산다)
        vector<EditorNetPlayTraceSource> _listTraceSource; ///< 트레이스를 켠 세션이면 프로세스마다 하나
        string                           _traceDirectory;  ///< 비면 합치지 않는다
    };

    /** @brief 멈춤 잡의 결과입니다. */
    struct EditorNetPlayStopResult
    {
        string _mergedTracePath; ///< 합친 트레이스(없으면 빈 글)
    };

    /**
     * @class EditorNetPlayStopJob
     * @brief 끝난 자식의 읽기 스레드를 합류하고 트레이스를 합치는 에디터 백그라운드 잡입니다(`EditorNetPlaySession::advanceStop` 이 낸다).
     * @details 자식이 모두 끝난 뒤에만 낸다 - 합류는 파이프가 닫힌 읽기 스레드를 기다릴 뿐이라 워커를 오래 잡지 않는다. 합치기는 트레이스 파일 읽기 · JSON 합치기 · 쓰기(IO + CPU)다.
     */
    class EditorNetPlayStopJob final : public EditorBackgroundTask<EditorNetPlayStopInput, EditorNetPlayStopResult>
    {
    public:
        /** @brief 잡을 냅니다(UI 스레드). */
        void request( EditorNetPlayStopInput input )
        {
            const uint32 generation = beginRequest( std::move( input ) );
            submitEditorBackgroundJob( "EditorNetPlayStop", SW_DELEGATE_FUNCTION( TaskArgsDelegate, EditorNetPlayStopJob::runJob ), MakeTaskArgs( _pState, generation ) );
        }

        /** @brief 합류와 합치기입니다. 워커 잡과 이 스레드에서 끝까지 기다리는 길(`stopAndWait`)이 같이 쓴다. */
        static EditorNetPlayStopResult runWork( const EditorNetPlayStopInput& input )
        {
            for ( sw::thread* const pReader : input._listReader )
            {
                if ( pReader->joinable() )
                    pReader->join();
            }
            EditorNetPlayStopResult result;
            result._mergedTracePath = mergeTraces( input );
            return result;
        }

    private:
        static void runJob( const TaskArgs& args )
        {
            const shared_ptr<State> pState     = args.get<shared_ptr<State>>( 0 );
            const uint32            generation = args.get<uint32>( 1 );
            EditorNetPlayStopInput  input;
            if ( readInput( pState, generation, input ) == false )
                return;
            // 종료 신호가 올라도 합류는 끝까지 한다 - 세션이 읽기 스레드를 남긴 채 사라지면 안 된다(합치기만 건너뛴다).
            if ( isStopRaised( readStopSignal( pState ) ) )
                input._traceDirectory.clear();
            publish( pState, generation, runWork( input ) );
        }

        /** @brief 트레이스를 남긴 프로세스의 파일을 모아 합칩니다. 둘 미만이면 빈 문자열입니다. */
        static string mergeTraces( const EditorNetPlayStopInput& input )
        {
            string mergedPath;
#if SW_PROFILER_TRACE_ENABLED
            if ( input._traceDirectory.empty() )
                return mergedPath;
            vector<ProfilerTraceMergeInput> listInput;
            for ( const EditorNetPlayTraceSource& source : input._listTraceSource )
            {
                ProfilerTraceMergeInput mergeInput;
                mergeInput._label     = source._label;
                const bool bTraceRead = source._tracePath.empty() == false && FileUtil::readOptionalTextFile( source._tracePath, mergeInput._json ) == OptionalFileRead::Read; // 자식이 죽어 파일이 없으면 아래 경고 하나로 알린다
                if ( bTraceRead )
                    listInput.push_back( std::move( mergeInput ) );
                else
                    SW_LOG_WARNING( "No trace from %# (%#)", source._label, source._tracePath );
            }
            if ( listInput.size() < 2 )
                return mergedPath;
            string json;
            string report;
            if ( ProfilerTraceExport::mergeTraceJSON( listInput, json, report ) == false )
            {
                SW_LOG_WARNING( "Traces could not be merged: %#", report );
                return mergedPath;
            }
            mergedPath = input._traceDirectory + "/" + EditorNetPlaySession::kMergedTraceFileName;
            if ( FileUtil::writeTextFile( mergedPath, json ) == false )
            {
                SW_LOG_WARNING( "Merged trace could not be written to %#", mergedPath );
                mergedPath.clear();
                return mergedPath;
            }
            SW_LOG_INFO( "Traces merged: %# -> %#", report, mergedPath );
            return mergedPath;
#else
            (void)input;
            return mergedPath;
#endif
        }
    };
} // namespace sw::editor

namespace sw::editor
{
    const utf8* toString( EditorNetMode mode )
    {
        switch ( mode )
        {
            case EditorNetMode::Standalone:
                return "Standalone";
            case EditorNetMode::DedicatedServer:
                return "Dedicated Server";
            case EditorNetMode::ListenHost:
                return "Listen Host";
        }
        return "Unknown";
    }

    EditorNetPlaySession::EditorNetPlaySession()
        : _listProcess{}
        , _listStoppingProcess{}
        , _pStopJob{ make_unique<EditorNetPlayStopJob>() }
        , _listLogLine{}
        , _listDrainScratch{}
        , _traceDirectory{}
        , _mergedTracePath{}
        , _debugChannelToken{}
        , _settings{}
        , _pendingSettings{}
        , _presetStore{}
        , _stopDeadline{ Deadline::afterMilliseconds( 0 ) }
        , _port{ 0 }
        , _debugChannelPort{ 0 }
        , _stopPhase{ StopPhase::Idle }
        , _bReplayServer{ false }
        , _bMergedTraceUnopened{ false }
    {
    }

    SubsystemInitResult EditorNetPlaySession::initializeSubsystem()
    {
        // 사용자 파일이라 깨졌어도 기동은 계속한다. 깨진 파일을 치우고 오류를 남기는 일은 `load` 가 이미 했다(`EditorUserFile`).
        // 고른 프리셋은 읽은 파일에만 있다. 파일이 없거나 치웠으면 빈 목록이므로, 읽었을 때만 다음 세션의 구성으로 적용한다.
        const EditorUserFileResult   loadResult = _presetStore.load( EditorNetPlayPresetStore::getDefaultFilePath() );
        const EditorNetPlaySettings* pSelected  = loadResult == EditorUserFileResult::Loaded ? _presetStore.findSelectedPreset() : nullptr;
        if ( pSelected != nullptr )
        {
            _pendingSettings = *pSelected;
            SW_LOG_INFO( "Multiplayer preset '%#' restored - %#, %# player(s)", _presetStore.getSelectedPresetName(), toString( _pendingSettings._mode ), _pendingSettings._clientCount );
        }
        return SubsystemInitResult::Succeeded;
    }

    void EditorNetPlaySession::resetToFreshEditor()
    {
        stopAndWait(); // 다음 단계는 새 프로세스와 같은 자리다 - 남은 멈춤까지 끝낸다. 합친 트레이스는 아래에서 버린다
        _pendingSettings      = EditorNetPlaySettings{};
        _bMergedTraceUnopened = false;
        _mergedTracePath.clear();
    }

    string EditorNetPlaySession::takeUnopenedMergedTrace()
    {
        if ( _bMergedTraceUnopened == false )
            return {};
        _bMergedTraceUnopened = false;
        return _mergedTracePath;
    }

    vector<string> EditorNetPlaySession::makeWindowArguments( const EditorNetPlaySettings& settings, int32 windowIndex )
    {
        if ( settings._bArrangeWindows == false )
            return {};
        SW_ASSERT( settings._windowColumnCount > 0 ); // `start` 가 막는다(`isWindowLayoutValid`)
        const int32 column = windowIndex % settings._windowColumnCount;
        const int32 row    = windowIndex / settings._windowColumnCount;
        const int32 x      = column * ( settings._windowWidth + EditorNetPlaySessionInternal::kWindowFrameWidth );
        const int32 y      = row * ( settings._windowHeight + EditorNetPlaySessionInternal::kWindowFrameHeight );
        return { formatToString( "-W=%#", settings._windowWidth ), formatToString( "-H=%#", settings._windowHeight ), formatToString( "-WinX=%#", x ), formatToString( "-WinY=%#", y ) };
    }

    EditorNetPlaySession::~EditorNetPlaySession()
    {
        stopAndWait(); // 읽기 스레드를 남긴 채 사라지지 않는다. 합친 경로는 쓸 곳이 없다 - 로그에 남는다
    }

    string EditorNetPlaySession::makeDefaultExecutablePath( const utf8* pBaseName )
    {
#if defined( SW_PLATFORM_WINDOWS )
        const string fileName = string( pBaseName ) + ".exe";
#else
        const string fileName = pBaseName;
#endif
        return FileUtil::joinPath( ProcessRoots::get()._binary.str(), fileName );
    }

    void EditorNetPlaySession::readChildOutput( ChildProcess* pChild )
    {
        string line;
        while ( pChild->_process.readOutputLine( line ) )
        {
            ScopedLock<mutex> lock{ pChild->_mutex };
            pChild->_listPendingLine.push_back( line );
        }
        pChild->_exitCode.store( pChild->_process.waitForExit() );
        pChild->_exited.store( 1 );
    }

    uint16 EditorNetPlaySession::findFreeUDPPort()
    {
        UDPNetTransport probe;
        if ( probe.open( 0, true ) == false )
            return 0;
        const uint16 port = probe.getLocalAddress()._port;
        probe.close();
        return port;
    }

    uint16 EditorNetPlaySession::findFreeStreamPort()
    {
        unique_ptr<IStreamTransport>                   probe = StreamTransportFactory::createPlatformTransport();
        EditorNetPlaySessionInternal::PortProbeHandler handler;
        if ( probe == nullptr || probe->initialize( &handler, StreamTransportConfig{} ) == false )
            return 0;
        uint16 port = 0;
        if ( probe->listen( NetAddress::makeLoopback( 0 ) ) )
            port = probe->getListenPort();
        probe->shutdown();
        return port;
    }

    string EditorNetPlaySession::makeTracePath( const string& fileStem ) const
    {
        return _traceDirectory.empty() ? string{} : _traceDirectory + "/" + fileStem + EditorNetPlaySessionInternal::kTraceExtension;
    }

    bool EditorNetPlaySession::launchChild( const string& label, const string& executablePath, vector<string> listArgument, const string& tracePath )
    {
        unique_ptr<ChildProcess> pChild = make_unique<ChildProcess>();
        pChild->_label                  = label;
        pChild->_tracePath              = tracePath;
        if ( tracePath.empty() == false )
            listArgument.push_back( "-gv_traceSaveOnExit=" + tracePath );
        const std::optional<AbsolutePath> executable = AbsolutePath::tryMake( executablePath );
        const ProcessCommand              command    = executable.has_value() ? ProcessCommand::at( *executable, std::move( listArgument ) ) : ProcessCommand{};
        ProcessOptions                    options;
        options._bPipeStandardInput = true; // `quit` 과 `pause` 를 보내는 채널
        if ( FileUtil::exists( executablePath ) == false || pChild->_process.launch( command, options ) == false )
        {
            SW_LOG_ERROR( "Could not launch %#: %#", label, command.toCommandLine() );
            return false;
        }
        pChild->_processID = pChild->_process.getProcessID();
        SW_LOG_INFO( "Launched %# (pid %#): %#", label, pChild->_processID, command.toCommandLine() );
        pChild->_reader = sw::thread( &EditorNetPlaySession::readChildOutput, pChild.get() );
        _listProcess.push_back( std::move( pChild ) );
        return true;
    }

    bool EditorNetPlaySession::startReplayServer( const string& intentFilePath )
    {
        if ( intentFilePath.empty() )
        {
            SW_LOG_WARNING( "A replay server needs an intent recording (.swintent) path" );
            return false;
        }
        EditorNetPlaySettings settings = _pendingSettings;
        settings._mode                 = EditorNetMode::DedicatedServer;
        settings._clientCount          = 0; // 클라이언트 없이 서버 하나만 — 의도 재생이 폰을 움직이고 에디터는 그 서버만 본다
        _bReplayServer                 = true;
        const bool bStarted            = start( settings );
        _bReplayServer                 = false;
        if ( bStarted == false )
            return false;
        // 서버는 멈춘 채 떴다(`-gv_serverStartPaused`). 틱 0 에서 재생을 건다 — 다음 `step N` 이 목표 틱까지 정확히 진행한다.
        if ( sendServerCommand( "intent.play " + intentFilePath ) == false )
        {
            SW_LOG_ERROR( "Could not send intent.play to the replay server" );
            requestStop();
            return false;
        }
        return true;
    }

    bool EditorNetPlaySession::start( const EditorNetPlaySettings& settings )
    {
        if ( isRunning() )
        {
            SW_LOG_WARNING( "A multiplayer session is already running - stop it first" );
            return false;
        }
        // 앞 세션의 프로세스를 아직 거두는 중이다 - 같은 트레이스 폴더 · 로그 줄 · 포트 조사를 겹치지 않는다(끝은 `Editor.NetPlayStopping` == 0).
        if ( isStopping() )
        {
            SW_LOG_WARNING( "The previous multiplayer session is still stopping - start again when it has finished" );
            return false;
        }
        if ( settings._mode == EditorNetMode::Standalone || ( settings._clientCount < 1 && _bReplayServer == false ) )
        {
            SW_LOG_WARNING( "Net mode %# with %# player(s) needs no processes - play in the editor instead", toString( settings._mode ), settings._clientCount );
            return false;
        }
        if ( EditorNetPlaySessionInternal::isWindowLayoutValid( settings ) == false )
            return false;
        _settings = settings;
        _listLogLine.clear();
        _mergedTracePath.clear();
        _bMergedTraceUnopened = false;
        _traceDirectory.clear();
        _port = settings._port != 0 ? settings._port : findFreeUDPPort();
        if ( _port == 0 )
        {
            SW_LOG_ERROR( "No free UDP port for the multiplayer session" );
            return false;
        }
#if SW_PROFILER_TRACE_ENABLED
        if ( settings._bRecordTraces )
        {
            _traceDirectory = settings._traceDirectory.empty()
                                ? FileUtil::joinPath( FileUtil::joinPath( ProcessRoots::get()._saved.str(), "Traces" ),
                                                      formatToString( "netplay_%#", WallClock::nowUnixMilliseconds().count() ) )
                                : settings._traceDirectory;
            if ( FileUtil::ensureDirectoryExists( _traceDirectory ) == false )
            {
                SW_LOG_ERROR( "Trace folder %# could not be created - the multiplayer session was not started", _traceDirectory );
                _traceDirectory.clear();
                return false;
            }
        }
#endif
        const string serverPath = settings._serverExecutablePath.empty() ? makeDefaultExecutablePath( "Server" ) : settings._serverExecutablePath;
        const string clientPath = settings._clientExecutablePath.empty() ? makeDefaultExecutablePath( "App" ) : settings._clientExecutablePath;
        const string port       = to_string( _port );
        // 사용자가 적은 추가 인자(백엔드 스위치)는 공백으로 나눈다 — 셸이 아니므로 그 글이 명령을 늘리지 못한다.
        const vector<string> listUserClientArgument = StringUtil::splitQuotedTokens( settings._clientArguments );
        // 클라이언트도 서버 틱 번호로 해시를 남긴다(S7). 포커스는 한 창만 가지므로 나머지 클라이언트가 배경 상한에 걸리지 않게 전속을 명시로 고른다
        // (`FramePacingState::FullSpeed`). 리슨 호스트는 넷 역할로 이미 전속(`Server`)이다.
        vector<string> listClientArgument{ "-gv_netConnectAddress=127.0.0.1:" + port, "-gv_standardInputCommands=1",
                                           formatToString( "-gv_stateHashHistory=%#", kServerStateHashHistoryTicks ), "-gv_framePacing=FullSpeed" };
        listClientArgument.insert( listClientArgument.end(), listUserClientArgument.begin(), listUserClientArgument.end() );

        // 서버를 먼저 띄운다. 클라이언트는 서버가 늦게 떠도 연결 시한 안에서 다시 시도하지만, 서버가 먼저 떠 있으면 첫 시도에 연결된다.
        // `App` 창은 띄우는 순서대로 나란히 놓는다. 리슨 호스트가 첫 위치이고, 전용 서버는 창이 없다.
        const bool bDedicated = settings._mode == EditorNetMode::DedicatedServer;
        // 전용 서버는 디버그 채널을 늘 연다 — Inspector 탭이 붙는 곳이다. 고르지 못하면 채널 없는 서버로 띄우지 않는다.
        _debugChannelPort = bDedicated ? findFreeStreamPort() : 0;
        _debugChannelToken.clear();
        if ( bDedicated && _debugChannelPort == 0 )
        {
            SW_LOG_ERROR( "No free TCP port for the dedicated server's debug channel" );
            return false;
        }
        int32 windowIndex = 0;
        bool  bLaunched   = false;
        if ( bDedicated )
        {
            _debugChannelToken = EditorNetPlaySessionInternal::makeDebugChannelToken();
            vector<string> listServerArgument{ "-gv_netListenPort=" + port, formatToString( "-gv_debugChannelPort=%#", _debugChannelPort ),
                                               "-gv_debugChannelToken=" + _debugChannelToken, formatToString( "-gv_stateHashHistory=%#", kServerStateHashHistoryTicks ) };
            if ( _bReplayServer )
                listServerArgument.push_back( "-gv_serverStartPaused=1" );
            bLaunched = launchChild( "Server", serverPath, std::move( listServerArgument ), makeTracePath( "server" ) );
        }
        else
        {
            vector<string> listHostArgument{ "-gv_netListenPort=" + port, "-gv_standardInputCommands=1" };
            listHostArgument.insert( listHostArgument.end(), listUserClientArgument.begin(), listUserClientArgument.end() );
            const vector<string> listWindowArgument = makeWindowArguments( settings, windowIndex++ );
            listHostArgument.insert( listHostArgument.end(), listWindowArgument.begin(), listWindowArgument.end() );
            bLaunched = launchChild( "Host", clientPath, std::move( listHostArgument ), makeTracePath( "host" ) );
        }
        const int32 clientCount = bDedicated ? settings._clientCount : settings._clientCount - 1;
        for ( int32 clientIndex = 1; bLaunched && clientIndex <= clientCount; ++clientIndex )
        {
            const string         number             = to_string( clientIndex );
            vector<string>       listArgument       = listClientArgument;
            const vector<string> listWindowArgument = makeWindowArguments( settings, windowIndex++ );
            listArgument.insert( listArgument.end(), listWindowArgument.begin(), listWindowArgument.end() );
            bLaunched = launchChild( "Client" + number, clientPath, std::move( listArgument ), makeTracePath( "client" + number ) );
        }
        if ( bLaunched == false )
        {
            requestStop(); // 띄운 프로세스는 끈다(거두기는 프레임마다 `update` 가 잇는다). 합칠 트레이스는 없다
            return false;
        }
        SW_LOG_INFO( "Multiplayer session started - %#, %# player(s), UDP port %#, traces %#", toString( settings._mode ), settings._clientCount, _port,
                     _traceDirectory.empty() ? string( "off" ) : _traceDirectory );
        return true;
    }

    void EditorNetPlaySession::update()
    {
        for ( size_t processIndex = 0; processIndex < _listProcess.size(); ++processIndex )
        {
            ChildProcess& child = *_listProcess[processIndex];
            drainChildLines( child, static_cast<int32>( processIndex ) );
            if ( child._exited.load() != 0 && child._bExitReported == false )
            {
                child._bExitReported = true;
                SW_LOG_WARNING( "%# exited with code %# during the session - last line: %#", child._label, child._exitCode.load(), child._lastLine );
            }
        }
        // 거두는 중인 프로세스의 줄도 옮긴다(끝날 때까지 나오는 줄 - 서버의 끊김 줄 등). 번호는 멈추기 전과 같다(목록을 통째로 옮겼다).
        for ( size_t processIndex = 0; processIndex < _listStoppingProcess.size(); ++processIndex )
        {
            drainChildLines( *_listStoppingProcess[processIndex], static_cast<int32>( processIndex ) );
        }
        advanceStop( false );
        if ( _listLogLine.size() > kMaxLogLine )
            _listLogLine.erase( _listLogLine.begin(), _listLogLine.begin() + static_cast<ptrdiff_t>( _listLogLine.size() - kMaxLogLine ) );
    }

    void EditorNetPlaySession::drainChildLines( ChildProcess& child, int32 processIndex )
    {
        vector<string> listLine;
        {
            ScopedLock<mutex> lock{ child._mutex };
            listLine.swap( child._listPendingLine );
        }
        for ( string& line : listLine )
        {
            // 자식 로그의 소스 위치 줄(` -> 파일:줄`)은 옮기지 않는다. 옮기면 한 사건이 두 줄이 되어 Output Log 를 채운다.
            if ( line.rfind( EditorNetPlaySessionInternal::kSourceLocationPrefix, 0 ) == 0 )
                continue;
            // Profiler 탭의 주기 질의 응답은 보관만 한다. 0.5 초마다 프로세스 수만큼 오므로, 출력하면 Output Log 와 마지막 줄을 채운다.
            if ( NetStatsReport::tryParseLine( line, child._statsReport ) )
            {
                ++child._statsReportCount;
                continue;
            }
            // Sync 탭이 물은 이력(한 줄)도 보관만 한다 — 수천 틱이 한 줄이라 Output Log 에 옮기지 않는다. 형식이 틀린 줄은 버리지 않고 로그로 넘긴다.
            if ( StateHashHistory::tryParseReportLine( line, child._listStateHash ) )
            {
                ++child._stateHashReportCount;
                continue;
            }
            // Sync 탭이 물은 오브젝트별 해시(한 줄)도 보관만 한다 — 오브젝트 수만큼 길다.
            if ( SceneStateHash::tryParseObjectReportLine( line, child._objectHashTick, child._listObjectHash ) )
            {
                ++child._objectHashReportCount;
                continue;
            }
            // 자식의 오류 줄은 경고로 남긴다. 에디터 로그의 오류 수는 에디터 자신의 오류만 센다. 줄에는 자식의 로그 수준 레이블이 그대로 있다.
            if ( line.find( "[Error]" ) != string::npos )
                SW_LOG_WARNING( "[%#] %#", child._label, line );
            else
                SW_LOG_INFO( "[%#] %#", child._label, line );
            if ( processIndex > 0 && line.find( EditorNetPlaySessionInternal::kConnectedLine ) != string::npos )
                child._bConnected = true; // 0 번은 서버(전용 서버나 리슨 호스트)다
            if ( NetEmulationReport::tryParseLine( line, child._emulationReport ) )
                ++child._emulationReportCount;
            child._lastLine = line;
            _listLogLine.push_back( EditorNetPlayLogLine{ std::move( line ), processIndex } );
        }
    }

    bool EditorNetPlaySession::sendServerCommand( string_view command )
    {
        return sendProcessCommand( kServerProcessIndex, command );
    }

    bool EditorNetPlaySession::sendProcessCommand( int32 processIndex, string_view command )
    {
        if ( processIndex < 0 || processIndex >= getProcessCount() )
            return false;
        ChildProcess& child = *_listProcess[static_cast<size_t>( processIndex )];
        if ( child._exited.load() != 0 )
            return false;
        return child._process.writeInput( string( command ) + "\n" );
    }

    int32 EditorNetPlaySession::getConnectedClientCount() const
    {
        int32 count = 0;
        for ( const unique_ptr<ChildProcess>& pChild : _listProcess )
        {
            count += pChild->_bConnected ? 1 : 0;
        }
        return count;
    }

    EditorNetPlayProcessStatus EditorNetPlaySession::getProcessStatus( int32 processIndex ) const
    {
        EditorNetPlayProcessStatus status;
        if ( processIndex < 0 || processIndex >= getProcessCount() )
            return status;
        const ChildProcess& child     = *_listProcess[static_cast<size_t>( processIndex )];
        status._label                 = child._label;
        status._lastLine              = child._lastLine;
        status._tracePath             = child._tracePath;
        status._emulationReport       = child._emulationReport;
        status._emulationReportCount  = child._emulationReportCount;
        status._statsReport           = child._statsReport;
        status._statsReportCount      = child._statsReportCount;
        status._listStateHash         = child._listStateHash;
        status._stateHashReportCount  = child._stateHashReportCount;
        status._listObjectHash        = child._listObjectHash;
        status._objectHashTick        = child._objectHashTick;
        status._objectHashReportCount = child._objectHashReportCount;
        status._processID             = child._processID;
        status._bRunning              = child._exited.load() == 0;
        status._bConnected            = child._bConnected;
        status._exitCode              = status._bRunning ? 0 : child._exitCode.load();
        return status;
    }

    void EditorNetPlaySession::requestStop()
    {
        if ( _listProcess.empty() )
            return;
        SW_ASSERT( _stopPhase == StopPhase::Idle ); // 거두는 중이면 `_listProcess` 는 비어 있다(`start` 가 거절한다)
        // 정상 종료한다. 클라이언트부터 끄면 서버 로그에 끊김이 남아 접속과 끊김을 한 줄로 확인할 수 있다. 이미 끝난 프로세스는 건너뛴다.
        for ( size_t processIndex = _listProcess.size(); processIndex-- > 0; )
        {
            ChildProcess& child  = *_listProcess[processIndex];
            child._bExitReported = true; // 끄는 중의 끝남은 알리지 않는다
            if ( child._exited.load() == 0 && child._process.writeInput( "quit\n" ) == false )
                SW_LOG_WARNING( "Could not send quit to %# - it is terminated after the timeout", child._label );
        }
        // 프로세스는 거둘 때까지 따로 둔다 - 이때부터 세션은 돌지 않는다(`isRunning` 거짓 · 명령을 보내지 않는다 · 디버그 채널을 내놓는다).
        _listStoppingProcess = std::move( _listProcess );
        _listProcess.clear();
        _debugChannelPort = 0;
        _debugChannelToken.clear();
        _stopDeadline = Deadline::afterMilliseconds( static_cast<int64>( _settings._stopTimeoutSeconds * constant::kMillisecondsPerSecond ) );
        _stopPhase    = StopPhase::WaitingForExit;
        SW_LOG_INFO( "Multiplayer session stopping - %# process(es)", _listStoppingProcess.size() );
    }

    void EditorNetPlaySession::advanceStop( bool bInline )
    {
        switch ( _stopPhase )
        {
            case StopPhase::Idle:
                return;
            case StopPhase::WaitingForExit:
            {
                bool bAllExited = true;
                for ( const unique_ptr<ChildProcess>& pChild : _listStoppingProcess )
                {
                    bAllExited = bAllExited && pChild->_exited.load() != 0;
                }
                if ( bAllExited == false && _stopDeadline.isExpired() == false )
                    return;
                EditorNetPlayStopInput input;
                for ( const unique_ptr<ChildProcess>& pChild : _listStoppingProcess )
                {
                    if ( pChild->_exited.load() == 0 )
                    {
                        SW_LOG_WARNING( "%# did not exit within %# s - terminating it (its trace is lost)", pChild->_label, _settings._stopTimeoutSeconds );
                        (void)pChild->_process.terminate( 1 ); // 죽으면 파이프가 닫혀 읽기 스레드가 끝난다
                    }
                    input._listReader.push_back( &pChild->_reader );
                    input._listTraceSource.push_back( EditorNetPlayTraceSource{ pChild->_label, pChild->_tracePath } );
                }
                input._traceDirectory = _traceDirectory;
                if ( bInline )
                {
                    finishStop( EditorNetPlayStopJob::runWork( input )._mergedTracePath );
                    return;
                }
                _pStopJob->request( std::move( input ) );
                _stopPhase = StopPhase::Merging;
                return;
            }
            case StopPhase::Merging:
            {
                EditorNetPlayStopResult result;
                if ( _pStopJob->take( result ) == EditorTaskPoll::Ready )
                    finishStop( result._mergedTracePath );
                return;
            }
        }
    }

    void EditorNetPlaySession::stopAndWait()
    {
        requestStop();
        // 기다리는 동안에도 줄을 옮긴다(`update`). 끝남 기다리기는 이 스레드에서 끝까지 하고(@p bInline), 이미 낸 합치기 잡이 있으면 그 결과를 기다린다.
        while ( _stopPhase == StopPhase::WaitingForExit )
        {
            for ( size_t processIndex = 0; processIndex < _listStoppingProcess.size(); ++processIndex )
            {
                drainChildLines( *_listStoppingProcess[processIndex], static_cast<int32>( processIndex ) );
            }
            advanceStop( true );
            if ( _stopPhase == StopPhase::WaitingForExit )
                ThreadUtil::sleepForMilliseconds( kUpdatePollMilliseconds );
        }
        while ( _stopPhase == StopPhase::Merging )
        {
            advanceStop( false );
            if ( _stopPhase == StopPhase::Merging )
                ThreadUtil::sleepForMilliseconds( kUpdatePollMilliseconds );
        }
    }

    void EditorNetPlaySession::finishStop( const string& mergedTracePath )
    {
        // 읽기 스레드는 합류했다 - 남은 줄은 모두 쌓여 있다.
        for ( size_t processIndex = 0; processIndex < _listStoppingProcess.size(); ++processIndex )
        {
            drainChildLines( *_listStoppingProcess[processIndex], static_cast<int32>( processIndex ) );
        }
        for ( [[maybe_unused]] const unique_ptr<ChildProcess>& pChild : _listStoppingProcess ) // Shipping 은 로그를 지워 안 쓴다
        {
            SW_LOG_INFO( "%# exited with code %#", pChild->_label, pChild->_exitCode.load() );
        }
        _listStoppingProcess.clear();
        _mergedTracePath      = mergedTracePath;
        _bMergedTraceUnopened = _mergedTracePath.empty() == false;
        _stopPhase            = StopPhase::Idle;
        SW_LOG_INFO( "Multiplayer session stopped%#", _mergedTracePath.empty() ? string{} : " - merged trace " + _mergedTracePath );
    }
} // namespace sw::editor

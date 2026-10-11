#include "pch.h"

#include "Editor/Common/Commands/EditorNetPlaySession.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Network/Transport/UDPNetTransport.h"
#include "Core/Process/Process.h"
#include "Core/Time/MonotonicClock.h"
#include "Core/Time/WallClock.h"

#include "Engine/Profiling/ProfilerTraceExport.h"
#include "Engine/Resource/ResourcePaths.h"

#include <chrono>
#include <thread>

namespace sw::editor
{
    /** @brief 자식 프로세스 하나 — 읽기 스레드가 줄을 쌓고, 게임 스레드가 `update` 에서 꺼낸다. */
    struct EditorNetPlaySession::ChildProcess
    {
        Process        _process{};
        std::thread    _reader{};
        mutex          _mutex{};
        vector<string> _listPendingLine{}; ///< 읽기 스레드가 넣는다(_mutex)
        string         _label{};
        string         _lastLine{};
        string         _tracePath{};
        atomic<int32>  _exitCode{ 0 };
        atomic<uint32> _exited{ 0 };    ///< 읽기 스레드가 파이프 끝을 보고 종료 코드를 거뒀다
        int32          _processID{ 0 }; ///< 띄울 때의 pid(거둔 뒤에도 패널에 보인다)
        bool           _bExitReported{ false };
        bool           _bConnected{ false }; ///< 클라이언트 — 서버에 붙었다는 줄을 남겼다
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
            /** @brief 정상 종료를 기다리며 다시 보는 간격입니다. */
            static constexpr int64 kStopPollMilliseconds = 50;
            /** @brief 트레이스 파일 확장자입니다(`ProfilerTraceExport::kFileExtension` 과 같은 글 — 트레이스가 빠진 빌드에도 이름은 있어야 한다). */
#if SW_PROFILER_TRACE_ENABLED
            static constexpr const utf8* kTraceExtension = ProfilerTraceExport::kFileExtension;
#else
            static constexpr const utf8* kTraceExtension = ".trace.json";
#endif

            /** @brief 명령줄 인자 하나를 따옴표로 감쌉니다(경로의 공백). */
            static string quote( string_view text ) { return "\"" + string( text ) + "\""; }

            /** @brief 경로를 앞으로 빗금으로 — 명령줄에서 `\"` 가 따옴표를 깨지 않게. */
            static string toForwardSlashes( string path )
            {
                for ( utf8& character : path )
                {
                    if ( character == '\\' )
                        character = '/';
                }
                return path;
            }
        };
    } // namespace
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
        , _listLogLine{}
        , _listDrainScratch{}
        , _traceDirectory{}
        , _mergedTracePath{}
        , _settings{}
        , _pendingSettings{}
        , _port{ 0 }
    {
    }

    EditorNetPlaySession::~EditorNetPlaySession()
    {
        if ( isRunning() )
            (void)stop(); // 합친 경로는 쓸 곳이 없다 — 로그에 남는다
    }

    string EditorNetPlaySession::makeDefaultExecutablePath( const utf8* pBaseName )
    {
#if defined( SW_PLATFORM_WINDOWS )
        const string fileName = string( pBaseName ) + ".exe";
#else
        const string fileName = pBaseName;
#endif
        return FileUtil::joinPath( FileUtil::getBinaryDirectory(), fileName );
    }

    void EditorNetPlaySession::readChildOutput( ChildProcess* pChild )
    {
        string line;
        while ( pChild->_process.readOutputLine( line ) )
        {
            std::scoped_lock<mutex> lock{ pChild->_mutex };
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

    string EditorNetPlaySession::makeTracePath( const string& fileStem ) const
    {
        return _traceDirectory.empty() ? string{} : _traceDirectory + "/" + fileStem + EditorNetPlaySessionInternal::kTraceExtension;
    }

    bool EditorNetPlaySession::launchChild( const string& label, const string& executablePath, const string& arguments, const string& tracePath )
    {
        unique_ptr<ChildProcess> pChild = make_unique<ChildProcess>();
        pChild->_label                  = label;
        pChild->_tracePath              = tracePath;
        string command                  = EditorNetPlaySessionInternal::quote( executablePath ) + " " + arguments;
        if ( tracePath.empty() == false )
            command += " " + EditorNetPlaySessionInternal::quote( "-gv_traceSaveOnExit=" + tracePath );
#if !defined( SW_PLATFORM_WINDOWS )
        command = "exec " + command; // `sh -c` 가 부모로 남지 않게 — 종료 코드 · 신호가 실제 프로그램에 닿는다
#endif
        ProcessOptions options;
        options._bPipeStandardInput = true; // `quit` · `pause` 통로
        if ( FileUtil::exists( executablePath ) == false || pChild->_process.launch( command, options ) == false )
        {
            SW_LOG_ERROR( "Could not launch %#: %#", label, command );
            return false;
        }
        pChild->_processID = pChild->_process.getProcessID();
        SW_LOG_INFO( "Launched %# (pid %#): %#", label, pChild->_processID, command );
        pChild->_reader = std::thread( &EditorNetPlaySession::readChildOutput, pChild.get() );
        _listProcess.push_back( std::move( pChild ) );
        return true;
    }

    bool EditorNetPlaySession::start( const EditorNetPlaySettings& settings )
    {
        if ( isRunning() )
        {
            SW_LOG_WARNING( "A multiplayer session is already running - stop it first" );
            return false;
        }
        if ( settings._mode == EditorNetMode::Standalone || settings._clientCount < 1 )
        {
            SW_LOG_WARNING( "Net mode %# with %# player(s) needs no processes - play in the editor instead", toString( settings._mode ), settings._clientCount );
            return false;
        }
        _settings = settings;
        _listLogLine.clear();
        _mergedTracePath.clear();
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
                                ? FileUtil::joinPath( FileUtil::joinPath( ResourcePaths::getProjectFolderPath(), "Saved/Traces" ),
                                                      "netplay_" + to_string( WallClock::nowUnixMilliseconds() ) )
                                : settings._traceDirectory;
            _traceDirectory = EditorNetPlaySessionInternal::toForwardSlashes( _traceDirectory );
            if ( FileUtil::ensureDirectoryExists( _traceDirectory ) == false )
            {
                SW_LOG_WARNING( "Trace folder %# could not be created - the session runs without traces", _traceDirectory );
                _traceDirectory.clear();
            }
        }
#endif
        const string serverPath = settings._serverExecutablePath.empty() ? makeDefaultExecutablePath( "Server" ) : settings._serverExecutablePath;
        const string clientPath = settings._clientExecutablePath.empty() ? makeDefaultExecutablePath( "App" ) : settings._clientExecutablePath;
        const string port       = to_string( _port );
        const string clientArguments =
            EditorNetPlaySessionInternal::quote( "-gv_netConnectAddress=127.0.0.1:" + port ) + " -gv_standardInputCommands=1 " + settings._clientArguments;

        // 서버를 먼저 띄운다 — 클라이언트는 서버가 늦게 서도 연결을 다시 시도하지만(연결 시한 안), 먼저 서 있으면 첫 시도에 붙는다.
        const bool  bDedicated  = settings._mode == EditorNetMode::DedicatedServer;
        bool        bLaunched   = bDedicated ? launchChild( "Server", serverPath, "-gv_netListenPort=" + port, makeTracePath( "server" ) )
                                             : launchChild( "Host", clientPath, "-gv_netListenPort=" + port + " -gv_standardInputCommands=1 " + settings._clientArguments,
                                                            makeTracePath( "host" ) );
        const int32 clientCount = bDedicated ? settings._clientCount : settings._clientCount - 1;
        for ( int32 clientIndex = 1; bLaunched && clientIndex <= clientCount; ++clientIndex )
        {
            const string number = to_string( clientIndex );
            bLaunched           = launchChild( "Client" + number, clientPath, clientArguments, makeTracePath( "client" + number ) );
        }
        if ( bLaunched == false )
        {
            (void)stop(); // 띄운 것은 끈다 — 합칠 트레이스는 없다
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
            ChildProcess&  child = *_listProcess[processIndex];
            vector<string> listLine;
            {
                std::scoped_lock<mutex> lock{ child._mutex };
                listLine.swap( child._listPendingLine );
            }
            for ( string& line : listLine )
            {
                // 자식 로그의 소스 위치 줄(` -> 파일:줄`)은 옮기지 않는다 — 한 사건이 두 줄이 되어 Output Log 를 메운다.
                if ( line.rfind( EditorNetPlaySessionInternal::kSourceLocationPrefix, 0 ) == 0 )
                    continue;
                // 자식의 오류 줄은 경고로 — 에디터 로그의 오류 수는 에디터 자신의 것으로 남긴다. 줄에는 자식의 수준 꼬리표가 그대로 있다.
                if ( line.find( "[Error]" ) != string::npos )
                    SW_LOG_WARNING( "[%#] %#", child._label, line );
                else
                    SW_LOG_INFO( "[%#] %#", child._label, line );
                if ( processIndex > 0 && line.find( EditorNetPlaySessionInternal::kConnectedLine ) != string::npos )
                    child._bConnected = true; // 0 번은 서버(전용 서버 · 리슨 호스트)다
                child._lastLine = line;
                _listLogLine.push_back( EditorNetPlayLogLine{ std::move( line ), static_cast<int32>( processIndex ) } );
            }
            if ( child._exited.load() != 0 && child._bExitReported == false )
            {
                child._bExitReported = true;
                SW_LOG_WARNING( "%# exited with code %# during the session - last line: %#", child._label, child._exitCode.load(), child._lastLine );
            }
        }
        if ( _listLogLine.size() > kMaxLogLine )
            _listLogLine.erase( _listLogLine.begin(), _listLogLine.begin() + static_cast<ptrdiff_t>( _listLogLine.size() - kMaxLogLine ) );
    }

    bool EditorNetPlaySession::sendServerCommand( string_view command )
    {
        if ( _listProcess.empty() || _listProcess[0]->_exited.load() != 0 )
            return false;
        return _listProcess[0]->_process.writeInput( string( command ) + "\n" );
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
        const ChildProcess& child = *_listProcess[static_cast<size_t>( processIndex )];
        status._label             = child._label;
        status._lastLine          = child._lastLine;
        status._tracePath         = child._tracePath;
        status._processID         = child._processID;
        status._bRunning          = child._exited.load() == 0;
        status._bConnected        = child._bConnected;
        status._exitCode          = status._bRunning ? 0 : child._exitCode.load();
        return status;
    }

    string EditorNetPlaySession::stop()
    {
        if ( _listProcess.empty() )
            return {};
        // 정상 종료 — 클라이언트부터 끄면 서버 로그에 끊김이 남는다(접속 · 끊김을 한 줄로 확인할 수 있게). 이미 끝난 것은 건너뛴다.
        for ( size_t processIndex = _listProcess.size(); processIndex-- > 0; )
        {
            ChildProcess& child  = *_listProcess[processIndex];
            child._bExitReported = true; // 끄는 중의 끝남은 알리지 않는다
            if ( child._exited.load() == 0 && child._process.writeInput( "quit\n" ) == false )
                SW_LOG_WARNING( "Could not send quit to %# - it is terminated after the timeout", child._label );
        }
        const Deadline deadline   = Deadline::afterMilliseconds( static_cast<int64>( _settings._stopTimeoutSeconds * 1000.0f ) );
        bool           bAllExited = false;
        while ( bAllExited == false && deadline.isExpired() == false )
        {
            bAllExited = true;
            for ( const unique_ptr<ChildProcess>& pChild : _listProcess )
            {
                bAllExited = bAllExited && pChild->_exited.load() != 0;
            }
            if ( bAllExited == false )
                std::this_thread::sleep_for( std::chrono::milliseconds( EditorNetPlaySessionInternal::kStopPollMilliseconds ) );
        }
        for ( const unique_ptr<ChildProcess>& pChild : _listProcess )
        {
            if ( pChild->_exited.load() == 0 )
            {
                SW_LOG_WARNING( "%# did not exit within %# s - terminating it (its trace is lost)", pChild->_label, _settings._stopTimeoutSeconds );
                (void)pChild->_process.terminate( 1 ); // 죽으면 파이프가 닫혀 읽기 스레드가 끝난다
            }
        }
        for ( const unique_ptr<ChildProcess>& pChild : _listProcess )
        {
            if ( pChild->_reader.joinable() )
                pChild->_reader.join();
        }
        update(); // 남은 줄을 로그로
        for ( const unique_ptr<ChildProcess>& pChild : _listProcess )
        {
            SW_LOG_INFO( "%# exited with code %#", pChild->_label, pChild->_exitCode.load() );
        }
        _mergedTracePath = mergeTraces();
        _listProcess.clear();
        SW_LOG_INFO( "Multiplayer session stopped%#", _mergedTracePath.empty() ? string{} : " - merged trace " + _mergedTracePath );
        return _mergedTracePath;
    }

    string EditorNetPlaySession::mergeTraces()
    {
#if SW_PROFILER_TRACE_ENABLED
        if ( _traceDirectory.empty() )
            return {};
        vector<ProfilerTraceMergeInput> listInput;
        for ( const unique_ptr<ChildProcess>& pChild : _listProcess )
        {
            ProfilerTraceMergeInput input;
            input._label = pChild->_label;
            if ( pChild->_tracePath.empty() == false && FileUtil::readTextFile( pChild->_tracePath, input._json ) )
                listInput.push_back( std::move( input ) );
            else
                SW_LOG_WARNING( "No trace from %# (%#)", pChild->_label, pChild->_tracePath );
        }
        if ( listInput.size() < 2 )
            return {};
        string json;
        string report;
        if ( ProfilerTraceExport::mergeTraceJSON( listInput, json, report ) == false )
        {
            SW_LOG_WARNING( "Traces could not be merged: %#", report );
            return {};
        }
        const string mergedPath = _traceDirectory + "/" + kMergedTraceFileName;
        if ( FileUtil::writeTextFile( mergedPath, json ) == false )
        {
            SW_LOG_WARNING( "Merged trace could not be written to %#", mergedPath );
            return {};
        }
        SW_LOG_INFO( "Traces merged: %# -> %#", report, mergedPath );
        return mergedPath;
#else
        return {};
#endif
    }
} // namespace sw::editor

#include "pch.h"

#include "App/Module/ModuleCompiler.h"

#include "App/Module/LiveReloadManager.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Process/Process.h"

namespace sw
{
    SW_LOG_CALLER( "ModuleCompiler" );

    ModuleCompiler::ModuleCompiler( LiveReloadManager* pLiveReloadManager )
        : _pLiveReloadManager{ pLiveReloadManager }
        , _pCurrentProcess{ nullptr }
        , _workerThread{}
        , _buildTimer{}
        , _targetName{}
        , _mutex{}
        , _buildState{ BuildState::Idle }
        , _bBlockedByLoadedBinary{ 0 }
        , _lastExitCode{ 0 }
        , _lastDurationSec{ 0.0f }
        , _bIsCompiling{ false }
        , _bCancelRequested{ false }
    {
    }

    ModuleCompiler::~ModuleCompiler()
    {
        shutdown();
    }

    void ModuleCompiler::shutdown()
    {
        ModuleCompiler::cancel();
        if ( _workerThread.joinable() )
            _workerThread.join();
    }

    bool ModuleCompiler::compileModule( string_view targetName )
    {
        if ( _bIsCompiling.load( std::memory_order_relaxed ) )
        {
            SW_LOG_WARNING( "Compilation is already in progress (target: %#)", _targetName.c_str() );
            return false;
        }

        if ( _workerThread.joinable() )
            _workerThread.join();

        {
            std::lock_guard<mutex> lock( _mutex );
            _targetName = string( targetName );
        }

        _bCancelRequested.store( false, std::memory_order_relaxed );
        _bBlockedByLoadedBinary.store( 0, std::memory_order_relaxed );
        _bIsCompiling.store( true, std::memory_order_relaxed );
        _buildState.store( BuildState::Compiling, std::memory_order_relaxed );
        _buildTimer.resetTimer();
        _buildTimer.startTimer();

        _workerThread = std::thread( &ModuleCompiler::runBuildThread, this, string( targetName ) );
        return true;
    }

    bool ModuleCompiler::compileAll()
    {
        return compileModule( "" );
    }

    void ModuleCompiler::cancel()
    {
        if ( _bIsCompiling.load( std::memory_order_relaxed ) == false )
            return;

        _bCancelRequested.store( true, std::memory_order_relaxed );

        std::lock_guard<mutex> lock( _mutex );
        if ( _pCurrentProcess != nullptr )
            _pCurrentProcess->terminate( 1 );
    }

    float32 ModuleCompiler::getElapsedTimeSec() const
    {
        if ( _bIsCompiling.load( std::memory_order_relaxed ) == false )
            return _lastDurationSec.load( std::memory_order_relaxed );

        return _buildTimer.getTotalTime();
    }

    string ModuleCompiler::getTargetName() const
    {
        std::lock_guard<mutex> lock( _mutex );
        return _targetName;
    }

    string ModuleCompiler::findBuildDirectory() const
    {
        string       resultDir = FileUtil::getDirectoryPart( FileUtil::getExecutablePath() );
        const string parentDir = FileUtil::getDirectoryPart( resultDir ); // parent of Bin

        if ( FileUtil::fileExists( FileUtil::joinPath( parentDir, "build.ninja" ) ) ||
             FileUtil::fileExists( FileUtil::joinPath( parentDir, "CMakeCache.txt" ) ) )
            resultDir = parentDir;

        return resultDir;
    }

    void ModuleCompiler::runBuildThread( string targetName )
    {
        const string buildDir = findBuildDirectory();
        if ( buildDir.empty() || FileUtil::directoryExists( buildDir ) == false )
        {
            SW_LOG_ERROR( "Failed to find build directory for compilation!" );
            _buildState.store( BuildState::Failed, std::memory_order_relaxed );
            _lastExitCode.store( -1, std::memory_order_relaxed );
            _bIsCompiling.store( false, std::memory_order_relaxed );
            return;
        }

        const string targetDisplayName = targetName.empty() ? "all" : targetName;
        SW_LOG_INFO( "Starting compilation for target '%#' (Build dir: %#)...", targetDisplayName.c_str(), buildDir.c_str() );

        string cmdLine = "cmake --build \"" + buildDir + "\"";
        if ( targetName.empty() == false )
            cmdLine += " --target " + targetName;

        ProcessOptions options{};
        options._workingDirectory = buildDir;
        options._bCreateWindow    = false;

        unique_ptr<Process> pProcess = make_unique<Process>();
        if ( pProcess->launch( cmdLine, options ) == false )
        {
            SW_LOG_ERROR( "Failed to launch CMake process! Command: %#", cmdLine.c_str() );
            _buildState.store( BuildState::Failed, std::memory_order_relaxed );
            _lastExitCode.store( -1, std::memory_order_relaxed );
            _bIsCompiling.store( false, std::memory_order_relaxed );
            return;
        }

        {
            std::lock_guard<mutex> lock( _mutex );
            _pCurrentProcess = std::move( pProcess );
        }

        string outputLine;
        while ( _pCurrentProcess != nullptr && _pCurrentProcess->readOutputLine( outputLine ) )
        {
            if ( outputLine.empty() )
                continue;

            // 이미 로드된 DLL 을 다시 링크하려다 막힌 경우다. 링커 메시지만 보면 원인이 안 보이므로
            // 따로 표시해 두고 아래에서 사람이 읽을 수 있는 설명을 남긴다 — 핫리로드로 고칠 수 없는
            // 상황(엔진 자체가 바뀜)이라 재시작이 필요하다는 것이 요점이다.
            if ( outputLine.find( "failed to write output" ) != string::npos && outputLine.find( "permission denied" ) != string::npos )
                _bBlockedByLoadedBinary.store( 1, std::memory_order_relaxed );

            if ( outputLine.find( "FAILED:" ) != string::npos || outputLine.find( "error:" ) != string::npos || outputLine.find( "Error" ) != string::npos )
                SW_LOG_ERROR( "%#", outputLine.c_str() );
            else if ( outputLine.find( "warning:" ) != string::npos || outputLine.find( "Warning" ) != string::npos )
                SW_LOG_WARNING( "%#", outputLine.c_str() );
            else
                SW_LOG_INFO( "%#", outputLine.c_str() );
        }

        int32 exitCode = -1;
        {
            std::lock_guard<mutex> lock( _mutex );
            if ( _pCurrentProcess != nullptr )
            {
                exitCode = _pCurrentProcess->waitForExit();
                _pCurrentProcess.reset();
            }
        }

        _buildTimer.stopTimer();
        const float32 durationSec = _buildTimer.getTotalTime();

        _lastDurationSec.store( durationSec, std::memory_order_relaxed );
        _lastExitCode.store( exitCode, std::memory_order_relaxed );

        if ( _bCancelRequested.load( std::memory_order_relaxed ) )
        {
            _buildState.store( BuildState::Failed, std::memory_order_relaxed );
            SW_LOG_WARNING( "Compilation was cancelled by user." );
        }
        else if ( exitCode == 0 )
        {
            _buildState.store( BuildState::Success, std::memory_order_relaxed );
            SW_LOG_INFO( "Compilation succeeded in %#s (target: %#)!", Fmt( static_cast<float64>( durationSec ), Format().precision( 2 ) ), targetDisplayName.c_str() );

#if !defined( SW_SHIPPING )
            if ( _pLiveReloadManager != nullptr && targetName.empty() == false )
                _pLiveReloadManager->triggerReload( targetName );
#endif
        }
        else
        {
            _buildState.store( BuildState::Failed, std::memory_order_relaxed );
            if ( _bBlockedByLoadedBinary.load( std::memory_order_relaxed ) != 0 )
            {
                SW_LOG_ERROR( "Compilation failed (target: %#): 이미 실행 중이라 교체할 수 없는 바이너리가 있습니다. "
                              "핫리로드는 플러그인 모듈만 바꿀 수 있고, 엔진 자체가 다시 링크돼야 하는 변경이면 재시작해야 합니다.",
                              targetDisplayName.c_str() );
            }
            else
            {
                SW_LOG_ERROR( "Compilation failed with exit code %# (target: %#)", exitCode, targetDisplayName.c_str() );
            }
        }

        _bIsCompiling.store( false, std::memory_order_relaxed );
    }
} // namespace sw

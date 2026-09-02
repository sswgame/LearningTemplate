#include "pch.h"

#include "App/Module/ModuleCompiler.h"

#include "Core/Common/StdHeaders.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Process/Process.h"

#include "Engine/Utility/Module/LiveReloadManager.h"

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

    void ModuleCompiler::initialize( LiveReloadManager* pLiveReloadManager )
    {
        _pLiveReloadManager = pLiveReloadManager;
    }

    void ModuleCompiler::shutdown()
    {
        cancel();
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
        {
            _pCurrentProcess->terminate( 1 );
        }
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
        {
            resultDir = parentDir;
        }

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
        {
            cmdLine += " --target " + targetName;
        }

        ProcessOptions options{};
        options._workingDirectory = buildDir;
        options._bCreateWindow    = false;

        auto pProc = make_unique<Process>();
        if ( pProc->launch( cmdLine, options ) == false )
        {
            SW_LOG_ERROR( "Failed to launch CMake process! Command: %#", cmdLine.c_str() );
            _buildState.store( BuildState::Failed, std::memory_order_relaxed );
            _lastExitCode.store( -1, std::memory_order_relaxed );
            _bIsCompiling.store( false, std::memory_order_relaxed );
            return;
        }

        {
            std::lock_guard<mutex> lock( _mutex );
            _pCurrentProcess = std::move( pProc );
        }

        string singleLine;
        while ( _pCurrentProcess != nullptr && _pCurrentProcess->readOutputLine( singleLine ) )
        {
            if ( singleLine.empty() )
                continue;

            if ( singleLine.find( "FAILED:" ) != string::npos || singleLine.find( "error:" ) != string::npos || singleLine.find( "Error" ) != string::npos )
                SW_LOG_ERROR( "%#", singleLine.c_str() );
            else if ( singleLine.find( "warning:" ) != string::npos || singleLine.find( "Warning" ) != string::npos )
                SW_LOG_WARNING( "%#", singleLine.c_str() );
            else
                SW_LOG_INFO( "%#", singleLine.c_str() );
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

            if ( _pLiveReloadManager != nullptr && targetName.empty() == false )
            {
                _pLiveReloadManager->triggerReload( targetName );
            }
        }
        else
        {
            _buildState.store( BuildState::Failed, std::memory_order_relaxed );
            SW_LOG_ERROR( "Compilation failed with exit code %# (target: %#)", exitCode, targetDisplayName.c_str() );
        }

        _bIsCompiling.store( false, std::memory_order_relaxed );
    }
} // namespace sw

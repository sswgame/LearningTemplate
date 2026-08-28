#include "pch.h"

#include "Core/Log/Logger.h"
#include "Core/Process/Process.h"

#if defined( SW_PLATFORM_LINUX )
	#include <csignal>
	#include <cstdio>
	#include <cstdlib>
	#include <sys/types.h>
	#include <sys/wait.h>
	#include <unistd.h>

namespace sw
{
	SW_LOG_CALLER( "LinuxProcess" );

	void Process::cleanup()
	{
		if ( _pStdOutRead != nullptr )
		{
			pclose( static_cast<FILE*>( _pStdOutRead ) );
			_pStdOutRead = nullptr;
		}

		_bufferedOutput.clear();
		_processId = 0;
		_bRunning  = false;
	}

	bool Process::launch( string_view command, const ProcessOptions& options )
	{
		cleanup();

		string cmd = string( command );
		if ( options.workingDirectory.empty() == false )
		{
			cmd = "cd \"" + options.workingDirectory + "\" && " + cmd;
		}
		cmd += " 2>&1";

		FILE* pPipe = popen( cmd.c_str(), "r" );
		if ( pPipe == nullptr )
		{
			SW_LOG_ERROR( "popen failed for command: %#", cmd.c_str() );
			return false;
		}

		_pStdOutRead = pPipe;
		_bRunning	 = true;
		return true;
	}

	bool Process::readOutputLine( string& outLine )
	{
		outLine.clear();

		if ( _pStdOutRead == nullptr )
			return false;

		utf8 lineBuffer[4096];
		if ( fgets( lineBuffer, sizeof( lineBuffer ), static_cast<FILE*>( _pStdOutRead ) ) != nullptr )
		{
			outLine = lineBuffer;
			while ( outLine.empty() == false && ( outLine.back() == '\n' || outLine.back() == '\r' ) )
				outLine.pop_back();
			return true;
		}

		return false;
	}

	int32 Process::waitForExit()
	{
		if ( _pStdOutRead == nullptr )
			return -1;

		const int32 status = pclose( static_cast<FILE*>( _pStdOutRead ) );
		_pStdOutRead	   = nullptr;
		_bRunning		   = false;

		return WIFEXITED( status ) ? WEXITSTATUS( status ) : -1;
	}

	bool Process::terminate( int32 exitCode )
	{
		(void)exitCode;
		if ( _pStdOutRead != nullptr )
		{
			pclose( static_cast<FILE*>( _pStdOutRead ) );
			_pStdOutRead = nullptr;
			_bRunning	 = false;
			return true;
		}
		return false;
	}

	bool Process::isRunning() const
	{
		return _bRunning;
	}
} // namespace sw

#endif

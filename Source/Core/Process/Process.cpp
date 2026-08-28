#include "pch.h"

#include "Core/Process/Process.h"

#include "Core/Log/Logger.h"

#include <utility>

namespace sw
{
	SW_LOG_CALLER( "Process" );

	Process::Process()
		: _pNativeHandle{ nullptr }
		, _pStdOutRead{ nullptr }
		, _pNativeThread{ nullptr }
		, _bufferedOutput{}
		, _processId{ 0 }
		, _bRunning{ false }
	{
	}

	Process::~Process()
	{
		cleanup();
	}

	Process::Process( Process&& other ) noexcept
		: _pNativeHandle{ other._pNativeHandle }
		, _pStdOutRead{ other._pStdOutRead }
		, _pNativeThread{ other._pNativeThread }
		, _bufferedOutput{ std::move( other._bufferedOutput ) }
		, _processId{ other._processId }
		, _bRunning{ other._bRunning }
	{
		other._pNativeHandle = nullptr;
		other._pStdOutRead	 = nullptr;
		other._pNativeThread = nullptr;
		other._processId	 = 0;
		other._bRunning		 = false;
	}

	Process& Process::operator=( Process&& other ) noexcept
	{
		if ( this != &other )
		{
			cleanup();

			_pNativeHandle	= other._pNativeHandle;
			_pStdOutRead	= other._pStdOutRead;
			_pNativeThread	= other._pNativeThread;
			_bufferedOutput = std::move( other._bufferedOutput );
			_processId		= other._processId;
			_bRunning		= other._bRunning;

			other._pNativeHandle = nullptr;
			other._pStdOutRead	 = nullptr;
			other._pNativeThread = nullptr;
			other._processId	 = 0;
			other._bRunning		 = false;
		}
		return *this;
	}

	int32 Process::execute( string_view command, const ProcessOptions& options, ProcessOutputDelegate onOutput )
	{
		Process proc;
		if ( proc.launch( command, options ) == false )
		{
			SW_LOG_ERROR( "Failed to launch command: %#", string( command ).c_str() );
			return -1;
		}

		string line;
		while ( proc.readOutputLine( line ) )
		{
			if ( onOutput.isBound() )
			{
				onOutput( line, false );
			}
		}

		return proc.waitForExit();
	}
} // namespace sw

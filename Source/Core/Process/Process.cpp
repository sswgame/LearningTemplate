#include "pch.h"

#include "Core/Process/Process.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Log/Logger.h"

namespace sw
{
    SW_LOG_CALLER( "Process" );

    Process::Process()
        : _pNativeHandle{ nullptr }
        , _pStdOutRead{ nullptr }
        , _pNativeThread{ nullptr }
        , _pStdInWrite{ nullptr }
        , _bufferedOutput{}
        , _processID{ 0 }
        , _bNewProcessGroup{ false }
    {
    }

    Process::~Process()
    {
        shutdown();
    }

    Process::Process( Process&& other ) noexcept
        : _pNativeHandle{ other._pNativeHandle }
        , _pStdOutRead{ other._pStdOutRead }
        , _pNativeThread{ other._pNativeThread }
        , _pStdInWrite{ other._pStdInWrite }
        , _bufferedOutput{ std::move( other._bufferedOutput ) }
        , _processID{ other._processID.load() }
        , _bNewProcessGroup{ other._bNewProcessGroup }
    {
        other._pNativeHandle = nullptr;
        other._pStdOutRead   = nullptr;
        other._pNativeThread = nullptr;
        other._pStdInWrite   = nullptr;
        other._processID.store( 0 );
    }

    Process& Process::operator=( Process&& other ) noexcept
    {
        if ( this != &other )
        {
            shutdown();

            _pNativeHandle    = other._pNativeHandle;
            _pStdOutRead      = other._pStdOutRead;
            _pNativeThread    = other._pNativeThread;
            _pStdInWrite      = other._pStdInWrite;
            _bufferedOutput   = std::move( other._bufferedOutput );
            _bNewProcessGroup = other._bNewProcessGroup;
            _processID.store( other._processID.load() );

            other._pNativeHandle = nullptr;
            other._pStdOutRead   = nullptr;
            other._pNativeThread = nullptr;
            other._pStdInWrite   = nullptr;
            other._processID.store( 0 );
        }
        return *this;
    }

    int32 Process::execute( string_view command, const ProcessOptions& options, const ProcessOutputDelegate& onOutput )
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
                onOutput( line );
        }

        return proc.waitForExit();
    }
} // namespace sw

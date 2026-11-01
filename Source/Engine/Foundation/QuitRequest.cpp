#include "pch.h"

#include "Engine/Foundation/QuitRequest.h"

#include "Core/Log/Logger.h"

namespace sw
{
    void QuitRequest::request( int32 exitCode )
    {
        if ( _bRequested )
            return;
        _bRequested = true;
        _exitCode   = exitCode;
        SW_LOG_INFO_ID( "Engine.QuitRequestedExitCode", "Quit requested (exit code %#)", exitCode );
    }
} // namespace sw

#include "pch.h"

#include "Engine/Foundation/QuitRequest.h"

#include "Core/Log/Logger.h"

namespace sw
{
    SW_LOG_CALLER( "Engine" );

    void QuitRequest::request( int32 exitCode )
    {
        if ( _bRequested )
            return;
        _bRequested = true;
        _exitCode   = exitCode;
        SW_LOG_INFO( "Quit requested (exit code %#)", exitCode );
    }
} // namespace sw

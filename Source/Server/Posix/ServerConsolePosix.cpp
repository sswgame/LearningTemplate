#include "pch.h"

#include "Server/ServerConsole.h"

#if !defined( SW_PLATFORM_WINDOWS )
    #include <poll.h>
    #include <unistd.h>

namespace sw
{
    bool ServerConsole::readLinePlatform( string& outLine )
    {
        for ( ;; )
        {
            if ( takeLine( outLine ) )
                return true;
            if ( _stopRequested.load() != 0 )
                return false;
            pollfd      descriptor{ STDIN_FILENO, POLLIN, 0 };
            const int32 ready = poll( &descriptor, 1, 100 );
            if ( ready <= 0 )
                continue; // 시한(멈춤 요청을 다시 본다) · EINTR
            utf8          arrBuffer[512];
            const ssize_t count = read( STDIN_FILENO, arrBuffer, sizeof( arrBuffer ) );
            if ( count <= 0 )
            {
                _endOfInput.store( 1 );
                return false;
            }
            _partialLine.append( arrBuffer, static_cast<size_t>( count ) );
        }
    }
} // namespace sw

#endif

#include "pch.h"

#include "Core/Log/LogContext.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    namespace
    {
        thread_local LogContext t_logContext{};

        struct LogContextInternal
        {
            static constexpr utf8 kArrHexDigit[] = "0123456789abcdef";

            static int32 appendText( utf8* pOutBuffer, int32 length, const utf8* pText )
            {
                for ( const utf8* pCharacter = pText; *pCharacter != '\0'; ++pCharacter )
                {
                    pOutBuffer[length++] = *pCharacter;
                }
                return length;
            }

            static int32 appendHex64( utf8* pOutBuffer, int32 length, uint64 value )
            {
                for ( int32 digitIndex = 15; digitIndex >= 0; --digitIndex )
                {
                    pOutBuffer[length++] = kArrHexDigit[( value >> ( digitIndex * 4 ) ) & 0xFu];
                }
                return length;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    LogTraceId LogTraceId::makeRandom()
    {
        LogTraceId traceId;
        while ( traceId.isValid() == false )
        {
            traceId._high = MathUtil::getRandom<uint64>();
            traceId._low  = MathUtil::getRandom<uint64>();
        }
        return traceId;
    }

    int32 LogContext::formatTag( utf8* pOutBuffer, int32 capacity ) const
    {
        if ( pOutBuffer == nullptr || capacity <= 0 )
            return 0;
        pOutBuffer[0] = '\0';
        if ( isEmpty() || capacity < kMaxTagSize )
            return 0;
        int32 length = LogContextInternal::appendText( pOutBuffer, 0, "[" );
        if ( _traceId.isValid() )
        {
            length = LogContextInternal::appendText( pOutBuffer, length, "trace=" );
            length = LogContextInternal::appendHex64( pOutBuffer, length, _traceId._high );
            length = LogContextInternal::appendHex64( pOutBuffer, length, _traceId._low );
        }
        if ( _principalId != 0 )
        {
            length = LogContextInternal::appendText( pOutBuffer, length, length > 1 ? " acct=" : "acct=" );
            length = LogContextInternal::appendHex64( pOutBuffer, length, _principalId );
        }
        length             = LogContextInternal::appendText( pOutBuffer, length, "] " );
        pOutBuffer[length] = '\0';
        return length;
    }

    const LogContext& LogContext::getCurrent() { return t_logContext; }

    ScopedLogContext::ScopedLogContext( const LogContext& context )
        : _previous{ t_logContext }
    {
        t_logContext = context;
    }

    ScopedLogContext::~ScopedLogContext() { t_logContext = _previous; }
} // namespace sw

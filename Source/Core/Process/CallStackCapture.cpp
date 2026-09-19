#include "pch.h"

#include "Core/Process/CallStackCapture.h"

#include "Core/Common/StdHeaders.h"
#include "Core/String/StringBuilder.h"

namespace sw
{
    string formatRawCallStackFrames( void* const* ppFrame, uint32 frameCount )
    {
        if ( ppFrame == nullptr || frameCount == 0 )
            return kEmptyCallStackText;

        StringBuilder<constant::kMaxBuffer8192> sb;
        for ( uint32 frameIndex = 0; frameIndex < frameCount; ++frameIndex )
        {
            sb.appendFormat( "  [%#] 0x%# (symbols busy)\n", frameIndex,
                             Fmt( reinterpret_cast<uint64>( ppFrame[frameIndex] ), Format().hex() ) );
        }
        return string( sb.view() );
    }
} // namespace sw

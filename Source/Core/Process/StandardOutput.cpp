#include "pch.h"

#include "Core/Process/StandardOutput.h"

#include <cstdio>

namespace sw
{
    bool StandardOutput::write( string_view text )
    {
        const size_t written = std::fwrite( text.data(), 1, text.size(), stdout );
        return written == text.size() && std::fflush( stdout ) == 0;
    }
} // namespace sw

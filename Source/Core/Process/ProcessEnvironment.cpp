#include "pch.h"

#include "Core/Process/ProcessEnvironment.h"

namespace sw
{
    bool ProcessEnvironment::tryReadNonEmpty( string_view name, string& outValue )
    {
        return tryRead( name, outValue ) && outValue.empty() == false;
    }
} // namespace sw

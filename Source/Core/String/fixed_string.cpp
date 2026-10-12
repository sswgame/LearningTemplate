#include "pch.h"

#include "Core/String/fixed_string.h"

namespace sw
{
    bool& FixedStringWarningGuard::getWarningFlag()
    {
        static thread_local bool s_bWarning = false;
        return s_bWarning;
    }
} // namespace sw

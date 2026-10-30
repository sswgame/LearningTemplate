#include "pch.h"

#include "Core/Process/ProcessRoots.h"

#include "Core/Log/Logger.h"

#include <optional>

namespace sw
{
    namespace
    {
        // 설치는 기동 스레드 하나에서만 한다(그 뒤 읽기만이라 락이 없다).
        std::optional<ProcessRoots> s_roots;
    } // namespace
} // namespace sw

namespace sw
{
    void ProcessRoots::install( ProcessRoots roots )
    {
        if ( s_roots.has_value() )
            SW_FATAL( "ProcessRoots::install called twice" );
        s_roots = std::move( roots );
    }

    const ProcessRoots& ProcessRoots::get()
    {
        if ( s_roots.has_value() == false )
            SW_FATAL( "ProcessRoots::get before install - the process roots are set once at startup" );
        return *s_roots;
    }
} // namespace sw

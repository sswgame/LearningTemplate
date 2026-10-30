#include "pch.h"

#include "Core/File/ProcessRoots.h"

#include "Core/Log/Logger.h"

#include <optional>

namespace sw
{
    namespace
    {
        // 설치는 기동 스레드 하나에서만 한다(그 뒤 읽기만이라 락이 없다).
        std::optional<ProcessRootSet> g_roots;
    } // namespace

    void ProcessRoots::install( ProcessRootSet roots )
    {
        if ( g_roots.has_value() )
            SW_FATAL( "ProcessRoots::install called twice" );
        g_roots = std::move( roots );
    }

    bool ProcessRoots::isInstalled()
    {
        return g_roots.has_value();
    }

    const ProcessRootSet& ProcessRoots::get()
    {
        if ( g_roots.has_value() == false )
            SW_FATAL( "ProcessRoots::get before install - the process roots are set once at startup" );
        return *g_roots;
    }
} // namespace sw

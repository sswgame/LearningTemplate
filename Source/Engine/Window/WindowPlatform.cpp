#include "pch.h"

#include "Engine/Window/WindowPlatform.h"

namespace sw
{
    namespace
    {
        const WindowPlatformFactory* s_pWindowPlatformFactory = nullptr;
    } // namespace

    const WindowPlatformFactory* WindowPlatform::find()
    {
        return s_pWindowPlatformFactory;
    }

    WindowPlatform::Registrar::Registrar( const WindowPlatformFactory& factory )
    {
        SW_ASSERT( s_pWindowPlatformFactory == nullptr );
        s_pWindowPlatformFactory = &factory;
    }
} // namespace sw

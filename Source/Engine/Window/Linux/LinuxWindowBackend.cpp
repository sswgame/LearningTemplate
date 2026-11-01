#include "pch.h"

#include "Engine/Window/Linux/LinuxWindowBackend.h"

#include "Core/Container/string.h"
#include "Core/Process/ProcessEnvironment.h"

namespace sw
{
    bool tryReadLinuxWindowBackend( LinuxWindowBackend& outBackend )
    {
        string backend;
        if ( ProcessEnvironment::tryRead( "SW_WINDOW_BACKEND", backend ) == false || backend == "x11" )
        {
            outBackend = LinuxWindowBackend::X11;
            return true;
        }
        if ( backend == "wayland" )
        {
            outBackend = LinuxWindowBackend::Wayland;
            return true;
        }
        SW_LOG_ERROR_ID( "Window.SWWINDOWBACKENDNot", "SW_WINDOW_BACKEND='%#' is not a window backend (x11, wayland)", backend.c_str() );
        return false;
    }
} // namespace sw

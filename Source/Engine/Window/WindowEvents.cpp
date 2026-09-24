#include "pch.h"

#include "Engine/Window/WindowEvents.h"

namespace sw
{
    WindowResizeEvent::WindowResizeEvent() noexcept
        : _width{ 0 }
        , _height{ 0 }
        , _bIsResizing{ SW_FALSE }
        , _bIsMaximized{ SW_FALSE }
        , _bIsMinimized{ SW_FALSE }
        , _reservedFlags{ 0 }
    {
    }

    WindowActivateEvent::WindowActivateEvent() noexcept
        : _bIsActivate{ SW_FALSE }
        , _reservedFlags{ 0 }
    {
    }
} // namespace sw

#include "pch.h"

#include "Engine/Window/NativeWindowEvent.h"

#if defined( SW_PLATFORM_WINDOWS )
    #include "Engine/Common/EnginePlatformHeaders.h"

namespace sw
{
    bool NativeWindowEvent::isMouseInput() const
    {
        return WM_MOUSEFIRST <= _message && _message <= WM_MOUSELAST;
    }

    bool NativeWindowEvent::isKeyboardInput() const
    {
        return WM_KEYFIRST <= _message && _message <= WM_KEYLAST;
    }

    bool NativeWindowEvent::isInputRelease() const
    {
        if ( isMouseInput() )
        {
            const bool bMouseUp = ( _message == WM_LBUTTONUP || _message == WM_RBUTTONUP || _message == WM_MBUTTONUP ||
                                    _message == WM_XBUTTONUP );
            return bMouseUp;
        }
        if ( isKeyboardInput() )
        {
            const bool bKeyUp = ( _message == WM_KEYUP || _message == WM_SYSKEYUP );
            return bKeyUp;
        }
        return false;
    }
} // namespace sw

#endif

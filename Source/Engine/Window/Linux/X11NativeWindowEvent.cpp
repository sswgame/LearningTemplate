#include "pch.h"

#include "Core/Foundation/PlatformOSHeaders.h"

#include "Engine/Window/Linux/Wayland/WaylandInputEvent.h"
#include "Engine/Window/NativeWindowEvent.h"

#if defined( SW_PLATFORM_LINUX ) && defined( SW_WITH_CLIENT_CODE )
    #include "Core/Foundation/X11Headers.h"

namespace sw
{
    namespace
    {
        /** @brief Wayland 창이 실은 입력이면 그것을, 아니면 nullptr 입니다. */
        const WaylandInputEvent* findWaylandInputEvent( const NativeWindowEvent& event )
        {
            if ( event._message != NativeWindowEvent::kMessageWayland || event._lParam == 0 )
                return nullptr;
            return reinterpret_cast<const WaylandInputEvent*>( event._lParam );
        }
    } // namespace

    bool NativeWindowEvent::isMouseInput() const
    {
        if ( const WaylandInputEvent* pWayland = findWaylandInputEvent( *this ) )
        {
            const WaylandInputKind kind     = pWayland->_kind;
            const bool             bPointer = kind == WaylandInputKind::PointerMove || kind == WaylandInputKind::PointerDown || kind == WaylandInputKind::PointerUp;
            return bPointer || kind == WaylandInputKind::PointerWheel;
        }
        if ( _message != kMessageX11 || _lParam == 0 )
            return false;
        const XEvent* pXev = reinterpret_cast<const XEvent*>( _lParam );
        return pXev->type == ButtonPress || pXev->type == ButtonRelease || pXev->type == MotionNotify;
    }

    bool NativeWindowEvent::isKeyboardInput() const
    {
        if ( const WaylandInputEvent* pWayland = findWaylandInputEvent( *this ) )
        {
            const WaylandInputKind kind = pWayland->_kind;
            return kind == WaylandInputKind::KeyDown || kind == WaylandInputKind::KeyUp || kind == WaylandInputKind::Text;
        }
        if ( _message != kMessageX11 || _lParam == 0 )
            return false;
        const XEvent* pXev = reinterpret_cast<const XEvent*>( _lParam );
        return pXev->type == KeyPress || pXev->type == KeyRelease;
    }

    bool NativeWindowEvent::isInputRelease() const
    {
        if ( const WaylandInputEvent* pWayland = findWaylandInputEvent( *this ) )
            return pWayland->_kind == WaylandInputKind::PointerUp || pWayland->_kind == WaylandInputKind::KeyUp;
        if ( _message != kMessageX11 || _lParam == 0 )
            return false;
        const XEvent* pXev = reinterpret_cast<const XEvent*>( _lParam );
        return pXev->type == ButtonRelease || pXev->type == KeyRelease;
    }
} // namespace sw

#endif

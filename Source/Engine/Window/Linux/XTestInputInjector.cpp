#include "pch.h"

#include "Engine/Window/Linux/XTestInputInjector.h"

#if defined( SW_PLATFORM_LINUX ) && defined( SW_WITH_CLIENT_CODE ) && defined( SW_WITH_XTEST )

    #include "Core/Foundation/PlatformOSHeaders.h"
    #include "Core/Foundation/X11Headers.h"

    #include <X11/extensions/XTest.h>

    #include "Core/Foundation/X11MacroUndef.h" // XTest 헤더 뒤에 X11 매크로를 다시 지운다

namespace sw
{
    XTestInputInjector::XTestInputInjector()
        : _pDisplay{ nullptr }
    {
    }

    XTestInputInjector::~XTestInputInjector()
    {
        if ( _pDisplay != nullptr )
            XCloseDisplay( static_cast<Display*>( _pDisplay ) );
    }

    bool XTestInputInjector::isCompiledIn()
    {
        return true;
    }

    bool XTestInputInjector::open( string& outReason )
    {
        if ( _pDisplay != nullptr )
            return true;
        Display* const pDisplay = XOpenDisplay( nullptr );
        if ( pDisplay == nullptr )
        {
            outReason = "cannot open the X display (DISPLAY not set or the server is not reachable)";
            return false;
        }
        int eventBase = 0;
        int errorBase = 0;
        int major     = 0;
        int minor     = 0;
        if ( XTestQueryExtension( pDisplay, &eventBase, &errorBase, &major, &minor ) == 0 )
        {
            XCloseDisplay( pDisplay );
            outReason = "the X server has no XTest extension";
            return false;
        }
        _pDisplay = pDisplay;
        return true;
    }

    bool XTestInputInjector::moveTo( int32 screenX, int32 screenY )
    {
        if ( _pDisplay == nullptr )
            return false;
        Display* const pDisplay = static_cast<Display*>( _pDisplay );
        // 화면 번호 -1 = 현재 포인터가 있는 화면. 절대 좌표(루트 창 기준)로 옮긴다.
        XTestFakeMotionEvent( pDisplay, -1, screenX, screenY, CurrentTime );
        XFlush( pDisplay );
        return true;
    }

    bool XTestInputInjector::setButton( uint32 button, bool bPressed )
    {
        if ( _pDisplay == nullptr || button < 1 || button > 9 )
            return false;
        Display* const pDisplay = static_cast<Display*>( _pDisplay );
        XTestFakeButtonEvent( pDisplay, button, bPressed ? 1 : 0, CurrentTime ); // 1 == True. X11MacroUndef.h 가 True 매크로를 지운다.
        XFlush( pDisplay );
        return true;
    }

    bool XTestInputInjector::setKey( const string& keysymName, bool bPressed )
    {
        if ( _pDisplay == nullptr )
            return false;
        Display* const pDisplay = static_cast<Display*>( _pDisplay );
        const KeySym   keysym   = XStringToKeysym( keysymName.c_str() );
        if ( keysym == 0 ) // NoSymbol
            return false;
        const KeyCode keycode = XKeysymToKeycode( pDisplay, keysym );
        if ( keycode == 0 )
            return false;
        XTestFakeKeyEvent( pDisplay, keycode, bPressed ? 1 : 0, CurrentTime );
        XFlush( pDisplay );
        return true;
    }
} // namespace sw

#elif defined( SW_PLATFORM_LINUX ) && defined( SW_WITH_CLIENT_CODE )

namespace sw
{
    // `SW_ENABLE_XTEST=OFF` 이거나 libxtst 가 없다 — 폴백으로 입력을 흉내 내지 않고 실패를 돌려준다.
    XTestInputInjector::XTestInputInjector()
        : _pDisplay{ nullptr }
    {
    }
    XTestInputInjector::~XTestInputInjector() = default;

    bool XTestInputInjector::isCompiledIn()
    {
        return false;
    }
    bool XTestInputInjector::open( string& outReason )
    {
        outReason = "XTest injector not built (SW_ENABLE_XTEST=OFF or libxtst missing)";
        return false;
    }
    bool XTestInputInjector::moveTo( int32, int32 )
    {
        return false;
    }
    bool XTestInputInjector::setButton( uint32, bool )
    {
        return false;
    }
    bool XTestInputInjector::setKey( const string&, bool )
    {
        return false;
    }
} // namespace sw

#endif

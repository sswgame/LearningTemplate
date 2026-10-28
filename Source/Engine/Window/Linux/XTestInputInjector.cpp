#include "pch.h"

#include "Engine/Window/Linux/XTestInputInjector.h"

#include "Core/Console/DevCommandRegistry.h"
#include "Core/Container/StringUtil.h"

#if defined( SW_PLATFORM_LINUX ) && defined( SW_WITH_CLIENT_CODE ) && defined( SW_WITH_XTEST )

    #include "Core/Common/PlatformOsHeaders.h"
    #include "Core/Common/X11Headers.h"

    #include <X11/extensions/XTest.h>

    #include "Core/Common/X11MacroUndef.h" // XTest 헤더 뒤에 X11 매크로를 다시 지운다

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

#if SW_DEV_COMMANDS_ENABLED && defined( SW_PLATFORM_LINUX ) && defined( SW_WITH_CLIENT_CODE )

namespace sw
{
    namespace
    {
        /// 명령 사이에 연결을 유지한다(누른 채로 다음 명령에서 떼는 시험이 있다). 개발 콘솔(게임 스레드)에서만 부른다.
        XTestInputInjector& getInjector()
        {
            static XTestInputInjector s_injector;
            return s_injector;
        }

        /// 동작 이름(down · up · tap · click)을 읽습니다. @p outPress · @p outRelease 는 누름 · 뗌이 필요한지입니다.
        bool parseAction( const string& action, bool& outPress, bool& outRelease )
        {
            const string lower = StringUtil::toLower( action.c_str() );
            if ( lower == "down" )
            {
                outPress   = true;
                outRelease = false;
                return true;
            }
            if ( lower == "up" )
            {
                outPress   = false;
                outRelease = true;
                return true;
            }
            if ( lower == "tap" || lower == "click" )
            {
                outPress   = true;
                outRelease = true;
                return true;
            }
            return false;
        }

        bool runXTest( const vector<string>& listArgument, string& outReply )
        {
            if ( listArgument.empty() )
                return false;
            const string verb = StringUtil::toLower( listArgument[0].c_str() );

            // 인자부터 읽고(사용법 거절) 그다음에 서버에 닿는다.
            int32 x        = 0;
            int32 y        = 0;
            int32 button   = 0;
            bool  bPress   = false;
            bool  bRelease = false;
            if ( verb == "move" )
            {
                if ( listArgument.size() != 3 || StringUtil::parseInt( listArgument[1], x ) == false || StringUtil::parseInt( listArgument[2], y ) == false )
                    return false;
            }
            else if ( verb == "button" )
            {
                if ( listArgument.size() != 3 || StringUtil::parseInt( listArgument[1], button ) == false || button < 1 || button > 9 || parseAction( listArgument[2], bPress, bRelease ) == false )
                    return false;
            }
            else if ( verb == "key" )
            {
                if ( listArgument.size() != 3 || parseAction( listArgument[2], bPress, bRelease ) == false )
                    return false;
            }
            else
                return false;

            XTestInputInjector& injector = getInjector();
            string              reason;
            if ( injector.open( reason ) == false )
            {
                outReply = "XTest unavailable: " + reason;
                return true;
            }

            bool bOk = true;
            if ( verb == "move" )
                bOk = injector.moveTo( x, y );
            else if ( verb == "button" )
            {
                if ( bPress )
                    bOk = injector.setButton( static_cast<uint32>( button ), true );
                if ( bOk && bRelease )
                    bOk = injector.setButton( static_cast<uint32>( button ), false );
            }
            else
            {
                if ( bPress )
                    bOk = injector.setKey( listArgument[1], true );
                if ( bOk && bRelease )
                    bOk = injector.setKey( listArgument[1], false );
            }
            outReply = bOk ? "ok" : "failed (unknown key name or injection rejected)";
            return true;
        }
    } // namespace

    SW_DEV_COMMAND( XTestInput, "input.xtest", "input.xtest move <x> <y> | button <1-9> <down|up|click> | key <keysym> <down|up|tap>",
                    "Inject mouse / key input into the X server through XTest (screen coordinates; X11 window input checks)", &runXTest );
} // namespace sw

#endif

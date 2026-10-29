/**
 * @file XTestInputInjectorDevCommands.cpp
 * @brief XTest 입력 주입 명령 `input.xtest` 입니다(리눅스 클라이언트, 배포 구성은 `*DevCommands.cpp` 를 짓지 않는다).
 */
#include "pch.h"

#include "Core/Console/DevCommandRegistry.h"
#include "Core/Container/StringUtil.h"

#include "Engine/Window/Linux/XTestInputInjector.h"

#if defined( SW_PLATFORM_LINUX ) && defined( SW_WITH_CLIENT_CODE )

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

/**
 * @file XTestInputInjector.cpp
 * @brief X 서버에 마우스 · 키 입력을 주입하는 XTest 주입기입니다(X11 창 입력 시험용).
 * @details 서버가 진짜 장치처럼 받아들이는 입력이라 창 쪽 코드(좌표 · 키 코드 변환 · `XkbSetDetectableAutoRepeat` 자동 반복)가 실제 경로로 돈다.
 *          `XSendEvent` 로는 이 경로를 만들 수 없다(자동 반복은 서버가 키를 누르고 있는 동안 만든다).
 *          상용 엔진과 같은 선: 언리얼 Automation Driver 와 유니티 Input Test Fixture 도 앱 안 이벤트 합성이 아니라 장치 층에서 입력을 넣는다.
 *          `libxtst`(CMake `SW_ENABLE_XTEST`)는 선택 의존이다 — 꺼져 있으면 `isCompiledIn()` 이 false 이고 나머지는 실패를 돌려준다(폴백 없음).
 * @note 리눅스 클라이언트 전용입니다. 주입기는 자기 X 연결을 따로 연다(창과 무관). 개발 명령은 `input.xtest`.
 */
#include "pch.h"

#include "Core/Container/string.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/LifetimeScope.h"
#include "Core/String/StringUtil.h"
#include "Core/Subsystem/Subsystem.h"
#include "Core/Subsystem/SubsystemSlot.h"

#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Foundation/EngineSubsystemRegistration.h"
#include "Engine/Foundation/EngineSubsystems.h"

namespace sw
{
    /**
     * @class XTestInputInjector
     * @brief XTest 확장으로 X 서버에 입력을 주입합니다. 좌표는 루트 창(화면) 기준입니다.
     */
    class XTestInputInjector final : public Subsystem
    {
    public:
        /** @brief 서브시스템 이름입니다 — 엔진 범위 서비스 하나(개발 명령 `input.xtest` 가 찾는다). */
        static constexpr const utf8* kSubsystemName = "XTestInputInjector";
        SubsystemInitResult          initializeSubsystem() override { return SubsystemInitResult::Succeeded; }
        void                         shutdownSubsystem() override {}
        const utf8*                  getSubsystemName() const override { return kSubsystemName; }

        XTestInputInjector();
        ~XTestInputInjector();
        XTestInputInjector( const XTestInputInjector& )            = delete;
        XTestInputInjector& operator=( const XTestInputInjector& ) = delete;

        /** @brief 이 빌드에 XTest 가 들어 있는지(`SW_WITH_XTEST`)입니다. */
        [[nodiscard]] static bool isCompiledIn();

        /** @brief X 서버에 연결하고 XTest 확장이 있는지 확인합니다. 실패하면 그 이유입니다. */
        [[nodiscard]] expected<void> open();

        /** @brief 포인터를 화면 좌표로 옮깁니다. */
        [[nodiscard]] bool moveTo( int32 screenX, int32 screenY );
        /** @brief 마우스 버튼(1 왼쪽 · 2 가운데 · 3 오른쪽 · 4/5 휠 위/아래)을 누르거나 뗍니다. */
        [[nodiscard]] bool setButton( uint32 button, bool bPressed );
        /** @brief 키(X keysym 이름, 예: `a` · `Return` · `Shift_L`)를 누르거나 뗍니다. 이름이나 키 코드를 못 찾으면 false 입니다. */
        [[nodiscard]] bool setKey( const string& keysymName, bool bPressed );

    private:
        [[maybe_unused]] void* _pDisplay; ///< `Display*` — X11 헤더는 XTest 구성에서만 들어와 불투명하게 둔다. XTest 없는 구성(빈 구현)은 읽지 않는다.

        SW_SUBSYSTEM_SLOT( XTestInputInjector, Optional );
    };
} // namespace sw

#if defined( SW_WITH_XTEST )

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

    expected<void> XTestInputInjector::open()
    {
        if ( _pDisplay != nullptr )
            return {};
        Display* const pDisplay = XOpenDisplay( nullptr );
        if ( pDisplay == nullptr )
            return unexpected( Error{ ErrorKind::ExternalFailure, "cannot open the X display (DISPLAY not set or the server is not reachable)" } );
        int eventBase = 0;
        int errorBase = 0;
        int major     = 0;
        int minor     = 0;
        if ( XTestQueryExtension( pDisplay, &eventBase, &errorBase, &major, &minor ) == 0 )
        {
            XCloseDisplay( pDisplay );
            return unexpected( Error{ ErrorKind::Unsupported, "the X server has no XTest extension" } );
        }
        _pDisplay = pDisplay;
        return {};
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

#else

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
    expected<void> XTestInputInjector::open()
    {
        return unexpected( Error{ ErrorKind::Unsupported, "XTest injector not built (SW_ENABLE_XTEST=OFF or libxtst missing)" } );
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

// 등록 · 명령은 두 갈래(XTest 있음 · 없음)가 함께 쓴다. 서브시스템 칸은 헤더의 클래스 바로 뒤다.
namespace sw
{
    namespace
    {
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

            // 연결은 엔진 범위의 모듈 부분이 명령 사이에 유지한다(누른 채로 다음 명령에서 떼는 시험이 있다). 개발 콘솔(게임 스레드)에서만 부른다.
            XTestInputInjector* const pInjector = engine::findSubsystem<XTestInputInjector>();
            if ( pInjector == nullptr )
            {
                outReply = "XTest unavailable: no engine scope";
                return true;
            }
            XTestInputInjector& injector = *pInjector;
            if ( const expected<void> opened = injector.open(); opened.has_value() == false )
            {
                StringBuilder<constant::buffer::kMax256> reply;
                reply.appendFormat( "XTest unavailable: %#", opened.error().message() );
                outReply.assign( reply.view() );
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

    SW_ENGINE_SUBSYSTEM_WITH_SLOT( XTestInputInjector, HostOnly, Process, Shared, (), SW_ENGINE_DEFAULT_FACTORY( XTestInputInjector ) ); // 엔진 범위 서비스(아래 층이라 모듈 부분 `SW_MODULE_PART` 를 쓸 수 없다)

    SW_DEV_COMMAND( XTestInput, "input.xtest", "input.xtest move <x> <y> | button <1-9> <down|up|click> | key <keysym> <down|up|tap>",
                    "Inject mouse / key input into the X server through XTest (screen coordinates; X11 window input checks)", &runXTest );
} // namespace sw

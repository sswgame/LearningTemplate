#include "pch.h"

#include "Engine/Window/Web/WebWindow.h"

#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Window/WindowPlatform.h"

#include <emscripten/emscripten.h>

namespace sw
{
    namespace
    {
        struct WebWindowInternal
        {
            /** @brief CSS 변수 하나(셸 HTML 의 `--sw-safe-*` — `env(safe-area-inset-*)` 를 담는다)를 CSS 픽셀로 읽습니다. 없으면 0 입니다. */
            static float32 readSafeInset( const utf8* pVariable )
            {
                StringBuilder<constant::buffer::kMax256> script;
                script.appendFormat( "parseFloat(getComputedStyle(document.documentElement).getPropertyValue('%#'))||0", pVariable );
                return static_cast<float32>( emscripten_run_script_int( script.c_str() ) );
            }

            /** @brief CSS 좌표(대상 기준)를 캔버스 물리 픽셀로 옮깁니다. */
            static int32 toPixel( double cssValue, float32 ratio ) { return static_cast<int32>( MathUtil::round( static_cast<float32>( cssValue ) * ratio ) ); }
        };

        const WindowPlatformFactory s_webWindowFactory{
            []() -> unique_ptr<IWindow>
        { return make_unique<WebWindow>(); },
            []() -> unique_ptr<ISplashWindow>
        { return nullptr; },
            []()
        { return false; } }; // 스플래시는 페이지(셸 HTML)가 그린다 — 엔진이 띄울 창이 없다
        const WindowPlatform::Registrar s_webWindowRegistrar{ s_webWindowFactory };
    } // namespace

    WebWindow::WebWindow()
        : CallbackInputWindow{}
        , _devicePixelRatio{ 1.0f }
        , _bCallbacksBound{ SW_FALSE }
        , _bPointerLocked{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    WebWindow::~WebWindow()
    {
        destroy();
    }

    bool WebWindow::initializeWindow( const utf8* pTitle, uint32 width, uint32 height )
    {
        (void)width;
        (void)height; // 캔버스 크기는 페이지가 정한다(CSS) — 설정의 창 크기를 따르지 않는다
        if ( storeTitle( pTitle ) == false )
            return false;
        applyTitle();
        syncCanvasSize();
        bindCallbacks();
        setFocusState( true );
        return true;
    }

    void WebWindow::destroy()
    {
        unbindCallbacks();
    }

    void WebWindow::applyTitle()
    {
        // 제목은 엔진이 쓴 글이다 — JS 글 안에서 따옴표 · 역슬래시만 막는다.
        string escaped = "document.title=\"";
        for ( const utf8 character : string_view( getTitle() ) )
        {
            if ( character == '"' || character == '\\' )
                escaped += '\\';
            escaped += character;
        }
        escaped += "\"";
        emscripten_run_script( escaped.c_str() );
    }

    void WebWindow::syncCanvasSize()
    {
        double cssWidth  = 0.0;
        double cssHeight = 0.0;
        if ( emscripten_get_element_css_size( kCanvasSelector, &cssWidth, &cssHeight ) != EMSCRIPTEN_RESULT_SUCCESS )
            return;
        _devicePixelRatio   = static_cast<float32>( emscripten_get_device_pixel_ratio() );
        const uint32 width  = static_cast<uint32>( MathUtil::max( 1.0, cssWidth * _devicePixelRatio ) );
        const uint32 height = static_cast<uint32>( MathUtil::max( 1.0, cssHeight * _devicePixelRatio ) );
        (void)emscripten_set_canvas_element_size( kCanvasSelector, static_cast<int>( width ), static_cast<int>( height ) );
        const float4 safeInsets{ WebWindowInternal::readSafeInset( "--sw-safe-left" ) * _devicePixelRatio, WebWindowInternal::readSafeInset( "--sw-safe-top" ) * _devicePixelRatio,
                                 WebWindowInternal::readSafeInset( "--sw-safe-right" ) * _devicePixelRatio, WebWindowInternal::readSafeInset( "--sw-safe-bottom" ) * _devicePixelRatio };
        applyPlatformResize( width, height, _devicePixelRatio, safeInsets );
    }

    void WebWindow::bindCallbacks()
    {
        if ( _bCallbacksBound != SW_FALSE )
            return;
        // 마우스 · 손가락은 캔버스, 키 · 크기 · 보임은 창 전체가 받는다(캔버스는 키 포커스를 잘 받지 않는다).
        (void)emscripten_set_mousemove_callback( kCanvasSelector, this, EM_TRUE, &WebWindow::onMouse );
        (void)emscripten_set_mousedown_callback( kCanvasSelector, this, EM_TRUE, &WebWindow::onMouse );
        (void)emscripten_set_mouseup_callback( kCanvasSelector, this, EM_TRUE, &WebWindow::onMouse );
        (void)emscripten_set_wheel_callback( kCanvasSelector, this, EM_TRUE, &WebWindow::onWheel );
        (void)emscripten_set_touchstart_callback( kCanvasSelector, this, EM_TRUE, &WebWindow::onTouch );
        (void)emscripten_set_touchmove_callback( kCanvasSelector, this, EM_TRUE, &WebWindow::onTouch );
        (void)emscripten_set_touchend_callback( kCanvasSelector, this, EM_TRUE, &WebWindow::onTouch );
        (void)emscripten_set_touchcancel_callback( kCanvasSelector, this, EM_TRUE, &WebWindow::onTouch );
        (void)emscripten_set_keydown_callback( EMSCRIPTEN_EVENT_TARGET_WINDOW, this, EM_TRUE, &WebWindow::onKey );
        (void)emscripten_set_keyup_callback( EMSCRIPTEN_EVENT_TARGET_WINDOW, this, EM_TRUE, &WebWindow::onKey );
        (void)emscripten_set_keypress_callback( EMSCRIPTEN_EVENT_TARGET_WINDOW, this, EM_TRUE, &WebWindow::onKey );
        (void)emscripten_set_resize_callback( EMSCRIPTEN_EVENT_TARGET_WINDOW, this, EM_TRUE, &WebWindow::onResize );
        (void)emscripten_set_visibilitychange_callback( this, EM_TRUE, &WebWindow::onVisibility );
        (void)emscripten_set_focus_callback( EMSCRIPTEN_EVENT_TARGET_WINDOW, this, EM_TRUE, &WebWindow::onFocus );
        (void)emscripten_set_blur_callback( EMSCRIPTEN_EVENT_TARGET_WINDOW, this, EM_TRUE, &WebWindow::onFocus );
        (void)emscripten_set_pointerlockchange_callback( EMSCRIPTEN_EVENT_TARGET_DOCUMENT, this, EM_TRUE, &WebWindow::onPointerLockChange );
        _bCallbacksBound = SW_TRUE;
    }

    void WebWindow::unbindCallbacks()
    {
        if ( _bCallbacksBound == SW_FALSE )
            return;
        (void)emscripten_html5_remove_all_event_listeners();
        _bCallbacksBound = SW_FALSE;
    }

    bool WebWindow::setPointerLock( WindowPointerLock lock )
    {
        if ( lock == WindowPointerLock::Released )
        {
            if ( _bPointerLocked != SW_FALSE )
                (void)emscripten_exit_pointerlock();
            return true;
        }
        if ( lock == WindowPointerLock::Confined )
            return false; // 브라우저에는 가두기가 없다 — 입력 관리자가 한 번 알린다
        if ( _bPointerLocked != SW_FALSE )
            return true;
        // 사용자 입력 처리 밖에서 청하면 브라우저가 거절한다 — defer 면 다음 입력 콜백 안에서 청한다(DEFERRED 도 받아들인 것이다).
        // https://emscripten.org/docs/api_reference/html5.h.html#c.emscripten_request_pointerlock
        const EMSCRIPTEN_RESULT result = emscripten_request_pointerlock( kCanvasSelector, EM_TRUE );
        return result == EMSCRIPTEN_RESULT_SUCCESS || result == EMSCRIPTEN_RESULT_DEFERRED;
    }

    EM_BOOL WebWindow::onPointerLockChange( int eventType, const EmscriptenPointerlockChangeEvent* pEvent, void* pUserData )
    {
        (void)eventType;
        static_cast<WebWindow*>( pUserData )->_bPointerLocked = pEvent->isActive ? SW_TRUE : SW_FALSE;
        return EM_FALSE;
    }

    EM_BOOL WebWindow::onMouse( int eventType, const EmscriptenMouseEvent* pEvent, void* pUserData )
    {
        WebWindow* const pWindow = static_cast<WebWindow*>( pUserData );
        if ( pWindow->_bPointerLocked != SW_FALSE && eventType == EMSCRIPTEN_EVENT_MOUSEMOVE )
        {
            // 잠긴 동안 위치는 서 있다 — 이동량(movementX/Y, CSS 픽셀)만 물리 픽셀로 넘긴다. 버튼 이벤트는 그대로 아래 길이다.
            PlatformInputEvent relative{};
            relative._kind      = PlatformInputKind::PointerRelative;
            relative._relativeX = static_cast<float32>( pEvent->movementX ) * pWindow->_devicePixelRatio;
            relative._relativeY = static_cast<float32>( pEvent->movementY ) * pWindow->_devicePixelRatio;
            pWindow->queueInput( relative );
            return EM_TRUE;
        }
        PlatformInputEvent input{};
        input._x      = WebWindowInternal::toPixel( pEvent->targetX, pWindow->_devicePixelRatio );
        input._y      = WebWindowInternal::toPixel( pEvent->targetY, pWindow->_devicePixelRatio );
        input._button = static_cast<uint8>( pEvent->button );
        input._kind   = eventType == EMSCRIPTEN_EVENT_MOUSEDOWN ? PlatformInputKind::PointerDown
                                                                : ( eventType == EMSCRIPTEN_EVENT_MOUSEUP ? PlatformInputKind::PointerUp : PlatformInputKind::PointerMove );
        pWindow->queueInput( input );
        return EM_TRUE;
    }

    EM_BOOL WebWindow::onWheel( int eventType, const EmscriptenWheelEvent* pEvent, void* pUserData )
    {
        (void)eventType;
        PlatformInputEvent input{};
        input._kind = PlatformInputKind::Wheel;
        // DOM 은 아래가 + 이고 줄 · 픽셀 단위가 섞인다 — 한 칸(줄 3 · 100 픽셀 남짓)을 1 로 접는다(브라우저마다 다르다, 기억).
        const double lines = pEvent->deltaMode == DOM_DELTA_PIXEL ? pEvent->deltaY / 100.0 : ( pEvent->deltaMode == DOM_DELTA_LINE ? pEvent->deltaY / 3.0 : pEvent->deltaY );
        input._wheel       = static_cast<float32>( -lines );
        static_cast<WebWindow*>( pUserData )->queueInput( input );
        return EM_TRUE;
    }

    EM_BOOL WebWindow::onKey( int eventType, const EmscriptenKeyboardEvent* pEvent, void* pUserData )
    {
        WebWindow* const   pWindow = static_cast<WebWindow*>( pUserData );
        PlatformInputEvent input{};
        if ( eventType == EMSCRIPTEN_EVENT_KEYPRESS )
        {
            // 확정 글자 — DOM key 가 한 글자(조합이 아닌 것)일 때만 글 입력이다("Enter" 같은 이름은 키다).
            const string_view key( pEvent->key );
            if ( key.empty() || key.size() > 4 || ( key.size() > 1 && static_cast<uint8>( key[0] ) < 0x80 ) )
                return EM_FALSE;
            input._kind = PlatformInputKind::Text;
            Memory::copy( input._arrText, key.data(), MathUtil::min( key.size(), sizeof( input._arrText ) - 1 ) );
            pWindow->queueInput( input );
            return EM_TRUE;
        }
        input._kind    = eventType == EMSCRIPTEN_EVENT_KEYDOWN ? PlatformInputKind::KeyDown : PlatformInputKind::KeyUp;
        input._bRepeat = pEvent->repeat ? 1u : 0u;
        Memory::copy( input._arrText, pEvent->code, MathUtil::min( string_view( pEvent->code ).size(), sizeof( input._arrText ) - 1 ) );
        pWindow->queueInput( input );
        // 글자를 내는 키는 브라우저가 keypress 를 이어 보내야 한다 — 기본 동작을 막으면 글이 오지 않는다. 탭 · 백스페이스 같은 탐색 키만 막는다.
        const string_view code( pEvent->code );
        return code == "Tab" || code == "Backspace" || code == "Space" || StringUtil::startsWith( code, "Arrow" ) ? EM_TRUE : EM_FALSE;
    }

    EM_BOOL WebWindow::onTouch( int eventType, const EmscriptenTouchEvent* pEvent, void* pUserData )
    {
        WebWindow* const        pWindow = static_cast<WebWindow*>( pUserData );
        const PlatformInputKind kind    = eventType == EMSCRIPTEN_EVENT_TOUCHSTART ? PlatformInputKind::TouchDown
                                                                                   : ( eventType == EMSCRIPTEN_EVENT_TOUCHMOVE ? PlatformInputKind::TouchMove : PlatformInputKind::TouchUp );
        for ( int touchIndex = 0; touchIndex < pEvent->numTouches; ++touchIndex )
        {
            const EmscriptenTouchPoint& touch = pEvent->touches[touchIndex];
            if ( touch.isChanged == false )
                continue;
            PlatformInputEvent input{};
            input._kind         = kind;
            input._pointerIndex = static_cast<uint16>( touch.identifier & 0xF ); // 브라우저 식별자는 누적 번호다 — 터치 장치의 손가락 칸(16)으로 접는다
            input._x            = WebWindowInternal::toPixel( touch.targetX, pWindow->_devicePixelRatio );
            input._y            = WebWindowInternal::toPixel( touch.targetY, pWindow->_devicePixelRatio );
            pWindow->queueInput( input );
        }
        return EM_TRUE; // 브라우저의 스크롤 · 확대 · 마우스 흉내를 막는다
    }

    EM_BOOL WebWindow::onResize( int eventType, const EmscriptenUiEvent* pEvent, void* pUserData )
    {
        (void)eventType;
        (void)pEvent;
        static_cast<WebWindow*>( pUserData )->syncCanvasSize(); // 창 크기 · 회전 · 확대(배율)가 모두 여기로 온다
        return EM_FALSE;
    }

    EM_BOOL WebWindow::onVisibility( int eventType, const EmscriptenVisibilityChangeEvent* pEvent, void* pUserData )
    {
        (void)eventType;
        WebWindow* const pWindow = static_cast<WebWindow*>( pUserData );
        pWindow->notifyLifecycleEvent( pEvent->hidden ? WindowLifecycleEvent::AppSuspended : WindowLifecycleEvent::AppResumed );
        return EM_FALSE;
    }

    EM_BOOL WebWindow::onFocus( int eventType, const EmscriptenFocusEvent* pEvent, void* pUserData )
    {
        (void)pEvent;
        static_cast<WebWindow*>( pUserData )->setFocusState( eventType == EMSCRIPTEN_EVENT_FOCUS );
        return EM_FALSE;
    }
} // namespace sw

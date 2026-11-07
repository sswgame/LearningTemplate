#include "pch.h"

#include "Engine/Window/Android/AndroidWindow.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Window/WindowPlatform.h"

#if defined( SW_PLATFORM_ANDROID )
    #include <android/configuration.h>
    #include <android/input.h>
    #include <android/native_window.h>
    #include <android_native_app_glue.h>
    #include <jni.h>

namespace sw
{
    namespace
    {
        struct AndroidWindowInternal
        {
            static android_app*   s_pApp;
            static AndroidWindow* s_pWindow; ///< 명령 · 입력 콜백이 찾는 창(한 액티비티에 하나)

            /** @brief 창이 서기 전의 명령 처리입니다 — 표면이 생겼는지만 본다(`waitForInitialSurface`). */
            static void onEarlyCommand( android_app* pApp, int32 command )
            {
                (void)pApp;
                (void)command; // 표면은 `pApp->window` 가 알려 준다 — 글루가 명령 전에 채운다
            }

            /** @brief ALooper 를 한 번 비웁니다. @p timeoutMs 가 0 이면 기다리지 않는다. 끝내라고 하면 false 입니다. */
            static bool pumpLooper( int32 timeoutMs )
            {
                int32                events  = 0;
                android_poll_source* pSource = nullptr;
                while ( ALooper_pollOnce( timeoutMs, nullptr, &events, reinterpret_cast<void**>( &pSource ) ) >= 0 )
                {
                    if ( pSource != nullptr )
                        pSource->process( s_pApp, pSource );
                    if ( s_pApp->destroyRequested != 0 )
                        return false;
                    timeoutMs = 0; // 첫 기다림 뒤에는 쌓인 것만 비운다
                }
                return s_pApp->destroyRequested == 0;
            }
        };

        android_app*   AndroidWindowInternal::s_pApp    = nullptr;
        AndroidWindow* AndroidWindowInternal::s_pWindow = nullptr;

        const WindowPlatformFactory s_androidWindowFactory{
            []() -> unique_ptr<IWindow>
        { return make_unique<AndroidWindow>(); },
            []() -> unique_ptr<ISplashWindow>
        { return nullptr; },
            []()
        { return false; } }; // 스플래시는 액티비티 테마(windowSplashScreen)가 그린다
        const WindowPlatform::Registrar s_androidWindowRegistrar{ s_androidWindowFactory };
    } // namespace

    AndroidWindow::AndroidWindow()
        : CallbackInputWindow{}
    {
    }

    AndroidWindow::~AndroidWindow()
    {
        destroy();
    }

    void AndroidWindow::setApplication( android_app* pApp )
    {
        AndroidWindowInternal::s_pApp = pApp;
    }

    android_app* AndroidWindow::getApplication()
    {
        return AndroidWindowInternal::s_pApp;
    }

    bool AndroidWindow::waitForInitialSurface()
    {
        android_app* const pApp = AndroidWindowInternal::s_pApp;
        if ( pApp == nullptr )
            return false;
        pApp->onAppCmd                 = &AndroidWindowInternal::onEarlyCommand;
        constexpr int32 kPollTimeoutMs = 100;
        while ( pApp->window == nullptr )
        {
            if ( AndroidWindowInternal::pumpLooper( kPollTimeoutMs ) == false )
                return false;
        }
        return true;
    }

    bool AndroidWindow::initializeWindow( const utf8* pTitle, uint32 width, uint32 height )
    {
        (void)width;
        (void)height; // 표면 크기는 기기가 정한다(전체 화면) — 설정의 창 크기를 따르지 않는다
        android_app* const pApp = AndroidWindowInternal::s_pApp;
        if ( storeTitle( pTitle ) == false || pApp == nullptr || pApp->window == nullptr )
        {
            SW_LOG_ERROR_ID( "AndroidWindow.NoNativeWindow", "Android window needs the native window first (AndroidWindow::waitForInitialSurface before App::initialize)" );
            return false;
        }
        AndroidWindowInternal::s_pWindow = this;
        pApp->userData                   = this;
        pApp->onAppCmd                   = &AndroidWindow::onAppCommand;
        pApp->onInputEvent               = &AndroidWindow::onInputEvent;
        syncSurfaceMetrics();
        setFocusState( true );
        return true;
    }

    void AndroidWindow::destroy()
    {
        android_app* const pApp = AndroidWindowInternal::s_pApp;
        if ( pApp != nullptr && AndroidWindowInternal::s_pWindow == this )
        {
            pApp->onAppCmd     = &AndroidWindowInternal::onEarlyCommand;
            pApp->onInputEvent = nullptr;
            pApp->userData     = nullptr;
        }
        if ( AndroidWindowInternal::s_pWindow == this )
            AndroidWindowInternal::s_pWindow = nullptr;
    }

    bool AndroidWindow::processMessages()
    {
        // 명령 · 입력 콜백이 이 안에서 불린다(같은 스레드) — 모인 입력은 바탕이 처리기로 넘긴다.
        if ( AndroidWindowInternal::pumpLooper( 0 ) == false )
            requestClose();
        return CallbackInputWindow::processMessages();
    }

    void* AndroidWindow::getNativeHandle() const
    {
        android_app* const pApp = AndroidWindowInternal::s_pApp;
        return pApp != nullptr ? pApp->window : nullptr;
    }

    void AndroidWindow::syncSurfaceMetrics()
    {
        android_app* const pApp = AndroidWindowInternal::s_pApp;
        if ( pApp == nullptr || pApp->window == nullptr )
            return;
        const int32   width   = ANativeWindow_getWidth( pApp->window );
        const int32   height  = ANativeWindow_getHeight( pApp->window );
        const int32   density = pApp->config != nullptr ? AConfiguration_getDensity( pApp->config ) : ACONFIGURATION_DENSITY_MEDIUM;
        const float32 scale   = static_cast<float32>( density > 0 ? density : ACONFIGURATION_DENSITY_MEDIUM ) / static_cast<float32>( ACONFIGURATION_DENSITY_MEDIUM ); // dp 기준 160
        applyPlatformResize( static_cast<uint32>( MathUtil::max( width, 0 ) ), static_cast<uint32>( MathUtil::max( height, 0 ) ), scale, readSafeInsets() );
    }

    float4 AndroidWindow::readSafeInsets() const
    {
        android_app* const pApp = AndroidWindowInternal::s_pApp;
        if ( pApp == nullptr || pApp->activity == nullptr )
            return float4{};
        // activity.getWindow().getDecorView().getRootWindowInsets() — 상태 바 · 내비게이션 바 · 노치가 든 시스템 창 여백(API 23+, getSystemWindowInset* 는 30 부터 낡았지만 남아 있다, 기억).
        JavaVM* const pVM  = pApp->activity->vm;
        JNIEnv*       pEnv = nullptr;
        if ( pVM->AttachCurrentThread( &pEnv, nullptr ) != JNI_OK || pEnv == nullptr )
            return float4{};
        float4        insets{};
        const jobject activity  = pApp->activity->clazz;
        const jclass  activityC = pEnv->GetObjectClass( activity );
        const jobject window    = pEnv->CallObjectMethod( activity, pEnv->GetMethodID( activityC, "getWindow", "()Landroid/view/Window;" ) );
        const jobject decorView = window != nullptr ? pEnv->CallObjectMethod( window, pEnv->GetMethodID( pEnv->GetObjectClass( window ), "getDecorView", "()Landroid/view/View;" ) ) : nullptr;
        const jobject rootInset = decorView != nullptr ? pEnv->CallObjectMethod( decorView, pEnv->GetMethodID( pEnv->GetObjectClass( decorView ), "getRootWindowInsets", "()Landroid/view/WindowInsets;" ) ) : nullptr;
        if ( rootInset != nullptr )
        {
            const jclass insetC = pEnv->GetObjectClass( rootInset );
            const auto   read   = [&]( const utf8* pMethod )
            { return static_cast<float32>( pEnv->CallIntMethod( rootInset, pEnv->GetMethodID( insetC, pMethod, "()I" ) ) ); };
            insets = float4{ read( "getSystemWindowInsetLeft" ), read( "getSystemWindowInsetTop" ), read( "getSystemWindowInsetRight" ), read( "getSystemWindowInsetBottom" ) };
        }
        if ( pEnv->ExceptionCheck() )
            pEnv->ExceptionClear(); // 메서드가 없는 옛 판 — 여백 0 으로 그린다(그리기는 이어 간다)
        pVM->DetachCurrentThread();
        return insets;
    }

    void AndroidWindow::onAppCommand( android_app* pApp, int32 command )
    {
        AndroidWindow* const pWindow = static_cast<AndroidWindow*>( pApp->userData );
        if ( pWindow == nullptr )
            return;
        switch ( command )
        {
            case APP_CMD_TERM_WINDOW:
                // 이 콜백이 돌아오면 ANativeWindow 가 사라진다 — 수명 구독자(렌더러)가 스왑체인 · 표면을 지금 놓는다.
                pWindow->notifyLifecycleEvent( WindowLifecycleEvent::SurfaceLost );
                break;
            case APP_CMD_INIT_WINDOW:
                pWindow->syncSurfaceMetrics();
                pWindow->notifyLifecycleEvent( WindowLifecycleEvent::SurfaceRestored );
                break;
            case APP_CMD_WINDOW_RESIZED:
            case APP_CMD_CONFIG_CHANGED:
            case APP_CMD_CONTENT_RECT_CHANGED:
                pWindow->syncSurfaceMetrics(); // 회전 · 분할 화면 · 접힘
                break;
            case APP_CMD_PAUSE:
                pWindow->notifyLifecycleEvent( WindowLifecycleEvent::AppSuspended );
                break;
            case APP_CMD_RESUME:
                pWindow->notifyLifecycleEvent( WindowLifecycleEvent::AppResumed );
                break;
            case APP_CMD_GAINED_FOCUS:
            case APP_CMD_LOST_FOCUS:
                pWindow->setFocusState( command == APP_CMD_GAINED_FOCUS );
                break;
            case APP_CMD_DESTROY:
                pWindow->requestClose();
                break;
            default:
                break; // 저장 상태 · 메모리 부족 · 시작 · 멈춤은 엔진이 받지 않는다
        }
    }

    int32 AndroidWindow::onInputEvent( android_app* pApp, AInputEvent* pEvent )
    {
        AndroidWindow* const pWindow = static_cast<AndroidWindow*>( pApp->userData );
        if ( pWindow == nullptr )
            return 0;
        if ( AInputEvent_getType( pEvent ) == AINPUT_EVENT_TYPE_KEY )
        {
            const int32        action = AKeyEvent_getAction( pEvent );
            PlatformInputEvent input{};
            input._kind    = action == AKEY_EVENT_ACTION_UP ? PlatformInputKind::KeyUp : PlatformInputKind::KeyDown;
            input._keyCode = AKeyEvent_getKeyCode( pEvent );
            input._bRepeat = AKeyEvent_getRepeatCount( pEvent ) > 0 ? 1u : 0u;
            pWindow->queueInput( input );
            return 1; // 뒤로 가기를 엔진이 받는다(Escape) — 시스템이 액티비티를 닫지 않게
        }
        if ( AInputEvent_getType( pEvent ) != AINPUT_EVENT_TYPE_MOTION )
            return 0;
        const int32  action      = AMotionEvent_getAction( pEvent );
        const int32  maskAction  = action & AMOTION_EVENT_ACTION_MASK;
        const size_t actionIndex = static_cast<size_t>( ( action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK ) >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT );
        const size_t count       = AMotionEvent_getPointerCount( pEvent );
        for ( size_t pointer = 0; pointer < count; ++pointer )
        {
            const bool        bActionPointer = pointer == actionIndex;
            PlatformInputKind kind           = PlatformInputKind::TouchMove;
            if ( maskAction == AMOTION_EVENT_ACTION_DOWN || ( maskAction == AMOTION_EVENT_ACTION_POINTER_DOWN && bActionPointer ) )
                kind = PlatformInputKind::TouchDown;
            else if ( maskAction == AMOTION_EVENT_ACTION_UP || maskAction == AMOTION_EVENT_ACTION_CANCEL || ( maskAction == AMOTION_EVENT_ACTION_POINTER_UP && bActionPointer ) )
                kind = PlatformInputKind::TouchUp;
            else if ( maskAction != AMOTION_EVENT_ACTION_MOVE )
                continue; // 다른 손가락의 누름 · 뗌 이벤트에서 그 손가락은 움직이지 않았다
            PlatformInputEvent input{};
            input._kind         = kind;
            input._pointerIndex = static_cast<uint16>( AMotionEvent_getPointerId( pEvent, pointer ) & 0xF );
            input._x            = static_cast<int32>( AMotionEvent_getX( pEvent, pointer ) );
            input._y            = static_cast<int32>( AMotionEvent_getY( pEvent, pointer ) );
            pWindow->queueInput( input );
        }
        return 1;
    }
} // namespace sw

#endif

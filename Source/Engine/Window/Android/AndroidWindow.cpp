#include "pch.h"

#include "Engine/Window/Android/AndroidWindow.h"

#include "Core/Container/vector.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Window/WindowPlatform.h"

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

            static constexpr int32  kEmptyGamepadSlot    = -1;
            static constexpr uint32 kGamepadScanInterval = 60; ///< 패드 장치 대조 간격(메시지 처리 수 — 60 Hz 에서 1 초). JNI 왕복이라 프레임마다 하지 않는다

            /** @brief 패드 · 조이스틱 입력 장치인지입니다(https://developer.android.com/develop/ui/views/touch-and-input/game-controllers/controller-input — 출처 비트가 장치 종류를 모두 담는다). */
            static bool isGamepadSource( int32 source )
            {
                return ( source & AINPUT_SOURCE_GAMEPAD ) == AINPUT_SOURCE_GAMEPAD || ( source & AINPUT_SOURCE_JOYSTICK ) == AINPUT_SOURCE_JOYSTICK;
            }

            /** @brief 지금 스레드를 JVM 에 붙입니다. 실패하면 nullptr 입니다(부른 쪽이 `DetachCurrentThread` 한다). */
            static JNIEnv* attachThread()
            {
                if ( s_pApp == nullptr || s_pApp->activity == nullptr )
                    return nullptr;
                JNIEnv* pEnv = nullptr;
                return s_pApp->activity->vm->AttachCurrentThread( &pEnv, nullptr ) == JNI_OK ? pEnv : nullptr;
            }

            /** @brief 지금 붙은 패드 · 조이스틱 입력 장치 번호를 @p outListID 에 담습니다(`InputDevice.getDeviceIds` · `getSources`). 못 읽으면 false 입니다. */
            static bool readGamepadDeviceIds( vector<int32>& outListID )
            {
                outListID.clear();
                JNIEnv* const pEnv = attachThread();
                if ( pEnv == nullptr )
                    return false;
                const jclass    deviceC    = pEnv->FindClass( "android/view/InputDevice" );
                const jmethodID getIds     = deviceC != nullptr ? pEnv->GetStaticMethodID( deviceC, "getDeviceIds", "()[I" ) : nullptr;
                const jmethodID getDevice  = deviceC != nullptr ? pEnv->GetStaticMethodID( deviceC, "getDevice", "(I)Landroid/view/InputDevice;" ) : nullptr;
                const jmethodID getSources = deviceC != nullptr ? pEnv->GetMethodID( deviceC, "getSources", "()I" ) : nullptr;
                const jintArray arrID      = getIds != nullptr ? static_cast<jintArray>( pEnv->CallStaticObjectMethod( deviceC, getIds ) ) : nullptr;
                const jsize     count      = arrID != nullptr ? pEnv->GetArrayLength( arrID ) : 0;
                jint* const     pId        = arrID != nullptr ? pEnv->GetIntArrayElements( arrID, nullptr ) : nullptr;
                for ( jsize index = 0; pId != nullptr && getDevice != nullptr && getSources != nullptr && index < count; ++index )
                {
                    const jobject device = pEnv->CallStaticObjectMethod( deviceC, getDevice, pId[index] );
                    if ( device == nullptr )
                        continue;
                    if ( isGamepadSource( pEnv->CallIntMethod( device, getSources ) ) )
                        outListID.push_back( static_cast<int32>( pId[index] ) );
                    pEnv->DeleteLocalRef( device );
                }
                if ( pId != nullptr )
                    pEnv->ReleaseIntArrayElements( arrID, pId, JNI_ABORT );
                const bool bFailed = pEnv->ExceptionCheck() || getIds == nullptr;
                if ( pEnv->ExceptionCheck() )
                    pEnv->ExceptionClear();
                s_pApp->activity->vm->DetachCurrentThread();
                return bFailed == false;
            }

            /** @brief 창이 서기 전의 명령 처리입니다 — 표면이 생겼는지만 본다(`waitForInitialSurface`). */
            static void onEarlyCommand( android_app* pApp, int32 command )
            {
                (void)pApp;
                (void)command; // 표면은 `pApp->window` 가 알려 준다 — 글루가 명령 전에 채운다
            }

            /** @brief ALooper 를 한 번 비웁니다. @p timeoutMs 가 0 이면 기다리지 않는다. 끝내라고 하면 false 입니다. */
            static bool pollLooper( int32 timeoutMs )
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
        , _arrGamepadDeviceID{}
        , _gamepadScanCountdown{ 0 }
    {
        for ( int32& deviceId : _arrGamepadDeviceID )
        {
            deviceId = AndroidWindowInternal::kEmptyGamepadSlot;
        }
    }

    string AndroidWindow::readIntentArguments()
    {
        // activity.getIntent().getStringExtra( "args" ) — https://developer.android.com/reference/android/content/Intent#getStringExtra(java.lang.String)
        JNIEnv* const pEnv = AndroidWindowInternal::attachThread();
        if ( pEnv == nullptr )
            return {};
        const jobject activity  = AndroidWindowInternal::s_pApp->activity->clazz;
        const jobject intent    = pEnv->CallObjectMethod( activity, pEnv->GetMethodID( pEnv->GetObjectClass( activity ), "getIntent", "()Landroid/content/Intent;" ) );
        const jstring extraName = pEnv->NewStringUTF( kIntentArgumentsExtra );
        const jstring extra     = intent != nullptr ? static_cast<jstring>( pEnv->CallObjectMethod( intent, pEnv->GetMethodID( pEnv->GetObjectClass( intent ), "getStringExtra", "(Ljava/lang/String;)Ljava/lang/String;" ),
                                                                                                    extraName ) )
                                                    : nullptr;
        string        arguments;
        const utf8*   pText = extra != nullptr ? pEnv->GetStringUTFChars( extra, nullptr ) : nullptr;
        if ( pText != nullptr )
        {
            arguments = pText;
            pEnv->ReleaseStringUTFChars( extra, pText );
        }
        if ( pEnv->ExceptionCheck() )
        {
            pEnv->ExceptionClear();
            SW_LOG_ERROR_ID( "AndroidWindow.IntentArgumentsFailed", "Android: reading the launch intent's '%#' extra threw - starting without a command line", kIntentArgumentsExtra );
        }
        AndroidWindowInternal::s_pApp->activity->vm->DetachCurrentThread();
        return arguments;
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
            if ( AndroidWindowInternal::pollLooper( kPollTimeoutMs ) == false )
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
        if ( AndroidWindowInternal::pollLooper( 0 ) == false )
            requestClose();
        if ( _gamepadScanCountdown == 0 )
        {
            syncGamepadDevices();
            _gamepadScanCountdown = AndroidWindowInternal::kGamepadScanInterval;
        }
        --_gamepadScanCountdown;
        return CallbackInputWindow::processMessages();
    }

    optional<uint32> AndroidWindow::findOrAssignGamepadSlot( int32 deviceId )
    {
        optional<uint32> emptySlot;
        for ( uint32 slot = 0; slot < kPlatformGamepadSlotCount; ++slot )
        {
            if ( _arrGamepadDeviceID[slot] == deviceId )
                return slot;
            if ( emptySlot.has_value() == false && _arrGamepadDeviceID[slot] == AndroidWindowInternal::kEmptyGamepadSlot )
                emptySlot = slot;
        }
        if ( emptySlot.has_value() == false )
        {
            SW_LOG_WARNING_ID( "AndroidWindow.GamepadSlotsFull", "Android: gamepad device %# has no free slot (%# slots) - its input is ignored", deviceId, kPlatformGamepadSlotCount );
            return nullopt;
        }
        _arrGamepadDeviceID[*emptySlot] = deviceId;
        PlatformInputEvent connected{};
        connected._kind         = PlatformInputKind::GamepadConnected;
        connected._pointerIndex = static_cast<uint16>( *emptySlot );
        queueInput( connected );
        return emptySlot;
    }

    void AndroidWindow::syncGamepadDevices()
    {
        vector<int32> listPresentID;
        if ( AndroidWindowInternal::readGamepadDeviceIds( listPresentID ) == false )
            return; // 못 읽은 차례는 칸을 건드리지 않는다 — 없다고 보고 끊으면 붙은 패드가 끊긴다
        for ( uint32 slot = 0; slot < kPlatformGamepadSlotCount; ++slot )
        {
            const int32 deviceId = _arrGamepadDeviceID[slot];
            if ( deviceId == AndroidWindowInternal::kEmptyGamepadSlot || std::find( listPresentID.begin(), listPresentID.end(), deviceId ) != listPresentID.end() )
                continue;
            _arrGamepadDeviceID[slot] = AndroidWindowInternal::kEmptyGamepadSlot;
            PlatformInputEvent disconnected{};
            disconnected._kind         = PlatformInputKind::GamepadDisconnected;
            disconnected._pointerIndex = static_cast<uint16>( slot );
            queueInput( disconnected );
        }
        for ( const int32 deviceId : listPresentID )
        {
            (void)findOrAssignGamepadSlot( deviceId ); // 입력 전에 붙은 패드도 칸을 받는다(연결 알림)
        }
    }

    bool AndroidWindow::queueGamepadKey( AInputEvent* pEvent )
    {
        if ( AndroidWindowInternal::isGamepadSource( AInputEvent_getSource( pEvent ) ) == false )
            return false;
        const optional<uint32> slot = findOrAssignGamepadSlot( AInputEvent_getDeviceId( pEvent ) );
        if ( slot.has_value() == false || AKeyEvent_getRepeatCount( pEvent ) > 0 )
            return true; // 되풀이 키는 버튼 상태를 바꾸지 않는다
        PlatformInputEvent input{};
        input._kind         = AKeyEvent_getAction( pEvent ) == AKEY_EVENT_ACTION_UP ? PlatformInputKind::GamepadKeyUp : PlatformInputKind::GamepadKeyDown;
        input._pointerIndex = static_cast<uint16>( *slot );
        input._keyCode      = AKeyEvent_getKeyCode( pEvent );
        queueInput( input );
        return true;
    }

    bool AndroidWindow::queueGamepadMotion( AInputEvent* pEvent )
    {
        if ( ( AInputEvent_getSource( pEvent ) & AINPUT_SOURCE_JOYSTICK ) != AINPUT_SOURCE_JOYSTICK )
            return false;
        const optional<uint32> foundSlot = findOrAssignGamepadSlot( AInputEvent_getDeviceId( pEvent ) );
        if ( foundSlot.has_value() == false )
            return true;
        const uint32 slot      = *foundSlot;
        const auto   queueAxis = [this, slot]( int32 axis, float32 value )
        {
            PlatformInputEvent input{};
            input._kind         = PlatformInputKind::GamepadAxis;
            input._pointerIndex = static_cast<uint16>( slot );
            input._keyCode      = axis;
            input._wheel        = value;
            queueInput( input );
        };
        const auto readAxis = [pEvent]( int32 axis )
        { return AMotionEvent_getAxisValue( pEvent, axis, 0 ); };
        queueAxis( AMOTION_EVENT_AXIS_X, readAxis( AMOTION_EVENT_AXIS_X ) );
        queueAxis( AMOTION_EVENT_AXIS_Y, readAxis( AMOTION_EVENT_AXIS_Y ) );
        queueAxis( AMOTION_EVENT_AXIS_Z, readAxis( AMOTION_EVENT_AXIS_Z ) );
        queueAxis( AMOTION_EVENT_AXIS_RZ, readAxis( AMOTION_EVENT_AXIS_RZ ) );
        // 방아쇠를 LTRIGGER/RTRIGGER 로 주는 패드와 BRAKE/GAS 로 주는 패드가 있다(위 문서) — 같은 뜻이라 큰 쪽 하나를 방아쇠 축으로 보낸다.
        queueAxis( AMOTION_EVENT_AXIS_LTRIGGER, MathUtil::max( readAxis( AMOTION_EVENT_AXIS_LTRIGGER ), readAxis( AMOTION_EVENT_AXIS_BRAKE ) ) );
        queueAxis( AMOTION_EVENT_AXIS_RTRIGGER, MathUtil::max( readAxis( AMOTION_EVENT_AXIS_RTRIGGER ), readAxis( AMOTION_EVENT_AXIS_GAS ) ) );
        queueAxis( AMOTION_EVENT_AXIS_HAT_X, readAxis( AMOTION_EVENT_AXIS_HAT_X ) );
        queueAxis( AMOTION_EVENT_AXIS_HAT_Y, readAxis( AMOTION_EVENT_AXIS_HAT_Y ) );
        return true;
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
            if ( pWindow->queueGamepadKey( pEvent ) )
                return 1; // 패드 장치의 키(얼굴 버튼 · D패드)는 패드 칸으로 간다 — 키보드 화살표로 섞이지 않는다
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
        if ( pWindow->queueGamepadMotion( pEvent ) )
            return 1;
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

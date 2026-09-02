#include "pch.h"

#include "Engine/Input/InputManager.h"

#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Input/ActionMap.h"
#include "Engine/Input/Devices/GamepadDevice.h"
#include "Engine/Input/Devices/KeyboardDevice.h"
#include "Engine/Input/Devices/MouseDevice.h"
#include "Engine/Window/IWindow.h"

// 이 파일은 플랫폼 독립적입니다. 플랫폼별 구현(게임패드 백엔드, 커서 잠금/가시성,
// 접근성 단축키 억제)은 registerPlatformGamepads() 등 8)번 훅을 통해서만 연결되며,
// 실제 구현은 Windows/InputManagerWin32.cpp · Linux/InputManagerX11.cpp에 있습니다.
// 새 플랫폼을 추가하거나 플랫폼 동작을 바꿀 때 이 파일을 건드릴 필요가 없어야 합니다.

namespace sw
{
    SW_LOG_CALLER( "InputManager" );
} // namespace sw

namespace sw
{
    InputManager::InputManager()
        : _queueRawEvent{}
        , _droppedRawEventCount{ 0 }
        , _listDevice{}
        , _pKeyboard{ nullptr }
        , _pMouse{ nullptr }
        , _pGamepad{ nullptr }
        , _pActionMap{ nullptr }
        , _listDrainedEvent{}
        , _inputHistory{}
        , _activeDeviceType{ InputDeviceType::KeyboardMouse }
        , _onActiveDeviceChanged{}
        , _onGamepadConnectionChanged{}
        , _onTextInput{}
        , _onTextComposition{}
        , _bInitialized{ SW_FALSE }
        , _bInputMuted{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    InputManager::~InputManager()
    {
        shutdown();
    }

    bool InputManager::initialize()
    {
        if ( _bInitialized == SW_TRUE )
            return true;

        _listDevice.clear();
        _queueRawEvent.clear();
        _droppedRawEventCount.store( 0, std::memory_order_relaxed );
        _listDrainedEvent.clear();
        _inputHistory.clear();

        // 1) 표준 키보드 장치 등록
        auto pKeyboard = make_unique<KeyboardDevice>();
        _pKeyboard     = pKeyboard.get();
        registerDevice( std::move( pKeyboard ) );

        // 2) 표준 마우스 장치 등록
        auto pMouse = make_unique<MouseDevice>();
        _pMouse     = pMouse.get();
        registerDevice( std::move( pMouse ) );

        // 3) 표준 게임패드 장치 등록 (플랫폼별 구현은 registerPlatformGamepads() 참고)
        registerPlatformGamepads();

        // 4) 통합 ActionMap 인스턴스 생성 및 연결
        _pActionMap = make_unique<ActionMap>();
        _pActionMap->setInputManager( this );

        _bInitialized = SW_TRUE;
        SW_LOG_INFO( "InputManager initialized with %d devices.", static_cast<int32>( _listDevice.size() ) );
        return true;
    }

    void InputManager::shutdown()
    {
        if ( _bInitialized == SW_FALSE )
            return;

        releaseMouseLockMode();
        restoreWindowsAccessibilityShortcuts();
        resetAllDeviceState();

        _listDevice.clear();
        _pKeyboard  = nullptr;
        _pMouse     = nullptr;
        _pGamepad   = nullptr;
        _pActionMap = nullptr;
        _queueRawEvent.clear();
        _droppedRawEventCount.store( 0, std::memory_order_relaxed );
        _listDrainedEvent.clear();
        _inputHistory.clear();

        _bInitialized = SW_FALSE;
        SW_LOG_INFO( "InputManager shut down." );
    }

    bool InputManager::postRawEvent( const RawInputEvent& rawEvent )
    {
        if ( _bInputMuted == SW_TRUE )
            return false;

        const bool bPushed = _queueRawEvent.push( rawEvent );
        if ( bPushed == false )
            _droppedRawEventCount.fetch_add( 1, std::memory_order_relaxed );
        return bPushed;
    }

    uint32 InputManager::drainRawEvents( RawInputEvent* pOutBuffer, uint32 maxCount )
    {
        return _queueRawEvent.drain( pOutBuffer, maxCount );
    }

    void InputManager::registerDevice( unique_ptr<IInputDevice> pDevice )
    {
        if ( pDevice == nullptr )
            return;

        if ( pDevice->getDeviceKind() == InputDeviceKind::Keyboard && _pKeyboard == nullptr )
            _pKeyboard = static_cast<KeyboardDevice*>( pDevice.get() );
        else if ( pDevice->getDeviceKind() == InputDeviceKind::Mouse && _pMouse == nullptr )
            _pMouse = static_cast<MouseDevice*>( pDevice.get() );
        else if ( pDevice->getDeviceKind() == InputDeviceKind::Gamepad && _pGamepad == nullptr )
            _pGamepad = static_cast<GamepadDevice*>( pDevice.get() );

        _listDevice.push_back( std::move( pDevice ) );
    }

    void InputManager::unregisterDevice( IInputDevice* pDevice )
    {
        if ( pDevice == nullptr )
            return;

        const bool bWasKeyboard = ( _pKeyboard == pDevice );
        const bool bWasMouse    = ( _pMouse == pDevice );
        const bool bWasGamepad  = ( _pGamepad == pDevice );
        if ( bWasKeyboard )
            _pKeyboard = nullptr;
        if ( bWasMouse )
            _pMouse = nullptr;
        if ( bWasGamepad )
            _pGamepad = nullptr;

        for ( auto it = _listDevice.begin(); it != _listDevice.end(); ++it )
        {
            if ( it->get() == pDevice )
            {
                _listDevice.erase( it );
                break;
            }
        }

        // 대표 편의 포인터(키보드/마우스/게임패드)가 해제되면 남은 장치 중 동일 종류로 재바인딩합니다.
        if ( bWasKeyboard == false && bWasMouse == false && bWasGamepad == false )
            return;

        for ( const auto& pRemaining : _listDevice )
        {
            if ( pRemaining == nullptr )
                continue;
            if ( bWasKeyboard && _pKeyboard == nullptr && pRemaining->getDeviceKind() == InputDeviceKind::Keyboard )
                _pKeyboard = static_cast<KeyboardDevice*>( pRemaining.get() );
            else if ( bWasMouse && _pMouse == nullptr && pRemaining->getDeviceKind() == InputDeviceKind::Mouse )
                _pMouse = static_cast<MouseDevice*>( pRemaining.get() );
            else if ( bWasGamepad && _pGamepad == nullptr && pRemaining->getDeviceKind() == InputDeviceKind::Gamepad )
                _pGamepad = static_cast<GamepadDevice*>( pRemaining.get() );
        }
    }

    IInputDevice* InputManager::getDevice( InputDeviceKind kind, uint32 deviceIndex ) const
    {
        for ( const auto& pDev : _listDevice )
        {
            if ( pDev != nullptr && pDev->getDeviceKind() == kind && pDev->getDeviceIndex() == deviceIndex )
                return pDev.get();
        }
        return nullptr;
    }

    GamepadDevice* InputManager::getGamepad( uint32 deviceIndex ) const
    {
        IInputDevice* pDev = getDevice( InputDeviceKind::Gamepad, deviceIndex );
        return static_cast<GamepadDevice*>( pDev );
    }

    void InputManager::beginFrame( float32 deltaSeconds )
    {
        const uint32 droppedCount = _droppedRawEventCount.exchange( 0, std::memory_order_relaxed );
        if ( droppedCount > 0 )
        {
            SW_LOG_WARNING( "Raw input event queue full (capacity=%d). %d event(s) dropped in previous frame.", static_cast<int32>( _queueRawEvent.capacity() ), droppedCount );
        }

        // 1) 등록된 모든 장치에 대해 프레임 시작 (이전 프레임 엣지 초기화) 및 폴링 호출
        for ( auto& pDev : _listDevice )
        {
            if ( pDev != nullptr )
            {
                pDev->onFrameBegin( deltaSeconds );
                pDev->poll( deltaSeconds );
            }
        }

        // 2) 락프리 큐에서 비동기 인입된 이번 프레임 원시 이벤트들을 일괄 드레인
        _listDrainedEvent.clear();
        _queueRawEvent.drain( _listDrainedEvent );

        // 3) 드레인된 원시 이벤트를 각 디바이스로 디스패치 (새 프레임 엣지 플래그 설정)
        for ( const RawInputEvent& rawEvt : _listDrainedEvent )
        {
            dispatchRawEvent( rawEvt );
        }

        // 4) 활성 장치 자동 감지 (O(1) 플래그 쿼리)
        if ( _pGamepad != nullptr && _pGamepad->isConnected() )
        {
            float32 sx{ 0.0f };
            float32 sy{ 0.0f };
            _pGamepad->getLeftStick( sx, sy );
            const bool bStickActive = ( sx * sx + sy * sy ) > 0.04f;
            if ( bStickActive || _pGamepad->getLeftTrigger() > 0.1f || _pGamepad->getRightTrigger() > 0.1f || _pGamepad->wasAnyButtonPressed() )
            {
                setActiveDeviceType( InputDeviceType::GamepadXbox );
            }
        }

        if ( _pKeyboard != nullptr && _pKeyboard->wasAnyKeyPressed() )
        {
            setActiveDeviceType( InputDeviceType::KeyboardMouse );
        }

        if ( _pMouse != nullptr )
        {
            int32 mdx{ 0 };
            int32 mdy{ 0 };
            _pMouse->getDelta( mdx, mdy );
            if ( _pMouse->wasAnyButtonPressed() || mdx != 0 || mdy != 0 || _pMouse->getMouseWheel() != 0.0f )
            {
                setActiveDeviceType( InputDeviceType::KeyboardMouse );
            }
        }
    }

    void InputManager::dispatchRawEvent( const RawInputEvent& rawEvt )
    {
        switch ( rawEvt._type )
        {
            case RawInputEventType::KeyDown:
                if ( _pKeyboard != nullptr )
                    _pKeyboard->setKeyDown( rawEvt._payload._keyData._key, true );
                setActiveDeviceType( InputDeviceType::KeyboardMouse );
                break;

            case RawInputEventType::KeyUp:
                if ( _pKeyboard != nullptr )
                    _pKeyboard->setKeyDown( rawEvt._payload._keyData._key, false );
                break;

            case RawInputEventType::MouseMove:
                if ( _pMouse != nullptr )
                {
                    _pMouse->setPosition( rawEvt._payload._mouseData._x, rawEvt._payload._mouseData._y );
                    _pMouse->addRawDelta( rawEvt._payload._mouseData._rawDeltaX, rawEvt._payload._mouseData._rawDeltaY );
                }
                setActiveDeviceType( InputDeviceType::KeyboardMouse );
                break;

            case RawInputEventType::MouseButtonDown:
                if ( _pMouse != nullptr )
                {
                    _pMouse->setPosition( rawEvt._payload._mouseData._x, rawEvt._payload._mouseData._y );
                    _pMouse->setButtonDown( rawEvt._payload._mouseData._button, true );
                }
                setActiveDeviceType( InputDeviceType::KeyboardMouse );
                break;

            case RawInputEventType::MouseButtonUp:
                if ( _pMouse != nullptr )
                {
                    _pMouse->setPosition( rawEvt._payload._mouseData._x, rawEvt._payload._mouseData._y );
                    _pMouse->setButtonDown( rawEvt._payload._mouseData._button, false );
                }
                break;

            case RawInputEventType::MouseDoubleClick:
                if ( _pMouse != nullptr )
                {
                    _pMouse->setPosition( rawEvt._payload._mouseData._x, rawEvt._payload._mouseData._y );
                    _pMouse->setButtonDown( rawEvt._payload._mouseData._button, true );
                }
                setActiveDeviceType( InputDeviceType::KeyboardMouse );
                break;

            case RawInputEventType::MouseWheel:
                if ( _pMouse != nullptr )
                    _pMouse->addWheelDelta( rawEvt._payload._mouseData._wheelDelta );
                break;

            case RawInputEventType::MouseWheelHorizontal:
                if ( _pMouse != nullptr )
                    _pMouse->addHorizontalWheelDelta( rawEvt._payload._mouseData._wheelDelta );
                break;

            case RawInputEventType::GamepadButtonDown:
            {
                GamepadDevice* pPad = getGamepad( rawEvt._deviceIndex );
                if ( pPad != nullptr )
                    pPad->setButtonDown( rawEvt._payload._gamepadData._button, true );
                setActiveDeviceType( InputDeviceType::GamepadXbox );
                break;
            }

            case RawInputEventType::GamepadButtonUp:
            {
                GamepadDevice* pPad = getGamepad( rawEvt._deviceIndex );
                if ( pPad != nullptr )
                    pPad->setButtonDown( rawEvt._payload._gamepadData._button, false );
                break;
            }

            case RawInputEventType::GamepadAxis:
            {
                GamepadDevice* pPad = getGamepad( rawEvt._deviceIndex );
                if ( pPad != nullptr )
                    pPad->setAxis( rawEvt._payload._gamepadData._axisIndex, rawEvt._payload._gamepadData._axisValue );
                break;
            }

            case RawInputEventType::GamepadConnectionChanged:
                if ( _onGamepadConnectionChanged.isBound() )
                    _onGamepadConnectionChanged( rawEvt._deviceIndex, rawEvt._payload._gamepadData._bConnected == SW_TRUE );
                break;

            case RawInputEventType::TextInput:
                onTextInput( rawEvt._payload._textData._arrUtf8 );
                break;

            case RawInputEventType::TextComposition:
                onTextComposition( rawEvt._payload._textData._arrUtf8 );
                break;

            case RawInputEventType::FocusGained:
                onWindowFocusGained();
                break;

            case RawInputEventType::FocusLost:
                onWindowFocusLost();
                break;

            case RawInputEventType::None:
            default:
                break;
        }
    }

    void InputManager::endFrame()
    {
        for ( auto& pDev : _listDevice )
        {
            if ( pDev != nullptr )
                pDev->onFrameEnd();
        }
    }

    void InputManager::onWindowFocusGained()
    {
        applyMouseLockMode();
    }

    void InputManager::onWindowFocusLost()
    {
        releaseMouseLockMode();
        resetAllDeviceState();
    }

    void InputManager::resetAllDeviceState()
    {
        for ( auto& pDev : _listDevice )
        {
            if ( pDev != nullptr )
                pDev->resetState();
        }
    }

    void InputManager::setActiveDeviceType( InputDeviceType type )
    {
        if ( _activeDeviceType != type )
        {
            _activeDeviceType = type;
            if ( _onActiveDeviceChanged.isBound() )
                _onActiveDeviceChanged( type );
        }
    }

    bool InputManager::wasAnyInputPressed() const
    {
        if ( _pKeyboard != nullptr && _pKeyboard->wasAnyKeyPressed() )
            return true;

        if ( _pMouse != nullptr && _pMouse->wasAnyButtonPressed() )
            return true;

        if ( _pGamepad != nullptr && _pGamepad->isConnected() && _pGamepad->wasAnyButtonPressed() )
            return true;

        return false;
    }

    void InputManager::onTextInput( string_view text )
    {
        if ( text.empty() == false && _onTextInput.isBound() )
            _onTextInput( text );
    }

    void InputManager::onTextComposition( string_view text )
    {
        if ( text.empty() == false && _onTextComposition.isBound() )
            _onTextComposition( text );
    }

    void InputManager::getMousePosition( int32& outX, int32& outY ) const
    {
        outX = _pMouse != nullptr ? _pMouse->getPositionX() : 0;
        outY = _pMouse != nullptr ? _pMouse->getPositionY() : 0;
    }

    void InputManager::getMousePositionNormalized( float32& outNormX, float32& outNormY ) const
    {
        outNormX = 0.0f;
        outNormY = 0.0f;

        IWindow* pWindow = IWindow::getActiveWindow();
        if ( pWindow != nullptr && _pMouse != nullptr )
        {
            const uint32 width  = pWindow->getWidth();
            const uint32 height = pWindow->getHeight();
            if ( width > 0 && height > 0 )
            {
                outNormX = MathUtil::clamp( static_cast<float32>( _pMouse->getPositionX() ) / static_cast<float32>( width ), 0.0f, 1.0f );
                outNormY = MathUtil::clamp( static_cast<float32>( _pMouse->getPositionY() ) / static_cast<float32>( height ), 0.0f, 1.0f );
            }
        }
    }

    void InputManager::getMouseDelta( int32& outDx, int32& outDy ) const
    {
        if ( _pMouse != nullptr )
            _pMouse->getDelta( outDx, outDy );
        else
        {
            outDx = 0;
            outDy = 0;
        }
    }

    void InputManager::getRawMouseDelta( float32& outDx, float32& outDy ) const
    {
        if ( _pMouse != nullptr )
            _pMouse->getRawDelta( outDx, outDy );
        else
        {
            outDx = 0.0f;
            outDy = 0.0f;
        }
    }

    void InputManager::setMouseLockMode( MouseLockMode mode )
    {
        if ( _pMouse != nullptr )
            _pMouse->setLockMode( mode );
        applyMouseLockMode();
    }

    void InputManager::setCursorVisible( bool bVisible )
    {
        if ( _pMouse != nullptr )
            _pMouse->setCursorVisible( bVisible );
        setCursorVisiblePlatform( bVisible );
    }

    void InputManager::setMouseClipSubRect( int32 left, int32 top, int32 right, int32 bottom )
    {
        if ( _pMouse != nullptr )
            _pMouse->setClipSubRect( left, top, right, bottom );
        applyMouseLockMode();
    }

    void InputManager::clearMouseClipSubRect()
    {
        if ( _pMouse != nullptr )
            _pMouse->clearClipSubRect();
        applyMouseLockMode();
    }

    float32 InputManager::getGamepadLeftTrigger( uint32 deviceIndex ) const
    {
        GamepadDevice* pPad = getGamepad( deviceIndex );
        return pPad != nullptr ? pPad->getLeftTrigger() : 0.0f;
    }

    float32 InputManager::getGamepadRightTrigger( uint32 deviceIndex ) const
    {
        GamepadDevice* pPad = getGamepad( deviceIndex );
        return pPad != nullptr ? pPad->getRightTrigger() : 0.0f;
    }

    bool InputManager::setGamepadVibration( float32 leftMotor, float32 rightMotor, uint32 deviceIndex )
    {
        GamepadDevice* pPad = getGamepad( deviceIndex );
        return pPad != nullptr ? pPad->setVibration( leftMotor, rightMotor ) : false;
    }

    bool InputManager::playGamepadVibration( float32 leftMotor, float32 rightMotor, float32 durationSeconds, uint32 deviceIndex )
    {
        GamepadDevice* pPad = getGamepad( deviceIndex );
        return pPad != nullptr ? pPad->playVibration( leftMotor, rightMotor, durationSeconds ) : false;
    }

    void InputManager::injectSnapshot( const InputSnapshot& snapshot )
    {
        _inputHistory.recordSnapshot( snapshot );
    }

    void InputManager::recordSnapshot( uint32 tickNumber )
    {
        InputSnapshot snapshot{};
        snapshot._tickNumber = tickNumber;

        // 1) 2D 축 벡터 (Move & Look)
        if ( _pActionMap != nullptr && _pActionMap->hasAction( "Move" ) )
        {
            snapshot._moveVector = _pActionMap->getVector2D( "Move" );
        }
        else if ( _pGamepad != nullptr && _pGamepad->isConnected() )
        {
            _pGamepad->getLeftStick( snapshot._moveVector._x, snapshot._moveVector._y );
        }

        if ( _pActionMap != nullptr && _pActionMap->hasAction( "Look" ) )
        {
            snapshot._lookVector = _pActionMap->getVector2D( "Look" );
        }
        else if ( _pGamepad != nullptr && _pGamepad->isConnected() )
        {
            _pGamepad->getRightStick( snapshot._lookVector._x, snapshot._lookVector._y );
        }

        // 2) 아날로그 트리거
        snapshot._leftTrigger  = getGamepadLeftTrigger();
        snapshot._rightTrigger = getGamepadRightTrigger();

        // 3) 64비트 버튼/액션 비트마스크
        uint64 mask = 0;
        if ( _pGamepad != nullptr && _pGamepad->isConnected() )
        {
            for ( uint32 btnIndex = 0; btnIndex < static_cast<uint32>( GamepadButton::Count ); ++btnIndex )
            {
                if ( _pGamepad->isButtonDown( static_cast<GamepadButton>( btnIndex ) ) )
                {
                    mask |= ( 1ULL << btnIndex );
                }
            }
        }

        if ( _pMouse != nullptr )
        {
            for ( uint32 btnIndex = 0; btnIndex < static_cast<uint32>( MouseButton::Count ); ++btnIndex )
            {
                if ( _pMouse->isButtonDown( static_cast<MouseButton>( btnIndex ) ) )
                {
                    mask |= ( 1ULL << ( 16 + btnIndex ) );
                }
            }
        }

        if ( _pActionMap != nullptr )
        {
            const vector<hashed_string>& listAction  = _pActionMap->getActionNames();
            const uint32                 actionCount = MathUtil::min( static_cast<uint32>( listAction.size() ), 32u );
            for ( uint32 actionIndex = 0; actionIndex < actionCount; ++actionIndex )
            {
                if ( _pActionMap->isActionDown( listAction[actionIndex] ) )
                {
                    mask |= ( 1ULL << ( 32 + actionIndex ) );
                }
            }
        }

        snapshot._buttonMask = mask;
        _inputHistory.recordSnapshot( snapshot );
    }
} // namespace sw

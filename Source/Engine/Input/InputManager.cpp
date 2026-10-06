#include "pch.h"

#include "Engine/Input/InputManager.h"

#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/MemoryProfiler.h"

#include "Engine/Input/Devices/GamepadDevice.h"
#include "Engine/Input/Devices/KeyboardDevice.h"
#include "Engine/Input/Devices/MouseDevice.h"
#include "Engine/Input/InputMap.h"
#include "Engine/Window/IWindow.h"

// 이 파일은 플랫폼 독립적이다. 플랫폼별 구현(게임패드 백엔드, 커서 잠금 · 표시,
// 접근성 단축키 억제)은 InputManager.h 의 "플랫폼별 구현" 절에 있는 훅(registerPlatformGamepads() 등)으로만
// 연결되고, 실제 구현은 Windows/InputManagerWin32.cpp · Linux/InputManagerX11.cpp 에 있다.
// 새 플랫폼을 더하거나 플랫폼 동작을 바꿀 때 이 파일을 건드릴 필요가 없어야 한다.

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
        , _pInputMap{ nullptr }
        , _listDrainedEvent{}
        , _pVirtualInput{ nullptr }
        , _virtualFrameIndex{ 0 }
        , _beginFrameCount{ 0 }
        , _activeGlyphStyle{ InputGlyphStyle::KeyboardMouse }
        , _onActiveDeviceChanged{}
        , _onGamepadConnectionChanged{}
        , _arrOnTextInput{}
        , _arrOnTextComposition{}
        , _arrCapturedKeyMask{}
        , _keyboardFocus{ InputKeyboardFocus::Game }
        , _virtualInputMode{ VirtualInputMode::Exclusive }
        , _pendingHighSurrogate{ 0 }
        , _consumedButtonMask{ 0 }
        , _bInitialized{ SW_FALSE }
        , _bInputMuted{ SW_FALSE }
        , _bWindowFocused{ SW_TRUE }
        , _bMouseLockEngaged{ SW_FALSE }
        , _bAltHeld{ SW_FALSE }
        , _bCursorHiddenApplied{ SW_FALSE }
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

        // 1) 표준 키보드 장치를 등록한다
        auto pKeyboard = make_unique<KeyboardDevice>();
        _pKeyboard     = pKeyboard.get();
        registerDevice( std::move( pKeyboard ) );

        // 2) 표준 마우스 장치를 등록한다
        auto pMouse = make_unique<MouseDevice>();
        _pMouse     = pMouse.get();
        registerDevice( std::move( pMouse ) );

        // 3) 표준 게임패드 장치를 등록한다(플랫폼별 구현은 registerPlatformGamepads() 참고)
        registerPlatformGamepads();

        // 4) 통합 InputMap 인스턴스를 만들어 연결한다
        _pInputMap = make_unique<InputMap>();
        _pInputMap->setInputManager( this );

        _bInitialized = SW_TRUE;
        SW_LOG_INFO( "InputManager initialized with %d devices.", static_cast<int32>( _listDevice.size() ) );
        return true;
    }

    uint32 InputManager::onModuleUnloading( const void* pBegin, const void* pEnd, bool& outKeepImageMapped )
    {
        (void)outKeepImageMapped;
        uint32 releasedCount{ 0 };
        if ( _onActiveDeviceChanged.isCodeWithin( pBegin, pEnd ) )
        {
            _onActiveDeviceChanged = {};
            ++releasedCount;
        }
        if ( _onGamepadConnectionChanged.isCodeWithin( pBegin, pEnd ) )
        {
            _onGamepadConnectionChanged = {};
            ++releasedCount;
        }
        for ( size_t focusIndex = 0; focusIndex < kKeyboardFocusCount; ++focusIndex )
        {
            if ( _arrOnTextInput[focusIndex].isCodeWithin( pBegin, pEnd ) )
            {
                _arrOnTextInput[focusIndex] = {};
                ++releasedCount;
            }
            if ( _arrOnTextComposition[focusIndex].isCodeWithin( pBegin, pEnd ) )
            {
                _arrOnTextComposition[focusIndex] = {};
                ++releasedCount;
            }
        }

        // 모듈이 등록한 장치는 vtable 이 그 이미지에 있다 — 내린 뒤 폴링하면 내려간 코드로 뛴다. 소멸자가 아직 있는 지금 내린다.
        vector<IInputDevice*> listModuleDevice;
        for ( const unique_ptr<IInputDevice>& pDevice : _listDevice )
        {
            if ( isAddressWithin( findVtableAddress( pDevice.get() ), pBegin, pEnd ) )
                listModuleDevice.push_back( pDevice.get() );
            else
                releasedCount += pDevice->releaseCodeWithin( pBegin, pEnd );
        }
        for ( IInputDevice* pDevice : listModuleDevice )
        {
            unregisterDevice( pDevice );
            ++releasedCount;
        }
        return releasedCount;
    }

    void InputManager::shutdown()
    {
        if ( _bInitialized == SW_FALSE )
            return;

        _pVirtualInput = nullptr;
        releaseMouseLockMode();
        if ( _bCursorHiddenApplied == SW_TRUE )
        {
            setCursorVisiblePlatform( true );
            _bCursorHiddenApplied = SW_FALSE;
        }
        _consumedButtonMask = 0;
        restoreWindowsAccessibilityShortcuts();
        resetAllDeviceState();

        _listDevice.clear();
        _pKeyboard = nullptr;
        _pMouse    = nullptr;
        _pGamepad  = nullptr;
        _pInputMap = nullptr;
        _queueRawEvent.clear();
        _droppedRawEventCount.store( 0, std::memory_order_relaxed );
        _listDrainedEvent.clear();

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

    bool InputManager::postWindowStateEvent( const RawInputEvent& rawEvent )
    {
        // 음소거는 "입력을 무시한다" 는 뜻이지 "창이 포커스를 잃은 것도 모른다" 는 뜻이 아니다. 음소거 중에 포커스를 잃고
        // 풀린 뒤에도 그 전의 눌림이 남아 있으면 안 된다.
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

        // 대표 편의 포인터(키보드 · 마우스 · 게임패드)가 해제되면 남은 장치 중 같은 종류로 다시 잡는다.
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
        SW_MEMORY_SCOPE( EngineMisc );
        ++_beginFrameCount;
        const uint32 droppedCount = _droppedRawEventCount.exchange( 0, std::memory_order_relaxed );
        if ( droppedCount > 0 )
            SW_LOG_WARNING( "Raw input event queue full (capacity=%d). %d event(s) dropped in previous frame.", static_cast<int32>( _queueRawEvent.capacity() ), droppedCount );

        // 1) 등록된 모든 장치의 프레임을 시작하고(이전 프레임 엣지 초기화) 폴링한다. 배타 가상 입력이면 폴링하지 않는다 — 패드 폴링
        //    (XInput · 조이스틱)이 가상 패드 상태를 실제 패드 값으로 덮는다.
        const bool bOsSuppressed = isOsInputSuppressed();
        for ( auto& pDev : _listDevice )
        {
            if ( pDev != nullptr )
            {
                pDev->onFrameBegin( deltaSeconds );
                if ( bOsSuppressed == false )
                    pDev->poll( deltaSeconds );
                pDev->onPolled();
            }
        }

        // 지난 프레임까지 뗀 가린 키를 푼다. 뗀 프레임에는 아직 가린다 — 게임이 누른 적 없는 키의 "뗌" 을 보지 않게.
        releaseCapturedKeys();

        // 2) 락프리 큐에 비동기로 들어온 이번 프레임 원시 이벤트를 한꺼번에 꺼낸다
        _listDrainedEvent.clear();
        _queueRawEvent.drain( _listDrainedEvent );
        // 배타 가상 입력: OS 가 낸 장치 사건 · 창 포커스 사건을 버린다(글자 입력도 — 가상 원천이 `makeTextInput` 으로 낸다).
        if ( bOsSuppressed )
            _listDrainedEvent.clear();
        // 2-1) 가상 입력 원천의 이번 프레임 사건을 OS 사건 뒤에 붙인다 — 같은 재생 자리(3)를 탄다.
        if ( _pVirtualInput != nullptr )
        {
            const size_t firstVirtual = _listDrainedEvent.size();
            _pVirtualInput->emitFrame( _virtualFrameIndex, _listDrainedEvent );
            for ( size_t eventIndex = firstVirtual; eventIndex < _listDrainedEvent.size(); ++eventIndex )
            {
                _listDrainedEvent[eventIndex]._bSynthetic = SW_TRUE;
            }
            ++_virtualFrameIndex;
        }

        // 3) 꺼낸 원시 이벤트를 **들어온 순서대로** 장치에 적용한다(새 프레임 엣지 플래그 설정). 포커스 잃음도 이 순서 안에서
        //    적용해야 그보다 먼저 들어온 키 누름이 리셋 뒤에 되살아나지 않는다(알트탭하면 캐릭터가 계속 달리던 원인).
        for ( const RawInputEvent& rawEvent : _listDrainedEvent )
        {
            dispatchRawEvent( rawEvent );
        }

        // 이벤트를 다 적용한 뒤 장치마다 한 번 마무리한다 — 마우스 스무딩은 이벤트 수가 아니라 흐른 시간으로 한 번 건다.
        for ( auto& pDev : _listDevice )
        {
            if ( pDev != nullptr )
                pDev->onEventsDispatched( deltaSeconds );
        }

        // 가운데 고정 잠금: 이번 프레임의 델타를 잰 뒤에 커서를 되돌린다. 잠금이 쉬는 동안(포커스 밖 · Alt · 콘솔)은 되돌리지 않는다 —
        // 되돌리면 풀린 커서도 프레임마다 가운데로 끌려와 제목 표시줄 · X 에 닿지 못한다.
        if ( _pMouse != nullptr && _pMouse->getLockMode() == MouseLockMode::LockedInCenter && isMouseLockActive() )
            recenterLockedCursorPlatform();

        // 4) 활성 장치 자동 감지(O(1) 플래그 조회)
        if ( _pGamepad != nullptr && _pGamepad->isConnected() )
        {
            const float2 stick        = _pGamepad->getLeftStick();
            const bool   bStickActive = stick.getLengthSquared() > 0.04f;
            if ( bStickActive || _pGamepad->getLeftTrigger() > 0.1f || _pGamepad->getRightTrigger() > 0.1f || _pGamepad->wasAnyButtonPressed() )
                setActiveGlyphStyle( InputGlyphStyle::GamepadXbox );
        }

        if ( _pKeyboard != nullptr && _pKeyboard->wasAnyKeyPressed() )
            setActiveGlyphStyle( InputGlyphStyle::KeyboardMouse );

        if ( _pMouse != nullptr )
        {
            const int2 mouseDelta = _pMouse->getDelta();
            if ( _pMouse->wasAnyButtonPressed() || mouseDelta != int2{} || _pMouse->getMouseWheel() != 0.0f )
                setActiveGlyphStyle( InputGlyphStyle::KeyboardMouse );
        }

        // 5) 통합 InputMap 을 이번 프레임 입력으로 갱신한다(게임플레이가 읽는 맵). 갱신은 맵의 주인이 한다 — 엔진 루프 ·
        //    리플레이 재동기화 · 시험이 같은 길을 탄다.
        if ( _pInputMap != nullptr )
            _pInputMap->update( deltaSeconds );
    }

    void InputManager::dispatchRawEvent( const RawInputEvent& rawEvent )
    {
        switch ( rawEvent._type )
        {
            case RawInputEventType::KeyDown:
            {
                const Key    key      = rawEvent._payload._keyData._key;
                const size_t keyIndex = static_cast<size_t>( key );
                const uint64 keyBit   = uint64{ 1 } << ( keyIndex % 64 );
                if ( keyIndex < static_cast<size_t>( Key::Count ) )
                {
                    // 게임이 아닌 쪽이 키보드를 쥔 동안 눌린 키는 뗄 때까지 게임에 가린다. 게임이 쥔 동안 새로 누르면 가림을 푼다.
                    if ( _keyboardFocus != InputKeyboardFocus::Game )
                        _arrCapturedKeyMask[keyIndex / 64] |= keyBit;
                    else
                        _arrCapturedKeyMask[keyIndex / 64] &= ~keyBit;
                }
                if ( _pKeyboard != nullptr )
                    _pKeyboard->setKeyDown( key, true );
                setActiveGlyphStyle( InputGlyphStyle::KeyboardMouse );
                break;
            }

            case RawInputEventType::KeyUp:
            {
                if ( _pKeyboard != nullptr )
                    _pKeyboard->setKeyDown( rawEvent._payload._keyData._key, false );
                break;
            }

            case RawInputEventType::MouseMove:
            {
                if ( _pMouse != nullptr )
                {
                    _pMouse->setPosition( rawEvent._payload._mouseData._x, rawEvent._payload._mouseData._y );
                    _pMouse->addRawDelta( rawEvent._payload._mouseData._rawDelta._x, rawEvent._payload._mouseData._rawDelta._y );
                }
                setActiveGlyphStyle( InputGlyphStyle::KeyboardMouse );
                break;
            }

            case RawInputEventType::MouseButtonDown:
            {
                if ( _pMouse != nullptr )
                {
                    _pMouse->setPosition( rawEvent._payload._mouseData._x, rawEvent._payload._mouseData._y );
                    _pMouse->setButtonDown( rawEvent._payload._mouseData._button, true );
                }
                setActiveGlyphStyle( InputGlyphStyle::KeyboardMouse );
                break;
            }

            case RawInputEventType::MouseButtonUp:
            {
                if ( _pMouse != nullptr )
                {
                    _pMouse->setPosition( rawEvent._payload._mouseData._x, rawEvent._payload._mouseData._y );
                    _pMouse->setButtonDown( rawEvent._payload._mouseData._button, false );
                }
                break;
            }

            case RawInputEventType::MouseDoubleClick:
            {
                if ( _pMouse != nullptr )
                {
                    _pMouse->setPosition( rawEvent._payload._mouseData._x, rawEvent._payload._mouseData._y );
                    _pMouse->setButtonDown( rawEvent._payload._mouseData._button, true );
                }
                setActiveGlyphStyle( InputGlyphStyle::KeyboardMouse );
                break;
            }

            case RawInputEventType::MouseWheel:
            {
                if ( _pMouse != nullptr )
                    _pMouse->addWheelDelta( rawEvent._payload._mouseData._wheelDelta );
                break;
            }

            case RawInputEventType::MouseWheelHorizontal:
            {
                if ( _pMouse != nullptr )
                    _pMouse->addHorizontalWheelDelta( rawEvent._payload._mouseData._wheelDelta );
                break;
            }

            case RawInputEventType::GamepadButtonDown:
            {
                GamepadDevice* pPad = getGamepad( rawEvent._deviceIndex );
                if ( pPad != nullptr )
                    pPad->setButtonDown( rawEvent._payload._gamepadData._button, true );
                setActiveGlyphStyle( InputGlyphStyle::GamepadXbox );
                break;
            }

            case RawInputEventType::GamepadButtonUp:
            {
                GamepadDevice* pPad = getGamepad( rawEvent._deviceIndex );
                if ( pPad != nullptr )
                    pPad->setButtonDown( rawEvent._payload._gamepadData._button, false );
                break;
            }

            case RawInputEventType::GamepadAxis:
            {
                GamepadDevice* pPad = getGamepad( rawEvent._deviceIndex );
                if ( pPad != nullptr )
                    pPad->setAxis( rawEvent._payload._gamepadData._axisIndex, rawEvent._payload._gamepadData._axisValue );
                break;
            }

            case RawInputEventType::GamepadConnectionChanged:
            {
                // 가상 연결 사건은 가상 세션 동안 패드의 연결 여부를 정한다(실제 연결은 폴링이 정한다).
                GamepadDevice* pPad = getGamepad( rawEvent._deviceIndex );
                if ( rawEvent._bSynthetic == SW_TRUE && pPad != nullptr )
                    pPad->setVirtualConnected( rawEvent._payload._gamepadData._bConnected == SW_TRUE );
                if ( _onGamepadConnectionChanged.isBound() )
                    _onGamepadConnectionChanged( rawEvent._deviceIndex, rawEvent._payload._gamepadData._bConnected == SW_TRUE );
                break;
            }

            case RawInputEventType::TextInput:
            {
                onTextInput( rawEvent._payload._textData._arrUtf8 );
                break;
            }

            case RawInputEventType::TextComposition:
            {
                onTextComposition( rawEvent._payload._textData._arrUtf8 );
                break;
            }

            case RawInputEventType::FocusGained:
            {
                onWindowFocusGained();
                break;
            }

            case RawInputEventType::FocusLost:
            {
                onWindowFocusLost();
                break;
            }

            case RawInputEventType::PointerEntered:
            case RawInputEventType::PointerLeft:
            {
                if ( _pMouse != nullptr )
                    _pMouse->setPointerInsideState( rawEvent._type == RawInputEventType::PointerEntered );
                break;
            }

            case RawInputEventType::MouseRawDelta:
            {
                if ( _pMouse != nullptr )
                    _pMouse->addRawDelta( rawEvent._payload._mouseData._rawDelta._x, rawEvent._payload._mouseData._rawDelta._y );
                setActiveGlyphStyle( InputGlyphStyle::KeyboardMouse );
                break;
            }

            case RawInputEventType::None:
            {
                break;
            }
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
        syncMouseLock();
    }

    void InputManager::onWindowFocusLost()
    {
        syncMouseLock();
        resetAllDeviceState();
    }

    void InputManager::resetAllDeviceState()
    {
        for ( auto& pDev : _listDevice )
        {
            if ( pDev != nullptr )
                pDev->resetState();
        }
        // 키가 모두 떼어졌으니 가릴 키도 없다.
        for ( uint64& word : _arrCapturedKeyMask )
        {
            word = 0;
        }
    }

    void InputManager::setKeyboardFocus( InputKeyboardFocus focus )
    {
        if ( focus == InputKeyboardFocus::Count )
            return;
        _keyboardFocus = focus;
        // 개발 콘솔 · 텍스트 입력이 키보드를 쥐는 동안은 마우스도 놓는다(커서로 창 밖 · 제목 표시줄에 갈 수 있게).
        syncMouseLock();
    }

    bool InputManager::isKeyVisibleToGame( Key key ) const
    {
        if ( _keyboardFocus != InputKeyboardFocus::Game )
            return false;
        const size_t keyIndex = static_cast<size_t>( key );
        if ( keyIndex >= static_cast<size_t>( Key::Count ) )
            return true;
        return ( _arrCapturedKeyMask[keyIndex / 64] & ( uint64{ 1 } << ( keyIndex % 64 ) ) ) == 0;
    }

    void InputManager::releaseCapturedKeys()
    {
        if ( _pKeyboard == nullptr )
            return;
        for ( size_t wordIndex = 0; wordIndex < kKeyMaskWordCount; ++wordIndex )
        {
            uint64 remaining = _arrCapturedKeyMask[wordIndex];
            while ( remaining != 0 )
            {
                const uint32 bitIndex = MathUtil::countTrailingZeros( remaining );
                remaining &= remaining - 1;
                const Key key = static_cast<Key>( wordIndex * 64 + bitIndex );
                if ( _pKeyboard->isKeyDown( key ) == false )
                    _arrCapturedKeyMask[wordIndex] &= ~( uint64{ 1 } << bitIndex );
            }
        }
    }

    void InputManager::setTextInputCallback( TextInputDelegate callback, InputKeyboardFocus owner )
    {
        if ( owner != InputKeyboardFocus::Count )
            _arrOnTextInput[static_cast<size_t>( owner )] = std::move( callback );
    }

    void InputManager::setTextCompositionCallback( TextInputDelegate callback, InputKeyboardFocus owner )
    {
        if ( owner != InputKeyboardFocus::Count )
            _arrOnTextComposition[static_cast<size_t>( owner )] = std::move( callback );
    }

    void InputManager::setActiveGlyphStyle( InputGlyphStyle type )
    {
        if ( _activeGlyphStyle != type )
        {
            _activeGlyphStyle = type;
            if ( _onActiveDeviceChanged.isBound() )
                _onActiveDeviceChanged( type );
        }
    }

    bool InputManager::wasAnyInputPressed() const
    {
        // 키보드는 게임이 포커스를 쥘 때만 "아무 키" 다 — 콘솔에 치는 글자가 "Press Any Key" 를 넘기지 않게.
        if ( _pKeyboard != nullptr && _keyboardFocus == InputKeyboardFocus::Game && _pKeyboard->wasAnyKeyPressed() )
            return true;

        if ( _pMouse != nullptr && _pMouse->wasAnyButtonPressed() )
            return true;

        if ( _pGamepad != nullptr && _pGamepad->isConnected() && _pGamepad->wasAnyButtonPressed() )
            return true;

        return false;
    }

    void InputManager::onTextInput( string_view text )
    {
        TextInputDelegate& onTextInput = _arrOnTextInput[static_cast<size_t>( _keyboardFocus )];
        if ( text.empty() == false && onTextInput.isBound() )
            onTextInput( text );
    }

    void InputManager::onTextComposition( string_view text )
    {
        TextInputDelegate& onTextComposition = _arrOnTextComposition[static_cast<size_t>( _keyboardFocus )];
        if ( text.empty() == false && onTextComposition.isBound() )
            onTextComposition( text );
    }

    float2 InputManager::getMousePositionNormalized() const
    {
        IWindow* pWindow = IWindow::getActiveWindow();
        if ( pWindow == nullptr || _pMouse == nullptr )
            return float2{};

        const uint32 width  = pWindow->getWidth();
        const uint32 height = pWindow->getHeight();
        if ( width == 0 || height == 0 )
            return float2{};

        return float2{ MathUtil::clamp( static_cast<float32>( _pMouse->getPositionX() ) / static_cast<float32>( width ), 0.0f, 1.0f ),
                       MathUtil::clamp( static_cast<float32>( _pMouse->getPositionY() ) / static_cast<float32>( height ), 0.0f, 1.0f ) };
    }

    int2 InputManager::getMouseDelta() const
    {
        return _pMouse != nullptr ? _pMouse->getDelta() : int2{};
    }

    bool InputManager::isPointerOverRect( int32 x, int32 y, int32 width, int32 height ) const
    {
        if ( _pMouse == nullptr || _pMouse->isPointerInside() == false )
            return false;
        const int2 mousePos = _pMouse->getPosition();
        return ( x <= mousePos._x && mousePos._x < ( x + width ) && y <= mousePos._y && mousePos._y < ( y + height ) );
    }

    bool InputManager::isPointerOwnedByGame() const
    {
        // 배타 가상 입력 중에는 OS 포인터를 게임이 쥐지 않는다(사람이 같은 기계를 쓴다). 잠금 모드(장치 상태)는 남겨 두고 떼면 `syncMouseLock` 이 건다.
        if ( isOsInputSuppressed() )
            return false;
        if ( _bWindowFocused == SW_FALSE || _bAltHeld == SW_TRUE || _keyboardFocus != InputKeyboardFocus::Game )
            return false;
        return _pMouse == nullptr || _pMouse->getLockMode() == MouseLockMode::None || _bMouseLockEngaged == SW_TRUE;
    }

    bool InputManager::isMouseLockActive() const
    {
        return _pMouse != nullptr && _pMouse->getLockMode() != MouseLockMode::None && isPointerOwnedByGame();
    }

    void InputManager::syncMouseLock()
    {
        if ( isMouseLockActive() )
            applyMouseLockMode();
        else
            releaseMouseLockMode();

        // `ShowCursor` 는 카운터다 — 바뀔 때만 부른다. 부를 때마다 더하면 레이어 하나 올릴 때마다 숨김 한 번이 더 필요해진다.
        const bool bHide = _pMouse != nullptr && _pMouse->isCursorVisible() == false && isPointerOwnedByGame();
        if ( bHide == ( _bCursorHiddenApplied == SW_TRUE ) )
            return;
        setCursorVisiblePlatform( bHide == false );
        _bCursorHiddenApplied = bHide ? SW_TRUE : SW_FALSE;
    }

    void InputManager::onPlatformFocusChanged( bool bFocused )
    {
        _bWindowFocused = bFocused ? SW_TRUE : SW_FALSE;
        if ( bFocused == false )
        {
            _bMouseLockEngaged = SW_FALSE;
            _bAltHeld          = SW_FALSE; // Alt+Tab 의 Alt 떼기는 다른 창으로 간다
        }
        syncMouseLock();
    }

    bool InputManager::onPlatformPointerPressed( MouseButton button )
    {
        if ( _bWindowFocused == SW_FALSE || _bMouseLockEngaged == SW_TRUE )
            return false;
        const bool bWasActive = isMouseLockActive();
        _bMouseLockEngaged    = SW_TRUE;
        syncMouseLock();
        // 잠금을 건 그 누름은 "창으로 돌아온다" 는 뜻이라 게임에 넘기지 않는다(언리얼 뷰포트의 마우스 캡처 클릭과 같다).
        if ( bWasActive || isMouseLockActive() == false || button >= MouseButton::Count )
            return false;
        _consumedButtonMask = static_cast<uint8>( _consumedButtonMask | ( 1u << static_cast<uint32>( button ) ) );
        return true;
    }

    bool InputManager::onPlatformPointerReleased( MouseButton button )
    {
        if ( isMouseButtonConsumed( button ) == false )
            return false;
        _consumedButtonMask = static_cast<uint8>( _consumedButtonMask & ~( 1u << static_cast<uint32>( button ) ) );
        return true;
    }

    bool InputManager::isMouseButtonConsumed( MouseButton button ) const
    {
        return button < MouseButton::Count && ( _consumedButtonMask & ( 1u << static_cast<uint32>( button ) ) ) != 0;
    }

    void InputManager::onPlatformAltChanged( bool bHeld )
    {
        const uint8 bNewHeld = bHeld ? SW_TRUE : SW_FALSE;
        if ( _bAltHeld == bNewHeld )
            return;
        _bAltHeld = bNewHeld;
        syncMouseLock();
    }

    void InputManager::setMouseLockMode( MouseLockMode mode )
    {
        if ( _pMouse == nullptr )
            return;
        const bool bWasLocked = _pMouse->getLockMode() != MouseLockMode::None;
        _pMouse->setLockMode( mode );
        // 잠금을 새로 켤 때 창이 지금 전경이면 바로 잡는다(시작 직후 · Esc 로 다시 잠글 때). 아니면 클라이언트를 눌러야 잡는다.
        if ( mode != MouseLockMode::None && bWasLocked == false )
        {
            const bool bFocused = isWindowFocusedPlatform();
            _bWindowFocused     = bFocused ? SW_TRUE : SW_FALSE;
            _bMouseLockEngaged  = bFocused ? SW_TRUE : SW_FALSE;
        }
        syncMouseLock();
    }

    void InputManager::setCursorVisible( bool bVisible )
    {
        if ( _pMouse != nullptr )
            _pMouse->setCursorVisible( bVisible );
        syncMouseLock();
    }

    void InputManager::setMouseClipSubRect( int32 left, int32 top, int32 right, int32 bottom )
    {
        if ( _pMouse != nullptr )
            _pMouse->setClipSubRect( left, top, right, bottom );
        syncMouseLock();
    }

    void InputManager::clearMouseClipSubRect()
    {
        if ( _pMouse != nullptr )
            _pMouse->clearClipSubRect();
        syncMouseLock();
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

    void InputManager::attachVirtualInput( IVirtualInputSource* pSource, VirtualInputMode mode, bool bResetState )
    {
        if ( pSource == nullptr )
        {
            detachVirtualInput();
            return;
        }
        const bool bWasExclusive = isOsInputSuppressed();
        _pVirtualInput           = pSource;
        _virtualInputMode        = mode;
        _virtualFrameIndex       = 0;
        // 사람이 누르고 있던 것 · 큐에 남은 OS 사건이 첫 프레임에 새지 않게 지운다.
        if ( bResetState )
            resetAllDeviceState();
        const bool bExclusive = mode == VirtualInputMode::Exclusive;
        if ( bExclusive )
            _queueRawEvent.clear();
        if ( bExclusive != bWasExclusive )
            setGamepadVirtualSession( bExclusive );
        syncMouseLock();
        SW_LOG_INFO( "Virtual input attached (%#)", bExclusive ? "exclusive" : "mixed" );
    }

    void InputManager::detachVirtualInput( bool bResetState )
    {
        if ( _pVirtualInput == nullptr )
            return;
        const bool bWasExclusive = isOsInputSuppressed();
        _pVirtualInput           = nullptr;
        if ( bResetState )
            resetAllDeviceState();
        if ( bWasExclusive )
            setGamepadVirtualSession( false );
        syncMouseLock();
        SW_LOG_INFO( "Virtual input detached after %# frame(s)", _virtualFrameIndex );
    }

    void InputManager::setGamepadVirtualSession( bool bVirtual )
    {
        for ( auto& pDev : _listDevice )
        {
            if ( pDev != nullptr && pDev->getDeviceKind() == InputDeviceKind::Gamepad )
                static_cast<GamepadDevice*>( pDev.get() )->setVirtualSession( bVirtual );
        }
    }
} // namespace sw

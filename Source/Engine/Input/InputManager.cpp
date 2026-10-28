#include "pch.h"

#include "Engine/Input/InputManager.h"

#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Input/Devices/GamepadDevice.h"
#include "Engine/Input/Devices/KeyboardDevice.h"
#include "Engine/Input/Devices/MouseDevice.h"
#include "Engine/Input/Map/InputMap.h"
#include "Engine/Window/NativeWindowEvent.h"

// 이 파일은 플랫폼에 독립적이다. 플랫폼별 구현(게임패드 백엔드, 커서 잠금과 표시,
// 접근성 단축키 억제)은 InputManager.h 의 "플랫폼별 구현" 절에 있는 훅(registerPlatformGamepads() 등)으로만
// 연결하고, 실제 구현은 Windows/InputManagerWin32.cpp 와 Linux/InputManagerX11.cpp 에 있다.
// 새 플랫폼을 추가하거나 플랫폼 동작을 바꿀 때 이 파일을 건드릴 필요가 없어야 한다.

namespace sw
{
    SW_LOG_CALLER( "InputManager" );
} // namespace sw

namespace sw
{
    namespace
    {
        struct InputManagerInternal
        {
            /// `GamepadDevice::isControlDown` 이 트리거에 쓰는 컨트롤 번호입니다.
            static constexpr uint16 kLeftTriggerControl  = 100;
            static constexpr uint16 kRightTriggerControl = 101;
            static constexpr uint32 kLeftTriggerBit      = 1u << static_cast<uint32>( GamepadButton::Count );
            static constexpr uint32 kRightTriggerBit     = 1u << ( static_cast<uint32>( GamepadButton::Count ) + 1 );

            /** @brief 컨트롤 하나의 가림 비트입니다(버튼은 열거자 번호, 트리거는 버튼 뒤 두 비트). 모르는 컨트롤은 0 입니다. */
            static uint32 makeGamepadControlBit( uint16 controlIndex )
            {
                if ( controlIndex == kLeftTriggerControl )
                    return kLeftTriggerBit;
                if ( controlIndex == kRightTriggerControl )
                    return kRightTriggerBit;
                if ( controlIndex < static_cast<uint16>( GamepadButton::Count ) )
                    return 1u << controlIndex;
                return 0;
            }

            /** @brief 패드 하나에서 눌려 있는 컨트롤(버튼과 두 트리거)의 비트를 모읍니다. */
            static uint32 collectDownMask( const GamepadDevice& pad )
            {
                uint32 mask = 0;
                for ( uint16 control = 0; control < static_cast<uint16>( GamepadButton::Count ); ++control )
                {
                    if ( pad.isControlDown( control ) )
                        mask |= makeGamepadControlBit( control );
                }
                if ( pad.isControlDown( kLeftTriggerControl ) )
                    mask |= kLeftTriggerBit;
                if ( pad.isControlDown( kRightTriggerControl ) )
                    mask |= kRightTriggerBit;
                return mask;
            }
        };
    } // namespace

    InputManager::InputManager()
        : _queueRawEvent{}
        , _droppedRawEventCount{ 0 }
        , _listDevice{}
        , _pKeyboard{ nullptr }
        , _pMouse{ nullptr }
        , _pGamepad{ nullptr }
        , _pInputMap{ nullptr }
        , _listDrainedEvent{}
        , _listFramePress{}
        , _frameLookDelta{}
        , _pfnNowMicroseconds{ &MonotonicClock::nowMicroseconds }
        , _frameEndMicroseconds{ 0 }
        , _pVirtualInput{ nullptr }
        , _virtualFrameIndex{ 0 }
        , _beginFrameCount{ 0 }
        , _activeGlyphStyle{ InputGlyphStyle::KeyboardMouse }
        , _onActiveDeviceChanged{}
        , _onGamepadConnectionChanged{}
        , _arrOnTextInput{}
        , _arrOnTextComposition{}
        , _arrCapturedKeyMask{}
        , _arrCapturedGamepadMask{}
        , _keyboardFocus{ InputKeyboardFocus::Game }
        , _virtualInputMode{ VirtualInputMode::Exclusive }
        , _cursorShape{ CursorShape::Arrow }
        , _pendingHighSurrogate{ 0 }
        , _consumedButtonMask{ 0 }
        , _bInitialized{ SW_FALSE }
        , _bInputMuted{ SW_FALSE }
        , _bWindowFocused{ SW_TRUE }
        , _bMouseLockEngaged{ SW_FALSE }
        , _bAltHeld{ SW_FALSE }
        , _bCursorHiddenApplied{ SW_FALSE }
        , _bOSDeviceInputBlocked{ SW_FALSE }
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

    uint32 InputManager::onModuleUnloading( const void* pBegin, const void* pEnd, bool& outKeepLibraryMapped )
    {
        (void)outKeepLibraryMapped;
        uint32 releasedCount{ 0 };
        releasedCount += _onActiveDeviceChanged.releaseIfCodeWithin( pBegin, pEnd ) ? 1u : 0u;
        releasedCount += _onGamepadConnectionChanged.releaseIfCodeWithin( pBegin, pEnd ) ? 1u : 0u;
        for ( size_t focusIndex = 0; focusIndex < kKeyboardFocusCount; ++focusIndex )
        {
            releasedCount += _arrOnTextInput[focusIndex].releaseIfCodeWithin( pBegin, pEnd ) ? 1u : 0u;
            releasedCount += _arrOnTextComposition[focusIndex].releaseIfCodeWithin( pBegin, pEnd ) ? 1u : 0u;
        }

        // 모듈이 등록한 장치는 vtable 이 그 이미지에 있다. 언로드한 뒤 폴링하면 사라진 코드로 점프한다. 소멸자가 아직 있는 지금 제거한다.
        vector<IInputDevice*> listModuleDevice;
        for ( const unique_ptr<IInputDevice>& pDevice : _listDevice )
        {
            if ( ModuleLibraryUtil::isAddressWithin( ModuleLibraryUtil::findVtableAddress( pDevice.get() ), pBegin, pEnd ) )
                listModuleDevice.push_back( pDevice.get() );
            else
                releasedCount += pDevice->removeCodeWithin( pBegin, pEnd );
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

        RawInputEvent stamped     = rawEvent;
        stamped._timeMicroseconds = _pfnNowMicroseconds();
        const bool bPushed        = _queueRawEvent.enqueue( stamped );
        if ( bPushed == false )
            _droppedRawEventCount.fetch_add( 1, std::memory_order_relaxed );
        return bPushed;
    }

    bool InputManager::postWindowStateEvent( const RawInputEvent& rawEvent )
    {
        // 음소거는 "입력을 무시한다" 는 뜻이지 "창이 포커스를 잃은 것도 모른다" 는 뜻이 아니다. 음소거 중에 포커스를 잃고
        // 음소거가 풀린 뒤에도 그 전의 눌림이 남아 있으면 안 된다.
        RawInputEvent stamped     = rawEvent;
        stamped._timeMicroseconds = _pfnNowMicroseconds();
        const bool bPushed        = _queueRawEvent.enqueue( stamped );
        if ( bPushed == false )
            _droppedRawEventCount.fetch_add( 1, std::memory_order_relaxed );
        return bPushed;
    }

    void InputManager::setClock( ClockFunction pfnNowMicroseconds )
    {
        SW_ASSERT( pfnNowMicroseconds != nullptr );
        _pfnNowMicroseconds = pfnNowMicroseconds;
    }

    void InputManager::assignFrameFractions( size_t osEventCount, float32 deltaSeconds )
    {
        // 창은 지난 프레임 길이만큼이다 — 엔진이 측정한 델타와 같은 시계(QPC)를 본다. 창 밖(그보다 먼저 들어온 이벤트)은 0, 시계 앞은 1 로 자른다.
        const int64   windowMicroseconds = MathUtil::max( int64{ 1 }, static_cast<int64>( static_cast<float64>( deltaSeconds ) * 1.0e6 ) );
        const int64   windowStart        = _frameEndMicroseconds - windowMicroseconds;
        const float64 inverseWindow      = 1.0 / static_cast<float64>( windowMicroseconds );
        for ( size_t eventIndex = 0; eventIndex < osEventCount; ++eventIndex )
        {
            RawInputEvent& event = _listDrainedEvent[eventIndex];
            const float64  place = static_cast<float64>( event._timeMicroseconds - windowStart ) * inverseWindow;
            event._frameFraction = static_cast<float32>( MathUtil::clamp( place, 0.0, 1.0 ) );
        }
    }

    void InputManager::recordFramePress( const RawInputEvent& rawEvent )
    {
        InputSlot  slot{};
        const bool bKeyPress     = rawEvent._type == RawInputEventType::KeyDown && rawEvent._bRepeat == SW_FALSE;
        const bool bMousePress   = rawEvent._type == RawInputEventType::MouseButtonDown || rawEvent._type == RawInputEventType::MouseDoubleClick;
        const bool bGamepadPress = rawEvent._type == RawInputEventType::GamepadButtonDown;
        if ( bKeyPress )
            slot = InputSlot::fromKey( rawEvent._payload._keyData._key );
        else if ( bMousePress )
            slot = InputSlot::fromMouseButton( rawEvent._payload._mouseData._button );
        else if ( bGamepadPress )
            slot = InputSlot{ InputDeviceKind::Gamepad, rawEvent._deviceIndex, static_cast<uint16>( rawEvent._payload._gamepadData._button ) };
        else
            return;
        for ( const FramePress& press : _listFramePress )
        {
            if ( press._slot == slot )
                return; // 첫 누름만 남긴다
        }
        _listFramePress.push_back( FramePress{
            slot, InputPressInstant{ _frameLookDelta, rawEvent._frameFraction, rawEvent._timeMicroseconds }
        } );
    }

    bool InputManager::tryGetPressInstant( const InputSlot& slot, InputPressInstant& outInstant ) const
    {
        for ( const FramePress& press : _listFramePress )
        {
            if ( press._slot == slot )
            {
                outInstant = press._instant;
                return true;
            }
        }
        // 이벤트 없이 눌렸다 — 폴링하는 장치(패드 · 트리거)는 `beginFrame` 의 폴링이 본 순간, 곧 프레임 끝이다.
        const IInputDevice* pDevice = getDevice( slot._deviceKind, slot._deviceIndex );
        if ( pDevice == nullptr || pDevice->wasControlPressed( slot._controlIndex ) == false )
            return false;
        outInstant = InputPressInstant{ _frameLookDelta, 1.0f, _frameEndMicroseconds };
        return true;
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

        // 대표 편의 포인터(키보드, 마우스, 게임패드)가 해제되면 남은 장치 중 같은 종류로 다시 잡는다.
        const bool bNoPrimaryDeviceRemoved = bWasKeyboard == false && bWasMouse == false && bWasGamepad == false;
        if ( bNoPrimaryDeviceRemoved )
            return;

        for ( const auto& pRemaining : _listDevice )
        {
            if ( pRemaining == nullptr )
                continue;
            const bool bAdoptAsMouse    = bWasMouse && _pMouse == nullptr && pRemaining->getDeviceKind() == InputDeviceKind::Mouse;
            const bool bAdoptAsGamepad  = bWasGamepad && _pGamepad == nullptr && pRemaining->getDeviceKind() == InputDeviceKind::Gamepad;
            const bool bReplaceKeyboard = bWasKeyboard && _pKeyboard == nullptr && pRemaining->getDeviceKind() == InputDeviceKind::Keyboard;
            if ( bReplaceKeyboard )
                _pKeyboard = static_cast<KeyboardDevice*>( pRemaining.get() );
            else if ( bAdoptAsMouse )
                _pMouse = static_cast<MouseDevice*>( pRemaining.get() );
            else if ( bAdoptAsGamepad )
                _pGamepad = static_cast<GamepadDevice*>( pRemaining.get() );
        }
    }

    IInputDevice* InputManager::getDevice( InputDeviceKind kind, uint32 deviceIndex ) const
    {
        for ( const auto& pDev : _listDevice )
        {
            const bool bMatchingDevice = pDev != nullptr && pDev->getDeviceKind() == kind && pDev->getDeviceIndex() == deviceIndex;
            if ( bMatchingDevice )
                return pDev.get();
        }
        return nullptr;
    }

    GamepadDevice* InputManager::getGamepad( uint32 deviceIndex ) const
    {
        IInputDevice* pDev = getDevice( InputDeviceKind::Gamepad, deviceIndex );
        return static_cast<GamepadDevice*>( pDev );
    }

    // 프레임 단계(폴링 → 큐 비우기 → 가상 입력 → 적용 → 마무리 → 글리프 감지 → 맵 갱신)의 순서가 곧 의미인 선형 본문이라 나누지 않는다.
    void InputManager::beginFrame( float32 deltaSeconds )
    {
        SW_MEMORY_SCOPE( EngineMisc );
        ++_beginFrameCount;
        const uint32 droppedCount = _droppedRawEventCount.exchange( 0, std::memory_order_relaxed );
        if ( droppedCount > 0 )
            SW_LOG_WARNING( "Raw input event queue full (capacity=%d). %d event(s) dropped in previous frame.", static_cast<int32>( _queueRawEvent.capacity() ), droppedCount );

        // 지난 프레임까지 뗀 패드 컨트롤은 가림을 푼다(폴링 전이라 지난 프레임의 상태다 — 키와 같은 규칙).
        releaseCapturedGamepadControls();

        // 1) 등록된 모든 장치의 프레임을 시작하고(이전 프레임 엣지 초기화) 폴링한다. 배타 가상 입력이면 폴링하지 않는다.
        //    패드 폴링(XInput, 조이스틱)이 가상 패드 상태를 실제 패드 값으로 덮어쓰기 때문이다.
        const bool bOSSuppressed = isOSInputSuppressed();
        for ( auto& pDev : _listDevice )
        {
            if ( pDev != nullptr )
            {
                pDev->onFrameBegin( deltaSeconds );
                if ( bOSSuppressed == false )
                    pDev->poll( deltaSeconds );
                pDev->onPolled();
            }
        }

        // 지난 프레임까지 뗀 키는 가림을 푼다. 뗀 프레임에는 아직 가린다. 게임이 누른 적 없는 키의 "뗌" 을 보지 않게 하기 위해서다.
        releaseCapturedKeys();

        // 2) 락프리 큐에 비동기로 들어온 이번 프레임 원시 이벤트를 한꺼번에 꺼낸다
        _listDrainedEvent.clear();
        _queueRawEvent.drain( _listDrainedEvent );
        // 배타 가상 입력: OS 가 낸 장치 이벤트와 창 포커스 이벤트를 버린다(글자 입력도 버리며, 가상 원천이 `makeTextInput` 으로 따로 낸다).
        // 에디터 UI 가 ImGui 키를 넘긴 이벤트(`_bEditorUI`)는 OS 입력이 아니라 에디터의 것이라 남긴다 — 시나리오가 ImGui 에 넣은 키가 같은 길로 닿는다.
        if ( bOSSuppressed )
        {
            (void)VectorUtil::removeIf( _listDrainedEvent, []( const RawInputEvent& event )
            { return event._bEditorUI == SW_FALSE; } ); // 지운 수는 쓰지 않는다
        }
        // OS 이벤트에 프레임 안 자리를 채운다(서브틱 입력). 가상 이벤트는 녹화된 자리를 그대로 쓴다 — 재생이 녹화와 같은 순간을 본다.
        _frameEndMicroseconds = _pfnNowMicroseconds();
        assignFrameFractions( _listDrainedEvent.size(), deltaSeconds );
        // 2-1) 가상 입력 원천의 이번 프레임 이벤트를 OS 이벤트 뒤에 붙인다. 같은 재생 경로(3)를 탄다.
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

        // 2-2) 가상 커서(패드 스틱 → 마우스 이동 · 버튼)의 이벤트를 붙인다. 가상 입력이 붙어 있어도 배타 모드(OS 입력을 버려 패드가 스크립트의 것뿐)면 돈다 —
        //      시나리오의 `GamepadAxis` 가 만든 스틱이 커서를 움직이는 길(실기와 같은 길)을 시나리오가 볼 수 있어야 한다. 배타가 아니면 사람의 패드와 스크립트가
        //      섞이므로 쉰다. 켜고 끄기와 스틱은 게임플레이 입력 맵이 정한다(`<InputMap virtualCursor>`) — 맵을 바꾸면 이 프레임부터 따른다.
        syncVirtualCursorToMap();
        const bool bVirtualInputOwnsPad = _pVirtualInput != nullptr && _virtualInputMode != VirtualInputMode::Exclusive;
        const bool bVirtualCursorActive = _virtualCursor.isEnabled() && bVirtualInputOwnsPad == false && _pMouse != nullptr;
        if ( bVirtualCursorActive )
        {
            // 커서는 게임 화면 안에 머문다(창 전체가 아니다 — 에디터 게임 뷰는 창의 일부다).
            const size_t firstCursorEvent = _listDrainedEvent.size();
            _virtualCursor.emitFrame( getGamepad( _virtualCursor.getSettings()._padIndex ), _pMouse->getPosition(), _gameView, deltaSeconds, _listDrainedEvent );
            for ( size_t eventIndex = firstCursorEvent; eventIndex < _listDrainedEvent.size(); ++eventIndex )
            {
                _listDrainedEvent[eventIndex]._frameFraction = 1.0f; // 패드를 지금 폴링해 만든 이벤트다 — 프레임 끝의 순간이다
            }
        }

        // 3) 꺼낸 원시 이벤트를 들어온 순서대로 장치에 적용한다(새 프레임 엣지 플래그 설정). 포커스를 잃는 이벤트도 이 순서 안에서
        //    적용해야 그보다 먼저 들어온 키 누름이 리셋 뒤에 다시 적용되지 않는다(알트탭하면 캐릭터가 계속 달리는 문제가 생긴다).
        //    적용하면서 누름마다 그 순간과 그 앞까지의 시선 누적을 적는다(`tryGetPressInstant`).
        _listFramePress.clear();
        _frameLookDelta = float2{};
        for ( const RawInputEvent& rawEvent : _listDrainedEvent )
        {
            recordFramePress( rawEvent );
            dispatchRawEvent( rawEvent );
            const bool bLookMotion = rawEvent._type == RawInputEventType::MouseMove || rawEvent._type == RawInputEventType::MouseRawDelta;
            if ( bLookMotion )
                _frameLookDelta += rawEvent._payload._mouseData._rawDelta;
        }

        // 콘솔이 키보드를 쥔 프레임에 눌려 있는 패드 컨트롤은 콘솔을 닫은 뒤에도 뗄 때까지 게임에 가린다(닫는 B 가 게임의 B 로 새지 않게).
        if ( _keyboardFocus == InputKeyboardFocus::DevConsole )
            captureHeldGamepadControls();

        // 이벤트를 모두 적용한 뒤 장치마다 한 번 마무리한다. 마우스 스무딩은 이벤트 수가 아니라 흐른 시간으로 한 번 적용한다.
        for ( auto& pDev : _listDevice )
        {
            if ( pDev != nullptr )
                pDev->onEventsDispatched( deltaSeconds );
        }

        // 가운데 고정 잠금: 이번 프레임의 델타를 측정한 뒤에 커서를 되돌린다. 잠금이 쉬는 동안(포커스 밖, Alt, 콘솔)은 되돌리지 않는다.
        // 되돌리면 잠금이 풀린 커서도 프레임마다 가운데로 끌려와서 제목 표시줄이나 X 에 닿지 못한다.
        const bool bRecenterLockedCursor = _pMouse != nullptr && _pMouse->getLockMode() == MouseLockMode::LockedInCenter && isMouseLockActive();
        if ( bRecenterLockedCursor )
            recenterLockedCursorPlatform();

        // 4) 활성 장치 자동 감지(O(1) 플래그 조회)
        if ( _pGamepad != nullptr && _pGamepad->isConnected() )
        {
            const float2 stick                 = _pGamepad->getLeftStick();
            const bool   bStickActive          = stick.getLengthSquared() > 0.04f;
            const bool   bGamepadInputDetected = bStickActive || _pGamepad->getLeftTrigger() > 0.1f || _pGamepad->getRightTrigger() > 0.1f || _pGamepad->wasAnyButtonPressed();
            if ( bGamepadInputDetected )
                setActiveGlyphStyle( InputGlyphStyle::GamepadXbox );
        }

        if ( _pKeyboard != nullptr && _pKeyboard->wasAnyKeyPressed() )
            setActiveGlyphStyle( InputGlyphStyle::KeyboardMouse );

        if ( _pMouse != nullptr )
        {
            const int2 mouseDelta          = _pMouse->getDelta();
            const bool bMouseInputDetected = _pMouse->wasAnyButtonPressed() || mouseDelta != int2{} || _pMouse->getMouseWheel() != 0.0f;
            if ( bMouseInputDetected )
                setActiveGlyphStyle( InputGlyphStyle::KeyboardMouse );
        }

        // 패드가 낸 마우스 이벤트는 마우스를 만졌다는 뜻이 아니다 — 글리프는 패드로 남긴다.
        if ( _virtualCursor.didEmitLastFrame() )
            setActiveGlyphStyle( InputGlyphStyle::GamepadXbox );

        // 5) 통합 InputMap 을 이번 프레임 입력으로 갱신한다(게임플레이가 읽는 맵). 갱신은 맵의 주인이 한다.
        //    엔진 루프, 리플레이 재동기화, 테스트가 같은 경로를 탄다.
        if ( _pInputMap != nullptr )
            _pInputMap->update( deltaSeconds );
    }

    void InputManager::syncVirtualCursorToMap()
    {
        const InputMapVirtualCursor stick   = _pInputMap != nullptr ? _pInputMap->getVirtualCursorStick() : InputMapVirtualCursor::Off;
        const bool                  bWanted = stick != InputMapVirtualCursor::Off;
        if ( bWanted != _virtualCursor.isEnabled() )
            _virtualCursor.setEnabled( bWanted );
        if ( bWanted == false )
            return;
        VirtualCursorSettings settings = _virtualCursor.getSettings();
        settings._bRightStick          = stick == InputMapVirtualCursor::RightStick;
        _virtualCursor.setSettings( settings );
    }

    // 이벤트 종류마다 장치 한 곳에 적용하는 짧은 switch 디스패처라 나누지 않는다.
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
                    // 게임이 아닌 쪽이 키보드를 가진 동안 눌린 키는 뗄 때까지 게임에 가린다. 게임이 가진 동안 새로 누르면 가림을 해제한다.
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
                // 가상 연결 이벤트가 가상 세션 동안 패드의 연결 여부를 정한다(실제 연결은 폴링이 정한다).
                GamepadDevice* pPad = getGamepad( rawEvent._deviceIndex );
                if ( rawEvent._bSynthetic == SW_TRUE && pPad != nullptr )
                    pPad->setVirtualConnected( rawEvent._payload._gamepadData._bConnected == SW_TRUE );
                if ( _onGamepadConnectionChanged.isBound() )
                    _onGamepadConnectionChanged.get()( rawEvent._deviceIndex, rawEvent._payload._gamepadData._bConnected == SW_TRUE );
                break;
            }

            case RawInputEventType::TextInput:
            {
                onTextInput( rawEvent._payload._textData._arrUTF8 );
                break;
            }

            case RawInputEventType::TextComposition:
            {
                onTextComposition( rawEvent._payload._textData._arrUTF8 );
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
        for ( uint32& mask : _arrCapturedGamepadMask )
        {
            mask = 0;
        }
    }

    void InputManager::setKeyboardFocus( InputKeyboardFocus focus )
    {
        if ( focus == InputKeyboardFocus::Count )
            return;
        _keyboardFocus = focus;
        // 개발 콘솔이나 텍스트 입력이 키보드를 가진 동안은 마우스도 놓는다(커서로 창 밖이나 제목 표시줄에 갈 수 있게).
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

    bool InputManager::isGamepadControlVisibleToGame( uint32 deviceIndex, uint16 controlIndex ) const
    {
        if ( _keyboardFocus == InputKeyboardFocus::DevConsole )
            return false;
        if ( deviceIndex >= kMaxGamepadSlot )
            return true;
        return ( _arrCapturedGamepadMask[deviceIndex] & InputManagerInternal::makeGamepadControlBit( controlIndex ) ) == 0;
    }

    void InputManager::captureHeldGamepadControls()
    {
        for ( uint32 padIndex = 0; padIndex < kMaxGamepadSlot; ++padIndex )
        {
            const GamepadDevice* pPad = getGamepad( padIndex );
            if ( pPad != nullptr )
                _arrCapturedGamepadMask[padIndex] |= InputManagerInternal::collectDownMask( *pPad );
        }
    }

    void InputManager::releaseCapturedGamepadControls()
    {
        for ( uint32 padIndex = 0; padIndex < kMaxGamepadSlot; ++padIndex )
        {
            if ( _arrCapturedGamepadMask[padIndex] == 0 )
                continue;
            const GamepadDevice* pPad = getGamepad( padIndex );
            // 패드가 없어졌으면 가릴 컨트롤도 없다. 있으면 아직 누르고 있는 것만 남긴다.
            _arrCapturedGamepadMask[padIndex] &= pPad != nullptr ? InputManagerInternal::collectDownMask( *pPad ) : 0u;
        }
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

    Subscription InputManager::bindTextInputCallback( TextInputDelegate callback, InputKeyboardFocus owner )
    {
        SW_ASSERT( owner != InputKeyboardFocus::Count );
        if ( owner == InputKeyboardFocus::Count )
            return Subscription{};
        return _arrOnTextInput[static_cast<size_t>( owner )].bind( std::move( callback ) );
    }

    Subscription InputManager::bindTextCompositionCallback( TextInputDelegate callback, InputKeyboardFocus owner )
    {
        SW_ASSERT( owner != InputKeyboardFocus::Count );
        if ( owner == InputKeyboardFocus::Count )
            return Subscription{};
        return _arrOnTextComposition[static_cast<size_t>( owner )].bind( std::move( callback ) );
    }

    void InputManager::setActiveGlyphStyle( InputGlyphStyle type )
    {
        if ( _activeGlyphStyle != type )
        {
            _activeGlyphStyle = type;
            if ( _onActiveDeviceChanged.isBound() )
                _onActiveDeviceChanged.get()( type );
        }
    }

    bool InputManager::wasAnyInputPressed() const
    {
        // 키보드는 게임이 포커스를 가질 때만 "아무 키" 로 본다. 콘솔에 치는 글자가 "Press Any Key" 를 넘기지 않게 하기 위해서다.
        const bool bKeyboardAnyKey = _pKeyboard != nullptr && _keyboardFocus == InputKeyboardFocus::Game && _pKeyboard->wasAnyKeyPressed();
        if ( bKeyboardAnyKey )
            return true;

        if ( _pMouse != nullptr && _pMouse->wasAnyButtonPressed() )
            return true;

        const bool bGamepadAnyButton = _pGamepad != nullptr && _pGamepad->isConnected() && _pGamepad->wasAnyButtonPressed();
        if ( bGamepadAnyButton )
            return true;

        return false;
    }

    bool InputManager::findFirstPressedSlot( InputSlot& outSlot ) const
    {
        if ( _pKeyboard != nullptr && _keyboardFocus != InputKeyboardFocus::DevConsole )
        {
            for ( uint32 index = static_cast<uint32>( Key::Unknown ) + 1; index < static_cast<uint32>( Key::Count ); ++index )
            {
                if ( _pKeyboard->wasKeyPressed( static_cast<Key>( index ) ) )
                {
                    outSlot = InputSlot::fromKey( static_cast<Key>( index ) );
                    return true;
                }
            }
        }
        if ( _pMouse != nullptr )
        {
            for ( uint32 index = 0; index < static_cast<uint32>( MouseButton::Count ); ++index )
            {
                if ( _pMouse->wasButtonPressed( static_cast<MouseButton>( index ) ) )
                {
                    outSlot = InputSlot::fromMouseButton( static_cast<MouseButton>( index ) );
                    return true;
                }
            }
        }
        for ( uint32 padIndex = 0; padIndex < kMaxGamepadSlot; ++padIndex )
        {
            const GamepadDevice* pPad = getGamepad( padIndex );
            if ( pPad == nullptr || pPad->isConnected() == false )
                continue;
            for ( uint32 index = 0; index < static_cast<uint32>( GamepadButton::Count ); ++index )
            {
                if ( pPad->wasButtonPressed( static_cast<GamepadButton>( index ) ) )
                {
                    outSlot = InputSlot::fromGamepadButton( static_cast<GamepadButton>( index ), static_cast<uint8>( padIndex ) );
                    return true;
                }
            }
        }
        return false;
    }

    void InputManager::onTextInput( string_view text )
    {
        const TextInputDelegate& onTextInput = _arrOnTextInput[static_cast<size_t>( _keyboardFocus )].get();
        if ( text.empty() == false && onTextInput.isBound() )
            onTextInput( text );
    }

    void InputManager::onTextComposition( string_view text )
    {
        const TextInputDelegate& onTextComposition = _arrOnTextComposition[static_cast<size_t>( _keyboardFocus )].get();
        if ( text.empty() == false && onTextComposition.isBound() )
            onTextComposition( text );
    }

    float2 InputManager::getMousePositionNormalized() const
    {
        if ( _pMouse == nullptr || _gameView.isEmpty() )
            return float2{};
        const float32 localX = static_cast<float32>( _pMouse->getPositionX() - _gameView._origin._x );
        const float32 localY = static_cast<float32>( _pMouse->getPositionY() - _gameView._origin._y );
        return float2{ MathUtil::clamp( localX / static_cast<float32>( _gameView._size._x ), 0.0f, 1.0f ),
                       MathUtil::clamp( localY / static_cast<float32>( _gameView._size._y ), 0.0f, 1.0f ) };
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
        // 배타 가상 입력 중에는 게임이 OS 포인터를 잡지 않는다(사람이 같은 기계를 쓴다). 잠금 모드(장치 상태)는 남겨 두고, 가상 입력을 떼면 `syncMouseLock` 이 다시 건다.
        if ( isOSInputSuppressed() )
            return false;
        const bool bInputWindowInactive = _bWindowFocused == SW_FALSE || _bAltHeld == SW_TRUE || _keyboardFocus != InputKeyboardFocus::Game;
        if ( bInputWindowInactive )
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

        // `ShowCursor` 는 카운터다. 바뀔 때만 부른다. 부를 때마다 더하면 레이어 하나를 올릴 때마다 숨김을 해제하는 호출이 한 번 더 필요해진다.
        const bool bHide = _pMouse != nullptr && _pMouse->isCursorVisible() == false && isPointerOwnedByGame();
        if ( bHide == ( _bCursorHiddenApplied == SW_TRUE ) )
            return;
        setCursorVisiblePlatform( bHide == false );
        _bCursorHiddenApplied = bHide ? SW_TRUE : SW_FALSE;
        if ( bHide == false )
            applyCursorShapePlatform(); // 숨긴 동안 바뀐 모양을 다시 보일 때 적용한다(X11 은 보이기가 기본 커서로 되돌린다)
    }

    void InputManager::setCursorShape( CursorShape shape )
    {
        if ( _cursorShape == shape )
            return;
        _cursorShape = shape;
        if ( _bCursorHiddenApplied == SW_FALSE )
            applyCursorShapePlatform();
    }

    void InputManager::onPlatformFocusChanged( bool bFocused )
    {
        _bWindowFocused = bFocused ? SW_TRUE : SW_FALSE;
        if ( bFocused == false )
        {
            _bMouseLockEngaged = SW_FALSE;
            _bAltHeld          = SW_FALSE; // Alt+Tab 의 Alt 뗌은 다른 창으로 간다
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
        const bool bIgnoreLockClick = bWasActive || isMouseLockActive() == false || button >= MouseButton::Count;
        if ( bIgnoreLockClick )
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
        // 잠금을 새로 켤 때 창이 지금 전경이면 바로 잡는다(시작 직후나 Esc 로 다시 잠글 때). 아니면 클라이언트를 눌러야 잡는다.
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

    void InputManager::attachVirtualInput( IVirtualInputSource* pSource, VirtualInputMode mode, bool bResetState )
    {
        if ( pSource == nullptr )
        {
            detachVirtualInput();
            return;
        }
        const bool bWasExclusive = isOSInputSuppressed();
        _pVirtualInput           = pSource;
        _bOSDeviceInputBlocked   = SW_FALSE; // 붙이는 쪽(자동화 실행기)이 다시 켠다
        _virtualInputMode        = mode;
        _virtualFrameIndex       = 0;
        // 사람이 누르고 있던 입력과 큐에 남은 OS 이벤트가 첫 프레임에 새지 않게 지운다.
        if ( bResetState )
            resetAllDeviceState();
        const bool bExclusive = mode == VirtualInputMode::Exclusive;
        // 배타 모드는 사람의 장치를 버린다. 포인터도 (0, 0) 에서 시작해야(가상 입력 스크립트의 단추 이벤트 기본 위치와 같다) 붙기 전 실제 마우스 위치가
        // 커서를 따라가는 게임 상태(건설 커서 등)에 새지 않는다. 새면 같은 시나리오가 실행마다 다른 결과를 낸다.
        if ( bExclusive )
        {
            _queueRawEvent.clear();
            if ( bResetState && _pMouse != nullptr )
                _pMouse->setPositionWithoutDelta( 0, 0 );
        }
        if ( bExclusive != bWasExclusive )
            setGamepadVirtualSession( bExclusive );
        syncMouseLock();
        SW_LOG_INFO( "Virtual input attached (%#)", bExclusive ? "exclusive" : "mixed" );
    }

    void InputManager::detachVirtualInput( bool bResetState )
    {
        if ( _pVirtualInput == nullptr )
            return;
        const bool bWasExclusive = isOSInputSuppressed();
        _pVirtualInput           = nullptr;
        _bOSDeviceInputBlocked   = SW_FALSE;
        if ( bResetState )
            resetAllDeviceState();
        if ( bWasExclusive )
            setGamepadVirtualSession( false );
        syncMouseLock();
        SW_LOG_INFO( "Virtual input detached after %# frame(s)", _virtualFrameIndex );
    }

    void InputManager::setOSDeviceInputBlocked( bool bBlocked )
    {
        // 배타 가상 입력 없이 막으면 사람도 자동화도 입력을 넣지 못한다. 붙어 있을 때만 켠다.
        const bool bApply = bBlocked && isOSInputSuppressed();
        if ( bBlocked && bApply == false )
            SW_LOG_WARNING( "OS device input stays live: blocking needs an exclusive virtual input source" );
        _bOSDeviceInputBlocked = bApply ? SW_TRUE : SW_FALSE;
    }

    bool InputManager::shouldDropNativeEvent( const NativeWindowEvent& event ) const
    {
        return _bOSDeviceInputBlocked == SW_TRUE && ( event.isMouseInput() || event.isKeyboardInput() );
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

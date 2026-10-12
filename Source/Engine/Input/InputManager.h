/**
 * @file InputManager.h
 * @brief App 과 Game 이 쓰는 입력 장치 레지스트리, 락프리 비동기 이벤트 큐, 이벤트 디스패치를 한데 모은 허브입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Container/ConcurrentQueue.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Module/ModuleUnloadListener.h"

#include "Engine/Input/Devices/GamepadDevice.h"
#include "Engine/Input/Devices/KeyboardDevice.h"
#include "Engine/Input/Devices/MouseDevice.h"
#include "Engine/Input/GamepadButtonUtil.h"
#include "Engine/Input/IInputDevice.h"
#include "Engine/Input/KeyCodeUtil.h"
#include "Engine/Input/RawInputEvent.h"
#include "Engine/Input/Virtual/IVirtualInputSource.h"

namespace sw
{
    struct NativeWindowEvent;

    class InputMap;

    /** @brief 지금 활성인 입력 장치 타입입니다(UI 글리프 자동 전환용). */
    enum class InputGlyphStyle : uint8
    {
        KeyboardMouse = 0,
        GamepadXbox,
        GamepadPlayStation,
        GamepadSwitch
    };

    /**
     * @brief 키보드(키 조회와 글자 입력)를 지금 누가 받는지 나타냅니다.
     * @details `Game` 이 아니면 게임 쪽 조회(`InputManager::isKeyDown`, 통합 InputMap 의 키보드 바인딩)는 "안 눌림" 이고, 글자는 그 주인의
     *          콜백에만 간다. 장치 상태는 계속 갱신한다.
     */
    enum class InputKeyboardFocus : uint8
    {
        Game = 0,   ///< 게임 코드와 통합 InputMap 이 받습니다.
        DevConsole, ///< 게임 창의 개발 콘솔이 열려 있는 동안 받습니다.
        UI,         ///< 런타임 UI 의 글자 입력 필드가 포커스를 가진 동안 받습니다(`UISystem` 이 잡고 놓습니다).
        Count
    };

    /**
     * @class InputManager
     * @brief 다형 IInputDevice 를 등록하고 관리하며, 락프리 원시 이벤트 큐로 OS 메시지를 프레임에 맞추는 중앙 허브입니다.
     * @details 모듈이 단 콜백(장치 변경, 게임패드 연결, 글자 입력, 조합)과 모듈이 등록한 장치를 가지므로 `IModuleUnloadListener` 를 구현한다.
     *          모듈 이미지를 언로드하기 전에 그 범위의 콜백을 해제하고, 그 범위에 vtable 이 있는 장치를 등록에서 제거한다(`onModuleUnloading`).
     */
    class SW_API InputManager final : public IModuleUnloadListener
    {
    public:
        using ActiveDeviceChangedDelegate = Delegate<void( InputGlyphStyle )>;
        using GamepadConnectionDelegate   = Delegate<void( uint32, bool )>;
        using TextInputDelegate           = Delegate<void( string_view )>;

        InputManager();
        ~InputManager() override;

        InputManager( const InputManager& )            = delete;
        InputManager& operator=( const InputManager& ) = delete;

        // ------------------------------------------------------------------------------
        // 1) 수명 주기와 프레임 제어
        // ------------------------------------------------------------------------------
        bool initialize();
        void shutdown();

        /**
         * @brief 프레임을 시작합니다. 장치를 폴링하고, 락프리 큐를 비워 이벤트를 들어온 순서대로 적용하고, 프레임 엣지를 맞춥니다.
         * @details 창 메시지는 `processNativeEvent` 가 큐에 넣기만 한다. 장치 상태를 바꾸는 경로는 여기 하나뿐이다.
         *          메시지를 받을 때 상태를 바로 바꾸면, 여기서 엣지를 지운 뒤 재생할 때 이미 눌린 키라서 "새로 눌림" 이 사라진다.
         *          끝에서 통합 InputMap(`getInputMap()`)을 갱신하므로, 그 맵을 따로 `update()` 하면 한 프레임에 두 번 흐른다.
         *          따로 만든 InputMap 은 만든 쪽이 갱신한다.
         * @param deltaSeconds 지난 프레임의 실제 시간(초)입니다. 진동 타이머와 재연결 주기가 이 값으로 흐릅니다.
         */
        void beginFrame( float32 deltaSeconds = 0.016f );
        /** @brief 프레임을 마치며 엣지 플래그와 원시 델타를 리셋합니다. */
        void endFrame();
        /** @brief 창이 포커스를 얻었습니다. 잠금은 다시 잡지 않으며, 클라이언트 영역을 눌러야 잡습니다(`isMouseLockActive`). */
        void onWindowFocusGained();
        /** @brief 창이 포커스를 잃으면 모든 장치 입력 상태를 초기화하고 마우스 클리핑을 풉니다. */
        void onWindowFocusLost();
        /** @brief 등록된 모든 장치의 입력 상태(키, 버튼, 축)를 초기화합니다(리플레이 재동기화 등에 씁니다). */
        void resetAllDeviceState();

        // ------------------------------------------------------------------------------
        // 2) 락프리 원시 이벤트 큐(Lock-Free Event Queue)
        // ------------------------------------------------------------------------------
        /** @brief OS 창 스레드나 백그라운드 폴러에서 잠금 없이 원시 이벤트를 넣습니다. */
        bool postRawEvent( const RawInputEvent& rawEvent );
        /** @brief 대기 중인 원시 이벤트를 꺼냅니다. */
        uint32 drainRawEvents( RawInputEvent* pOutBuffer, uint32 maxCount );
        uint32 getPendingRawEventCount() const { return _queueRawEvent.size(); }

        // ------------------------------------------------------------------------------
        // 3) 다형 장치 레지스트리(Device Registry)
        // ------------------------------------------------------------------------------
        void            registerDevice( unique_ptr<IInputDevice> pDevice );
        void            unregisterDevice( IInputDevice* pDevice );
        IInputDevice*   getDevice( InputDeviceKind kind, uint32 deviceIndex = 0 ) const;
        KeyboardDevice* getKeyboard() const { return _pKeyboard; }
        MouseDevice*    getMouse() const { return _pMouse; }
        GamepadDevice*  getGamepad( uint32 deviceIndex = 0 ) const;

        // ------------------------------------------------------------------------------
        // 4) InputMap · 장치 상태 조회
        // ------------------------------------------------------------------------------
        InputMap&       getInputMap() { return *_pInputMap; }
        const InputMap& getInputMap() const { return *_pInputMap; }

        InputGlyphStyle getActiveGlyphStyle() const { return _activeGlyphStyle; }
        void            setActiveGlyphStyle( InputGlyphStyle type );
        void            setActiveDeviceChangedCallback( ActiveDeviceChangedDelegate callback ) { _onActiveDeviceChanged = std::move( callback ); }
        void            setGamepadConnectionCallback( GamepadConnectionDelegate callback ) { _onGamepadConnectionChanged = std::move( callback ); }
        /** @brief 키보드 포커스가 @p owner 일 때 오는 글자(UTF-8)를 받을 콜백을 등록합니다. 주인마다 하나이며, 다시 등록하면 덮어씁니다. */
        void setTextInputCallback( TextInputDelegate callback, InputKeyboardFocus owner = InputKeyboardFocus::Game );
        /** @brief 키보드 포커스가 @p owner 일 때 오는 조합 중 글자(IME)를 받을 콜백을 등록합니다. */
        void setTextCompositionCallback( TextInputDelegate callback, InputKeyboardFocus owner = InputKeyboardFocus::Game );

        /**
         * @brief 키보드를 받을 쪽을 바꿉니다(개발 콘솔이 열고 닫을 때).
         * @details 포커스가 `Game` 이 아닌 동안 눌린 키는 포커스가 돌아온 뒤에도 뗄 때까지 게임에 보이지 않는다.
         *          콘솔을 닫은 Esc 가 다음 프레임에 게임의 "일시정지" 로 새지 않게 하기 위해서다. 포커스를 넘기기 전부터 눌려 있던 키(달리던 W)는 돌아오면 다시 보인다.
         */
        void               setKeyboardFocus( InputKeyboardFocus focus );
        InputKeyboardFocus getKeyboardFocus() const { return _keyboardFocus; }
        /** @brief 게임 쪽 조회가 이 키를 볼 수 있으면 true 입니다. 포커스가 `Game` 이고, 다른 포커스 동안 눌려서 아직 떼지 않은 키가 아니어야 합니다. */
        bool isKeyVisibleToGame( Key key ) const;

        /** @brief 언로드 리스너 목록에 등록하는 이름입니다. */
        const utf8* getModuleUnloadListenerName() const override { return "input callbacks"; }
        /**
         * @brief 호출 스텁이 [@p pBegin, @p pEnd) 안에 있는 콜백(이 관리자의 콜백 네 개와 장치별 콜백)을 해제하고, vtable 이 그 범위에 있는 장치를 등록에서 제거합니다.
         * @details 장치 소멸자는 아직 로드되어 있는 그 이미지의 코드이므로 지금 제거해야 한다. 해제한 개수(콜백 + 장치)를 반환한다.
         */
        uint32 onModuleUnloading( const void* pBegin, const void* pEnd, bool& outKeepImageMapped ) override;

        bool wasAnyInputPressed() const;
        /**
         * @brief 이번 프레임에 새로 눌린 키보드, 마우스, 게임패드 슬롯 하나를 찾습니다(키 바인딩 창이 받을 키). 순서는 키보드, 마우스, 패드이고, 한 장치 안에서는 열거 순서입니다.
         * @details 장치 상태를 바로 본다. 키보드 포커스가 `UI` 여도 키를 받는다(바인딩 창). 개발 콘솔이 키보드를 가졌으면 키보드는 보지 않는다.
         */
        [[nodiscard]] bool findFirstPressedSlot( InputSlot& outSlot ) const;
        void               onTextInput( string_view text );
        void               onTextComposition( string_view text );

        void setInputMuted( bool bMuted ) { _bInputMuted = bMuted ? SW_TRUE : SW_FALSE; }
        bool isInputMuted() const { return _bInputMuted == SW_TRUE; }

        // ------------------------------------------------------------------------------
        // 5) 게임플레이가 프레임마다 묻는 것만 여기서 답한다. 키, 버튼, 위치, 델타, 휠, 그리고 플랫폼에
        //    적용까지 해야 하는 잠금, 커서, 클립이다. 장치 설정(스무딩, 가속)과 드문 조회(포인터 진입과 이탈,
        //    가로 휠, 원시 델타, 잠금 모드 읽기)는 장치가 답한다(`getMouse()->setSmoothing()`).
        //    같은 답을 두 이름으로 내지 않기 위해 마우스 API 를 여기에 복제하지 않는다.
        // ------------------------------------------------------------------------------
        // 키 조회는 키보드 포커스를 따른다(`isKeyVisibleToGame`). 포커스와 상관없는 장치 상태는 `getKeyboard()` 가 답한다.
        bool isKeyDown( Key key ) const { return _pKeyboard != nullptr && isKeyVisibleToGame( key ) && _pKeyboard->isKeyDown( key ); }
        bool wasKeyPressed( Key key ) const { return _pKeyboard != nullptr && isKeyVisibleToGame( key ) && _pKeyboard->wasKeyPressed( key ); }
        bool wasKeyReleased( Key key ) const { return _pKeyboard != nullptr && isKeyVisibleToGame( key ) && _pKeyboard->wasKeyReleased( key ); }

        bool isMouseButtonDown( MouseButton button ) const { return _pMouse != nullptr ? _pMouse->isButtonDown( button ) : false; }
        bool wasMouseButtonPressed( MouseButton button ) const { return _pMouse != nullptr ? _pMouse->wasButtonPressed( button ) : false; }
        bool wasMouseButtonReleased( MouseButton button ) const { return _pMouse != nullptr ? _pMouse->wasButtonReleased( button ) : false; }

        int2    getMousePosition() const { return _pMouse != nullptr ? _pMouse->getPosition() : int2{}; }
        float2  getMousePositionNormalized() const;
        int2    getMouseDelta() const;
        float32 getMouseWheel() const { return _pMouse != nullptr ? _pMouse->getMouseWheel() : 0.0f; }

        /**
         * @brief 포인터가 창 안에 있고 주어진 사각형(픽셀) 위에 있으면 true 입니다.
         * @details 이것은 액션이 아니라 장치 상태이므로 InputMap 이 아니라 여기에 있다.
         *          쓰는 값(isPointerInside, getMousePosition)도 모두 여기에 있다.
         */
        bool isPointerOverRect( int32 x, int32 y, int32 width, int32 height ) const;

        void setMouseLockMode( MouseLockMode mode );
        void setCursorVisible( bool bVisible );

        void setMouseClipSubRect( int32 left, int32 top, int32 right, int32 bottom );
        void clearMouseClipSubRect();
        /**
         * @brief 마우스 잠금을 지금 OS 에 걸어야 하는지 반환합니다.
         * @details 게임이 잠금을 요청했고(`MouseLockMode` 가 None 이 아님) 아래 조건을 모두 만족할 때만 true 이다.
         *          창이 포커스를 가지고 있고, 포커스를 잃은 뒤 클라이언트 영역을 한 번 눌렀고(활성화만으로는 다시 잠그지 않는다. 제목 표시줄이나 X 를 눌러야 하기 때문이다),
         *          Alt 를 누르고 있지 않고, 키보드 포커스가 `Game` 이다(개발 콘솔이 열려 있지 않다).
         *          게임은 마우스 시점처럼 "잠긴 동안만" 하는 일을 이 값으로 가른다.
         */
        bool isMouseLockActive() const;
        /** @brief 잠금과 커서 숨김을 지금 상태(`isMouseLockActive`)에 맞춰 OS 에 다시 적용합니다. 창 크기나 위치가 바뀌면 부릅니다. */
        void syncMouseLock();

        // ------------------------------------------------------------------------------
        // 6) 게임패드 편의 API(장치에 위임)
        // ------------------------------------------------------------------------------
        float32            getGamepadLeftTrigger( uint32 deviceIndex = 0 ) const;
        float32            getGamepadRightTrigger( uint32 deviceIndex = 0 ) const;
        GamepadBatteryInfo getGamepadBatteryInfo( uint32 deviceIndex = 0 ) const
        {
            GamepadDevice* pPad = getGamepad( deviceIndex );
            return pPad != nullptr ? pPad->getBatteryInfo() : GamepadBatteryInfo{};
        }
        bool setGamepadVibration( float32 leftMotor, float32 rightMotor, uint32 deviceIndex = 0 );
        bool playGamepadVibration( float32 leftMotor, float32 rightMotor, float32 durationSeconds, uint32 deviceIndex = 0 );

        // ------------------------------------------------------------------------------
        // 7) 가상 입력(Virtual Input). 테스트, 시나리오, 입력 리플레이가 OS 이벤트와 같은 경로로 입력을 넣는다
        // ------------------------------------------------------------------------------
        /**
         * @brief 가상 입력 원천을 붙입니다. 빌려 쓰므로 떼기 전까지 살아 있어야 합니다. 이미 붙어 있으면 교체하고, 프레임 번호는 0 부터 다시 셉니다.
         * @details 붙이는 순간 모든 장치 상태를 지운다. 사람이 누르고 있던 키가 테스트의 첫 프레임에 남지 않게 하기 위해서다.
         *          배타 모드면 붙어 있는 동안 OS 의 키, 마우스, 패드 이벤트와 패드 폴링, 창 포커스 이벤트, 커서 가두기와 가운데 되돌리기를 하지 않고,
         *          패드 연결은 가상 연결 이벤트가 정한다. 엔진 키보드 포커스(`setKeyboardFocus`, 개발 콘솔)는 가상 키에도 그대로 적용된다.
         * @param bResetState false 면 장치 상태를 지우지 않습니다. `InputReplay::seekTo` 로 만든 상태에서 이어 재생할 때 씁니다.
         */
        void attachVirtualInput( IVirtualInputSource* pSource, VirtualInputMode mode = VirtualInputMode::Exclusive, bool bResetState = true );
        /**
         * @brief 가상 입력을 뗍니다. 커서 잠금을 지금 상태에 맞춰 다시 적용합니다.
         * @param bResetState true 면 장치 상태를 지웁니다(가상 키가 눌린 채 남지 않게 하기 위해서입니다). 재생으로 만든 상태를 남기려면 false 를 넘깁니다.
         */
        void                 detachVirtualInput( bool bResetState = true );
        bool                 isVirtualInputAttached() const { return _pVirtualInput != nullptr; }
        IVirtualInputSource* getVirtualInput() const { return _pVirtualInput; }
        /** @brief 배타 가상 입력이 붙어 OS 입력을 무시하는 중이면 true 입니다. */
        bool isOsInputSuppressed() const { return _pVirtualInput != nullptr && _virtualInputMode == VirtualInputMode::Exclusive; }
        /**
         * @brief 무인 자동화(시나리오 `input="exclusive"`)가 사람의 마우스와 키보드 입력을 창 메시지 단계에서 버리게 합니다. 언리얼의 `-unattended` 와 같은 의미입니다.
         * @details `isOsInputSuppressed` 는 게임 입력(이 매니저의 장치)만 막는다. 이 표시는 그보다 앞 단계인 창 메시지 분배 셸과 에디터 UI(ImGui 플랫폼 백엔드의 커서 조회)까지 막는다.
         *          창 이벤트(닫기, 크기, 포커스)는 그대로 둔다. 배타 가상 입력이 붙어 있을 때만 켜지고, `attachVirtualInput` 과 `detachVirtualInput` 이 끈다.
         *          입력 리플레이처럼 사람이 에디터를 계속 조작하는 배타 재생에서는 켜지 않는다.
         */
        void setOsDeviceInputBlocked( bool bBlocked );
        /** @brief 사람의 마우스, 키보드 이벤트를 버리는 중이면 true 입니다(`setOsDeviceInputBlocked`). */
        bool isOsDeviceInputBlocked() const { return _bOsDeviceInputBlocked == SW_TRUE; }
        /** @brief 창 메시지 @p event 를 입력 계층 앞에서 버려야 하면 true 입니다. 버리는 중인 마우스와 키보드 이벤트가 해당합니다. */
        bool shouldDropNativeEvent( const NativeWindowEvent& event ) const;
        /** @brief 붙인 뒤 지난 `beginFrame` 횟수이며, 다음 `beginFrame` 이 원천에 넘길 프레임 번호입니다. */
        uint32 getVirtualFrameIndex() const { return _virtualFrameIndex; }
        /** @brief 이번 `beginFrame` 이 장치에 적용한 원시 이벤트입니다(배타 가상 입력이면 가상 이벤트만). 입력 녹화가 읽으며, 다음 `beginFrame` 까지 유효합니다. */
        const vector<RawInputEvent>& getLastFrameEvents() const { return _listDrainedEvent; }
        /** @brief 지금까지 `beginFrame` 을 부른 횟수입니다(프레임마다 한 번 읽는 쪽이 같은 프레임을 두 번 읽지 않게). */
        uint32 getBeginFrameCount() const { return _beginFrameCount; }

        // ------------------------------------------------------------------------------
        // 8) 플랫폼 네이티브 이벤트 처리와 접근성 제어
        // ------------------------------------------------------------------------------
        void onNativeWindowEvent( const NativeWindowEvent& event );
        void processNativeEvent( const NativeWindowEvent& event );
        void pollPlatform();
        void disableWindowsAccessibilityShortcuts();
        void restoreWindowsAccessibilityShortcuts();

    private:
        void dispatchRawEvent( const RawInputEvent& rawEvent );
        /** @brief 모든 패드의 가상 세션을 켜거나 끕니다(배타 가상 입력이 붙고 뗄 때). */
        void setGamepadVirtualSession( bool bVirtual );

        // ------------------------------------------------------------------------------
        // 9) 플랫폼별 구현(Windows: InputManagerWin32.cpp, Linux: InputManagerX11.cpp, 리눅스 전용 서버: Headless/InputManagerHeadless.cpp)
        //    InputManager.cpp 는 이 함수들을 부르기만 한다. 거기에 #ifdef 를 추가하지 말 것.
        // ------------------------------------------------------------------------------
        /** @brief 플랫폼별 게임패드 백엔드를 만들어 registerDevice() 로 등록합니다(Windows: XInput, Linux: 조이스틱 API). */
        void registerPlatformGamepads();

        /**
         * @brief 게임패드 슬롯 네 개를 만들어 등록합니다. 플랫폼이 정하는 것은 만드는 타입뿐입니다.
         * @tparam GamepadType 슬롯 번호를 받는 게임패드 장치입니다(`XInputGamepadDevice`, `LinuxGamepadDevice`).
         * @details 슬롯 수(4), 0번을 편의 포인터로 잡는 것, 연결 콜백을 연결하는 것은 엔진 정책이므로 여기 한 곳에 둔다.
         *          플랫폼 파일마다 두면 한쪽만 고쳐서 그 플랫폼만 조용히 다르게 동작한다.
         */
        template <typename GamepadType>
        void registerGamepadSlots()
        {
            for ( uint32 padIndex = 0; padIndex < kMaxGamepadSlot; ++padIndex )
            {
                auto pGamepad = make_unique<GamepadType>( padIndex );
                // 0번은 편의 API(인자 없는 `getGamepad()`)가 쓰는 캐시다.
                if ( padIndex == 0 )
                    _pGamepad = pGamepad.get();

                pGamepad->setConnectionCallback( [this]( uint32 index, bool bConnected )
                {
                    if ( _onGamepadConnectionChanged.isBound() )
                        _onGamepadConnectionChanged( index, bConnected );
                } );
                registerDevice( std::move( pGamepad ) );
            }
        }
        /** @brief 커서 표시와 숨김을 OS 에 실제로 적용합니다. `syncMouseLock` 이 숨김 상태가 바뀔 때만 부릅니다(Win32 `ShowCursor` 는 카운터입니다). */
        void setCursorVisiblePlatform( bool bVisible );
        /** @brief 게임이 포인터를 가질 수 있는 상태인지 반환합니다. 포커스가 있고, Alt 를 누르지 않았고, 키보드 포커스가 `Game` 이고, (잠금을 요청 중이면) 클릭으로 다시 잡았어야 합니다. */
        bool isPointerOwnedByGame() const;
        /** @brief 창 포커스가 바뀌었습니다(메시지를 받는 곳에서 부릅니다). 잃으면 잠금 재획득 표시와 Alt 상태도 지웁니다. */
        void onPlatformFocusChanged( bool bFocused );
        /**
         * @brief 클라이언트 영역에서 마우스 버튼 @p button 이 눌렸습니다. 포커스가 있으면 잠금을 다시 잡습니다(비클라이언트 클릭은 여기로 오지 않습니다).
         * @return 이 누름으로 잠금이 걸렸으면 true 입니다. 이 누름과 짝인 뗌은 게임에 넘기지 않습니다(`isMouseButtonConsumed`).
         */
        bool onPlatformPointerPressed( MouseButton button );
        /** @brief 마우스 버튼 @p button 이 떼어졌습니다. 삼킨 누름의 짝이면 true 를 반환하고 삼킴을 해제합니다. */
        bool onPlatformPointerReleased( MouseButton button );
        /** @brief 잠금을 다시 잡느라 삼킨 누름이 아직 떼어지지 않았는지 반환합니다(더블클릭 · 폴링도 그 버튼을 게임에 넘기지 않습니다). */
        bool isMouseButtonConsumed( MouseButton button ) const;
        /** @brief Alt 를 누르거나 뗐습니다. 누르는 동안 잠금을 쉽니다. */
        void onPlatformAltChanged( bool bHeld );
        /**
         * @brief 활성 창이 지금 OS 전경인지 플랫폼에 묻습니다. 창이 없으면(단위 테스트) 추적해 둔 값을 반환합니다.
         * @details 게임이 잠금을 처음 켜는 순간에 쓴다. 그때까지 포커스 메시지를 하나도 받지 못했을 수 있다(창이 처음부터 뒤에 떴을 때).
         */
        bool isWindowFocusedPlatform() const;
        /** @brief OS 잠금을 겁니다(Win32 `ClipCursor`, X11 `XGrabPointer`). `syncMouseLock` 만 부릅니다. */
        void applyMouseLockMode();
        /** @brief OS 잠금을 풉니다. */
        void releaseMouseLockMode();
        /**
         * @brief 가운데 고정 잠금이면, 창이 포커스를 가지고 있을 때 커서를 잠금 영역 가운데로 되돌립니다(beginFrame 끝의 플랫폼 훅).
         * @details 프레임마다 되돌린다. 포커스를 얻을 때나 창이 움직일 때만 옮기면 커서가 잠금 영역 가장자리에 닿아 더 돌지 않는다.
         */
        void recenterLockedCursorPlatform();
        /** @brief 음소거와 상관없이 큐에 넣습니다. 포커스나 포인터 진입 같은 창 상태 알림에 씁니다(입력이 아닙니다). */
        bool postWindowStateEvent( const RawInputEvent& rawEvent );
        /** @brief 포커스가 `Game` 이 아닐 때 눌렀다가 지금은 뗀 키를 가림 목록에서 해제합니다(프레임 시작, 이벤트 적용 전). */
        void releaseCapturedKeys();

    private:
        static constexpr size_t kKeyboardFocusCount = static_cast<size_t>( InputKeyboardFocus::Count );
        static constexpr size_t kKeyMaskWordCount   = ( static_cast<size_t>( Key::Count ) + 63 ) / 64;

        ConcurrentQueue<RawInputEvent, 2048> _queueRawEvent;        /**< OS 와 폴러 스레드가 postRawEvent() 로 넣는 락프리 원시 이벤트 큐입니다. beginFrame() 이 매 프레임 비웁니다. */
        atomic<uint32>                       _droppedRawEventCount; /**< 큐가 가득 차서 버린 원시 이벤트의 누적 개수입니다. beginFrame() 에서 요약 로그를 남기고 0 으로 초기화합니다. */
        vector<unique_ptr<IInputDevice>>     _listDevice;           /**< 등록된 모든 장치(키보드, 마우스, 게임패드 등)를 소유하는 목록입니다. */
        KeyboardDevice*                      _pKeyboard;            /**< 편의 API 용 캐시 포인터입니다. 소유는 _listDevice 가 합니다. */
        MouseDevice*                         _pMouse;               /**< 편의 API 용 캐시 포인터입니다. */
        GamepadDevice*                       _pGamepad;             /**< 0번 게임패드 편의 API 용 캐시 포인터입니다(1~3번은 getGamepad(index) 로 조회). */
        unique_ptr<InputMap>                 _pInputMap;            /**< 이 InputManager 에 연결된 기본 InputMap 인스턴스입니다. */
        vector<RawInputEvent>                _listDrainedEvent;     /**< beginFrame() 에서 큐를 비워 담아 두는 임시 버퍼입니다(매 프레임 재사용). */
        IVirtualInputSource*                 _pVirtualInput;        /**< 붙인 가상 입력 원천입니다(빌림). nullptr 이면 OS 입력만 받습니다. */
        uint32                               _virtualFrameIndex;    /**< 붙인 뒤 `beginFrame` 횟수이며, 원천에 넘기는 프레임 번호입니다. */
        uint32                               _beginFrameCount;      /**< `beginFrame` 을 부른 횟수입니다. */
        InputGlyphStyle                      _activeGlyphStyle;     /**< 마지막으로 조작이 감지된 장치 종류입니다(UI 글리프 자동 전환용). */
        ActiveDeviceChangedDelegate          _onActiveDeviceChanged;
        GamepadConnectionDelegate            _onGamepadConnectionChanged;
        TextInputDelegate                    _arrOnTextInput[kKeyboardFocusCount];       /**< 키보드 포커스 주인마다의 글자 콜백입니다. */
        TextInputDelegate                    _arrOnTextComposition[kKeyboardFocusCount]; /**< 키보드 포커스 주인마다의 조합 중 글자 콜백입니다. */
        uint64                               _arrCapturedKeyMask[kKeyMaskWordCount];     /**< 포커스가 `Game` 이 아닐 때 눌려서 게임에 보이지 않게 가린 키입니다(뗀 다음 프레임에 해제). */
        InputKeyboardFocus                   _keyboardFocus;                             /**< 지금 키보드를 받는 쪽입니다. */
        VirtualInputMode                     _virtualInputMode;                          /**< 붙인 가상 입력 원천의 모드입니다. */
        [[maybe_unused]] uint16              _pendingHighSurrogate;                      /**< 짝을 기다리는 서로게이트의 앞 반쪽입니다(Win32 WM_CHAR). 0 이면 없습니다. */
        uint8                                _consumedButtonMask;                        /**< 잠금을 다시 잡느라 삼킨, 아직 떼지 않은 마우스 버튼 비트입니다(`1 << MouseButton`). */
        uint8                                _bInitialized          : 1;
        uint8                                _bInputMuted           : 1;
        uint8                                _bWindowFocused        : 1; /**< 창이 포커스를 가지고 있는지 나타냅니다(포커스 메시지로 갱신하며, 창 없는 테스트에서는 true). */
        uint8                                _bMouseLockEngaged     : 1; /**< 포커스를 잃은 뒤 클라이언트를 눌러 잠금을 다시 잡았는지 나타냅니다. */
        uint8                                _bAltHeld              : 1; /**< Alt 를 누르고 있는지 나타냅니다(누르는 동안 잠금을 쉽니다). */
        uint8                                _bCursorHiddenApplied  : 1; /**< OS 커서를 숨겨 둔 상태인지 나타냅니다. `ShowCursor` 는 카운터라서 전이할 때만 부릅니다. */
        uint8                                _bOsDeviceInputBlocked : 1; /**< 무인 자동화가 창 메시지 단계에서 사람의 마우스와 키보드 이벤트를 버리는지 나타냅니다(`setOsDeviceInputBlocked`). */
        [[maybe_unused]] uint8               _reserved              : 1;
    };
} // namespace sw

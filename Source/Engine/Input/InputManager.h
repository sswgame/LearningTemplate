/**
 * @file InputManager.h
 * @brief App · Game 용 입력 장치 등록부, 락프리 비동기 이벤트 큐, 이벤트 디스패치를 한데 모은 허브입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/ConcurrentQueue.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"

#include "Engine/Input/Devices/GamepadDevice.h"
#include "Engine/Input/Devices/KeyboardDevice.h"
#include "Engine/Input/Devices/MouseDevice.h"
#include "Engine/Input/Events/RawInputEvent.h"
#include "Engine/Input/IInputDevice.h"
#include "Engine/Input/InputSnapshot.h"
#include "Engine/Input/KeyCodes.h"

namespace sw
{
    struct NativeWindowEvent;

    class ActionMap;

    /** @brief 지금 활성인 입력 장치 타입입니다(UI 글리프 자동 전환용). */
    enum class InputDeviceType : uint8
    {
        KeyboardMouse = 0,
        GamepadXbox,
        GamepadPlayStation,
        GamepadSwitch
    };

    /**
     * @class InputManager
     * @brief 다형 IInputDevice 들을 등록 · 관리하고, 락프리 원시 이벤트 큐로 OS 메시지를 프레임에 맞추는 중앙 허브입니다.
     */
    class SW_API InputManager
    {
    public:
        using ActiveDeviceChangedDelegate = Delegate<void( InputDeviceType )>;
        using GamepadConnectionDelegate   = Delegate<void( uint32, bool )>;
        using TextInputDelegate           = Delegate<void( string_view )>;

        InputManager();
        ~InputManager();

        InputManager( const InputManager& )            = delete;
        InputManager& operator=( const InputManager& ) = delete;

        // ------------------------------------------------------------------------------
        // 1) 수명주기 · 프레임 제어
        // ------------------------------------------------------------------------------
        bool initialize();
        void shutdown();

        /** @brief 프레임을 시작합니다. 락프리 큐를 비우고, 장치 상태를 갱신하고, 프레임 엣지를 맞춥니다. */
        void beginFrame( float32 deltaSeconds = 0.016f );
        /** @brief 프레임을 마치며 엣지 플래그와 원시 델타를 리셋합니다. */
        void endFrame();
        /** @brief 창이 포커스를 얻으면 마우스 잠금 모드를 다시 적용합니다. */
        void onWindowFocusGained();
        /** @brief 창이 포커스를 잃으면 모든 장치 입력 상태를 초기화하고 마우스 클리핑을 풉니다. */
        void onWindowFocusLost();
        /** @brief 등록된 모든 장치의 입력 상태(키 · 버튼 · 축)를 초기화합니다(리플레이 재동기화 등에 씁니다). */
        void resetAllDeviceState();

        // ------------------------------------------------------------------------------
        // 2) 락프리 원시 이벤트 큐(Lock-Free Event Queue)
        // ------------------------------------------------------------------------------
        /** @brief OS 창 스레드 · 백그라운드 폴러에서 잠금 없이 원시 이벤트를 넣습니다. */
        bool postRawEvent( const RawInputEvent& rawEvent );
        /** @brief 대기 중인 원시 이벤트를 꺼냅니다. */
        uint32 drainRawEvents( RawInputEvent* pOutBuffer, uint32 maxCount );
        uint32 getPendingRawEventCount() const { return _queueRawEvent.size(); }

        // ------------------------------------------------------------------------------
        // 3) 다형 장치 등록부(Device Registry)
        // ------------------------------------------------------------------------------
        void            registerDevice( unique_ptr<IInputDevice> pDevice );
        void            unregisterDevice( IInputDevice* pDevice );
        IInputDevice*   getDevice( InputDeviceKind kind, uint32 deviceIndex = 0 ) const;
        KeyboardDevice* getKeyboard() const { return _pKeyboard; }
        MouseDevice*    getMouse() const { return _pMouse; }
        GamepadDevice*  getGamepad( uint32 deviceIndex = 0 ) const;

        // ------------------------------------------------------------------------------
        // 4) ActionMap · 장치 상태 조회
        // ------------------------------------------------------------------------------
        ActionMap&       getActionMap() { return *_pActionMap; }
        const ActionMap& getActionMap() const { return *_pActionMap; }

        InputDeviceType getActiveDeviceType() const { return _activeDeviceType; }
        void            setActiveDeviceType( InputDeviceType type );
        void            setActiveDeviceChangedCallback( ActiveDeviceChangedDelegate callback ) { _onActiveDeviceChanged = std::move( callback ); }
        void            setGamepadConnectionCallback( GamepadConnectionDelegate callback ) { _onGamepadConnectionChanged = std::move( callback ); }
        void            setTextInputCallback( TextInputDelegate callback ) { _onTextInput = std::move( callback ); }
        void            setTextCompositionCallback( TextInputDelegate callback ) { _onTextComposition = std::move( callback ); }

        bool wasAnyInputPressed() const;
        void onTextInput( string_view text );
        void onTextComposition( string_view text );

        void setInputMuted( bool bMuted ) { _bInputMuted = bMuted ? SW_TRUE : SW_FALSE; }
        bool isInputMuted() const { return _bInputMuted == SW_TRUE; }

        // ------------------------------------------------------------------------------
        // 5) 게임플레이가 프레임마다 묻는 것만 여기서 답한다: 키 · 버튼 · 위치 · 델타 · 휠, 그리고 플랫폼에
        //    적용까지 해야 하는 잠금 · 커서 · 클립. 장치 설정(스무딩 · 가속)과 드문 조회(포인터 진입 · 이탈,
        //    가로 휠, 원시 델타, 잠금 모드 읽기)는 장치가 답한다: `getMouse()->setSmoothing()`.
        //    같은 답을 두 이름으로 내지 않는다. 예전에는 마우스 API 23 개가 여기 그대로 복제돼 있었다.
        // ------------------------------------------------------------------------------
        bool isKeyDown( Key key ) const { return _pKeyboard != nullptr ? _pKeyboard->isKeyDown( key ) : false; }
        bool wasKeyPressed( Key key ) const { return _pKeyboard != nullptr ? _pKeyboard->wasKeyPressed( key ) : false; }
        bool wasKeyReleased( Key key ) const { return _pKeyboard != nullptr ? _pKeyboard->wasKeyReleased( key ) : false; }

        bool isMouseButtonDown( MouseButton button ) const { return _pMouse != nullptr ? _pMouse->isButtonDown( button ) : false; }
        bool wasMouseButtonPressed( MouseButton button ) const { return _pMouse != nullptr ? _pMouse->wasButtonPressed( button ) : false; }
        bool wasMouseButtonReleased( MouseButton button ) const { return _pMouse != nullptr ? _pMouse->wasButtonReleased( button ) : false; }

        int2    getMousePosition() const { return _pMouse != nullptr ? _pMouse->getPosition() : int2{}; }
        float2  getMousePositionNormalized() const;
        int2    getMouseDelta() const;
        float32 getMouseWheel() const { return _pMouse != nullptr ? _pMouse->getMouseWheel() : 0.0f; }

        /**
         * @brief 포인터가 창 안에 있고 주어진 사각형(픽셀) 위에 있으면 true 입니다.
         * @details 예전에는 ActionMap 에 있었습니다. 하지만 이것은 **액션이 아니라 장치 상태**이고, 쓰는 값도
         *          모두 여기 있습니다(isPointerInside · getMousePosition). 물어볼 곳이 하나여야 합니다.
         */
        bool isPointerOverRect( int32 x, int32 y, int32 width, int32 height ) const;

        void setMouseLockMode( MouseLockMode mode );
        void setCursorVisible( bool bVisible );

        void setMouseClipSubRect( int32 left, int32 top, int32 right, int32 bottom );
        void clearMouseClipSubRect();
        void applyMouseLockMode();
        void releaseMouseLockMode();

        // ------------------------------------------------------------------------------
        // 6) 게임패드 편의 API(장치로 넘겨 줌)
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
        // 7) 롤백 · 리플레이 입력 스냅샷 버퍼(Input Snapshot & History)와 가상 입력 주입
        // ------------------------------------------------------------------------------
        void                      recordSnapshot( uint32 tickNumber );
        const InputSnapshot*      getSnapshot( uint32 tickNumber ) const { return _inputHistory.getSnapshot( tickNumber ); }
        const InputSnapshot*      getLatestSnapshot() const { return _inputHistory.getLatestSnapshot(); }
        const InputHistoryBuffer& getInputHistory() const { return _inputHistory; }

        // ------------------------------------------------------------------------------
        // 8) 플랫폼 네이티브 이벤트 처리와 접근성 제어
        // ------------------------------------------------------------------------------
        void onNativeWindowEvent( const NativeWindowEvent& event );
        void processNativeEvent( const NativeWindowEvent& event );
        void pollPlatform();
        void disableWindowsAccessibilityShortcuts();
        void restoreWindowsAccessibilityShortcuts();

    private:
        void dispatchRawEvent( const RawInputEvent& rawEvt );

        // ------------------------------------------------------------------------------
        // 9) 플랫폼별 구현(Windows: InputManagerWin32.cpp / Linux: InputManagerX11.cpp)
        //    InputManager.cpp 는 이 함수들을 부르기만 한다. 거기에 #ifdef 를 더하지 말 것.
        // ------------------------------------------------------------------------------
        /** @brief 플랫폼별 게임패드 백엔드를 만들어 registerDevice() 로 등록합니다(Windows: XInput, Linux: 조이스틱 API). */
        void registerPlatformGamepads();

        /** @brief 게임패드 슬롯 수입니다. XInput 규격이 넷이고, 조이스틱도 `js0`~`js3` 로 맞춰 둡니다. */
        static constexpr uint32 kMaxGamepadSlot = 4;

        /**
         * @brief 게임패드 슬롯 넷을 만들어 등록합니다. 플랫폼이 정하는 것은 **만드는 타입뿐**입니다.
         * @tparam GamepadType 슬롯 번호를 받는 게임패드 장치(`GamepadXInput` · `GamepadJoystick`).
         * @details 슬롯 수(4) · 0번을 편의 포인터로 잡는 것 · 연결 콜백을 이어 주는 것은 **엔진 정책**인데,
         *          예전에는 그 정책이 플랫폼 파일마다 한 벌씩 있었습니다. 슬롯 수를 늘리거나 콜백 규칙을
         *          바꾸면 두 곳을 같이 고쳐야 했고, 한쪽만 고치면 **그 플랫폼만 조용히 다르게** 동작했습니다.
         */
        template <typename GamepadType>
        void registerGamepadSlots()
        {
            for ( uint32 padIndex = 0; padIndex < kMaxGamepadSlot; ++padIndex )
            {
                auto pGamepad = make_unique<GamepadType>( padIndex );
                // 0번은 편의 API(`getGamepad()` 인자 없는 형태)가 쓰는 캐시다.
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
        /** @brief 커서 표시 · 숨김을 OS 에 실제로 적용합니다(setCursorVisible() 의 플랫폼 훅). */
        void setCursorVisiblePlatform( bool bVisible );

    private:
        ConcurrentQueue<RawInputEvent, 2048> _queueRawEvent;        /**< OS · 폴러 스레드가 postRawEvent() 로 넣는 락프리 원시 이벤트 큐. beginFrame() 이 매 프레임 비움. */
        atomic<uint32>                       _droppedRawEventCount; /**< 큐가 가득 차 버린 원시 이벤트 수 누적. beginFrame() 에서 요약 로그를 남기고 0 으로 리셋. */
        vector<unique_ptr<IInputDevice>>     _listDevice;           /**< 등록된 모든 장치(키보드 · 마우스 · 게임패드 등)의 소유 목록. */
        KeyboardDevice*                      _pKeyboard;            /**< 편의 API 용 캐시 포인터. 실제 소유는 _listDevice. */
        MouseDevice*                         _pMouse;               /**< 편의 API 용 캐시 포인터. */
        GamepadDevice*                       _pGamepad;             /**< 0번 게임패드 편의 API 용 캐시 포인터(1~3번은 getGamepad(index) 로 조회). */
        unique_ptr<ActionMap>                _pActionMap;           /**< 이 InputManager 에 연결된 기본 ActionMap 인스턴스. */
        vector<RawInputEvent>                _listDrainedEvent;     /**< beginFrame() 에서 큐를 비워 담아 두는 임시 버퍼(매 프레임 재사용). */
        InputHistoryBuffer                   _inputHistory;         /**< 롤백 · 리플레이용 프레임별 입력 스냅샷 링 버퍼. */
        InputDeviceType                      _activeDeviceType;     /**< 마지막으로 조작이 감지된 장치 종류(UI 글리프 자동 전환용). */
        ActiveDeviceChangedDelegate          _onActiveDeviceChanged;
        GamepadConnectionDelegate            _onGamepadConnectionChanged;
        TextInputDelegate                    _onTextInput;
        TextInputDelegate                    _onTextComposition;
        uint8                                _bInitialized : 1;
        uint8                                _bInputMuted  : 1;
        [[maybe_unused]] uint8               _reserved     : 6;
    };
} // namespace sw

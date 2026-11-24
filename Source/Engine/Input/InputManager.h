/**
 * @file InputManager.h
 * @brief App 과 Game 이 쓰는 입력 장치 레지스트리, 락프리 비동기 이벤트 큐, 이벤트 디스패치를 한데 모은 허브입니다.
 */
#pragma once
#include "Core/Concurrency/DevCounter.h"
#include "Core/Concurrency/Queue/ConcurrentQueue.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Delegate/DelegateSlot.h"
#include "Core/Delegate/SubscriptionList.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Path/AbsolutePath.h"
#include "Core/Subsystem/ServiceSlot.h"
#include "Core/Subsystem/Subsystem.h"

#include "Engine/Input/Devices/GamepadDevice.h"
#include "Engine/Input/Devices/KeyboardDevice.h"
#include "Engine/Input/Devices/MouseDevice.h"
#include "Engine/Input/Devices/TouchDevice.h"
#include "Engine/Input/GamepadButtonUtil.h"
#include "Engine/Input/IInputDevice.h"
#include "Engine/Input/InputGlyphStyle.h"
#include "Engine/Input/InputKeyboardFocus.h"
#include "Engine/Input/InputUserDevices.h"
#include "Engine/Input/InputViewRect.h"
#include "Engine/Input/KeyCodeUtil.h"
#include "Engine/Input/RawInputEvent.h"
#include "Engine/Input/Virtual/IVirtualInputSource.h"
#include "Engine/Input/Virtual/InputReplay.h"
#include "Engine/Input/Virtual/VirtualCursor.h"
#include "Engine/Window/CursorShape.h"

namespace sw
{
    struct NativeWindowEvent;
    struct WindowTextInputArea;
    struct WindowTextInputEvent;

    class InputMap;

    /**
     * @struct InputPressInstant
     * @brief 이번 프레임에 컨트롤 하나가 눌린 순간입니다(CS2 식 서브틱 입력 — 클릭한 순간의 시선으로 쏜다).
     * @details 프레임 안의 자리와, 그 순간 직전까지 이 프레임에 쌓인 마우스 원시 이동량(시선 누적)을 함께 준다. 게임은 프레임 시작의 시선에
     *          `_lookDeltaBefore` 를 자기 감도로 더해 그 순간의 시선을 만든다. 폴링하는 장치(패드)의 누름은 폴링한 순간 = 프레임 끝(1)이다.
     */
    struct InputPressInstant
    {
        float2  _lookDeltaBefore{};     ///< 프레임 시작부터 누름 직전까지의 마우스 원시 이동량(`RawInputEvent` 의 원시 델타 합)
        float32 _frameFraction{ 0.0f }; ///< 0 = 프레임 창의 시작, 1 = 이번 `beginFrame`
        int64   _timeMicroseconds{ 0 }; ///< 누름이 큐에 들어온 시각(단조 시계). 가상 입력이면 0 이다

        /**
         * @brief 프레임 전체의 시선 입력(`getFrameLookDelta`) 중 누름 직전까지의 비율을 축마다 줍니다.
         * @details 시선 액션의 값(감도 · 반전을 거친 것)에 곱하면 그 순간까지의 시선 이동이 된다. 원시 이동량 자체에 감도를 다시 곱하지 않으므로 액션의 가공이 그대로 남는다.
         *          프레임 안에서 이동 방향이 오갔으면 비율이 0..1 밖일 수 있다(앞에서 크게 돌렸다가 되돌린 경우) — 가공이 선형이라 그대로 곱해도 맞다.
         *          프레임 전체 이동량이 그 축에서 0 이면(스틱처럼 이벤트가 아닌 시선) 비율은 1 이다 — 폴링한 값은 프레임 끝의 것이다.
         */
        float2 computeLookFraction( const float2& frameLookDelta ) const
        {
            const float32 fractionX = frameLookDelta._x != 0.0f ? _lookDeltaBefore._x / frameLookDelta._x : 1.0f;
            const float32 fractionY = frameLookDelta._y != 0.0f ? _lookDeltaBefore._y / frameLookDelta._y : 1.0f;
            return float2{ fractionX, fractionY };
        }
    };
} // namespace sw

namespace sw
{
    /**
     * @class InputManager
     * @brief 다형 IInputDevice 를 등록하고 관리하며, 락프리 원시 이벤트 큐로 OS 메시지를 프레임에 맞추는 중앙 허브입니다.
     * @details 모듈이 단 콜백(장치 변경, 게임패드 연결, 글자 입력, 조합)은 구독(`Subscription`)이라 그 모듈의 소유자가 사라질 때 풀린다.
     *          모듈 라이브러리를 언로드하기 전에 그 범위의 콜백을 해제하고, 그 범위에 vtable 이 있는 장치를 등록에서 제거한다(`onModuleUnloading`).
     */
    class SW_API InputManager final : public Subsystem
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
        [[nodiscard]] bool initialize();
        void               shutdown();

        /** @brief 서브시스템 이름입니다(다른 선언의 의존 칸이 이 상수를 가리킨다). */
        static constexpr const utf8* kSubsystemName = "Input";
        /** @brief 기동 순서의 자리 — 모듈이 등록한 장치 · 콜백을 든다(서버는 이벤트가 오지 않는 장치 목록이다). */
        /** @brief 초기화하고 엔진 설정의 정책(`EngineConfig::_bBackgroundGamepadInput`)을 겁니다. */
        SubsystemInitResult initializeSubsystem() override;
        /** @brief 창이 배경일 때도 패드를 폴링할지 정합니다(`EngineConfig::_bBackgroundGamepadInput`). 끄면 포커스가 없는 동안 패드 장치를 폴링하지 않는다. */
        void setBackgroundGamepadInput( bool bAllowed ) { _bBackgroundGamepadInput = bAllowed ? SW_TRUE : SW_FALSE; }
        /** @brief 패드를 이번 프레임에 폴링할지입니다 — 창이 포커스를 가졌거나 정책이 배경 입력을 허락하면 true 다. */
        [[nodiscard]] static bool canPollGamepad( bool bWindowFocused, bool bBackgroundAllowed ) { return bWindowFocused || bBackgroundAllowed; }
        void                      shutdownSubsystem() override { shutdown(); }
        const utf8*               getSubsystemName() const override { return kSubsystemName; }
        void                      resetGameInstanceState( GameInstanceBoundary boundary ) override;
        const utf8*               findGameInstanceState() const override;

        /**
         * @brief 프레임을 시작합니다. 장치를 폴링하고, 락프리 큐를 비워 이벤트를 들어온 순서대로 적용하고, 프레임 엣지를 맞춥니다.
         * @details 창 메시지는 `processNativeEvent` 가 큐에 넣기만 한다. 장치 상태를 바꾸는 경로는 여기 하나뿐이다.
         *          메시지를 받을 때 상태를 바로 바꾸면, 여기서 엣지를 지운 뒤 재생할 때 이미 눌린 키라서 "새로 눌림" 이 사라진다.
         *          끝에서 통합 InputMap(`getInputMap()`)을 갱신하므로, 그 맵을 따로 `update()` 하면 한 프레임에 두 번 흐른다.
         *          따로 만든 InputMap 은 만든 쪽이 갱신한다.
         * @param unscaledDeltaSeconds 지난 프레임의 실제 시간(초, 게임 시간 배율 · 정지와 무관)입니다. 진동 · 가상 커서 · 길게 누르기 · 이벤트의 프레임 안 자리가 이 값으로 흐릅니다.
         */
        void beginFrame( float32 unscaledDeltaSeconds = 0.016f );
        /** @brief 프레임을 마치며 엣지 플래그와 원시 델타를 리셋합니다. */
        void endFrame();
        /** @brief 창이 포커스를 얻었습니다. 잠금은 다시 잡지 않으며, 클라이언트 영역을 눌러야 잡습니다(`isMouseLockActive`). */
        void onWindowFocusGained();
        /** @brief 창이 포커스를 잃으면 모든 장치 입력 상태를 초기화하고 마우스 클리핑을 풉니다. */
        void onWindowFocusLost();
        /** @brief 창이 포커스를 가지고 있는지입니다(포커스 메시지로 갱신하며, 창 없는 테스트에서는 true). 감시 값 `Input.WindowFocused` 가 읽는다. */
        bool isWindowFocused() const { return _bWindowFocused == SW_TRUE; }
        /** @brief 등록된 모든 장치의 입력 상태(키, 버튼, 축)를 초기화합니다(리플레이 재동기화 등에 씁니다). */
        void resetAllDeviceState();
        /**
         * @brief 앱이 멈췄습니다(창 수명 이벤트 — 감사 P-6). 패드 진동을 지금 멈추고, 눌린 입력은 큐 순서 안에서 놓습니다(포커스를 잃을 때와 같은 길).
         * @details 창 포커스 상태(`isWindowFocused`)는 바꾸지 않는다 — 멈춤을 흉내 내는 데스크톱에서는 돌아온 뒤 포커스 이벤트가 오지 않는다.
         */
        void onAppSuspended();

        // ------------------------------------------------------------------------------
        // 2) 락프리 원시 이벤트 큐(Lock-Free Event Queue)
        // ------------------------------------------------------------------------------
        /** @brief OS 창 스레드나 백그라운드 폴러에서 잠금 없이 원시 이벤트를 넣습니다. 넣는 순간의 시각(`setClock` 의 시계)을 이벤트에 적습니다. 지금 모으는 입력 프레임(`getCollectingInputFrame`)에 적용됩니다. */
        bool postRawEvent( const RawInputEvent& rawEvent );
        /**
         * @brief 원시 이벤트를 입력 프레임 @p inputFrame 에 적용하도록 넣습니다. 그 번호의 `beginFrame`(이미 지났으면 다음 `beginFrame`)이 넣은 순서대로 적용합니다.
         * @details 에디터 UI 스레드 분리의 경계다 — 창 · ImGui 를 가진 스레드가 게임 뷰 입력을 다음 게임 프레임 번호로 넣으면
         *          스레드가 언제 돌든 같은 입력이 같은 게임 프레임에 닿는다(한 프레임 지연이 계약). 언리얼 `FDeferredWindowsMessage` 가 받은 스레드에서 수정 키 상태까지 값으로 떠 두고 나중에 처리하는 것과 같은 자리다.
         */
        bool postRawEventAt( const RawInputEvent& rawEvent, uint32 inputFrame );
        /** @brief 지금 이벤트를 모으는 입력 프레임 번호입니다(1 부터). 다음 `beginFrame` 이 이 번호까지를 적용하고 하나 올린다. */
        uint32 getCollectingInputFrame() const { return _collectingInputFrame.load( std::memory_order_acquire ); }
        uint32 getPendingRawEventCount() const { return _queueRawEvent.size(); }

        /** @brief 단조 시계 마이크로초를 읽는 함수입니다. */
        using ClockFunction = int64 ( * )();
        /**
         * @brief 이벤트 시각과 프레임 창을 측정하는 시계를 바꿉니다(테스트가 정해진 시각을 넣는다). 기본은 `MonotonicClock::nowMicroseconds` 입니다.
         * @details 큐에 넣는 쪽(창 스레드)과 `beginFrame` 이 같은 시계를 본다. 이벤트가 큐에 있는 동안 바꾸지 않는다. nullptr 은 받지 않는다(어설트).
         */
        void setClock( ClockFunction pfnNowMicroseconds );
        /**
         * @brief 이번 프레임에 @p slot 이 눌린 순간을 찾습니다. 이번 프레임에 눌리지 않았으면 false 입니다.
         * @details 이벤트로 눌렸으면 그 이벤트의 자리와 시선 누적이고, 폴링으로 눌렸으면(패드) 프레임 끝(1)과 프레임 전체의 시선 누적이다. 한 프레임에 여러 번 눌렸으면 첫 누름이다.
         */
        [[nodiscard]] bool tryGetPressInstant( const InputSlot& slot, InputPressInstant& outInstant ) const;
        /** @brief 이번 프레임에 쌓인 마우스 원시 이동량입니다(`InputPressInstant::_lookDeltaBefore` 와 같은 합). */
        float2 getFrameLookDelta() const { return _frameLookDelta; }

        // ------------------------------------------------------------------------------
        // 3) 다형 장치 레지스트리(Device Registry)
        // ------------------------------------------------------------------------------
        void            registerDevice( unique_ptr<IInputDevice> pDevice );
        void            unregisterDevice( IInputDevice* pDevice );
        IInputDevice*   getDevice( InputDeviceKind kind, uint32 deviceIndex = 0 ) const;
        KeyboardDevice* getKeyboard() const { return _pKeyboard; }
        MouseDevice*    getMouse() const { return _pMouse; }
        TouchDevice*    getTouch() const { return _pTouch; }
        GamepadDevice*  getGamepad( uint32 deviceIndex = 0 ) const;
        /**
         * @brief 패드 스틱으로 마우스 커서를 움직이는 가상 커서입니다. 읽기 전용입니다 — 켜고 끄기와 스틱은 입력 맵이 정합니다(`InputMap::getVirtualCursorStick`).
         * @details `beginFrame` 이 매 프레임 맵 값대로 맞추고, 켜져 있으면 스틱 · 버튼을 마우스 이동 · 버튼 이벤트로 바꿔 OS 이벤트 뒤에 적용한다.
         *          가상 입력(시나리오 · 리플레이)이 붙어 있으면 쉰다. 게임 코드가 켜는 창구를 두지 않는다 — 서비스가 묶이는 시점에 기대면 조용히 안 켜진다.
         */
        const VirtualCursor& getVirtualCursor() const { return _virtualCursor; }

        // ------------------------------------------------------------------------------
        // 4) InputMap · 장치 상태 조회
        // ------------------------------------------------------------------------------
        InputMap&       getInputMap() { return *_pInputMap; }
        const InputMap& getInputMap() const { return *_pInputMap; }

        /** @brief 로컬 사용자의 최대 수입니다(패드 슬롯 수와 같다). */
        static constexpr uint32 kMaxLocalUsers = kMaxGamepadSlot;
        /**
         * @brief 로컬 멀티를 엽니다 — 사용자 @p userCount 명(2 ~ `kMaxLocalUsers`)마다 장치를 짝지은 입력 맵을 둡니다(유니티 `InputUser` · 언리얼 로컬 플레이어마다의 Enhanced Input 자리).
         * @details 사용자 0 은 통합 맵(`getInputMap`)이고 키보드 · 마우스와 남는 패드를 쓴다. 사용자 1 부터는 통합 맵의 묶음 · 플레이어 설정 · 레이어 스택을 베낀 맵을 패드로 읽는다.
         *          짝은 연결된 패드가 나타날 때 짓고(`InputUserDevices::pairDevices`) 끊겨도 남는다. 베낀 뒤 통합 맵의 레이어를 바꿔도 다른 사용자의 맵은 그대로다.
         * @return 사용자 수가 범위 밖이거나 이미 열렸거나 통합 맵이 파일에서 읽은 묶음이 아니어서 베낄 수 없으면 알리고 false 입니다(아무것도 열지 않는다).
         */
        [[nodiscard]] bool openLocalUsers( uint32 userCount );
        /** @brief 로컬 멀티를 닫습니다. 통합 맵이 다시 모든 장치를 읽습니다. 열지 않았으면 아무것도 하지 않습니다. */
        void closeLocalUsers();
        /**
         * @brief 레이어를 모든 로컬 사용자의 맵에 넣습니다(`InputMap::pushLayer`) — 컷씬 막기 · 미니게임 문맥처럼 사람마다가 아니라 게임 전체의 것. 나중에 여는 사용자의 맵에도 넣습니다.
         * @details 주 사용자의 맵에만 넣으면 분할 화면의 다른 사람은 컷씬 동안 움직이고, 넣은 채 사용자를 열면 사본이 남아 풀 곳이 없다.
         */
        [[nodiscard]] Subscription pushSharedLayer( const hashed_string& layer, bool bBlockLower = false );
        /** @brief 레이어를 모든 로컬 사용자의 맵에서 켜 둡니다(`InputMap::enableLayer`). 나중에 여는 사용자의 맵에서도 켭니다. */
        [[nodiscard]] Subscription enableSharedLayer( const hashed_string& layer );
        /** @brief 로컬 멀티가 열렸으면 그 사용자 수, 아니면 1 입니다. */
        uint32 getLocalUserCount() const { return _listUserDevices.empty() ? 1u : static_cast<uint32>( _listUserDevices.size() ); }
        /** @brief 로컬 사용자 @p userIndex 의 입력 맵입니다. 사용자 0 은 늘 통합 맵이고, 열리지 않은 사용자는 nullptr 입니다. */
        InputMap* findUserMap( uint32 userIndex );

        InputGlyphStyle getActiveGlyphStyle() const { return _activeGlyphStyle; }
        void            setActiveGlyphStyle( InputGlyphStyle type );
        // 아래 콜백 칸은 칸마다 주인이 하나다 — 거는 쪽이 구독을 쥐고, 놓으면 칸이 빈다(이미 차 있으면 단언, `DelegateSlot`).
        [[nodiscard]] Subscription bindActiveDeviceChangedCallback( ActiveDeviceChangedDelegate callback ) { return _onActiveDeviceChanged.bind( std::move( callback ) ); }
        [[nodiscard]] Subscription bindGamepadConnectionCallback( GamepadConnectionDelegate callback ) { return _onGamepadConnectionChanged.bind( std::move( callback ) ); }
        /** @brief 키보드 포커스가 @p owner 일 때 오는 글자(UTF-8)를 받을 콜백을 겁니다. 주인(포커스)마다 칸 하나다. */
        [[nodiscard]] Subscription bindTextInputCallback( TextInputDelegate callback, InputKeyboardFocus owner = InputKeyboardFocus::Game );
        /** @brief 키보드 포커스가 @p owner 일 때 오는 조합 중 글자(IME)를 받을 콜백을 겁니다. */
        [[nodiscard]] Subscription bindTextCompositionCallback( TextInputDelegate callback, InputKeyboardFocus owner = InputKeyboardFocus::Game );

        /**
         * @brief @p owner 가 키보드를 쥡니다. 돌려준 구독이 쥔 동안이며, 쥔 주인들 가운데 우선순위(`InputKeyboardFocus` 순서)가 가장 높은 쪽이 받습니다. `Game` 은 아무도 쥐지 않은 상태라 쥘 수 없습니다.
         * @details 칸 하나에 마지막으로 쓴 쪽이 이기게 두면 주인이 저마다 "내 값일 때만 되돌린다" 를 해야 하고, 쥔 순서와 놓는 순서가 어긋나면(도구 창이 콘솔을 덮은 동안 콘솔이 사라짐) 없는 주인에게 돌아간다.
         *          포커스가 `Game` 이 아닌 동안 눌린 키는 포커스가 돌아온 뒤에도 뗄 때까지 게임에 보이지 않는다.
         *          콘솔을 닫은 Esc 가 다음 프레임에 게임의 "일시정지" 로 새지 않게 하기 위해서다. 포커스를 넘기기 전부터 눌려 있던 키(달리던 W)는 돌아오면 다시 보인다.
         */
        [[nodiscard]] Subscription claimKeyboardFocus( InputKeyboardFocus owner );
        /** @brief 지금 키보드를 받는 쪽입니다(아무도 쥐지 않았으면 `Game`). */
        InputKeyboardFocus getKeyboardFocus() const { return _keyboardFocus; }
        /** @brief 게임 쪽 조회가 이 키를 볼 수 있으면 true 입니다. 포커스가 `Game` 이고, 다른 포커스 동안 눌려서 아직 떼지 않은 키가 아니어야 합니다. */
        bool isKeyVisibleToGame( Key key ) const;
        /**
         * @brief 게임 쪽 조회가 패드 @p deviceIndex 의 컨트롤 @p controlIndex(버튼 0..13, 왼쪽 트리거 100, 오른쪽 트리거 101)를 볼 수 있으면 true 입니다.
         * @details 개발 콘솔이 열려 있는 동안은 모두 false 입니다 — 콘솔이 패드 A · B · 십자키로 조작되므로(디버그 키 맵) 게임이 같은 입력을 또 받으면 안 된다
         *          (shooter3d 는 패드 `Back` 이 카메라 전환과 겹친다). 콘솔이 쥔 동안 눌렸거나 닫을 때 눌려 있던 컨트롤은 뗄 때까지 가린다(닫는 B 가 게임의 B 로 새지 않게).
         *          키보드와 달리 이 가림은 `Game` 이 아닌 포커스 중 콘솔(`DevConsole`)에만 걸린다 — 런타임 UI 글자 입력 중에도 패드로 UI 를 다녀야 한다.
         *          키보드 포커스를 무시하는 맵(디버그 키 맵, UI 맵)은 이 조회를 쓰지 않고 장치 상태를 그대로 읽는다.
         */
        bool isGamepadControlVisibleToGame( uint32 deviceIndex, uint16 controlIndex ) const;

        bool wasAnyInputPressed() const;
        /**
         * @brief 이번 프레임에 새로 눌린 키보드, 마우스, 게임패드 슬롯 하나를 찾습니다(키 바인딩 창이 받을 키). 순서는 키보드, 마우스, 패드이고, 한 장치 안에서는 열거 순서입니다.
         * @details 장치 상태를 바로 본다. 키보드 포커스가 `UI` 여도 키를 받는다(바인딩 창). 개발 콘솔이 키보드를 가졌으면 키보드는 보지 않는다.
         */
        [[nodiscard]] bool findFirstPressedSlot( InputSlot& outSlot ) const;
        void               onTextInput( string_view text );
        void               onTextComposition( string_view text );
        /**
         * @brief 창이 낸 조합 · 확정 글(`NativeWindowEvent::kMessageTextInput`)을 큐에 넣습니다.
         * @details 글 이벤트의 칸보다 긴 확정 글은 글자 경계에서 나눠 넣는다(입력기는 문장을 한 번에 확정할 수 있다 - 자르면 글을 잃는다). 조합 글은 `postTextComposition` 이다.
         */
        void postWindowTextInput( const WindowTextInputEvent& event );
        /**
         * @brief 조합 글을 큐에 넣습니다. 칸보다 길면 글자 경계에서 나누고 앞 조각에 이어짐(`_bTextContinued`)을 켠다 — 큐를 비울 때 이어 붙여 콜백은 온전한 글을 한 번 받는다.
         * @details 조합은 지금 상태 전체를 매번 다시 알린다 - 자르면 일본어 긴 변환이 위젯에서 끝이 잘려 보였다.
         */
        void postTextComposition( string_view text );
        /** @brief 글 커서의 자리를 주 창의 입력기에 알립니다(창 픽셀). 창이 없으면 아무것도 하지 않습니다. 런타임 UI 글자 입력이 부른다. */
        void setTextInputArea( const WindowTextInputArea& area );

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

        int2 getMousePosition() const { return _pMouse != nullptr ? _pMouse->getPosition() : int2{}; }
        /** @brief 포인터의 게임 화면 안 비율(0 ~ 1, 왼쪽 위 원점)입니다. 게임 화면은 `getGameViewRect` 이고 창 전체가 아니다. 게임 화면이 없으면 0 입니다. */
        float2 getMousePositionNormalized() const;
        /** @brief 게임 화면의 창 안 사각형을 정합니다(엔진이 프레임마다 — `InputViewRect`). */
        void setGameViewRect( const InputViewRect& view ) { _gameView = view; }
        /** @brief 게임 화면의 창 안 사각형입니다. */
        const InputViewRect& getGameViewRect() const { return _gameView; }
        int2                 getMouseDelta() const;
        float32              getMouseWheel() const { return _pMouse != nullptr ? _pMouse->getMouseWheel() : 0.0f; }

        /**
         * @brief 포인터가 창 안에 있고 주어진 사각형(픽셀) 위에 있으면 true 입니다.
         * @details 이것은 액션이 아니라 장치 상태이므로 InputMap 이 아니라 여기에 있다.
         *          쓰는 값(isPointerInside, getMousePosition)도 모두 여기에 있다.
         */
        bool isPointerOverRect( int32 x, int32 y, int32 width, int32 height ) const;

        /**
         * @brief 마우스를 @p mode 로 잠그고 커서를 숨깁니다. 돌려준 구독이 풀리면 놓습니다 — 여럿이 쥐면 나중에 쥔 쪽의 방식이고, 하나라도 쥐고 있으면 풀리지 않습니다.
         * @details 칸 하나를 함께 쓰면 떠나는 조종자(이전 씬 · 하위 레벨 · 분할 화면의 다른 사람)의 놓기가 새로 빙의한 조종자의 잠금을 푼다.
         */
        [[nodiscard]] Subscription claimMouseLock( MouseLockMode mode );
        /**
         * @brief OS 커서 모양을 바꿉니다(런타임 UI 가 호버한 위젯의 `UICursorShape` 로 프레임마다 부른다). 같은 모양이면 아무것도 하지 않습니다.
         * @details 숨긴 커서(잠금)에는 적용하지 않고 기억했다가 다시 보일 때 적용한다. 에디터에서는 ImGui 가 OS 커서를 쥐므로 이 모양은 창에 보이지 않고, 게임 뷰 패널이 `getCursorShape` 를 읽어 ImGui 커서로 낸다.
         */
        void        setCursorShape( CursorShape shape );
        CursorShape getCursorShape() const { return _cursorShape; }

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

        // 6) 게임패드의 방아쇠 · 배터리 · 진동은 장치에 직접 묻는다(`getGamepad( index )->getLeftTrigger()` · `getBatteryInfo()` · `playVibration(…)`).

        // ------------------------------------------------------------------------------
        // 7) 가상 입력(Virtual Input). 테스트, 시나리오, 입력 리플레이가 OS 이벤트와 같은 경로로 입력을 넣는다
        // ------------------------------------------------------------------------------
        /**
         * @brief 가상 입력 원천을 맨 위에 붙입니다. 돌려준 구독이 붙임의 주인이며, 풀리면 이 붙임과 그 위에 붙은 것을 모두 뗍니다.
         * @details 붙임은 쌓인다 — 맨 위 원천만 이벤트를 내고, 아래 원천은 멈췄다가 위가 떨어지면 다시 낸다. 덮은 쪽(재생)은 덮인 쪽(시나리오)보다 오래 살지 못한다.
         *          칸 하나를 덮어쓰면 먼저 붙인 쪽이 입력을 조용히 잃고, 자기 것이 아니라 떼지도 못해 덮은 쪽이 남는다.
         *          프레임 번호는 붙임마다 0 부터 세고 멈춘 동안에도 흐른다(그동안의 이벤트는 내지 않는다 — 시나리오 시각과 어긋나지 않게).
         *          붙이는 순간 모든 장치 상태를 지운다. 사람이 누르고 있던 키가 테스트의 첫 프레임에 남지 않게 하기 위해서다. 뗄 때도 지운다(가상 키가 눌린 채 남지 않게).
         *          배타 모드면 붙어 있는 동안 OS 의 키, 마우스, 패드 이벤트와 패드 폴링, 창 포커스 이벤트, 커서 가두기와 가운데 되돌리기를 하지 않고,
         *          패드 연결은 가상 연결 이벤트가 정한다. 엔진 키보드 포커스(`claimKeyboardFocus`, 개발 콘솔)는 가상 키에도 그대로 적용된다.
         * @param source 빌려 쓰므로 떼기 전까지 살아 있어야 합니다(구독을 원천보다 뒤에 선언한다). 이미 붙은 원천이면 오류를 남기고 빈 구독을 돌려줍니다.
         * @param bResetState false 면 장치 상태를 지우지 않습니다. `InputReplay::seekTo` 로 만든 상태에서 이어 재생할 때 씁니다.
         */
        [[nodiscard]] Subscription attachVirtualInput( IVirtualInputSource& source, VirtualInputMode mode = VirtualInputMode::Exclusive, bool bResetState = true );
        /**
         * @brief 장치 상태를 지우고 @p source 를 배타로 붙여 @p listDeltaSeconds 의 프레임을 바로 돌린 뒤, 만든 상태를 남기고 뗍니다(되감기 · 탐색).
         * @details 다시 만든 프레임은 실제 프레임이 아니므로 아래 붙임의 프레임 번호는 흐르지 않는다.
         */
        void runVirtualFrames( IVirtualInputSource& source, const vector<float32>& listDeltaSeconds );
        bool isVirtualInputAttached() const { return _listVirtualInput.empty() == false; }
        /** @brief @p source 가 붙어 있으면(위에 덮여 멈췄어도) true 입니다. */
        bool isVirtualInputAttached( const IVirtualInputSource& source ) const { return findVirtualInput( source ) != nullptr; }
        /** @brief 지금 이벤트를 내는(맨 위) 원천입니다. 없으면 nullptr 입니다. */
        IVirtualInputSource* getVirtualInput() const { return _listVirtualInput.empty() ? nullptr : _listVirtualInput.back()._pSource; }
        /** @brief 배타 가상 입력이 (위에 덮여 있더라도) 붙어 OS 입력을 무시하는 중이면 true 입니다 — 시나리오가 부른 혼합 재생이 그 시나리오의 배타를 풀지 않습니다. */
        bool isOSInputSuppressed() const;
        /**
         * @brief 무인 자동화(시나리오 `input="exclusive"`)가 사람의 마우스와 키보드 입력을 창 메시지 단계에서 버리게 합니다. 언리얼의 `-unattended` 와 같은 의미입니다.
         * @details `isOSInputSuppressed` 는 게임 입력(이 매니저의 장치)만 막는다. 이 표시는 그보다 앞 단계인 창 메시지 분배 셸과 에디터 UI(ImGui 플랫폼 백엔드의 커서 조회)까지 막는다.
         *          창 이벤트(닫기, 크기, 포커스)는 그대로 둔다. 배타 가상 입력이 붙어 있을 때만 켜지고, 맨 위 붙임에 적혀 그 붙임이 떨어질 때 꺼진다(위에 덮여 있는 동안에도 켜져 있다).
         *          입력 리플레이처럼 사람이 에디터를 계속 조작하는 배타 재생에서는 켜지 않는다.
         */
        void setOSDeviceInputBlocked( bool bBlocked );
        /** @brief 사람의 마우스, 키보드 이벤트를 버리는 중이면 true 입니다(`setOSDeviceInputBlocked`). */
        bool isOSDeviceInputBlocked() const;
        /** @brief 창 메시지 @p event 를 입력 계층 앞에서 버려야 하면 true 입니다. 버리는 중인 마우스와 키보드 이벤트가 해당합니다. */
        bool shouldDropNativeEvent( const NativeWindowEvent& event ) const;
        /**
         * @brief 창 메시지 입구(호스트)가 버리기 검사보다 먼저 부릅니다 — 사람이 낸 OS 키보드 · 마우스 입력(뗌 제외)을 셉니다(`getHumanInputSerial`).
         * @details 가상 입력은 창 메시지를 거치지 않으므로 이 수에 들지 않는다 — 데모 · 어트랙트 재생이 "사람이 손대면 멈춤" 을 가상 입력과 섞지 않고 본다(`ScenarioPlayback`).
         *          배타 모드가 사람 입력을 버려도 센다(버리기 전에 부른다). 게임패드는 창 메시지가 아니라 장치 폴링이라 세지 않는다.
         */
        void recordNativeEvent( const NativeWindowEvent& event );
        /** @brief 지금까지 센 사람 입력 수입니다(늘기만 한다 — 시작 때 값과 비교한다). */
        uint64 getHumanInputSerial() const { return _humanInputSerial; }
        /** @brief 지금까지 OS 에서 받은 원시 마우스 이동 보고 수입니다(Win32 WM_INPUT, 이동 0 인 보고는 세지 않는다). 늘기만 한다 — 감시 값 `Input.RawMouseEvents`. */
        const DevCounter& getRawMouseEventCount() const { return _rawMouseEventCount; }
        /** @brief @p source 를 붙인 뒤 지난 `beginFrame` 횟수이며, 다음 `beginFrame` 의 프레임 번호입니다. 붙어 있지 않으면 0 입니다. */
        uint32 getVirtualFrameIndex( const IVirtualInputSource& source ) const;
        /** @brief 이번 `beginFrame` 이 장치에 적용한 원시 이벤트입니다(배타 가상 입력이면 가상 이벤트만). 입력 녹화가 읽으며, 다음 `beginFrame` 까지 유효합니다. */
        const vector<RawInputEvent>& getLastFrameEvents() const { return _listPolledEvent; }
        /** @brief 지금까지 `beginFrame` 을 부른 횟수입니다(프레임마다 한 번 읽는 쪽이 같은 프레임을 두 번 읽지 않게). */
        uint32 getBeginFrameCount() const { return _beginFrameCount; }
        /**
         * @brief 세션 입력 기록(버그 리포트가 담는 것)을 시작합니다. 있던 기록은 지웁니다. `beginFrame` 이 적용한 이벤트를 프레임마다 담고 @p maxFrameCount 를 채우면 스스로 멈춥니다.
         * @details 언리얼 `DemoRec` 처럼 늘 도는 녹화가 아니라 사람이 켜고 끄는 고정 용량 기록이다(꺼져 있을 때 비용은 비트 하나 검사). 개발 명령 `bugreport.input.start` 가 부른다.
         */
        void startSessionRecording( uint32 maxFrameCount ) { _sessionRecording.startRecording( "BugReportSession", maxFrameCount ); }
        /** @brief 세션 입력 기록을 멈춥니다. 담은 프레임은 다음 시작까지 남습니다(버그 리포트가 읽는다). */
        void stopSessionRecording() { _sessionRecording.stopRecording(); }
        /**
         * @brief @p filePath(.swreplay)를 세션 입력 기록으로 읽어 배타 가상 입력으로 붙입니다(앞 재생은 끝낸다). 끝 프레임을 낸 `beginFrame` 이 스스로 떼고, 덮었던 원천(시나리오)이 다시 냅니다.
         * @return 읽지 못하면 false 이고 붙이지 않습니다(있던 기록은 그대로).
         */
        [[nodiscard]] bool playSessionRecording( const AbsolutePath& filePath );
        /** @brief 세션 입력 기록입니다(읽기 전용). */
        const InputReplay& getSessionRecording() const { return _sessionRecording; }

        // ------------------------------------------------------------------------------
        // 8) 플랫폼 네이티브 이벤트 처리와 접근성 제어
        // ------------------------------------------------------------------------------
        void onNativeWindowEvent( const NativeWindowEvent& event );
        void processNativeEvent( const NativeWindowEvent& event );
        void pollPlatform();
        /**
         * @brief OS 접근성 단축키(Windows 고정 · 토글 · 필터 키, X11 AccessX)를 끄고 끄기 전 상태를 이 관리자에 적어 둡니다. 이미 껐으면 아무것도 하지 않습니다.
         * @details 끄기 전 상태는 이 관리자의 칸이다(`_arrSavedAccessibilityFlags`) — 언리얼 `FWindowsApplication` 이 시작 때의 StickyKeys 를 멤버로 들고 내릴 때 되돌리는 것과 같다.
         */
        void disableWindowsAccessibilityShortcuts();
        /** @brief `disableWindowsAccessibilityShortcuts` 가 적어 둔 상태로 되돌립니다(포커스를 잃을 때와 `shutdown` 이 부른다). 끈 적이 없으면 아무것도 하지 않습니다. */
        void restoreWindowsAccessibilityShortcuts();

    private:
        /** @brief 원시 이벤트 큐의 칸 하나입니다 — 이벤트와 그것을 적용할 입력 프레임 번호(`postRawEventAt`). 번호는 리플레이 파일에 쓰는 `RawInputEvent` 배치를 바꾸지 않게 칸에 둔다. */
        struct QueuedRawEvent
        {
            RawInputEvent _event{};
            uint32        _inputFrame{ 0 };
        };

        /** @brief 이번 프레임에 눌린 컨트롤 하나와 그 순간입니다(`tryGetPressInstant`). */
        struct FramePress
        {
            InputSlot         _slot{};
            InputPressInstant _instant{};
        };

        /** @brief 붙은 가상 입력 하나입니다(`attachVirtualInput`). */
        struct VirtualInputAttachment
        {
            IVirtualInputSource* _pSource{ nullptr };                  ///< 붙인 원천(빌림)
            uint64               _id{ 0 };                             ///< 주인 구독의 id
            uint32               _frameIndex{ 0 };                     ///< 붙인 뒤 `beginFrame` 횟수이며, 원천에 넘기는 프레임 번호
            VirtualInputMode     _mode{ VirtualInputMode::Exclusive }; ///< 이 붙임의 모드
            bool                 _bOSDeviceInputBlocked{ false };      ///< NOLINT(sw-bool-flag): 집합체다(중괄호로 만든다). 무인 자동화가 창 메시지 단계에서 사람의 마우스와 키보드 이벤트를 버린다(`setOSDeviceInputBlocked`)
        };

        /** @brief 마우스 잠금을 쥔 것 하나입니다(`claimMouseLock`). */
        struct MouseLockClaim
        {
            uint64        _id{ 0 };
            MouseLockMode _mode{ MouseLockMode::None };
        };

        /** @brief 마우스 잠금 @p id 의 구독이 풀렸습니다. */
        void releaseMouseLock( uint64 id );
        /** @brief 쥔 것들에서 잠금 방식 · 커서 표시를 다시 정해 장치와 OS 에 맞춥니다. */
        void syncMouseLockClaims();

        /** @brief 모든 사용자의 맵에 건 레이어 하나입니다(`pushSharedLayer` · `enableSharedLayer`). */
        struct SharedLayer
        {
            hashed_string        _layer{};
            uint64               _id{ 0 };
            vector<Subscription> _listMapLayer{};       ///< 맵마다 건 구독(닫힌 사용자의 것은 무효다)
            bool                 _bStacked{ false };    ///< NOLINT(sw-bool-flag): 집합체다. 스택에 넣는다(아니면 켜기만)
            bool                 _bBlockLower{ false }; ///< NOLINT(sw-bool-flag): 위와 같다. 막는 칸이다
        };

        /** @brief 공유 레이어 @p shared 를 @p map 에 겁니다. */
        static void applySharedLayer( SharedLayer& shared, InputMap& map );
        /** @brief 공유 레이어를 지금 있는 모든 맵에 걸고 구독을 냅니다. */
        Subscription addSharedLayer( const hashed_string& layer, bool bStacked, bool bBlockLower );
        /** @brief 공유 레이어 @p id 의 구독이 풀렸습니다. */
        void releaseSharedLayer( uint64 id );

        /** @brief @p source 의 붙임입니다. 없으면 nullptr 입니다. */
        const VirtualInputAttachment* findVirtualInput( const IVirtualInputSource& source ) const;
        /** @brief @p source 를 맨 위에 쌓습니다. 이미 붙어 있으면 오류를 남기고 0, 아니면 붙임의 id 를 돌려줍니다. */
        uint64 pushVirtualInput( IVirtualInputSource& source, VirtualInputMode mode, bool bResetState );
        /** @brief 붙임 @p index 와 그 위를 모두 뗍니다. 커서 잠금과 패드 세션을 남은 붙임에 맞춥니다. */
        void popVirtualInputs( size_t index, bool bResetState );
        /** @brief 키보드를 쥔 주인 @p ownerID(`InputKeyboardFocus`)의 구독이 풀렸습니다. */
        void releaseKeyboardFocus( uint64 ownerID );
        /** @brief 쥔 주인들에서 키보드를 받을 쪽을 다시 고르고, 바뀌었으면 입력기 · 마우스 잠금을 맞춥니다. */
        void syncKeyboardFocus();
        /** @brief 붙임 @p id 의 구독이 풀렸습니다. */
        void releaseVirtualInput( uint64 id );

        void dispatchRawEvent( const RawInputEvent& rawEvent );
        /** @brief 누름 이벤트면 그 컨트롤의 이번 프레임 첫 누름 순간을 적습니다(적용하기 전에 부른다 — 시선 누적은 그 이벤트 앞까지다). */
        void recordFramePress( const RawInputEvent& rawEvent );
        /** @brief 큐에서 꺼낸 OS 이벤트 [0, @p osEventCount) 에 프레임 안 자리를 채웁니다. 창은 [지금 - @p deltaSeconds, 지금] 입니다. */
        void assignFrameFractions( size_t osEventCount, float32 deltaSeconds );
        /** @brief 가상 커서를 입력 맵의 `virtualCursor` 대로 켜고 끄고 스틱을 맞춥니다. 바뀔 때만 켜고 끕니다(켜고 끌 때 버튼 상태를 새로 잡는다). */
        void syncVirtualCursorToMap();
        /** @brief 로컬 멀티가 열렸으면 짝이 없는 사용자에게 연결된 패드를 짝짓고 맵에 알립니다(`beginFrame` · `openLocalUsers`). */
        void pairLocalUsers();
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
                        _onGamepadConnectionChanged.get()( index, bConnected );
                } );
                registerDevice( std::move( pGamepad ) );
            }
        }
        /** @brief 커서 표시와 숨김을 OS 에 실제로 적용합니다. `syncMouseLock` 이 숨김 상태가 바뀔 때만 부릅니다(Win32 `ShowCursor` 는 카운터입니다). */
        void setCursorVisiblePlatform( bool bVisible );
        /** @brief 커서 모양(`_cursorShape`)을 OS 에 적용합니다(Win32 창 클래스 커서 + `SetCursor`, X11 `XDefineCursor`). 숨긴 동안에는 부르지 않습니다. */
        void applyCursorShapePlatform();
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
        /** @brief 시각을 적어 입력 프레임 @p inputFrame 의 칸으로 큐에 넣습니다(음소거는 부르는 쪽이 본다). 큐가 차면 버린 수를 세고 false 입니다. */
        bool enqueueRawEvent( const RawInputEvent& rawEvent, uint32 inputFrame );
        /** @brief 포커스가 `Game` 이 아닐 때 눌렀다가 지금은 뗀 키를 가림 목록에서 해제합니다(프레임 시작, 이벤트 적용 전). */
        void releaseCapturedKeys();
        /** @brief 개발 콘솔이 키보드를 쥔 동안 눌려 있는 패드 컨트롤을 가림 목록에 넣습니다(이벤트를 적용한 뒤). */
        void captureHeldGamepadControls();
        /** @brief 콘솔이 쥔 동안 눌렀다가 지금은 뗀 패드 컨트롤을 가림 목록에서 해제합니다(프레임 시작, 폴링 전). */
        void releaseCapturedGamepadControls();

    private:
        static constexpr size_t kKeyboardFocusCount = static_cast<size_t>( InputKeyboardFocus::Count );
        static constexpr size_t kKeyMaskWordCount   = ( static_cast<size_t>( Key::Count ) + 63 ) / 64;

        ConcurrentQueue<QueuedRawEvent, 2048>     _queueRawEvent;           /**< OS 와 폴러 스레드가 postRawEvent() 로 넣는 락프리 원시 이벤트 큐입니다. beginFrame() 이 매 프레임 비웁니다. */
        atomic<uint32>                            _droppedRawEventCount;    /**< 큐가 가득 차서 버린 원시 이벤트의 누적 개수입니다. beginFrame() 에서 요약 로그를 남기고 0 으로 초기화합니다. */
        atomic<uint32>                            _collectingInputFrame;    /**< 지금 이벤트를 모으는 입력 프레임 번호입니다(`getCollectingInputFrame`). beginFrame() 이 올린다. */
        atomic<uint32>                            _unsupportedReportedMask; /**< 창이 못 하는 동작(포인터 묶기 · 커서 모양 · 커서 숨기기)을 이미 알린 비트입니다(X11 · Wayland — 처음 한 번만 오류로 알린다). */
        atomic<bool>                              _bTrackingPointerLeave;   /**< 포인터 이탈 알림(Win32 WM_MOUSELEAVE)을 요청해 둔 상태인지입니다. TrackMouseEvent 는 이탈을 한 번 알리고 풀린다. */
        vector<unique_ptr<IInputDevice>>          _listDevice;              /**< 등록된 모든 장치(키보드, 마우스, 게임패드 등)를 소유하는 목록입니다. */
        KeyboardDevice*                           _pKeyboard;               /**< 편의 API 용 캐시 포인터입니다. 소유는 _listDevice 가 합니다. */
        MouseDevice*                              _pMouse;                  /**< 편의 API 용 캐시 포인터입니다. */
        TouchDevice*                              _pTouch;                  /**< 편의 API 용 캐시 포인터입니다. */
        GamepadDevice*                            _pGamepad;                /**< 0번 게임패드 편의 API 용 캐시 포인터입니다(1~3번은 getGamepad(index) 로 조회). */
        unique_ptr<InputMap>                      _pInputMap;               /**< 이 InputManager 에 연결된 기본 InputMap 인스턴스입니다. */
        vector<unique_ptr<InputMap>>              _listUserMap;             /**< 로컬 멀티의 사용자 1 부터의 입력 맵입니다(`openLocalUsers`). 닫혔으면 비었습니다. */
        vector<InputUserDevices>                  _listUserDevices;         /**< 로컬 멀티의 사용자마다 짝지은 장치입니다(사용자 0 포함). 닫혔으면 비었습니다. */
        vector<MouseLockClaim>                    _listMouseLockClaim;      /**< 마우스 잠금을 쥔 것들입니다(뒤가 이긴다). */
        uint64                                    _lastMouseLockID;         /**< 마지막으로 내준 잠금의 id 입니다. */
        vector<SharedLayer>                       _listSharedLayer;         /**< 모든 사용자의 맵에 건 레이어입니다. */
        uint64                                    _lastSharedLayerID;       /**< 마지막으로 내준 공유 레이어의 id 입니다. */
        vector<RawInputEvent>                     _listPolledEvent;         /**< beginFrame() 에서 큐를 비워 담아 두는 임시 버퍼입니다(매 프레임 재사용). */
        vector<QueuedRawEvent>                    _listQueuedEvent;         /**< 큐에서 꺼낸 칸입니다 — 뒤 입력 프레임 번호가 붙은 칸은 그 프레임의 beginFrame() 까지 넣은 순서대로 남는다. */
        vector<FramePress>                        _listFramePress;          /**< 이번 프레임에 눌린 컨트롤마다 첫 누름 순간입니다(beginFrame 이 비우고 채운다). */
        float2                                    _frameLookDelta;          /**< 이번 프레임에 적용한 마우스 원시 이동량의 합입니다. */
        int2                                      _lastPlatformPointer;     /**< 버튼 이벤트에 좌표가 없는 창(Wayland `wl_pointer.button`)이 쓰는 포인터의 마지막 자리입니다. */
        [[maybe_unused]] optional<int2>           _lastRawMouseAbsolute;    /**< 직전 절대 원시 마우스 보고의 화면 픽셀 좌표입니다(Win32 원격 데스크톱 · 가상 머신 — `Win32RawMouseUtil::readMotion`). 등록을 바꾸면 비운다. */
        ClockFunction                             _pfnNowMicroseconds;      /**< 이벤트 시각과 프레임 창을 측정하는 시계입니다(`setClock`). */
        int64                                     _frameEndMicroseconds;    /**< 이번 `beginFrame` 의 시각입니다(폴링한 누름의 순간). */
        vector<VirtualInputAttachment>            _listVirtualInput;        /**< 붙은 가상 입력입니다(뒤가 위). 비었으면 OS 입력만 받습니다. */
        VirtualCursor                             _virtualCursor;           /**< 패드 스틱으로 마우스 커서를 움직이는 가상 커서입니다. */
        InputReplay                               _sessionRecording;        /**< 버그 리포트용 세션 입력 기록입니다(`startSessionRecording`, 고정 용량). */
        InputViewRect                             _gameView;                ///< 게임 화면의 창 안 사각형(`setGameViewRect`)
        uint64                                    _lastVirtualInputID;      /**< 마지막으로 내준 붙임의 id 입니다(0 은 없음). */
        uint32                                    _beginFrameCount;         /**< `beginFrame` 을 부른 횟수입니다. */
        InputGlyphStyle                           _activeGlyphStyle;        /**< 마지막으로 조작이 감지된 장치 종류입니다(UI 글리프 자동 전환용). */
        DelegateSlot<ActiveDeviceChangedDelegate> _onActiveDeviceChanged;
        DelegateSlot<GamepadConnectionDelegate>   _onGamepadConnectionChanged;
        DelegateSlot<TextInputDelegate>           _arrOnTextInput[kKeyboardFocusCount];        /**< 키보드 포커스 주인마다의 글자 콜백입니다. */
        DelegateSlot<TextInputDelegate>           _arrOnTextComposition[kKeyboardFocusCount];  /**< 키보드 포커스 주인마다의 조합 중 글자 콜백입니다. */
        string                                    _pendingComposition;                         /**< 이어지는 조합 조각(`_bTextContinued`)을 마지막 조각까지 모으는 글입니다. 큐를 비우는 스레드만 쓴다. */
        uint64                                    _arrCapturedKeyMask[kKeyMaskWordCount];      /**< 포커스가 `Game` 이 아닐 때 눌려서 게임에 보이지 않게 가린 키입니다(뗀 다음 프레임에 해제). */
        uint32                                    _arrCapturedGamepadMask[kMaxGamepadSlot];    /**< 개발 콘솔이 키보드를 쥔 동안 눌려서 게임에 보이지 않게 가린 패드 컨트롤입니다(비트는 `makeGamepadControlBit`). */
        uint32                                    _arrSavedAccessibilityFlags[3];              /**< 접근성 단축키를 끄기 전의 OS 값입니다 — Windows 는 고정 · 토글 · 필터 키의 dwFlags, X11 은 [0] 에 XKB enabled_ctrls. */
        uint32                                    _arrKeyboardClaimCount[kKeyboardFocusCount]; /**< 키보드 포커스 주인마다 쥔 구독 수입니다(`claimKeyboardFocus`). */
        InputKeyboardFocus                        _keyboardFocus;                              /**< 지금 키보드를 받는 쪽입니다(쥔 주인 가운데 우선순위가 가장 높은 것). */
        CursorShape                               _cursorShape;                                /**< OS 커서 모양입니다(`setCursorShape`). */
        [[maybe_unused]] uint16                   _pendingHighSurrogate;                       /**< 짝을 기다리는 서로게이트의 앞 반쪽입니다(Win32 WM_CHAR). 0 이면 없습니다. */
        uint8                                     _consumedButtonMask;                         /**< 잠금을 다시 잡느라 삼킨, 아직 떼지 않은 마우스 버튼 비트입니다(`1 << MouseButton`). */
        uint8                                     _bInitialized                    : 1;
        uint8                                     _bInputMuted                     : 1;
        uint8                                     _bWindowFocused                  : 1; /**< 창이 포커스를 가지고 있는지 나타냅니다(포커스 메시지로 갱신하며, 창 없는 테스트에서는 true). */
        uint8                                     _bMouseLockEngaged               : 1; /**< 포커스를 잃은 뒤 클라이언트를 눌러 잠금을 다시 잡았는지 나타냅니다. */
        uint8                                     _bAltHeld                        : 1; /**< Alt 를 누르고 있는지 나타냅니다(누르는 동안 잠금을 쉽니다). */
        uint8                                     _bCursorHiddenApplied            : 1; /**< OS 커서를 숨겨 둔 상태인지 나타냅니다. `ShowCursor` 는 카운터라서 전이할 때만 부릅니다. */
        uint8                                     _bNativeWindowFocused            : 1; /**< OS 창 자체가 포커스를 가졌는지입니다(X11 FocusIn/FocusOut) — 보조 폴링 · 가운데 되돌리기를 건너뛸지 정한다. */
        uint8                                     _bAccessibilityShortcutsDisabled : 1; /**< 접근성 단축키를 꺼 두었는지입니다(`_arrSavedAccessibilityFlags` 가 유효하다). */
        uint8                                     _bBackgroundGamepadInput         : 1; /**< 배경에서도 패드를 폴링하는지입니다 — 엔진은 설정이 정한다(`initializeSubsystem`), 설정 없이 만든 단위 테스트는 켠 채로 시작한다. */
        uint64                                    _humanInputSerial{ 0 };               /**< 사람이 낸 OS 키보드 · 마우스 입력 수(`recordNativeEvent`) */
        DevCounter                                _rawMouseEventCount;                  /**< OS 에서 받은 원시 마우스 이동 보고 수(`getRawMouseEventCount`) */
        Subscription                              _sessionPlayback;                     /**< 세션 입력 기록 재생의 붙임입니다(`playSessionRecording`). */
        SubscriptionList                          _mouseLockSubscriptions;              /**< 마우스 잠금의 구독입니다(`claimMouseLock`). */
        SubscriptionList                          _sharedLayerSubscriptions;            /**< 공유 레이어의 구독입니다. */
        SubscriptionList                          _keyboardFocusSubscriptions;          /**< 키보드를 쥔 구독입니다(`claimKeyboardFocus`). */
        SubscriptionList                          _virtualInputSubscriptions;           /**< 가상 입력 붙임의 구독입니다. 다른 멤버보다 먼저 닫히게 맨 끝에 둔다. */

    private:
        SW_SERVICE_SLOT( InputManager, Required );
    };
} // namespace sw

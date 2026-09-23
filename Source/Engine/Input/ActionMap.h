/**
 * @file ActionMap.h
 * @brief 통합 ActionMap 입니다.
 *        ActionEntry/ActionBinding 하나로 단일 슬롯 · 1D/2D 합성 축 · 스틱 · 조합 키를 모두 담고,
 *        ActionPhase 상태 머신(Started, Ongoing, Triggered, Completed, Canceled),
 *        모디파이어 파이프라인(데드존, 축 반전, 감도 배율), LIFO 컨텍스트 스택,
 *        이벤트 델리게이트 디스패치, 선입력 버퍼링, 격투 커맨드 시퀀스, XML 직렬화를 제공합니다.
 */
#pragma once
#include "Core/Common/Defines.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

#include "Engine/Input/GamepadButtons.h"
#include "Engine/Input/IInputDevice.h"
#include "Engine/Input/KeyCodes.h"

namespace sw
{
    enum class InputDeviceType : uint8;

    class InputManager;

    // ------------------------------------------------------------------------------
    // 1) 스키마: 액션 값 타입 · 바인딩 종류 · 트리거 · 페이즈 · 데드존 모드
    // ------------------------------------------------------------------------------
    enum class InputActionValueType : uint8
    {
        Boolean = 0, ///< 디지털 버튼 / On-Off
        Axis1D,      ///< 1차원 아날로그 축 [-1.0, +1.0]
        Axis2D       ///< 2차원 평면 이동/시점 벡터
    };

    enum class BindingKind : uint8
    {
        SingleSlot = 0,    ///< 단일 Key / MouseButton / GamepadButton
        Axis1DComposite,   ///< 1D 축 합성 (Negative Slot: -1.0, Positive Slot: +1.0)
        Vector2DComposite, ///< 2D 벡터 합성 (Up, Down, Left, Right 4방향)
        GamepadStick2D,    ///< 게임패드 아날로그 스틱 (Left / Right)
        Chord,             ///< 조합 키 (Modifier Slot + Trigger Slot)
        MouseDelta2D,      ///< 마우스 2D 센서 델타 (FPS/TPS 룩 벡터)
        Shortcut,          ///< 다중 수정자 마스크 + 키 조합 (Ctrl + Shift + Key)
        AnyKey,            ///< 임의의 키/버튼 입력 ("Press Any Key")
        VirtualJoystick2D, ///< 마우스 드래그 기반 가상 조이스틱(온스크린 스틱 프로토타이핑/테스트용)
        Count              ///< 종류 수. **저장되지 않음.** 표(`kArrBindingKindTraits`)의 크기를 컴파일 시점에 맞추는 데만 씀.
    };

    /**
     * @struct BindingKinds
     * @brief `BindingKind` 하나에 딸린 값들을 모아 둔 표입니다. 종류를 늘릴 때 고칠 곳은 **여기 한 줄**입니다.
     *
     * [왜 표인가]
     * 예전에는 종류를 하나 더하려면 **여섯 곳**을 손으로 맞춰야 했습니다: 열거자 · 충돌 검사의 슬롯 수
     * switch · 평가 switch · 저장 switch · 로드의 문자열 if/else 사슬 · 그리고 그 모두를 손으로
     * 나열한 테스트. 게다가 세 switch 에 `default:` 가 있어 **빠뜨려도 컴파일러가 말해 주지 않았습니다.**
     * 충돌 검사는 새 종류를 못 본 채 지나갔고(같은 키를 두 번 바인드해도 조용했습니다), 저장은 그 바인딩을
     * **파일에서 통째로 빠뜨렸습니다.** 조용히 데이터를 버리는 쪽이 제일 나쁩니다.
     * 이제 표가 기준이고, `Count` 와의 `static_assert` 가 빠진 줄을 컴파일 오류로 만듭니다.
     *
     * [왜 리플렉션이 아닌가]
     * 같은 폴더의 `KeyCodes` · `MouseButtons` 는 리플렉션 등록부로 이름을 얻습니다. 여기서는 쓰지 않습니다.
     * (1) XML 에 적히는 이름("single" · "axis1d")이 열거자 이름과 **일부러 다르고**(파일 포맷입니다),
     * (2) 등록부는 엔진 서비스가 묶여 있어야 답합니다. 묶이지 않은 채 저장하면 `KeyCodes::toName` 이
     * "Unknown" 을 반환하듯 종류 이름도 "Unknown" 이 되어 **저장이 조용히 망가집니다.** 저장 경로가
     * 서비스 바인딩에 기대게 둘 이유가 없습니다. 아홉 줄짜리 표는 그 대가를 치를 만큼 크지도 않습니다.
     */
    struct SW_API BindingKinds
    {
        /** @brief XML `kind` 특성에 적는 이름입니다. 저장과 로드가 **이 하나**를 같이 씁니다. */
        static const utf8* toName( BindingKind kind );
        /** @brief 이름에서 종류를 찾습니다. 모르는 이름이면 `BindingKind::Count` 입니다. */
        static BindingKind fromName( string_view name );
        /**
         * @brief 이 종류에서 **키 충돌 검사가 훑어야 할** 슬롯 수입니다.
         * @details "쓰는 슬롯 수" 가 아닙니다. 스틱 · 마우스 델타 · AnyKey 는 슬롯을 쓰더라도 특정 키를
         *          점유하지 않으므로 충돌 대상이 아니라 0 입니다. 이름을 `getSlotCount` 로 줄이면
         *          다음 사람이 그 차이를 모르고 슬롯 순회에 씁니다.
         */
        static uint32 getConflictSlotCount( BindingKind kind );
    };

    enum class ConflictResolution : uint8
    {
        Swap = 0,    ///< 기존 액션과 새 액션의 키를 서로 맞바꿈
        Override,    ///< 기존 액션에서 해당 키를 제거하고 새 액션에만 할당
        AddSecondary ///< 기존 액션을 유지하고 보조 바인딩 슬롯으로 추가
    };

    enum class ActionTrigger : uint8
    {
        Pressed = 0,
        Released,
        Down,
        DoubleClicked,
        HoldThreshold,
        HoldAndRelease, ///< 임계 시간 이상 누르고 있다가 뗀 순간 발화 (차지 샷)
        Tap,            ///< 짧게 눌렀다 뗀 순간 발화 (회피/대시)
        Pulse,          ///< 누르고 있는 동안 주기적으로 반복 발화 (연사)
        DoubleTap,      ///< 같은 키를 짧은 시간 내에 2번 탭했을 때 발화
        Repeat,         ///< 처음 누를 때 한 번 바로 발화하고, navRepeatDelay 뒤부터 navRepeatRate 간격으로 반복 발화 (UI 내비게이션)
        Count
    };

    enum class ActionPhase : uint8
    {
        None = 0,
        Started,   ///< 최초 입력 감지 (0.0 → 0 아닌 값 / Down 시작)
        Ongoing,   ///< 입력 지속 및 트리거 대기 (Hold 진행 중)
        Triggered, ///< 트리거 조건 달성 순간 발화 (Performed)
        Completed, ///< 트리거 발화 후 입력 정상 해제
        Canceled   ///< 트리거 미달성 상태에서 입력 조기 해제 (예: Hold 시간 미달 취소)
    };

    enum class GamepadStick : uint8
    {
        Left  = 0,
        Right = 1
    };

    enum class DeadzoneShape : uint8
    {
        Radial = 0, ///< 360도 원형 거리 기반 데드존 (자유 이동)
        Axial       ///< X/Y 독립 십자형 데드존 (4방향 그리드/플랫포머)
    };

    namespace ActionMapDefaults
    {
        inline constexpr utf8    kDefaultLayerName[]     = "Gameplay";
        inline constexpr utf8    kTitleLayerName[]       = "Title";
        inline constexpr utf8    kDebugLayerName[]       = "Debug";
        inline constexpr utf8    kReloadShadersAction[]  = "ReloadShaders";
        inline constexpr utf8    kReloadEditorAction[]   = "ReloadEditor";
        inline constexpr utf8    kReloadGameAction[]     = "ReloadGame";
        inline constexpr utf8    kQuickSaveAction[]      = "QuickSave";
        inline constexpr utf8    kQuickLoadAction[]      = "QuickLoad";
        inline constexpr float32 kDoubleClickTime        = 0.35f;
        inline constexpr float32 kDoubleClickMaxDistance = 6.0f;
        inline constexpr float32 kDoubleTapTime          = 0.22f;
        inline constexpr float32 kHoldThreshold          = 0.4f;
        inline constexpr float32 kTapMaxTime             = 0.2f;
        inline constexpr float32 kPulseInterval          = 0.1f;
        inline constexpr float32 kDoubleClickTimeMin     = 0.05f;
        inline constexpr float32 kDoubleClickTimeMax     = 2.0f;
        inline constexpr float32 kDoubleClickDistanceMax = 64.0f;
        inline constexpr float32 kHoldThresholdMin       = 0.05f;
        inline constexpr float32 kHoldThresholdMax       = 10.0f;
        inline constexpr float32 kNeverPressedSentinel   = 1.0e9f;
    } // namespace ActionMapDefaults

    /// @brief XML 레이어 정의입니다(우선순위, enabled, blockLower, alwaysOn).
    struct LayerDef
    {
        hashed_string          _name;
        int32                  _priority;
        uint8                  _bEnabled    : 1;
        uint8                  _bBlockLower : 1; /**< 켜져 있으면 우선순위가 낮거나 스택 아래에 있는 레이어를 비활성화합니다. */
        uint8                  _bAlwaysOn   : 1; /**< 스택이나 enableOnlyLayer 에 영향받지 않고 항상 활성입니다. */
        [[maybe_unused]] uint8 _reserved    : 5;

        LayerDef()
            : _name{}
            , _priority{ 0 }
            , _bEnabled{ SW_TRUE }
            , _bBlockLower{ SW_FALSE }
            , _bAlwaysOn{ SW_FALSE }
            , _reserved{ 0 } {}
    };

    enum class DigitalNormalization : uint8
    {
        Circular = 0,   ///< 대각선 입력 시 반경 1.0으로 정규화 (표준 3D/FPS 액션)
        IndependentAxes ///< X/Y 독립 1.0 유지 (2D 그리드/탑다운)
    };

    /**
     * @struct ActionHandle
     * @brief 해시맵을 거치지 않고 O(1) 로 액션 상태를 읽는 토큰입니다.
     */
    struct ActionHandle
    {
        static constexpr uint32 kInvalidIndex = invalid_index::kUint32;

        uint32 _index{ kInvalidIndex };
        uint32 _generation{ 0 };

        constexpr bool isValid() const { return _index != kInvalidIndex; }
    };

    /**
     * @struct ActionBinding
     * @brief 단일 · 합성 · 스틱 · 조합 키 · 마우스 룩을 모두 담는 통합 바인딩입니다.
     */
    struct ActionBinding
    {
        /** @brief _cachedLayerIndex 가 아직 해석되지 않았음을 나타내는 무효값입니다. */
        static constexpr uint32 kInvalidLayerIndex = invalid_index::kUint32;

        hashed_string  _layer{};
        BindingKind    _kind{ BindingKind::SingleSlot };
        ActionTrigger  _trigger{ ActionTrigger::Pressed };
        uint8          _deviceIndex{ 0 };
        uint8          _modifierMask{ 0 }; /**< ModifierKey::Ctrl | Shift | Alt | Super */
        InputSlot      _arrSlot[4]{};      /**< SingleSlot[0], Axis1D[0=Neg,1=Pos], Vector2D[0=Up,1=Down,2=Left,3=Right], Chord[0=Mod,1=Trig] */
        GamepadStick   _stick{ GamepadStick::Left };
        float32        _deadzone{ 0.0f };
        float32        _outerDeadzone{ 1.0f };
        float32        _responseExponent{ 1.0f };
        float32        _scale{ 1.0f };                          /**< MouseDelta2D: 감도 배율. VirtualJoystick2D: 드래그 반경(px). */
        mutable uint32 _cachedLayerIndex{ kInvalidLayerIndex }; /**< _listLayerEntry 안정 인덱스. 포인터가 아니라 인덱스라 재할당에 안전합니다. */
        mutable float2 _joystickAnchor{ 0.0f, 0.0f };           /**< VirtualJoystick2D 전용. 드래그를 시작한 순간의 마우스 위치(플로팅 앵커). */
        mutable bool   _bJoystickAnchored{ false };             /**< VirtualJoystick2D 전용. 드래그 중이라 앵커가 잡혀 있는지 여부. */

        ActionBinding() = default;
    };

    /**
     * @struct DebugActionState
     * @brief 디버깅과 온스크린 뷰어용 실시간 액션 평가 상태입니다.
     */
    struct DebugActionState
    {
        hashed_string          _action;
        hashed_string          _layer;
        InputActionValueType   _valueType;
        ActionPhase            _phase;
        float2                 _value;
        float32                _holdDuration;
        uint8                  _bTriggered : 1;
        uint8                  _bDown      : 1;
        [[maybe_unused]] uint8 _reserved   : 6;
    };

    /**
     * @class ActionMap
     * @brief 액션 · 레이어 · 바인딩을 담고 매 프레임 평가하는 통합 액션 맵입니다.
     */
    class SW_API ActionMap
    {
    public:
        using LayerDef                 = sw::LayerDef;
        using ActionCallbackDelegate   = Delegate<void()>;
        using PhaseCallbackDelegate    = Delegate<void( ActionPhase )>;
        using Vector2DCallbackDelegate = Delegate<void( float2 )>;

        ActionMap();

        // ------------------------------------------------------------------------------
        // 2) 로드 · 수명주기
        // ------------------------------------------------------------------------------
        /** @brief 액션 · 레이어 · 바인딩을 모두 비웁니다. */
        void clear();
        /** @brief 리소스 상대 경로에서 InputMap XML 을 로드합니다. */
        bool loadFromResource( string_view relativePath );
        /** @brief 엔진 기본 폴백 바인딩을 등록합니다(Ctrl+F6/F7/F8, WASD, Space 등). */
        void bindDefaultFallback();
        /** @brief 한 프레임의 입력 상태를 평가하고 델리게이트를 디스패치합니다. */
        void update( float32 deltaSeconds );

        // ------------------------------------------------------------------------------
        // 3) 통합 바인딩 API: InputSlot · 키 · 패드 · 마우스 · 1D 합성 · 2D 합성 · 스틱 · 조합 키
        // ------------------------------------------------------------------------------
        /** @brief 바인딩 없이 액션 이름과 값 타입만 등록합니다(에디터에서 이름만 먼저 만들고 바인딩은 나중에 더할 때 씁니다). */
        void createAction( const hashed_string& action, InputActionValueType valueType = InputActionValueType::Boolean );
        void bind( const hashed_string& action, InputSlot slot, ActionTrigger trigger = ActionTrigger::Pressed, const hashed_string& layer = {} );
        void bind( const hashed_string& action, Key key, ActionTrigger trigger = ActionTrigger::Pressed, const hashed_string& layer = {} );
        void bind( const hashed_string& action, GamepadButton button, ActionTrigger trigger = ActionTrigger::Pressed, const hashed_string& layer = {} );
        void bind( const hashed_string& action, MouseButton mouse, ActionTrigger trigger = ActionTrigger::Pressed, const hashed_string& layer = {} );

        /** @brief 1D 축 합성 바인딩입니다(negativeKey: -1.0, positiveKey: +1.0). */
        void    bindAxis1DComposite( const hashed_string& action, Key negativeKey, Key positiveKey, const hashed_string& layer = {} );
        float32 getAxis1D( const hashed_string& action ) const;

        /** @brief 2D 이동 축 바인딩을 등록합니다(키보드 4방향 합성). */
        void bindVector2D( const hashed_string& action, Key up, Key down, Key left, Key right, float32 deadzone = 0.0f, const hashed_string& layer = {} );
        /** @brief 게임패드 아날로그 스틱 2D 이동 축 바인딩을 등록합니다(데드존, 바깥 데드존, 응답 곡선 가속). */
        void bindGamepadStick2D( const hashed_string& action, GamepadStick stick = GamepadStick::Left, float32 deadzone = 0.15f, const hashed_string& layer = {}, uint8 padIndex = 0, float32 outerDeadzone = 1.0f, float32 responseExponent = 1.0f );
        void bindMouseDelta( const hashed_string& action, float32 sensitivity = 1.0f, const hashed_string& layer = {} );
        /** @brief 마우스 드래그로 움직이는 가상 조이스틱 2D 축 바인딩을 등록합니다(앵커는 activationButton 을 누른 지점에 놓입니다). */
        void   bindVirtualJoystick2D( const hashed_string& action, MouseButton activationButton = MouseButton::Left, float32 radius = 64.0f, float32 deadzone = 0.1f, const hashed_string& layer = {}, float32 outerDeadzone = 1.0f );
        float2 getVector2D( const hashed_string& action ) const;

        /** @brief 조합 키(Chord) 바인딩을 등록합니다(예: Ctrl + F7, Shift + W). */
        void bindChord( const hashed_string& action, Key modifierKey, Key triggerKey, ActionTrigger trigger = ActionTrigger::Pressed, const hashed_string& layer = {} );
        void bindShortcut( const hashed_string& action, Key key, uint8 modifierMask, ActionTrigger trigger = ActionTrigger::Pressed, const hashed_string& layer = {} );
        void bindAnyKey( const hashed_string& action, const hashed_string& layer = {} );

        bool isChordDown( const hashed_string& action ) const;
        bool wasChordTriggered( const hashed_string& action ) const;

        // ------------------------------------------------------------------------------
        // 3.1) 이벤트 기반 델리게이트 바인딩
        // ------------------------------------------------------------------------------
        void bindActionCallback( const hashed_string& action, ActionTrigger trigger, ActionCallbackDelegate callback );
        void bindPhaseCallback( const hashed_string& action, ActionPhase phase, ActionCallbackDelegate callback );
        void bindVector2DCallback( const hashed_string& action, Vector2DCallbackDelegate callback );
        void clearCallbacks();

        // ------------------------------------------------------------------------------
        // 4) LIFO 컨텍스트 스택 · 모달/비모달 제어
        // ------------------------------------------------------------------------------
        void registerLayer( const hashed_string& name, int32 priority = 0, bool enabled = true, bool blockLower = false, bool alwaysOn = false );
        void setLayerEnabled( const hashed_string& layer, bool enabled );
        /** @brief LIFO 레이어 스택 맨 위에 레이어를 넣습니다(blockLower=false: 비모달 동시 조작, true: 모달 차단). */
        void pushLayer( const hashed_string& layer, bool blockLower = false, bool showCursor = false );
        /** @brief LIFO 레이어 스택 맨 위의 레이어를 뺍니다. */
        void popLayer();
        /** @brief LIFO 레이어 스택에서 특정 레이어를 찾아 뺍니다. */
        void popLayer( const hashed_string& layer );
        /** @brief 지금 LIFO 스택 맨 위 레이어 이름을 반환합니다. */
        string_view getCurrentTopLayer() const;
        void        enableOnlyLayer( const hashed_string& layer );

        // ------------------------------------------------------------------------------
        // 5) 감도 · 축 반전 · 데드존 모드 · 접근성 설정
        // ------------------------------------------------------------------------------
        void                 setInvertX( bool invert ) { _bInvertX = invert ? SW_TRUE : SW_FALSE; }
        void                 setInvertY( bool invert ) { _bInvertY = invert ? SW_TRUE : SW_FALSE; }
        bool                 isInvertX() const { return _bInvertX == SW_TRUE; }
        bool                 isInvertY() const { return _bInvertY == SW_TRUE; }
        void                 setMouseSensitivity( float2 sens ) { _mouseSensitivity = sens; }
        float2               getMouseSensitivity() const { return _mouseSensitivity; }
        void                 setGamepadSensitivity( float2 sens ) { _gamepadSensitivity = sens; }
        float2               getGamepadSensitivity() const { return _gamepadSensitivity; }
        void                 setDeadzoneShape( DeadzoneShape shape ) { _deadzoneShape = shape; }
        DeadzoneShape        getDeadzoneShape() const { return _deadzoneShape; }
        void                 setDigitalNormalization( DigitalNormalization mode ) { _digitalNormalization = mode; }
        DigitalNormalization getDigitalNormalization() const { return _digitalNormalization; }

        /** @brief 액션의 토글 모드를 켜거나 끕니다(접근성: 조준 · 달리기를 한 번 눌러 켜기). */
        void setToggleMode( const hashed_string& action, bool bToggle );
        bool isActionToggled( const hashed_string& action ) const;

        void setSuppressBaseActionOnChord( bool bSuppress ) { _bSuppressBaseActionOnChord = bSuppress ? SW_TRUE : SW_FALSE; }
        bool isSuppressBaseActionOnChord() const { return _bSuppressBaseActionOnChord == SW_TRUE; }

        void setNavRepeatDelay( float32 delay ) { _navRepeatDelay = delay; }
        void setNavRepeatRate( float32 rate ) { _navRepeatRate = rate; }

        // ------------------------------------------------------------------------------
        // 6) 액션 선입력 버퍼링 · 격투 커맨드 판정
        // ------------------------------------------------------------------------------
        void bufferAction( const hashed_string& action, float32 expirationSeconds = 0.2f );
        bool consumeBufferedAction( const hashed_string& action );

        /** @brief 지정한 시간 창 안에 커맨드 시퀀스(예: ["Down", "DownRight", "Right", "Attack"])가 이어서 나왔는지 검사합니다. */
        bool wasCommandSequenceTriggered( const vector<hashed_string>& listSequence, float32 maxWindowSeconds = 0.35f ) const;
        bool wasCommandPatternTriggered( const hashed_string& pattern, float32 maxWindowSeconds = 0.8f ) const;

        // ------------------------------------------------------------------------------
        // 7) UI 글리프 조회 · 게임 안 키 리매핑 직렬화
        // ------------------------------------------------------------------------------
        string getGlyphForAction( const hashed_string& action ) const;
        string getGlyphForAction( const hashed_string& action, InputDeviceType previewDevice ) const;

        /** @brief 런타임에 액션의 키 바인딩을 바꿉니다. */
        bool rebindKey( const hashed_string& action, Key newKey, uint32 bindIndex = 0 );
        bool rebindSlot( const hashed_string& action, InputSlot slot, uint32 bindIndex = 0 );
        bool rebindWithResolution( const hashed_string& action, InputSlot newSlot, ConflictResolution strategy = ConflictResolution::Swap, uint32 bindIndex = 0 );
        /** @brief 같은 레이어 안에서 다른 액션이 이미 그 슬롯을 점유하고 있는지 검사합니다. */
        bool hasBindingConflict( const InputSlot& slot, const hashed_string& layer, string& outConflictingAction ) const;
        /** @brief 지정 액션의 바인딩을 처음 등록한 기본 바인딩으로 되돌립니다. */
        bool resetActionToDefault( const hashed_string& action );
        /** @brief 모든 액션의 바인딩을 기본값으로 한꺼번에 되돌립니다. */
        void resetAllBindingsToDefault();
        /** @brief 유저 키 바인딩을 XML 파일로 저장합니다. */
        bool saveUserBindings( string_view filePath ) const;
        /** @brief 저장된 유저 키 바인딩 XML 파일을 로드해 적용합니다. */
        bool loadUserBindings( string_view filePath );

        // ------------------------------------------------------------------------------
        // 8) 입력 장치 연결 · 임계값 · 디버그
        // ------------------------------------------------------------------------------
        void setInputManager( InputManager* pInput ) { _pInput = pInput; }
        void setDoubleClickTime( float32 seconds );
        void setDoubleClickMaxDistance( float32 pixels );
        void setDoubleTapTime( float32 seconds ) { _doubleTapTime = seconds; }
        void setHoldThreshold( float32 seconds );
        void setNavRepeatTiming( float32 delaySeconds, float32 rateSeconds )
        {
            _navRepeatDelay = delaySeconds;
            _navRepeatRate  = rateSeconds;
        }
        InputManager* getInputManager() const { return _pInput; }
        float32       getDoubleClickTime() const { return _doubleClickTime; }
        float32       getDoubleTapTime() const { return _doubleTapTime; }
        float32       getHoldThreshold() const { return _holdThreshold; }
        float32       getNavRepeatDelay() const { return _navRepeatDelay; }
        float32       getNavRepeatRate() const { return _navRepeatRate; }

        /** @brief 실시간 액션 평가 상태를 덤프합니다(HUD · 에디터 인스펙터용). */
        void getDebugActionStates( vector<DebugActionState>& outListState ) const;

        // ------------------------------------------------------------------------------
        // 9) 조회 · 프레임 상태(통합 쿼리와 O(1) ActionHandle 핫패스)
        // ------------------------------------------------------------------------------
        ActionHandle getActionHandle( const hashed_string& action ) const;

        bool                         hasLayer( const hashed_string& layer ) const;
        bool                         isLayerEnabled( const hashed_string& layer ) const;
        int32                        getLayerPriority( const hashed_string& layer ) const;
        const vector<hashed_string>& getLayerNames() const { return _listLayerName; }
        const hashed_string&         getDefaultLayerName() const { return _defaultLayerName; }
        bool                         hasAction( const hashed_string& action ) const;
        const vector<hashed_string>& getActionNames() const { return _listActionName; }
        ActionTrigger                getBindingTrigger( const hashed_string& action, uint32 bindIndex ) const;
        uint32                       getBindingCount( const hashed_string& action ) const;
        const ActionBinding*         getBinding( const hashed_string& action, uint32 bindIndex ) const;

        bool wasActionTriggered( const hashed_string& action ) const;
        bool wasActionTriggered( ActionHandle handle ) const;

        bool isActionDown( const hashed_string& action ) const;
        bool isActionDown( ActionHandle handle ) const;

        bool wasActionPressed( const hashed_string& action ) const;
        bool wasActionPressed( ActionHandle handle ) const;

        bool wasActionReleased( const hashed_string& action ) const;
        bool wasActionReleased( ActionHandle handle ) const;

        bool wasActionDoubleClicked( const hashed_string& action ) const;
        bool wasActionDoubleClicked( ActionHandle handle ) const;

        bool wasActionHoldThreshold( const hashed_string& action ) const;
        bool wasActionHoldThreshold( ActionHandle handle ) const;

        float32 getActionHoldDuration( const hashed_string& action ) const;
        float32 getActionHoldDuration( ActionHandle handle ) const;

        float2 getVector2D( ActionHandle handle ) const;

        ActionPhase getActionPhase( const hashed_string& action ) const;
        ActionPhase getActionPhase( ActionHandle handle ) const;

        static ActionTrigger actionTriggerFromName( string_view name );
        static const utf8*   actionTriggerToName( ActionTrigger trigger );

    private:
        struct ActionBindingState
        {
            float32                _holdDuration;
            float32                _timeSinceLastPress;
            float32                _pulseTimer;
            int2                   _lastPress; ///< 더블클릭 판정용 직전 누름 위치
            uint8                  _bDown          : 1;
            uint8                  _bPressed       : 1;
            uint8                  _bReleased      : 1;
            uint8                  _bDoubleClicked : 1;
            uint8                  _bHoldThreshold : 1;
            uint8                  _bWasDown       : 1;
            uint8                  _bTriggered     : 1;
            [[maybe_unused]] uint8 _reserved       : 1;

            ActionBindingState()
                : _holdDuration{ 0.0f }
                , _timeSinceLastPress{ ActionMapDefaults::kNeverPressedSentinel }
                , _pulseTimer{ 0.0f }
                , _lastPress{}
                , _bDown{ SW_FALSE }
                , _bPressed{ SW_FALSE }
                , _bReleased{ SW_FALSE }
                , _bDoubleClicked{ SW_FALSE }
                , _bHoldThreshold{ SW_FALSE }
                , _bWasDown{ SW_FALSE }
                , _bTriggered{ SW_FALSE }
                , _reserved{ 0 } {}
        };

        struct ActionCallbackEntry
        {
            ActionTrigger          _trigger{ ActionTrigger::Pressed };
            ActionCallbackDelegate _callback;
        };

        struct PhaseCallbackEntry
        {
            ActionPhase            _phase{ ActionPhase::None };
            ActionCallbackDelegate _callback;
        };

        struct ActionEntry
        {
            InputActionValueType             _valueType{ InputActionValueType::Boolean };
            vector<ActionBinding>            _listBinding{};
            vector<ActionBindingState>       _listBindingState{};
            vector<ActionBinding>            _listDefaultBinding{};
            vector<ActionCallbackEntry>      _listActionCallback{};
            vector<PhaseCallbackEntry>       _listPhaseCallback{};
            vector<Vector2DCallbackDelegate> _listVector2DCallback{};
            float2                           _currentValue{ 0.0f, 0.0f };
            float32                          _holdDuration{ 0.0f };
            ActionPhase                      _currentPhase{ ActionPhase::None };
            uint32                           _handleIndex{ ActionHandle::kInvalidIndex };
            uint32                           _generation{ 1 };
            uint8                            _bDown          : 1;
            uint8                            _bPressed       : 1;
            uint8                            _bReleased      : 1;
            uint8                            _bDoubleClicked : 1;
            uint8                            _bHoldThreshold : 1;
            uint8                            _bTriggered     : 1;
            uint8                            _bToggleMode    : 1;
            uint8                            _bToggleState   : 1;

            ActionEntry()
                : _valueType{ InputActionValueType::Boolean }
                , _listBinding{}
                , _listBindingState{}
                , _listDefaultBinding{}
                , _listActionCallback{}
                , _listPhaseCallback{}
                , _listVector2DCallback{}
                , _currentValue{ 0.0f, 0.0f }
                , _holdDuration{ 0.0f }
                , _currentPhase{ ActionPhase::None }
                , _handleIndex{ ActionHandle::kInvalidIndex }
                , _generation{ 1 }
                , _bDown{ SW_FALSE }
                , _bPressed{ SW_FALSE }
                , _bReleased{ SW_FALSE }
                , _bDoubleClicked{ SW_FALSE }
                , _bHoldThreshold{ SW_FALSE }
                , _bTriggered{ SW_FALSE }
                , _bToggleMode{ SW_FALSE }
                , _bToggleState{ SW_FALSE } {}
        };

        static constexpr size_t kMaxBufferedActions = 16;
        static constexpr size_t kMaxCommandHistory  = 32;

        struct BufferedActionItem
        {
            hashed_string _action{};
            float32       _remainingTime{ 0.0f };
        };

        struct CommandHistoryEntry
        {
            hashed_string _action{};
            float32       _timestamp{ 0.0f };
        };

        const ActionEntry* getActionFromHandle( ActionHandle handle ) const;

        string       getGlyphForActionInternal( const hashed_string& action, InputDeviceType device ) const;
        bool         evaluateBindingDown( const ActionBinding& binding, float2& outValue ) const;
        bool         evaluateTrigger( ActionTrigger trigger, const ActionBindingState& state, float32 deltaSeconds ) const;
        bool         isBindingLayerActive( const ActionBinding& binding ) const;
        bool         isLayerActiveInternal( const hashed_string& layer ) const;
        void         ensureActionListed( const hashed_string& action );
        LayerDef&    ensureLayer( const hashed_string& name, int32 priority = 0, bool enabled = true, bool blockLower = false, bool alwaysOn = false );
        ActionEntry& getOrCreateAction( const hashed_string& action, InputActionValueType valueType = InputActionValueType::Boolean );
        /**
         * @brief 바인딩 하나의 공통 머리입니다. 레이어를 보장하고 액션을 목록에 올린 뒤, 레이어 인덱스를 캐시한 빈 바인딩을 반환합니다.
         * @details 예전에는 `bind*` 아홉이 이 열두 줄(과 아래 `commitBinding` 의 넷)을 각자 들었습니다. 바인딩 종류를 하나 더하면
         *          그것을 복사해야 했고, 세 목록(현재 · 기본값 · 상태) 중 하나만 빠져도 바인딩과 상태의 인덱스가 어긋났습니다.
         *          종류별 필드(슬롯 · 데드존 · 배율 …)만 부르는 쪽이 채웁니다.
         */
        ActionBinding beginBinding( const hashed_string& action, BindingKind kind, ActionTrigger trigger, const hashed_string& layer );
        /** @brief 바인딩을 액션의 세 목록(현재 · 기본값 · 상태)에 **함께** 넣습니다. 그래서 셋의 인덱스가 늘 같습니다. */
        void               commitBinding( const hashed_string& action, InputActionValueType valueType, const ActionBinding& binding );
        LayerDef*          findLayer( const hashed_string& name );
        const LayerDef*    findLayer( const hashed_string& name ) const;
        ActionEntry*       findAction( const hashed_string& action );
        const ActionEntry* findAction( const hashed_string& action ) const;

    private:
        InputManager*                        _pInput;
        unordered_map<hashed_string, uint32> _mapAction;       /**< 액션 이름 → _listActionEntry 인덱스. */
        unordered_map<hashed_string, uint32> _mapLayer;        /**< 레이어 이름 → _listLayerEntry 인덱스. */
        vector<ActionEntry>                  _listActionEntry; /**< 안정된 인덱스로만 접근하는 액션 슬롯 저장소(소유). 포인터를 들지 않으므로 재할당에 안전. */
        vector<LayerDef>                     _listLayerEntry;  /**< 안정된 인덱스로만 접근하는 레이어 저장소(소유). ActionBinding::_cachedLayerIndex 가 참조. */
        vector<hashed_string>                _listActionName;
        vector<hashed_string>                _listLayerName;
        vector<hashed_string>                _listLayerStack;
        BufferedActionItem                   _arrBufferedAction[kMaxBufferedActions];
        uint32                               _bufferedActionCount;
        uint32                               _bufferedActionHead;
        CommandHistoryEntry                  _arrCommandHistory[kMaxCommandHistory];
        uint32                               _commandHistoryCount;
        uint32                               _commandHistoryHead;
        uint32                               _nextGeneration;
        hashed_string                        _defaultLayerName;
        float2                               _mouseSensitivity;
        float2                               _gamepadSensitivity;
        float32                              _doubleClickTime;
        float32                              _doubleClickMaxDistance;
        float32                              _doubleTapTime;
        float32                              _holdThreshold;
        float32                              _navRepeatDelay;
        float32                              _navRepeatRate;
        float32                              _totalElapsedTime;
        DeadzoneShape                        _deadzoneShape;
        DigitalNormalization                 _digitalNormalization;
        uint8                                _bInvertX                   : 1;
        uint8                                _bInvertY                   : 1;
        uint8                                _bSuppressBaseActionOnChord : 1;
        [[maybe_unused]] uint8               _reservedFlags              : 5;
    };
} // namespace sw

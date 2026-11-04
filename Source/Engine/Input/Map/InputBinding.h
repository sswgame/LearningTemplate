/**
 * @file InputBinding.h
 * @brief 입력 맵의 스키마 형식입니다. 액션 값 타입 · 바인딩 종류 · 트리거 · 페이즈 · 스틱 · 레이어 정의 · 액션 핸들 · 바인딩 하나(`ActionBinding`).
 * @details `InputMap.h` 에서 뗐다 — 발화 규칙(`InputTriggerRules`) · 리매핑 · 글리프처럼 맵 본체 없이 바인딩만 보는 쪽이 맵 전체를 include 하지 않게 한다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

#include "Engine/Input/GamepadButtonUtil.h"
#include "Engine/Input/IInputDevice.h"
#include "Engine/Input/KeyCodeUtil.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    struct ActionBinding;

    // ------------------------------------------------------------------------------
    // 1) 스키마: 액션 값 타입, 바인딩 종류, 트리거, 페이즈, 데드존 모드
    // ------------------------------------------------------------------------------
    ENUM()
    enum class InputActionValueType : uint8
    {
        Boolean = 0, ///< 디지털 버튼 / On-Off
        Axis1D,      ///< 1차원 아날로그 축 [-1.0, +1.0]
        Axis2D       ///< 2차원 평면 이동/시점 벡터
    };

    /**
     * @brief 입력 맵이 켜는 패드 가상 커서입니다(`<InputMap virtualCursor="RightStick">`, 적지 않으면 끔).
     * @details 명령형 게임(RTS · 도시 건설)이 패드만으로 커서를 쓰게 한다. 맵을 들고 있는 `InputManager` 가 매 프레임 이 값대로 켜고 끈다 —
     *          게임 코드가 켜고 끄지 않으므로 서비스가 묶이는 시점 · 오브젝트 시작 순서에 기대지 않는다.
     *          가상 커서는 그 스틱과 패드 A · B 를 마우스 이동 · 왼쪽 · 오른쪽 버튼으로 바꾸므로, 맵이 같은 스틱 · 버튼을 액션에 따로 묶으면
     *          한 입력이 두 입력이 된다. 그래서 읽기가 그런 바인딩을 오류로 거절한다(`loadFromAsset`).
     */
    ENUM()
    enum class InputMapVirtualCursor : uint8
    {
        Off = 0,    ///< 가상 커서를 쓰지 않는다
        LeftStick,  ///< 왼쪽 스틱이 커서를 움직인다
        RightStick, ///< 오른쪽 스틱이 커서를 움직인다
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
        MouseWheel1D,      ///< 마우스 휠 이번 프레임 이동량(한 칸 = 1, 위가 +)을 읽는 1D 축. 핫바 넘기기와 확대에 쓴다.
        Count              ///< 종류 개수. 저장하지 않으며, `kArrBindingKindInfo` 의 크기를 컴파일 시점에 맞추는 데만 쓴다.
    };

    /**
     * @struct BindingKinds
     * @brief `BindingKind` 하나에 딸린 값을 모아 둔 테이블입니다. 종류를 늘릴 때 고칠 곳은 이 테이블의 한 줄뿐입니다.
     *
     * [왜 테이블인가]
     * 종류마다 딸린 값(충돌 검사의 슬롯 수, 평가, 저장과 로드에 쓰는 이름)을 switch 여러 곳에 흩어 두면
     * `default:` 때문에 하나를 빠뜨려도 컴파일러가 알려 주지 않습니다. 충돌 검사는 새 종류를 못 본 채 지나가고,
     * 저장은 그 바인딩을 파일에서 통째로 빠뜨립니다. 그래서 테이블을 기준으로 두고,
     * `Count` 와의 `static_assert` 가 빠진 줄을 컴파일 오류로 만듭니다.
     *
     * [왜 리플렉션을 쓰지 않는가]
     * 같은 폴더의 `KeyCodeUtil` 과 `MouseButtonUtil` 은 리플렉션 레지스트리로 이름을 얻지만, 여기서는 쓰지 않습니다.
     * 첫째, XML 에 적히는 이름("single", "axis1d")은 파일 포맷이라 열거자 이름과 일부러 다릅니다.
     * 둘째, 레지스트리는 엔진 서비스가 바인딩되어 있어야 답합니다. 바인딩되지 않은 채 저장하면
     * `KeyCodeUtil::toName` 이 "Unknown" 을 반환하듯 종류 이름도 "Unknown" 이 되어 저장이 조용히 망가집니다.
     * 저장 경로가 서비스 바인딩에 기대야 할 이유는 없고, 아홉 줄짜리 테이블이라 직접 두는 비용도 작습니다.
     */
    struct SW_API BindingKinds
    {
        /** @brief XML `kind` 특성에 적는 이름입니다. 저장과 로드가 이 이름 하나를 함께 씁니다. */
        static const utf8* toName( BindingKind kind );
        /** @brief 이름으로 종류를 찾습니다. 모르는 이름이면 `BindingKind::Count` 를 반환합니다. */
        static BindingKind fromName( string_view name );
        /**
         * @brief 이 종류에서 키 충돌 검사가 훑어야 할 슬롯 수입니다.
         * @details "쓰는 슬롯 수"가 아니다. 스틱, 마우스 델타, AnyKey 는 슬롯을 쓰더라도 특정 키를 점유하지 않으므로
         *          충돌 대상이 아니라서 0 이다. 이름을 `getSlotCount` 로 줄이면 이 차이를 모르는 사람이 슬롯 순회에 쓰게 된다.
         */
        static uint32 getConflictSlotCount( BindingKind kind );

        /** @brief 키 하나로 바꿀 수 있는 슬롯이 없는 종류에 대해 `getRebindSlotIndex` 가 반환하는 값입니다. */
        static constexpr uint32 kNoRebindSlot = constant::invalid_index::kUint32;
        /**
         * @brief 키 하나를 새로 잡았을 때(`InputMap::rebindKey`, 에디터의 Rebind) 그 키가 들어갈 슬롯을 반환합니다.
         * @details 단일 슬롯과 Shortcut 은 0, Chord 는 방아쇠(1)이고 수식 키는 그대로 둔다.
         *          합성 축, 스틱, 마우스 델타, AnyKey 는 키 하나로 바꿀 수 없어 `kNoRebindSlot` 이다.
         */
        static uint32 getRebindSlotIndex( BindingKind kind );
        /**
         * @brief 이번 프레임의 이동량을 내는 종류(마우스 이동, 휠)인지 묻습니다.
         * @details 이동량은 [-1, 1] 로 묶지 않고, 액션 값 단계의 전역 축 반전(`InputAccessibilitySettings::_bInvertX/Y`)도 걸지 않는다.
         *          휠 두 칸은 2 이고, 시점 반전이 확대나 핫바 방향을 뒤집지도 않는다.
         *          마우스 이동의 반전은 그 바인딩 평가에서 한 번만 적용한다.
         */
        static bool isRelative( BindingKind kind );
        /**
         * @brief 바인딩 하나가 이동량으로 쓰이는지 묻습니다. 종류가 이동량이거나, 스틱에 배율 · 프레임 시간 곱이 걸린 것입니다.
         * @details 스틱 값에 배율(`_axisScale`)이나 프레임 시간 곱을 걸면 [-1, 1] 을 넘는 시점 속도(픽셀/프레임)가 되므로 묶지 않는다.
         *          마우스 이동과 같은 쪽으로 합쳐져 한 `Look` 액션에 둘을 함께 묶을 수 있다.
         */
        static bool isRelative( const ActionBinding& binding );
    };
} // namespace sw

namespace sw
{
    enum class ConflictResolution : uint8
    {
        Swap = 0,    ///< 기존 액션과 새 액션의 키를 서로 맞바꾼다.
        Override,    ///< 기존 액션에서 해당 키를 제거하고 새 액션에만 할당한다.
        AddSecondary ///< 기존 액션은 그대로 두고 새 액션을 보조 바인딩 슬롯에 추가한다.
    };

    ENUM( Invalid = Count, Count = Count )
    enum class ActionTrigger : uint8
    {
        Pressed = 0,
        Released,
        Down,
        DoubleClicked,
        HoldThreshold,
        HoldAndRelease, ///< 임계 시간 이상 누르고 있다가 뗀 순간 발화한다(차지 샷).
        Tap,            ///< 짧게 눌렀다 뗀 순간 발화한다(회피, 대시).
        Pulse,          ///< 누르고 있는 동안 주기적으로 반복 발화한다(연사).
        DoubleTap,      ///< 같은 키를 짧은 시간 안에 두 번 탭하면 발화한다.
        Repeat,         ///< 처음 누를 때 한 번 발화하고, navRepeatDelay 뒤부터 navRepeatRate 간격으로 반복 발화한다(UI 내비게이션).
        MultiTap,       ///< 같은 키를 누름 사이 간격 안에 N 번(`ActionBinding::_tapCount`) 누르면 발화한다(유니티 MultiTap). DoubleTap 은 N = 2 인 경우다.
        /**
         * @brief 누른 뒤 포인터가 끌기 문턱(`InputMap::getDragThreshold`) 안에 머문 채 뗀 순간 발화한다(클릭과 끌기를 가른다 — RTS 선택 상자, 에디터 뷰포트 우클릭 메뉴).
         * @details 포인터가 없는 슬롯(키 · 패드)은 끌 수 없으므로 뗀 순간과 같다. 누름 위치는 그 누름의 마우스 위치다.
         */
        Click,
        /** @brief 누른 채 포인터가 끌기 문턱을 처음 넘은 프레임에 한 번 발화한다(끌기 시작). 끄는 동안은 `isActionDown` 으로 읽는다. 포인터가 없는 슬롯은 발화하지 않는다. */
        Drag,
        Count
    };

    enum class ActionPhase : uint8
    {
        None = 0,
        Started,   ///< 입력을 처음 감지했다(0.0 에서 0 이 아닌 값으로, 또는 Down 시작).
        Ongoing,   ///< 입력이 이어지며 트리거를 기다린다(Hold 진행 중).
        Triggered, ///< 트리거 조건을 만족한 순간 발화한다(Performed).
        Completed, ///< 트리거가 발화한 뒤 입력이 정상적으로 해제됐다.
        Canceled   ///< 트리거 조건을 채우기 전에 입력이 해제됐다(예: Hold 시간 미달).
    };

    ENUM()
    enum class GamepadStick : uint8
    {
        Left  = 0,
        Right = 1
    };

    /**
     * @brief 스틱 바인딩이 내는 성분입니다(`<stick axis="Y">`). `Both` 는 2D 값이고, `X` · `Y` 는 그 성분 하나를 1D 축 값으로 냅니다.
     * @details 언리얼의 `Gamepad_RightY` 키, 유니티 Input System 의 `<Gamepad>/rightStick/y` 경로에 해당한다. 확대처럼 1D 액션이 스틱 한 축을 읽을 때 쓴다.
     *          `scaleByDelta` 와 함께 쓰면 값이 초당 양(확대 칸/초)이 된다 — 칸 단위 액션에 스틱 값을 매 프레임 그대로 넣으면 너무 빠르다.
     */
    ENUM()
    enum class GamepadStickAxis : uint8
    {
        Both = 0, ///< 2D 값(x, y)
        X,        ///< 가로 성분을 1D 값으로
        Y,        ///< 세로 성분(위가 +)을 1D 값으로
    };

    namespace InputMapDefaults
    {
        inline constexpr utf8 kDefaultLayerName[]    = "Gameplay";
        inline constexpr utf8 kTitleLayerName[]      = "Title";
        inline constexpr utf8 kReloadShadersAction[] = "ReloadShaders";
        inline constexpr utf8 kReloadGameAction[]    = "ReloadGame";
        // 개발 콘솔 디버그 키 맵(`default.input.xml`)의 기본값이다. 여는 키는 늘 켜진 Debug 레이어에 두고, 나머지 키는 콘솔이 열려 있는 동안만 켜지는 DevConsole 레이어에 둔다.
        inline constexpr utf8    kDevConsoleLayerName[]            = "DevConsole";
        inline constexpr utf8    kDevConsoleToggleAction[]         = "DevConsoleToggle";
        inline constexpr utf8    kDevConsoleCloseAction[]          = "DevConsoleClose";
        inline constexpr utf8    kDevConsoleSubmitAction[]         = "DevConsoleSubmit";
        inline constexpr utf8    kDevConsoleCompleteAction[]       = "DevConsoleComplete";
        inline constexpr utf8    kDevConsoleHistoryBackAction[]    = "DevConsoleHistoryBack";
        inline constexpr utf8    kDevConsoleHistoryForwardAction[] = "DevConsoleHistoryForward";
        inline constexpr utf8    kDevConsoleDeleteBackwardAction[] = "DevConsoleDeleteBackward";
        inline constexpr float32 kDoubleClickTime                  = 0.35f;
        inline constexpr float32 kDoubleClickMaxDistance           = 6.0f;
        inline constexpr float32 kDoubleTapTime                    = 0.22f;
        inline constexpr float32 kHoldThreshold                    = 0.4f;
        inline constexpr float32 kTapMaxTime                       = 0.2f;
        inline constexpr float32 kPulseInterval                    = 0.1f;
        inline constexpr float32 kDoubleClickTimeMin               = 0.05f;
        inline constexpr float32 kDoubleClickTimeMax               = 2.0f;
        inline constexpr float32 kDoubleClickDistanceMax           = 64.0f;
        inline constexpr float32 kDragThreshold                    = 6.0f;  ///< 클릭과 끌기를 가르는 포인터 이동 거리(픽셀). ImGui `io.MouseDragThreshold` 의 기본값과 같다
        inline constexpr float32 kDragThresholdMax                 = 64.0f; ///< `dragThreshold` 의 위 한계(픽셀)
        inline constexpr float32 kHoldThresholdMin                 = 0.05f;
        inline constexpr float32 kHoldThresholdMax                 = 10.0f;
        inline constexpr float32 kNeverPressedSentinel             = 1.0e9f;
        inline constexpr float32 kStickDeadzone                    = 0.15f; ///< 입력 맵 XML 의 스틱 바인딩이 `deadzone` 을 적지 않았을 때(패드 스틱의 흔한 중심 흔들림)
    } // namespace InputMapDefaults

    /// @brief XML 레이어 정의입니다(우선순위, enabled, blockLower, alwaysOn).
    struct LayerDefinition
    {
        hashed_string          _name;
        int32                  _priority;
        uint8                  _bEnabled    : 1;
        uint8                  _bBlockLower : 1; /**< 켜져 있으면 우선순위가 낮거나 스택 아래에 있는 레이어를 비활성화합니다. */
        uint8                  _bAlwaysOn   : 1; /**< 스택이나 enableOnlyLayer 에 영향받지 않고 항상 활성입니다. */
        [[maybe_unused]] uint8 _reserved    : 5;

        LayerDefinition()
            : _name{}
            , _priority{ 0 }
            , _bEnabled{ SW_TRUE }
            , _bBlockLower{ SW_FALSE }
            , _bAlwaysOn{ SW_FALSE }
            , _reserved{ 0 } {}
    };
} // namespace sw

namespace sw
{
    /**
     * @struct ActionHandle
     * @brief 해시맵을 거치지 않고 O(1) 로 액션 상태를 읽는 핸들입니다.
     */
    struct ActionHandle
    {
        static constexpr uint32 kInvalidIndex = constant::invalid_index::kUint32;

        uint32 _index{ kInvalidIndex };
        uint32 _generation{ 0 };

        constexpr bool isValid() const { return _index != kInvalidIndex; }
    };
} // namespace sw

namespace sw
{
    /**
     * @struct ActionBinding
     * @brief 단일 슬롯, 합성 축, 스틱, 조합 키, 마우스 룩을 모두 표현하는 통합 바인딩입니다.
     */
    struct ActionBinding
    {
        /** @brief `_cachedLayerIndex` 가 아직 정해지지 않았음을 나타내는 무효값입니다. */
        static constexpr uint32 kInvalidLayerIndex = constant::invalid_index::kUint32;

        hashed_string    _layer{};
        BindingKind      _kind{ BindingKind::SingleSlot };
        ActionTrigger    _trigger{ ActionTrigger::Pressed };
        uint8            _deviceIndex{ 0 };
        uint8            _modifierMask{ 0 }; /**< ModifierKey::Ctrl | Shift | Alt | Super */
        InputSlot        _arrSlot[4]{};      /**< SingleSlot[0], Axis1D[0=Neg,1=Pos], Vector2D[0=Up,1=Down,2=Left,3=Right], Chord[0=Mod,1=Trig] */
        GamepadStick     _stick{ GamepadStick::Left };
        GamepadStickAxis _stickAxis{ GamepadStickAxis::Both }; /**< GamepadStick2D: 내는 성분. X · Y 면 그 성분이 1D 값(x)이다. */
        float32          _deadzone{ 0.0f };
        float32          _outerDeadzone{ 1.0f };
        float32          _responseExponent{ 1.0f };
        float32          _scale{ 1.0f };           /**< MouseDelta2D · MouseWheel1D: 감도 배율. VirtualJoystick2D: 드래그 반경(px). */
        float2           _axisScale{ 1.0f, 1.0f }; /**< GamepadStick2D: 축마다 배율(언리얼 Enhanced Input 의 Scale). 마우스 Y 는 아래가 +라 스틱을 Look 에 묶으려면 Y 를 음수로 둔다. */
        uint8            _bScaleByDelta : 1;       /**< GamepadStick2D: 프레임 시간을 곱한다(Scale By Delta Time). 배율을 초당 값(픽셀/초)으로 적을 수 있다. */
        /** @brief 바인딩 값의 부호를 뒤집습니다(언리얼 Negate 모디파이어, 유니티 Invert 프로세서). 눌림과 발화는 그대로이고 값만 바뀐다. */
        uint8         _bNegate           : 1;
        mutable uint8 _bJoystickAnchored : 1; /**< VirtualJoystick2D 전용입니다. 드래그 중이라 앵커가 잡혀 있는지 나타냅니다. */
        /**
         * @brief 이 바인딩의 발화 시간(초)입니다. 0 이면 맵 전체 값을 씁니다(`InputMap::getEffectiveTriggerTime`).
         * @details Hold · HoldAndRelease 는 누를 시간, Tap 은 떼야 할 최대 시간, Pulse 는 반복 간격, DoubleTap · DoubleClicked 는 두 누름 사이 최대 간격이다.
         *          언리얼 Enhanced Input 의 트리거마다 시간(Hold Time Threshold · Tap Release Time Threshold · Pulse Interval),
         *          유니티 Input System 의 Interaction 마다 duration 에 해당한다. 같은 맵에서 짧은 차지와 긴 차지를 함께 쓸 수 있다.
         */
        float32 _triggerTime{ 0.0f };
        /** @brief MultiTap 이 발화하는 누름 횟수입니다(2 이상). 다른 트리거는 쓰지 않는다. XML 의 `taps` 속성이며 기본값 2 는 적지 않는다. */
        uint8          _tapCount{ 2 };
        mutable uint32 _cachedLayerIndex{ kInvalidLayerIndex }; /**< `_listLayerEntry` 의 안정된 인덱스입니다. 포인터가 아니라 인덱스라서 재할당되어도 안전합니다. */
        mutable float2 _joystickAnchor{ 0.0f, 0.0f };           /**< VirtualJoystick2D 전용입니다. 드래그를 시작한 순간의 마우스 위치(플로팅 앵커)입니다. */

        ActionBinding()
            : _bScaleByDelta{ SW_FALSE }
            , _bNegate{ SW_FALSE }
            , _bJoystickAnchored{ SW_FALSE }
        {
        }
    };
} // namespace sw

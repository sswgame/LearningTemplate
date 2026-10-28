/**
 * @file InputMap.h
 * @brief 입력 액션을 정의하고 매 프레임 평가하는 통합 InputMap 을 선언합니다.
 * @details `ActionBinding` 하나로 단일 슬롯, 1D/2D 합성 축, 스틱, 조합 키를 모두 표현한다.
 *          액션은 `ActionPhase` 상태 머신(Started, Ongoing, Triggered, Completed, Canceled)으로 평가하고,
 *          모디파이어 파이프라인(데드존, 축 반전, 감도 배율, 바인딩마다 부호 뒤집기)을 거친다. 시간을 쓰는 트리거는 바인딩마다 시간을 둘 수 있다.
 *          그 밖에 LIFO 컨텍스트 스택, 이벤트 델리게이트 디스패치, XML 직렬화를 제공한다. 선입력 버퍼링과 격투 커맨드 판정은 `InputActionBuffer`(`getActionBuffer`)가 맡는다.
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

#include "Engine/Asset/AssetID.h"
#include "Engine/Input/GamepadButtonUtil.h"
#include "Engine/Input/IInputDevice.h"
#include "Engine/Input/KeyCodeUtil.h"
#include "Engine/Input/Map/InputActionBuffer.h"

namespace sw
{
    enum class InputGlyphStyle : uint8;

    struct ActionBinding;
    struct InputPressInstant;

    class InputManager;
    class XMLNode;

    // ------------------------------------------------------------------------------
    // 1) 스키마: 액션 값 타입, 바인딩 종류, 트리거, 페이즈, 데드존 모드
    // ------------------------------------------------------------------------------
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
        static constexpr uint32 kNoRebindSlot = invalid_index::kUint32;
        /**
         * @brief 키 하나를 새로 잡았을 때(`InputMap::rebindKey`, 에디터의 Rebind) 그 키가 들어갈 슬롯을 반환합니다.
         * @details 단일 슬롯과 Shortcut 은 0, Chord 는 방아쇠(1)이고 수식 키는 그대로 둔다.
         *          합성 축, 스틱, 마우스 델타, AnyKey 는 키 하나로 바꿀 수 없어 `kNoRebindSlot` 이다.
         */
        static uint32 getRebindSlotIndex( BindingKind kind );
        /**
         * @brief 이번 프레임의 이동량을 내는 종류(마우스 이동, 휠)인지 묻습니다.
         * @details 이동량은 [-1, 1] 로 묶지 않고, 액션 값 단계의 전역 축 반전(`InputPlayerSettings::_bInvertX/Y`)도 걸지 않는다.
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
    enum class GamepadStickAxis : uint8
    {
        Both = 0, ///< 2D 값(x, y)
        X,        ///< 가로 성분을 1D 값으로
        Y,        ///< 세로 성분(위가 +)을 1D 값으로
    };

    enum class DeadzoneShape : uint8
    {
        Radial = 0, ///< 360도 원형 거리 기반 데드존(자유 이동).
        Axial       ///< X/Y 독립 십자형 데드존(4방향 그리드, 플랫포머).
    };

    namespace InputMapDefaults
    {
        inline constexpr utf8 kDefaultLayerName[]    = "Gameplay";
        inline constexpr utf8 kTitleLayerName[]      = "Title";
        inline constexpr utf8 kReloadShadersAction[] = "ReloadShaders";
        inline constexpr utf8 kReloadGameAction[]    = "ReloadGame";
        // 개발 콘솔 셸 맵(`default.input.xml`)의 기본값이다. 여는 키는 늘 켜진 Debug 레이어에 두고, 나머지 키는 콘솔이 열려 있는 동안만 켜지는 DevConsole 레이어에 둔다.
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
    enum class DigitalNormalization : uint8
    {
        Circular = 0,   ///< 대각선 입력의 반경을 1.0 으로 정규화한다(표준 3D/FPS 액션).
        IndependentAxes ///< X/Y 를 각각 독립적으로 1.0 까지 유지한다(2D 그리드, 탑다운).
    };

    /**
     * @struct ActionHandle
     * @brief 해시맵을 거치지 않고 O(1) 로 액션 상태를 읽는 핸들입니다.
     */
    struct ActionHandle
    {
        static constexpr uint32 kInvalidIndex = invalid_index::kUint32;

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
        static constexpr uint32 kInvalidLayerIndex = invalid_index::kUint32;

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
        bool             _bScaleByDelta{ false };  /**< GamepadStick2D: 프레임 시간을 곱한다(Scale By Delta Time). 배율을 초당 값(픽셀/초)으로 적을 수 있다. */
        /**
         * @brief 이 바인딩의 발화 시간(초)입니다. 0 이면 맵 전체 값을 씁니다(`InputMap::getEffectiveTriggerTime`).
         * @details Hold · HoldAndRelease 는 누를 시간, Tap 은 떼야 할 최대 시간, Pulse 는 반복 간격, DoubleTap · DoubleClicked 는 두 누름 사이 최대 간격이다.
         *          언리얼 Enhanced Input 의 트리거마다 시간(Hold Time Threshold · Tap Release Time Threshold · Pulse Interval),
         *          유니티 Input System 의 Interaction 마다 duration 에 해당한다. 같은 맵에서 짧은 차지와 긴 차지를 함께 쓸 수 있다.
         */
        float32 _triggerTime{ 0.0f };
        /** @brief 바인딩 값의 부호를 뒤집습니다(언리얼 Negate 모디파이어, 유니티 Invert 프로세서). 눌림과 발화는 그대로이고 값만 바뀐다. */
        bool _bNegate{ false };
        /** @brief MultiTap 이 발화하는 누름 횟수입니다(2 이상). 다른 트리거는 쓰지 않는다. XML 의 `taps` 속성이며 기본값 2 는 적지 않는다. */
        uint8          _tapCount{ 2 };
        mutable uint32 _cachedLayerIndex{ kInvalidLayerIndex }; /**< `_listLayerEntry` 의 안정된 인덱스입니다. 포인터가 아니라 인덱스라서 재할당되어도 안전합니다. */
        mutable float2 _joystickAnchor{ 0.0f, 0.0f };           /**< VirtualJoystick2D 전용입니다. 드래그를 시작한 순간의 마우스 위치(플로팅 앵커)입니다. */
        mutable bool   _bJoystickAnchored{ false };             /**< VirtualJoystick2D 전용입니다. 드래그 중이라 앵커가 잡혀 있는지 나타냅니다. */

        ActionBinding() = default;
    };
} // namespace sw

namespace sw
{
    /**
     * @struct DebugActionState
     * @brief 디버깅과 온스크린 뷰어에 쓰는 실시간 액션 평가 상태입니다.
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
} // namespace sw

namespace sw
{
    /**
     * @struct InputPlayerSettings
     * @brief 플레이어마다의 입력 설정입니다 — 감도, 축 반전, 스틱 데드존, 대각선 정규화. 맵 정의(바인딩 · 레이어)와 수명이 달라 맵을 다시 읽어도 남습니다.
     * @details 사용자 설정 처리기(`UserSettingsEngineHandlers`)가 채운다. 언리얼 `UEnhancedInputUserSettings` 의 플레이어 설정 자리입니다.
     */
    struct InputPlayerSettings
    {
        float2               _mouseSensitivity{ 1.0f, 1.0f };
        float2               _gamepadSensitivity{ 1.0f, 1.0f };
        float32              _stickDeadzoneOverride{ -1.0f }; ///< 게임패드 스틱 바인딩의 안쪽 데드존을 덮어쓴다. 0 미만이면 바인딩마다의 값(`ActionBinding::_deadzone`)
        DeadzoneShape        _deadzoneShape{ DeadzoneShape::Radial };
        DigitalNormalization _digitalNormalization{ DigitalNormalization::Circular };
        bool                 _bInvertX{ false };
        bool                 _bInvertY{ false };
    };
} // namespace sw

namespace sw
{
    /**
     * @class InputMap
     * @brief 액션, 레이어, 바인딩을 보관하고 매 프레임 평가하는 통합 액션 맵입니다.
     */
    class SW_API InputMap
    {
    public:
        using LayerDefinition          = sw::LayerDefinition;
        using ActionCallbackDelegate   = Delegate<void()>;
        using PhaseCallbackDelegate    = Delegate<void( ActionPhase )>;
        using Vector2DCallbackDelegate = Delegate<void( float2 )>;

        InputMap();

        // ------------------------------------------------------------------------------
        // 2) 로드와 수명 주기
        // ------------------------------------------------------------------------------
        /** @brief 액션, 레이어, 바인딩을 모두 비웁니다. */
        void clear();
        /** @brief 리소스 상대 경로에서 InputMap XML 을 로드합니다. */
        [[nodiscard]] bool loadFromAsset( const AssetID& assetID );
        /**
         * @brief 이미 읽은 맵에 다른 InputMap XML 의 레이어와 액션을 더합니다(확장 모듈 · 게임 모드가 기본 맵에 액션을 보탠다).
         * @details 더하는 파일은 맵 전체 값(`defaultLayer` · 시간 · 거리 · `virtualCursor`)을 적지 않는다 — 그것은 기본 맵의 것이다.
         *          이미 있는 액션 이름, 이미 있는 레이어를 `<layers>` 에 다시 정의한 것, 같은 레이어에서 같은 발화 규칙의 같은 입력(키 · 수정 키 · 단추)을
         *          다른 액션이 이미 쓰는 바인딩은 오류다. 실패하면 맵이 일부만 더해진 채이므로 부른 쪽은 오류로 멈춘다.
         * @return 모두 더했으면 true 입니다.
         */
        [[nodiscard]] bool appendFromAsset( const AssetID& assetID );
        /**
         * @brief 레이어, 액션, 현재 바인딩을 `loadFromAsset` 가 읽는 `<InputMap>` 정의로 씁니다(에디터 InputMap 패널의 저장).
         * @param relativePath 리소스 id 또는 절대 경로입니다. 쓸 위치는 `AssetPaths::getWritePath` 가 정하며, 작업 폴더 기준이 아닙니다.
         * @return 파일 형식으로 표현할 수 없는 바인딩(가상 조이스틱, 아무 키)이 있거나 쓰지 못하면 false 이며, 이때 파일은 건드리지 않습니다.
         * @details 플레이어의 키 리매핑은 여기에 저장하지 않는다. UserSettings 의 `keyBinding` 설정(사용자 파일)에 저장한다.
         */
        [[nodiscard]] bool saveToAsset( string_view relativePath ) const;
        /** @brief 한 프레임의 입력 상태를 평가하고 델리게이트를 디스패치합니다. */
        void update( float32 deltaSeconds );

        // ------------------------------------------------------------------------------
        // 3) 통합 바인딩 API: InputSlot, 키, 패드, 마우스, 1D 합성, 2D 합성, 스틱, 조합 키
        // ------------------------------------------------------------------------------
        /** @brief 바인딩 없이 액션 이름과 값 타입만 등록합니다. 에디터에서 이름만 먼저 만들고 바인딩은 나중에 추가할 때 씁니다. */
        void createAction( const hashed_string& action, InputActionValueType valueType = InputActionValueType::Boolean );
        void bind( const hashed_string& action, InputSlot slot, ActionTrigger trigger = ActionTrigger::Pressed, const hashed_string& layer = {} );
        void bind( const hashed_string& action, Key key, ActionTrigger trigger = ActionTrigger::Pressed, const hashed_string& layer = {} );
        void bind( const hashed_string& action, GamepadButton button, ActionTrigger trigger = ActionTrigger::Pressed, const hashed_string& layer = {} );
        void bind( const hashed_string& action, MouseButton mouse, ActionTrigger trigger = ActionTrigger::Pressed, const hashed_string& layer = {} );

        /**
         * @brief 1D 축 합성 바인딩입니다(negativeKey: -1.0, positiveKey: +1.0).
         * @param trigger 액션 발화(`wasActionTriggered`) 규칙입니다. 축 값(`getAxis1D`)은 trigger 와 관계없이 누르는 동안 읽힙니다.
         *                축을 매 프레임 읽을 때는 `Down`, 누를 때마다 한 칸씩 넘길 때(무기 교체)는 `Pressed` 를 씁니다.
         */
        void bindAxis1DComposite( const hashed_string& action, Key negativeKey, Key positiveKey, const hashed_string& layer = {},
                                  ActionTrigger trigger = ActionTrigger::Down );
        /**
         * @brief 위와 같지만 슬롯 둘을 받습니다. 게임패드 버튼 둘(D 패드 위, 아래로 확대)처럼 키가 아닌 장치도 한 축으로 묶을 수 있습니다.
         * @details 두 슬롯이 같은 장치여야 저장된다(`<axis1d source="gamepad">`). 언리얼 Enhanced Input 의 Axis1D 액션에 버튼 둘(한쪽 Negate)을 매핑하는 것에 해당한다.
         */
        void    bindAxis1DComposite( const hashed_string& action, InputSlot negativeSlot, InputSlot positiveSlot, const hashed_string& layer = {},
                                     ActionTrigger trigger = ActionTrigger::Down );
        float32 getAxis1D( const hashed_string& action ) const;

        /** @brief 2D 이동 축 바인딩을 등록합니다(키보드 4방향 합성). */
        void bindVector2D( const hashed_string& action, Key up, Key down, Key left, Key right, float32 deadzone = 0.0f, const hashed_string& layer = {} );
        /** @brief 게임패드 아날로그 스틱 2D 이동 축 바인딩을 등록합니다(데드존, 바깥 데드존, 응답 곡선 가속). */
        /**
         * @param axisScale 축마다 배율입니다. 1 이 아니거나 @p bScaleByDelta 면 값이 [-1, 1] 을 넘는 이동량이 되어 마우스 이동(`bindMouseDelta`)과 같은 액션에 합쳐집니다.
         * @param bScaleByDelta 켜면 값에 프레임 시간(초)을 곱합니다. 오른쪽 스틱을 `Look` 에 묶을 때 @p axisScale 을 초당 픽셀로 적으면 프레임률과 무관한 속도가 됩니다.
         * @param axis `Both` 가 아니면 그 성분 하나를 1D 축 값으로 냅니다(액션 값 형식이 Axis1D). 배율은 그 성분의 것(`axisScale` 의 x 또는 y)입니다.
         */
        void bindGamepadStick2D( const hashed_string& action, GamepadStick stick = GamepadStick::Left, float32 deadzone = InputMapDefaults::kStickDeadzone, const hashed_string& layer = {}, uint8 padIndex = 0, float32 outerDeadzone = 1.0f, float32 responseExponent = 1.0f,
                                 float2 axisScale = float2{ 1.0f, 1.0f }, bool bScaleByDelta = false, GamepadStickAxis axis = GamepadStickAxis::Both );
        void bindMouseDelta( const hashed_string& action, float32 sensitivity = 1.0f, const hashed_string& layer = {} );
        /** @brief 마우스 휠을 1D 축으로 등록합니다. 값은 이번 프레임 휠 이동량(한 칸 = 1, 위가 +)에 @p scale 을 곱한 값이며, 휠을 굴린 프레임에만 0 이 아닙니다. */
        void bindMouseWheel( const hashed_string& action, float32 scale = 1.0f, const hashed_string& layer = {} );
        /** @brief 마우스 드래그로 움직이는 가상 조이스틱 2D 축 바인딩을 등록합니다(앵커는 activationButton 을 누른 지점에 놓입니다). */
        void   bindVirtualJoystick2D( const hashed_string& action, MouseButton activationButton = MouseButton::Left, float32 radius = 64.0f, float32 deadzone = 0.1f, const hashed_string& layer = {}, float32 outerDeadzone = 1.0f );
        float2 getVector2D( const hashed_string& action ) const;

        /** @brief 조합 키(Chord) 바인딩을 등록합니다(예: Ctrl + F7, Shift + W). */
        void bindChord( const hashed_string& action, Key modifierKey, Key triggerKey, ActionTrigger trigger = ActionTrigger::Pressed, const hashed_string& layer = {} );
        /**
         * @brief 수정 키 마스크 + 키 바인딩을 등록합니다(예: Ctrl + Shift + S).
         * @details 수정 키는 **정확히** 그 조합이어야 한다 — 마스크에 없는 수정 키(Ctrl · Shift · Alt · Super)가 눌려 있으면 발화하지 않는다.
         *          요구한 키만 보면 Ctrl+Shift+Z 가 Ctrl+Z 도 함께 발화시킨다. 방아쇠 키 자신이 수정 키면 그 수정 키는 비교에서 뺀다.
         */
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

        // ------------------------------------------------------------------------------
        // 4) LIFO 컨텍스트 스택, 모달/비모달 제어
        // ------------------------------------------------------------------------------
        void registerLayer( const hashed_string& name, int32 priority = 0, bool enabled = true, bool blockLower = false, bool alwaysOn = false );
        void setLayerEnabled( const hashed_string& layer, bool enabled );
        /** @brief LIFO 레이어 스택 맨 위에 레이어를 넣습니다(blockLower=false 는 비모달 동시 조작, true 는 모달 차단). */
        void pushLayer( const hashed_string& layer, bool blockLower = false, bool showCursor = false );
        /** @brief LIFO 레이어 스택 맨 위의 레이어를 뺍니다. */
        void popLayer();
        /** @brief LIFO 레이어 스택에서 특정 레이어를 찾아 뺍니다. */
        void popLayer( const hashed_string& layer );
        /** @brief 지금 LIFO 스택 맨 위 레이어 이름을 반환합니다. */
        string_view getCurrentTopLayer() const;
        void        enableOnlyLayer( const hashed_string& layer );

        // ------------------------------------------------------------------------------
        // 5) 감도, 축 반전, 데드존 모드, 접근성 설정
        // ------------------------------------------------------------------------------
        /** @brief 플레이어 설정(감도 · 축 반전 · 데드존 · 대각선 정규화)입니다. `clear` 나 맵 다시 읽기를 해도 남습니다. */
        InputPlayerSettings&       getPlayerSettings() { return _playerSettings; }
        const InputPlayerSettings& getPlayerSettings() const { return _playerSettings; }

        /** @brief 액션의 토글 모드를 켜거나 끕니다(접근성: 조준이나 달리기를 한 번 눌러 켜기). */
        void setToggleMode( const hashed_string& action, bool bToggle );
        bool isActionToggled( const hashed_string& action ) const;

        void setSuppressBaseActionOnChord( bool bSuppress ) { _bSuppressBaseActionOnChord = bSuppress ? SW_TRUE : SW_FALSE; }
        bool isSuppressBaseActionOnChord() const { return _bSuppressBaseActionOnChord == SW_TRUE; }

        void setNavRepeatDelay( float32 delay ) { _navRepeatDelay = delay; }
        void setNavRepeatRate( float32 rate ) { _navRepeatRate = rate; }

        // ------------------------------------------------------------------------------
        // 6) 액션 선입력 버퍼링, 격투 커맨드 판정 — 트리거된 액션은 `update` 가 이력에 남긴다
        // ------------------------------------------------------------------------------
        InputActionBuffer&       getActionBuffer() { return _actionBuffer; }
        const InputActionBuffer& getActionBuffer() const { return _actionBuffer; }

        // ------------------------------------------------------------------------------
        // 7) UI 글리프 조회, 게임 안 키 리매핑 직렬화
        // ------------------------------------------------------------------------------
        string getGlyphForAction( const hashed_string& action ) const;
        string getGlyphForAction( const hashed_string& action, InputGlyphStyle previewDevice ) const;
        /** @brief 슬롯 하나의 표기(`[ Space ]`, PlayStation 이면 `[ Circle ]`)를 반환합니다. 리바인딩 화면이 아직 적용하지 않은 키를 보일 때 씁니다. */
        static string getGlyphForSlot( const InputSlot& slot, InputGlyphStyle device );

        /**
         * @brief 런타임에 액션의 키 바인딩을 바꿉니다. 바인딩 종류는 그대로이며, Chord 는 방아쇠만 바뀌고 수식 키는 남습니다.
         * @details 키 하나로 바꿀 수 없는 종류(합성 축, 스틱 등)면 바꾸지 않고 경고를 남긴 뒤 false 를 반환한다(`BindingKinds::getRebindSlotIndex`).
         *          단일 키로 바꿔 버리면 에디터에서 `Ctrl+S` 를 다시 잡을 때 수식 키가, A/D 축을 다시 잡을 때 축이 사라지기 때문이다.
         *          가상 커서가 마우스로 바꾸는 패드 버튼으로 바꾸려 해도 거절한다(맵 읽기와 같은 검사, `reportVirtualCursorConflict`).
         */
        [[nodiscard]] bool rebindKey( const hashed_string& action, Key newKey, uint32 bindIndex = 0 );
        [[nodiscard]] bool rebindSlot( const hashed_string& action, InputSlot slot, uint32 bindIndex = 0 );
        [[nodiscard]] bool rebindWithResolution( const hashed_string& action, InputSlot newSlot, ConflictResolution strategy = ConflictResolution::Swap, uint32 bindIndex = 0 );
        /**
         * @brief 키보드 바인딩 @p bindIndex 를 키 + 수정 키 마스크로 바꿉니다(단축키 재배치 — 수정 키도 바꾼다). 마스크가 0 이면 단일 키, 아니면 단축키가 됩니다.
         * @details 키보드의 단일 키 · 단축키 바인딩만 바꾼다. `rebindSlot` 은 방아쇠 키만 바꿔 Ctrl+D 를 Ctrl+Shift+D 로 옮길 수 없다.
         * @return 액션 · 바인딩이 없거나, 키보드 단일 키 · 단축키가 아니거나, 키가 없거나 모르는 수정 키 비트면 false 입니다.
         */
        [[nodiscard]] bool rebindKeyShortcut( const hashed_string& action, uint32 bindIndex, Key key, uint8 modifierMask );
        /** @brief 처음 등록한 기본 바인딩(`resetActionToDefault` 가 되돌리는 대상)입니다. 없으면 nullptr 입니다. */
        const ActionBinding* getDefaultBinding( const hashed_string& action, uint32 bindIndex ) const;
        /**
         * @brief 바인딩 @p bindIndex 와 같은 레이어 · 같은 발화 규칙 · 같은 입력(키 · 수정 키 · 단추)을 다른 액션이 쓰면 그 이름을 @p outOtherAction 에 담고 true 입니다.
         * @details 한 번 눌러 둘이 발화하는 겹침이다. 맵 더하기(`appendFromAsset`)와 재배치 창이 같은 검사를 쓴다.
         */
        [[nodiscard]] bool findSameLayerInputConflict( const hashed_string& action, uint32 bindIndex, hashed_string& outOtherAction ) const;
        /** @brief `modifier` 속성 글자(`Ctrl`, `Ctrl+Shift` …)를 수정 키 마스크로 읽습니다. 모르는 이름 · 빈 글 · 같은 이름 두 번이면 false 입니다. */
        [[nodiscard]] static bool tryParseModifierNames( string_view text, uint8& outModifierMask );
        /** @brief 수정 키 마스크를 `modifier` 속성 글자로 씁니다(`Ctrl+Shift`). 0 이면 빈 글입니다. */
        static string formatModifierNames( uint8 modifierMask );
        /**
         * @brief 바인딩 @p bindIndex 가 리바인딩으로 바꾸는 슬롯(`BindingKinds::getRebindSlotIndex`)의 현재 키를 반환합니다.
         * @return 액션이나 바인딩이 없거나 키 하나로 바꿀 수 없는 종류면 false 입니다.
         */
        [[nodiscard]] bool findRebindSlot( const hashed_string& action, uint32 bindIndex, InputSlot& outSlot ) const;
        /** @brief 위와 같지만 처음 등록한 기본 바인딩(`resetActionToDefault` 가 되돌리는 대상)의 키를 반환합니다. */
        [[nodiscard]] bool findDefaultRebindSlot( const hashed_string& action, uint32 bindIndex, InputSlot& outSlot ) const;
        /** @brief 바인딩 @p bindIndex 가 속한 레이어 이름을 반환합니다. 없으면 빈 이름입니다. */
        hashed_string findBindingLayer( const hashed_string& action, uint32 bindIndex ) const;
        /** @brief 같은 레이어 안에서 다른 액션이 이미 그 슬롯을 점유하고 있는지 검사합니다. */
        bool hasBindingConflict( const InputSlot& slot, const hashed_string& layer, string& outConflictingAction ) const;
        /** @brief 지정 액션의 바인딩을 처음 등록한 기본 바인딩으로 되돌립니다. */
        bool resetActionToDefault( const hashed_string& action );
        /** @brief 모든 액션의 바인딩을 기본값으로 한꺼번에 되돌립니다. */
        void resetAllBindingsToDefault();
        /** @brief 유저 키 바인딩을 XML 파일로 저장합니다. */
        [[nodiscard]] bool saveUserBindings( string_view filePath ) const;
        /**
         * @brief 저장된 유저 키 바인딩 XML 파일을 로드해 적용합니다.
         * @return 가상 커서가 마우스로 바꾸는 스틱 · 패드 버튼을 다시 묶은 바인딩이 있으면 그 바인딩은 넣지 않고 false 입니다(맵 읽기와 같은 거절).
         */
        [[nodiscard]] bool loadUserBindings( string_view filePath );

        // ------------------------------------------------------------------------------
        // 8) 입력 장치 연결, 임계값, 디버그
        // ------------------------------------------------------------------------------
        void setInputManager( InputManager* pInput ) { _pInput = pInput; }
        /**
         * @brief true 면 이 맵은 키보드 포커스(`InputManager::setKeyboardFocus`)와 상관없이 키를 읽습니다. 기본은 false 이며 게임 쪽 맵이 이 값을 씁니다.
         * @details 셸 맵(개발 콘솔, 리로드 단축키)만 켠다. 콘솔이 키보드를 가진 동안에도 닫는 키와 편집 키를 받아야 하기 때문이다.
         *          패드도 같다 — 콘솔이 열린 동안 게임 맵은 패드 바인딩을 "안 눌림" 으로 보고(`InputManager::isGamepadControlVisibleToGame`), 이 맵은 장치 상태를 그대로 읽는다.
         *          단, 키보드 포커스가 도구(`InputKeyboardFocus::Tool`)인 동안은 이 맵도 키를 보지 않는다 — 에디터 패널에 친 글자가 셸 · UI 맵의 키(콘솔 여는 `~`, UI 확인 Enter)가 되지 않게.
         *          도구 자신의 맵만 `setToolKeyboardReader( true )` 로 그 키를 읽는다.
         */
        void               setKeyboardFocusIgnored( bool bIgnored ) { _bKeyboardFocusIgnored = bIgnored ? SW_TRUE : SW_FALSE; }
        bool               isKeyboardFocusIgnored() const { return _bKeyboardFocusIgnored == SW_TRUE; }
        [[nodiscard]] bool setDoubleClickTime( float32 seconds );
        [[nodiscard]] bool setDoubleClickMaxDistance( float32 pixels );
        /** @brief 클릭과 끌기를 가르는 포인터 이동 거리(픽셀)입니다. `Click` · `Drag` 트리거가 씁니다. [0, `kDragThresholdMax`] 밖이면 거절합니다. */
        [[nodiscard]] bool setDragThreshold( float32 pixels );
        /** @brief true 면 키보드 포커스가 도구(`InputKeyboardFocus::Tool`)인 동안에도 키를 읽습니다(에디터 입력 맵). */
        void               setToolKeyboardReader( bool bReads ) { _bReadsToolKeyboard = bReads ? SW_TRUE : SW_FALSE; }
        bool               isToolKeyboardReader() const { return _bReadsToolKeyboard == SW_TRUE; }
        void               setDoubleTapTime( float32 seconds ) { _doubleTapTime = seconds; }
        [[nodiscard]] bool setHoldThreshold( float32 seconds );
        void               setNavRepeatTiming( float32 delaySeconds, float32 rateSeconds )
        {
            _navRepeatDelay = delaySeconds;
            _navRepeatRate  = rateSeconds;
        }
        /** @brief 이 맵이 켜는 가상 커서입니다. `InputManager` 가 매 프레임 읽어 켜고 끕니다. */
        InputMapVirtualCursor getVirtualCursorStick() const { return _virtualCursorStick; }
        void                  setVirtualCursorStick( InputMapVirtualCursor stick ) { _virtualCursorStick = stick; }
        InputManager*         getInputManager() const { return _pInput; }
        float32               getDoubleClickTime() const { return _doubleClickTime; }
        float32               getDragThreshold() const { return _dragThreshold; }
        float32               getDoubleClickMaxDistance() const { return _doubleClickMaxDistance; }
        float32               getDoubleTapTime() const { return _doubleTapTime; }
        float32               getHoldThreshold() const { return _holdThreshold; }
        float32               getNavRepeatDelay() const { return _navRepeatDelay; }
        float32               getNavRepeatRate() const { return _navRepeatRate; }

        /** @brief 실시간 액션 평가 상태를 덤프합니다(HUD, 에디터 인스펙터용). */
        void getDebugActionStates( vector<DebugActionState>& outListState ) const;

        // ------------------------------------------------------------------------------
        // 9) 조회와 프레임 상태(통합 쿼리와 O(1) ActionHandle 핫패스)
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
        /** @brief 바인딩 @p bindIndex 의 발화 규칙을 바꿉니다(에디터용). 액션이나 바인딩이 없으면 false 입니다. */
        [[nodiscard]] bool setBindingTrigger( const hashed_string& action, uint32 bindIndex, ActionTrigger trigger );
        /**
         * @brief 바인딩 @p bindIndex 의 발화 시간(`ActionBinding::_triggerTime`)을 바꿉니다. 0 이하면 맵 전체 값으로 돌아갑니다.
         * @details 맵 정의의 일부라서 기본 바인딩(`resetActionToDefault` 가 되돌리는 대상)에도 같이 적는다. 액션이나 바인딩이 없으면 false 입니다.
         */
        [[nodiscard]] bool setBindingTriggerTime( const hashed_string& action, uint32 bindIndex, float32 seconds );
        /** @brief 바인딩 @p bindIndex 의 값 부호 뒤집기(`ActionBinding::_bNegate`)를 바꿉니다. 기본 바인딩에도 같이 적습니다. */
        [[nodiscard]] bool setBindingNegate( const hashed_string& action, uint32 bindIndex, bool bNegate );
        /** @brief 바인딩 @p bindIndex 의 MultiTap 횟수(`ActionBinding::_tapCount`)를 바꿉니다. 2 미만은 2 로 올린다. 기본 바인딩에도 같이 적습니다. */
        [[nodiscard]] bool setBindingTapCount( const hashed_string& action, uint32 bindIndex, uint8 tapCount );
        /** @brief 바인딩이 발화하는 데 필요한 누름 횟수입니다. MultiTap 은 `_tapCount`, DoubleTap · DoubleClicked 는 2, 나머지는 0 입니다. */
        static uint32 getRequiredTapCount( const ActionBinding& binding );
        /** @brief 바인딩의 트리거가 실제로 쓰는 시간(초)입니다. 바인딩 값이 0 이면 트리거별 맵 값입니다. 시간을 쓰지 않는 트리거는 0 입니다. */
        float32              getEffectiveTriggerTime( const ActionBinding& binding ) const;
        uint32               getBindingCount( const hashed_string& action ) const;
        const ActionBinding* getBinding( const hashed_string& action, uint32 bindIndex ) const;

        bool wasActionTriggered( const hashed_string& action ) const;
        bool wasActionTriggered( ActionHandle handle ) const;

        bool isActionDown( const hashed_string& action ) const;
        bool isActionDown( ActionHandle handle ) const;

        bool wasActionPressed( const hashed_string& action ) const;
        bool wasActionPressed( ActionHandle handle ) const;
        /**
         * @brief 이번 프레임에 액션이 눌린 순간입니다(서브틱 입력 — CS2 처럼 클릭한 순간의 시선으로 쏠 때 쓴다). 이번 프레임에 눌리지 않았으면 false 입니다.
         * @details 바인딩마다 그 바인딩이 완성된 순간(코드 · 조합이면 마지막으로 눌린 컨트롤)을 보고, 여러 바인딩이 함께 눌렸으면 가장 이른 것이다.
         *          아날로그 축의 임계처럼 누름 이벤트가 없는 바인딩만 눌렸으면 false 이다 — 그 순간을 모른다(지어내지 않는다).
         */
        [[nodiscard]] bool tryGetActionPressInstant( ActionHandle handle, InputPressInstant& outInstant ) const;

        bool wasActionReleased( const hashed_string& action ) const;
        bool wasActionReleased( ActionHandle handle ) const;

        float32 getActionHoldDuration( const hashed_string& action ) const;
        float32 getActionHoldDuration( ActionHandle handle ) const;

        float2 getVector2D( ActionHandle handle ) const;

        ActionPhase getActionPhase( const hashed_string& action ) const;
        ActionPhase getActionPhase( ActionHandle handle ) const;

        static ActionTrigger actionTriggerFromName( string_view name );
        static const utf8*   actionTriggerToName( ActionTrigger trigger );

    private:
        /** @brief 입력을 읽는 눈입니다. 게임이 보는 값과 장치 값은 개발 콘솔 · 포커스 · 캡처가 가린 키에서 달라진다. */
        enum class SlotRead : uint8
        {
            Game,  ///< 게임이 보는 값. 콘솔이 쥔 키는 "안 눌림" 이다(`isSlotHiddenByConsole`).
            Device ///< 가림과 상관없는 장치 값. 바인딩이 "보이게 되는 순간" 이미 눌려 있었는지 추적하는 데만 쓴다.
        };

        /**
         * @brief 바인딩 하나의 프레임 사이 상태입니다.
         * @details `_bPhysicalDown` 은 레이어가 꺼져 있거나 콘솔이 키를 가려도 매 프레임 장치를 읽어 갱신한다(`_bDown` 은 보이는 입력에서만 눌림이다).
         *          바인딩이 "보이게 되는" 프레임(`_bLive` 가 false 에서 true)에 이미 눌려 있던 입력이 새 누름(Pressed)으로 읽히지 않게 하는 근거다 — `update` 가 `_bWasDown` 을 맞춘다.
         *          레이어가 켜질 때, 콘솔이 닫혀 키가 다시 보일 때, 리바인딩 · 기본값 복원 · 새 바인딩으로 상태가 새로 만들어질 때(`makeBindingState`)가 모두 이 한 규칙이다.
         */
        struct ActionBindingState
        {
            float32                _holdDuration;
            float32                _timeSinceLastPress;
            float32                _pulseTimer;
            int2                   _lastPress; ///< 더블클릭 판정에 쓰는 직전 누름 위치입니다.
            uint8                  _tapStreak; ///< 간격 안에 이어진 누름 수입니다(DoubleTap · MultiTap 판정).
            uint8                  _bDown          : 1;
            uint8                  _bPressed       : 1;
            uint8                  _bReleased      : 1;
            uint8                  _bDoubleClicked : 1;
            uint8                  _bHoldThreshold : 1;
            uint8                  _bWasDown       : 1;
            uint8                  _bTriggered     : 1;
            uint8                  _bPhysicalDown  : 1; ///< 직전 프레임에 레이어 · 콘솔 가림과 상관없이 장치가 눌려 있었는지입니다.
            uint8                  _bLive          : 1; ///< 직전 프레임에 바인딩이 보였는지입니다(레이어가 켜져 있고, 콘솔이 눌린 키를 가리지 않음).
            uint8                  _bDragged       : 1; ///< 이번 누름 동안 포인터가 끌기 문턱을 넘었는지입니다(`Click` · `Drag`). 뗀 다음 프레임에 지운다.
            uint8                  _bDragStarted   : 1; ///< 이번 프레임에 끌기 문턱을 처음 넘었는지입니다(`Drag` 의 발화).
            [[maybe_unused]] uint8 _reserved       : 5;

            ActionBindingState()
                : _holdDuration{ 0.0f }
                , _timeSinceLastPress{ InputMapDefaults::kNeverPressedSentinel }
                , _pulseTimer{ 0.0f }
                , _lastPress{}
                , _tapStreak{ 0 }
                , _bDown{ SW_FALSE }
                , _bPressed{ SW_FALSE }
                , _bReleased{ SW_FALSE }
                , _bDoubleClicked{ SW_FALSE }
                , _bHoldThreshold{ SW_FALSE }
                , _bWasDown{ SW_FALSE }
                , _bTriggered{ SW_FALSE }
                , _bPhysicalDown{ SW_FALSE }
                , _bLive{ SW_FALSE }
                , _bDragged{ SW_FALSE }
                , _bDragStarted{ SW_FALSE }
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
            uint8                            _bDown        : 1;
            uint8                            _bPressed     : 1;
            uint8                            _bReleased    : 1;
            uint8                            _bTriggered   : 1;
            uint8                            _bToggleMode  : 1;
            uint8                            _bToggleState : 1;

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
                , _bTriggered{ SW_FALSE }
                , _bToggleMode{ SW_FALSE }
                , _bToggleState{ SW_FALSE } {}
        };

        const ActionEntry* getActionFromHandle( ActionHandle handle ) const;

        string getGlyphForActionInternal( const hashed_string& action, InputGlyphStyle device ) const;
        bool   evaluateBindingDown( const ActionBinding& binding, float2& outValue, SlotRead read = SlotRead::Game ) const;
        /** @brief 단일 키 바인딩이 수정 키(Ctrl · Alt)가 눌린 동안 물러나야 하는지 봅니다(`_bSuppressBaseActionOnChord`). */
        bool isBaseKeySuppressedByModifier( const ActionBinding& binding, SlotRead read ) const;
        /** @brief 게임패드 스틱 바인딩의 값을 데드존 · 응답 곡선 · 감도를 적용해 읽습니다. 연결이 없으면 false 입니다. */
        bool evaluateGamepadStick( const ActionBinding& binding, float2& outValue ) const;
        /** @brief 가상 조이스틱 바인딩의 값을 읽습니다. 활성화 버튼을 처음 누른 지점이 앵커입니다. */
        bool evaluateVirtualJoystick( const ActionBinding& binding, float2& outValue, SlotRead read ) const;
        /** @brief 눌린 수정 키(Ctrl · Shift · Alt · Super)가 단축키 바인딩의 마스크와 정확히 같은지 봅니다. 방아쇠 키 자신이 수정 키면 그 키는 비교에서 뺍니다. */
        bool areShortcutModifiersExact( const ActionBinding& binding, SlotRead read ) const;
        /** @brief 바인딩의 첫 슬롯이 마우스 단추면 true 입니다(끌기 · 더블클릭 거리를 보는 바인딩). */
        static bool isPointerBinding( const ActionBinding& binding );
        /** @brief 눌린 바인딩의 포인터가 이번 누름 위치(`_lastPress`)에서 끌기 문턱을 넘었는지 갱신합니다(`_bDragged` · `_bDragStarted`). */
        void advanceDrag( const ActionBinding& binding, ActionBindingState& state, const int2& mousePos ) const;
        /**
         * @brief `loadFromAsset` 와 `appendFromAsset` 가 함께 쓰는 본문입니다. @p bAppend 면 맵을 비우지 않고, 맵 전체 값을 받지 않고, 이미 있는 이름을 오류로 봅니다.
         * @details 두 입구가 각자 읽으면 레이어 · 액션 읽기 규칙이 둘로 갈라진다.
         */
        [[nodiscard]] bool loadDefinition( const AssetID& assetID, bool bAppend );
        /** @brief `loadDefinition` 의 맵 전체 값 단계입니다. 시간 문턱 · 가상 커서 · 기본 레이어를 읽고, 더하지 않을 때만 맵을 비우고 적용합니다. 깨졌거나 거절되면 false 입니다. */
        [[nodiscard]] bool readMapSettings( const XMLNode& root, string_view absPath, bool bAppend );
        /** @brief `loadDefinition` 의 레이어 단계입니다. `<layers>` 블록의 레이어를 등록하고 기본 레이어를 보장합니다. 깨진 값은 @p inoutbValid 를 내립니다. */
        void readLayerDefinitions( const XMLNode& root, string_view absPath, bool bAppend, bool& inoutbValid );
        /** @brief `loadDefinition` 의 액션 단계입니다. 최상위 `<layer>` 와 `<action>` 을 읽고 더한 액션 이름을 @p outListLoadedAction 에 모읍니다. */
        void readActionDefinitions( const XMLNode& root, string_view absPath, bool bAppend, vector<hashed_string>& outListLoadedAction, bool& inoutbValid );
        /** @brief 액션 @p action 의 바인딩 중 같은 레이어에서 같은 발화 규칙 · 같은 입력을 다른 액션이 이미 쓰는 것을 알리고 그 수를 반환합니다(`appendFromAsset`). */
        [[nodiscard]] uint32 countSameLayerInputConflicts( string_view sourceName, const hashed_string& action ) const;

        /** @brief 한 프레임 동안 액션 하나의 바인딩들을 합산한 값입니다. */
        struct ActionFrameAccum
        {
            bool    _bAnyDown{ false };
            bool    _bAnyPressed{ false };
            bool    _bAnyReleased{ false };
            bool    _bAnyTriggered{ false };
            bool    _bAnyHoldReached{ false }; ///< 어떤 바인딩이든 자기 홀드 시간을 채웠는지입니다.
            float32 _maxHold{ 0.0f };
            float2  _totalValue{ 0.0f, 0.0f };
            float2  _relativeValue{ 0.0f, 0.0f }; ///< 마우스 이동량입니다. 묶지 않습니다.
        };
        /** @brief 바인딩 하나의 프레임 상태(눌림 · 홀드 · 연타)를 갱신하고 액션 합산에 더합니다. */
        void updateBinding( const ActionBinding& binding, ActionBindingState& state, float32 deltaSeconds, const int2& mousePos, ActionFrameAccum& accum ) const;
        /** @brief 눌림이 시작된 프레임에 연타 횟수를 세어 `_bDoubleClicked` 를 정합니다. */
        void advanceTapStreak( const ActionBinding& binding, ActionBindingState& state, uint32 requiredTaps, float32 doubleWindow, const int2& mousePos ) const;
        /** @brief 이번 프레임 합산으로 ActionPhase 를 전이합니다. */
        static void advanceActionPhase( ActionEntry& actionEntry, bool bPrevDown, bool bAnyHoldReached );
        /** @brief Triggered · Phase · 2D 벡터 델리게이트를 부릅니다. */
        static void dispatchActionCallbacks( const ActionEntry& actionEntry, bool bPrevDown );
        /**
         * @brief 새로 만드는 바인딩 상태입니다. 지금 장치가 눌려 있으면 `_bPhysicalDown` 을 미리 채워, 첫 `update` 에서 Pressed 가 아니라 Held 로 시작하게 합니다.
         * @details 바인딩 상태를 만드는 길(`commitBinding`, 리바인딩, 기본값 복원)은 모두 이 함수 하나를 거친다 — 빈 `ActionBindingState{}` 를 직접 넣는 길이 남으면
         *          이미 눌린 키가 한 번 새 누름으로 읽힌다.
         */
        ActionBindingState makeBindingState( const ActionBinding& binding ) const;
        /** @brief 바인딩 하나의 슬롯이 바뀐 뒤 그 상태를 새로 만듭니다. 다른 바인딩의 상태는 건드리지 않습니다. */
        void reseedBindingState( ActionEntry& entry, uint32 bindIndex ) const;
        /** @brief 액션의 바인딩 상태를 모두 새로 만듭니다(기본값 복원). */
        void rebuildBindingStates( ActionEntry& entry ) const;
        /** @brief 이 맵이 보는 키 하나의 눌림 상태입니다. `isKeyboardFocusIgnored` 면 장치 상태 그대로, 아니면 키보드 포커스를 따릅니다(`SlotRead::Device` 는 늘 장치 상태). */
        bool isKeyboardKeyDown( Key key, SlotRead read = SlotRead::Game ) const;
        bool wasKeyboardKeyPressed( Key key ) const;
        /** @brief 포커스를 무시하는 맵이 지금 키보드를 보지 않아야 하면 true 입니다 — 도구 포커스 동안 도구 맵이 아닌 맵(`setToolKeyboardReader`). */
        bool isToolKeyboardHidden() const;
        /** @brief 슬롯 하나의 눌림입니다. 키보드 슬롯은 `isKeyboardKeyDown` 과 같은 규칙을 따릅니다. */
        bool isSlotDown( const InputSlot& slot, SlotRead read = SlotRead::Game ) const;
        bool wasSlotPressed( const InputSlot& slot, SlotRead read = SlotRead::Game ) const;
        /**
         * @brief 슬롯이 이 프레임에 눌려 있었는지입니다(`isSlotDown` 이거나 `wasSlotPressed`).
         * @details `isSlotDown` 만 보면 같은 프레임 안에서 Down 과 Up 이 모두 처리된 탭(매크로 주입, 초고속 입력, 가상 입력 hold=0)을 놓친다.
         *          그 프레임에는 "눌렸었다" 로 취급한다. 단일 키와 합성(axis1d, vector2d) 모두 같은 규칙이다.
         */
        bool isSlotHeldThisFrame( const InputSlot& slot, SlotRead read = SlotRead::Game ) const;
        /** @brief 개발 콘솔이 입력을 쥐어 이 맵이 슬롯을 "안 눌림" 으로 봐야 하면 true 입니다(키보드와 패드 슬롯). 키보드 포커스를 무시하는 맵(셸 맵, UI 맵)은 늘 false 입니다. */
        bool isSlotHiddenByConsole( const InputSlot& slot ) const;
        /**
         * @brief 바인딩 값을 합쳐 액션 값을 계산합니다.
         * @details 축, 버튼, 스틱 몫(@p bounded)은 축 반전을 적용한 뒤 [-1, 1](또는 원)에 묶는다.
         *          마우스 이동량 몫(@p relative)은 묶지 않고 그대로 더한다. 이동량은 픽셀 단위 상대값이고, 반전은 바인딩 평가에서 이미 적용했기 때문이다.
         */
        float2 composeActionValue( const float2& bounded, const float2& relative ) const;
        bool   evaluateTrigger( const ActionBinding& binding, const ActionBindingState& state, float32 deltaSeconds ) const;
        bool   isBindingLayerActive( const ActionBinding& binding ) const;
        /**
         * @brief 활성 레이어의 코드(chord) 바인딩 중 수식 키가 눌려 있고 방아쇠 키가 조건을 만족하는 것이 있는지 봅니다.
         * @param bTriggerJustPressed true 면 방아쇠 키가 이번 프레임에 눌렸는지(`wasChordTriggered`), false 면 눌려 있는지(`isChordDown`)를 봅니다.
         * @details 두 질의(`wasChordTriggered`, `isChordDown`)는 방아쇠 키를 묻는 한 줄만 다르다.
         */
        bool             hasActiveChord( const hashed_string& action, bool bTriggerJustPressed ) const;
        bool             isLayerActiveInternal( const hashed_string& layer ) const;
        void             ensureActionListed( const hashed_string& action );
        LayerDefinition& ensureLayer( const hashed_string& name, int32 priority = 0, bool enabled = true, bool blockLower = false, bool alwaysOn = false );
        ActionEntry&     getOrCreateAction( const hashed_string& action, InputActionValueType valueType = InputActionValueType::Boolean );
        /**
         * @brief 바인딩 하나를 만들 때 공통으로 거치는 앞부분입니다. 레이어를 보장하고 액션을 목록에 올린 뒤, 레이어 인덱스를 캐시한 빈 바인딩을 반환합니다.
         * @details 모든 `bind*` 가 이 함수와 `commitBinding` 을 거친다. 세 목록(현재, 기본값, 상태) 중 하나만 빠져도 바인딩과 상태의 인덱스가 어긋나기 때문이다.
         *          종류별 필드(슬롯, 데드존, 배율 등)는 이 함수를 부르는 쪽이 채운다.
         */
        ActionBinding beginBinding( const hashed_string& action, BindingKind kind, ActionTrigger trigger, const hashed_string& layer );
        /** @brief 바인딩을 액션의 세 목록(현재, 기본값, 상태)에 함께 넣습니다. 그래서 세 목록의 인덱스가 항상 같습니다. */
        void                   commitBinding( const hashed_string& action, InputActionValueType valueType, const ActionBinding& binding );
        LayerDefinition*       findLayer( const hashed_string& name );
        const LayerDefinition* findLayer( const hashed_string& name ) const;
        ActionEntry*           findAction( const hashed_string& action );
        const ActionEntry*     findAction( const hashed_string& action ) const;

    private:
        /**
         * @brief 가상 커서(`_virtualCursorStick`)가 마우스로 바꾸는 스틱 · 패드 버튼(기본 설정의 A · B, 0 번 패드)을 따로 묶은 바인딩마다 알리고 그 수를 반환합니다.
         * @details 가상 커서가 꺼져 있으면 0 이다. 읽기(`loadFromAsset`)가 0 이 아니면 실패로 돌린다.
         */
        [[nodiscard]] uint32 countVirtualCursorConflicts( string_view sourceName ) const;
        /**
         * @brief 바인딩 하나가 가상 커서가 마우스로 바꾸는 스틱 · 패드 버튼을 묶는지 보고, 묶으면 알리고 true 를 반환합니다.
         * @details 맵 읽기(`countVirtualCursorConflicts`)와 사용자 바인딩 읽기(`loadUserBindings`)가 같은 검사를 쓴다 — 한쪽만 고치면 다른 길로 같은 충돌이 들어온다.
         */
        [[nodiscard]] bool reportVirtualCursorConflict( string_view sourceName, const hashed_string& action, const ActionBinding& binding ) const;

    private:
        friend struct InputMapActionLoader; ///< `<action>` 노드 하나를 읽는 도구(InputMapSerialization.cpp)

    private:
        InputManager*                        _pInput;
        unordered_map<hashed_string, uint32> _mapAction;       /**< 액션 이름에서 `_listActionEntry` 인덱스를 찾는 맵입니다. */
        unordered_map<hashed_string, uint32> _mapLayer;        /**< 레이어 이름에서 `_listLayerEntry` 인덱스를 찾는 맵입니다. */
        vector<ActionEntry>                  _listActionEntry; /**< 액션 슬롯을 소유하는 저장소입니다. 안정된 인덱스로만 접근하며 포인터를 보관하지 않으므로 재할당되어도 안전합니다. */
        vector<LayerDefinition>              _listLayerEntry;  /**< 레이어를 소유하는 저장소입니다. 안정된 인덱스로만 접근하고, `ActionBinding::_cachedLayerIndex` 가 이 인덱스를 참조합니다. */
        vector<hashed_string>                _listActionName;
        vector<hashed_string>                _listLayerName;
        vector<hashed_string>                _listLayerStack;
        InputActionBuffer                    _actionBuffer;
        uint32                               _nextGeneration;
        hashed_string                        _defaultLayerName;
        InputPlayerSettings                  _playerSettings;
        float32                              _doubleClickTime;
        float32                              _doubleClickMaxDistance;
        float32                              _dragThreshold; ///< 클릭과 끌기를 가르는 포인터 이동 거리(픽셀, `dragThreshold`)
        float32                              _doubleTapTime;
        float32                              _holdThreshold;
        float32                              _navRepeatDelay;
        float32                              _navRepeatRate;
        float32                              _totalElapsedTime;
        float32                              _lastDeltaSeconds;   ///< 가장 최근 `update` 의 프레임 시간. 프레임 시간 곱 바인딩이 쓴다.
        InputMapVirtualCursor                _virtualCursorStick; ///< 이 맵이 켜는 가상 커서(`<InputMap virtualCursor>`)
        uint8                                _bSuppressBaseActionOnChord : 1;
        uint8                                _bKeyboardFocusIgnored      : 1; /**< 키보드 포커스와 상관없이 키를 읽습니다(셸 맵). */
        uint8                                _bReadsToolKeyboard         : 1; /**< 도구 포커스 동안에도 키를 읽습니다(에디터 맵). */
        [[maybe_unused]] uint8               _reservedFlags              : 5;
    };
} // namespace sw

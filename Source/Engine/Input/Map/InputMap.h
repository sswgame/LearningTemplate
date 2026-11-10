/**
 * @file InputMap.h
 * @brief 입력 액션을 정의하고 매 프레임 평가하는 통합 InputMap 을 선언합니다.
 * @details `ActionBinding` 하나로 단일 슬롯, 1D/2D 합성 축, 스틱, 조합 키를 모두 표현한다.
 *          액션은 `ActionPhase` 상태 머신(Started, Ongoing, Triggered, Completed, Canceled)으로 평가하고,
 *          모디파이어 파이프라인(데드존, 축 반전, 감도 배율, 바인딩마다 부호 뒤집기)을 거친다. 시간을 쓰는 트리거는 바인딩마다 시간을 둘 수 있다.
 *          그 밖에 LIFO 컨텍스트 스택, 이벤트 델리게이트 디스패치, 맵 정의 XML 직렬화를 제공한다(언리얼 Enhanced Input 의 매핑 컨텍스트 + 모디파이어 · 트리거 자리).
 *          맵 둘레의 일은 협력 클래스가 맡는다 — 선입력 · 격투 커맨드는 `InputActionBuffer`(`getActionBuffer`), 플레이어 설정 · 토글은 `InputAccessibilitySettings`
 *          (`getAccessibilitySettings`), 플레이어 리매핑 파일은 `InputRemapSerializer`, UI 글리프는 `InputGlyphResolver`.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Name/hashed_string.h"
#include "Core/Path/AbsolutePath.h"

#include "Engine/Content/AssetID.h"
#include "Engine/Input/GamepadButtonUtil.h"
#include "Engine/Input/IInputDevice.h"
#include "Engine/Input/KeyCodeUtil.h"
#include "Engine/Input/Map/InputAccessibilitySettings.h"
#include "Engine/Input/Map/InputActionBuffer.h"
#include "Engine/Input/Map/InputBinding.h"
#include "Engine/Input/Map/InputTriggerRules.h"

namespace sw
{
    struct InputPressInstant;

    class InputManager;
    class XMLDocument;
    class XMLNode;
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
     * @class InputMap
     * @brief 액션, 레이어, 바인딩을 보관하고 매 프레임 평가하는 통합 액션 맵입니다.
     */
    class SW_API InputMap
    {
    public:
        using LayerDef                 = sw::LayerDef;
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
         * @brief 맵 묶음을 차례로 읽습니다. 첫 파일이 기본 맵(`loadFromAsset`)이고 나머지는 레이어로 쌓입니다.
         * @details 뒤 파일은 차이를 적는다 — `<action>` 은 새 액션, `<override name>` 은 앞 파일 액션의 바인딩을 통째로 바꾸고(바인딩이 없으면 그 액션은 눌리지 않는다),
         *          `<remove name>` 은 그 액션을 지운다. 한 액션은 묶음에서 한 번만 덮거나 지울 수 있다 — 여러 파일이 덮으면 이기는 쪽이 읽는 순서에 달린다.
         *          맵 전체 값은 기본 맵의 것이고, 예외는 `virtualCursor` 하나다(기능이라 그것을 쓰는 게임의 묶음에 파일로 얹힌다 · 묶음에서 한 번만 적는다).
         *          겹침 · 가상 커서 검사는 다 읽은 맵에 한 번 한다 — 앞 파일끼리의 겹침을 뒤 파일이 풀 수 있으므로 파일마다 보면 거짓 오류가 난다.
         *          언리얼 Enhanced Input 의 매핑 컨텍스트 쌓기 · 유니티 Action Asset 의 맵 켜기 · Godot 의 프로젝트 입력 맵과 비교한 까닭은 `docs/InputMapBundle.md`.
         * @return 모두 읽었고 겹침이 없으면 true 입니다. 실패하면 맵을 비운다(일부만 든 맵으로 게임을 띄우지 않는다).
         */
        [[nodiscard]] bool loadBundle( const vector<AssetID>& listAsset );
        /** @brief 읽은 파일 수입니다(`loadFromAsset` 은 1, `loadBundle` 은 묶음 길이, 파일에서 읽지 않은 맵은 0). */
        uint32 getSourceCount() const { return static_cast<uint32>( _listSource.size() ); }
        /** @brief 읽은 파일 @p sourceIndex 의 리소스 id 입니다. */
        const string& getSourceAssetPath( uint32 sourceIndex ) const { return _listSource[sourceIndex]._assetPath; }
        /** @brief 액션을 마지막으로 정한 파일 번호입니다(패널이 보이고 저장이 돌려보낸다). 없는 액션이면 0 입니다. */
        uint32 getActionSourceIndex( const hashed_string& action ) const;
        /** @brief 액션이 앞 파일의 것을 `<override>` 로 덮은 것이면 true 입니다. */
        bool isActionOverride( const hashed_string& action ) const;
        /**
         * @brief 읽은 파일 @p sourceIndex 의 몫을 그 파일 형식의 글로 만듭니다 — 그 파일이 정한 맵 전체 값 · 레이어 · 액션 · 덮기 · 지우기와, 뒤 파일이 덮거나 지운 그 파일의 원래 정의입니다.
         * @return 형식이 담지 못하는 바인딩이 있으면 false 입니다.
         */
        [[nodiscard]] bool writeSourceText( uint32 sourceIndex, string& outText ) const;
        /** @brief `writeSourceText` 의 글을 @p absPath 에 씁니다. 묶음을 고친 패널이 파일마다 원래 자리로 돌려보낼 때 씁니다. */
        [[nodiscard]] bool saveSourceToAsset( uint32 sourceIndex, const AbsolutePath& absPath ) const;
        /**
         * @brief 레이어, 액션, 현재 바인딩을 `loadFromAsset` 가 읽는 `<InputMap>` 정의로 씁니다(에디터 InputMap 패널의 저장).
         * @param absPath 쓸 디스크 경로입니다(리소스 id 는 부르는 쪽이 `AssetPaths::toDiskPath` 로 푼다).
         * @return 파일 형식으로 표현할 수 없는 바인딩(가상 조이스틱, 아무 키)이 있거나 쓰지 못하면 false 이며, 이때 파일은 건드리지 않습니다.
         * @details 플레이어의 키 리매핑은 여기에 저장하지 않는다. UserSettings 의 `keyBinding` 설정(사용자 파일)에 저장한다.
         */
        [[nodiscard]] bool saveToAsset( const AbsolutePath& absPath ) const;
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
         * @details 두 슬롯이 같은 장치여야 저장된다(`<axis1d source="Gamepad">`). 언리얼 Enhanced Input 의 Axis1D 액션에 버튼 둘(한쪽 Negate)을 매핑하는 것에 해당한다.
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
        // 3.1) 이벤트 기반 델리게이트 바인딩 — 돌려받은 구독이 사라지면 떨어진다(언리얼 Enhanced Input `BindAction` 의 핸들 자리를 소유 구독으로).
        //      액션 이름이 비었거나 델리게이트가 비었으면 빈 구독이다.
        // ------------------------------------------------------------------------------
        /** @brief 액션이 발화한(`Triggered`) 프레임마다 부를 델리게이트를 겁니다. 방아쇠 조건은 바인딩(`ActionTrigger`)이 정한다. */
        [[nodiscard]] Subscription bindActionCallback( const hashed_string& action, ActionCallbackDelegate callback );
        /** @brief 액션이 @p phase 에 있는 프레임마다 부를 델리게이트를 겁니다. */
        [[nodiscard]] Subscription bindPhaseCallback( const hashed_string& action, ActionPhase phase, ActionCallbackDelegate callback );
        /** @brief 2D 값이 0 이 아닌 프레임과 놓인 프레임에 부를 델리게이트를 겁니다. */
        [[nodiscard]] Subscription bindVector2DCallback( const hashed_string& action, Vector2DCallbackDelegate callback );

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
        /** @brief 플레이어 설정(감도 · 축 반전 · 데드존 · 대각선 정규화 · 토글 액션)입니다. `clear` 나 맵 다시 읽기를 해도 남습니다. */
        InputAccessibilitySettings&       getAccessibilitySettings() { return _accessibility; }
        const InputAccessibilitySettings& getAccessibilitySettings() const { return _accessibility; }

        /** @brief 토글 액션(`InputAccessibilitySettings::setToggleAction`)이 지금 켜져 있는지입니다. 누를 때마다 뒤집히고, 토글에서 빠지면 꺼집니다. */
        bool isActionToggled( const hashed_string& action ) const;

        void setSuppressBaseActionOnChord( bool bSuppress ) { _bSuppressBaseActionOnChord = bSuppress ? SW_TRUE : SW_FALSE; }
        bool isSuppressBaseActionOnChord() const { return _bSuppressBaseActionOnChord == SW_TRUE; }

        void setNavRepeatDelay( float32 delay ) { _triggerRules._navRepeatDelay = delay; }
        void setNavRepeatRate( float32 rate ) { _triggerRules._navRepeatRate = rate; }

        // ------------------------------------------------------------------------------
        // 6) 액션 선입력 버퍼링, 격투 커맨드 판정 — 트리거된 액션은 `update` 가 이력에 남긴다
        // ------------------------------------------------------------------------------
        InputActionBuffer&       getActionBuffer() { return _actionBuffer; }
        const InputActionBuffer& getActionBuffer() const { return _actionBuffer; }

        // ------------------------------------------------------------------------------
        // 7) 게임 안 키 리매핑 — UI 글리프는 `InputGlyphResolver` 가 맡는다
        // ------------------------------------------------------------------------------
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
        /**
         * @brief @p action 의 바인딩 @p bindIndex 를 키 + 수정 키로 바꾸고, 그 입력을 가진 @p otherAction 의 바인딩에는 @p action 의 옛 입력을 줍니다.
         * @details 겹침은 상대의 모든 바인딩에서 찾으므로 맞바꾸기도 겹친 그 바인딩을 고친다(첫 바인딩을 고치면 바꾼 뒤에도 겹쳐 한 번 눌러 둘 다 발화한다).
         * @return 상대에게 그 입력이 없거나 어느 쪽이든 바꿀 수 없으면 false 이고 아무것도 바뀌지 않습니다.
         */
        [[nodiscard]] bool swapKeyShortcut( const hashed_string& action, uint32 bindIndex, const hashed_string& otherAction, Key key, uint8 modifierMask );
        /** @brief 처음 등록한 기본 바인딩(`resetActionToDefault` 가 되돌리는 대상)입니다. 없으면 nullptr 입니다. */
        const ActionBinding* getDefaultBinding( const hashed_string& action, uint32 bindIndex ) const;
        /**
         * @brief 바인딩 @p bindIndex 와 같은 레이어 · 같은 발화 규칙 · 같은 입력(키 · 수정 키 · 단추)을 다른 액션이 쓰면 그 이름을 @p outOtherAction 에 담고 true 입니다.
         * @details 한 번 눌러 둘이 발화하는 겹침이다. 맵 더하기(`appendFromAsset`)와 재배치 창이 같은 검사를 쓴다.
         */
        [[nodiscard]] bool findSameLayerInputConflict( const hashed_string& action, uint32 bindIndex, hashed_string& outOtherAction ) const;
        /**
         * @brief 바인딩 @p bindIndex 가 리바인딩으로 바꾸는 슬롯(`BindingKinds::getRebindSlotIndex`)의 현재 키를 반환합니다.
         * @return 액션이나 바인딩이 없거나 키 하나로 바꿀 수 없는 종류면 false 입니다.
         */
        [[nodiscard]] bool findRebindSlot( const hashed_string& action, uint32 bindIndex, InputSlot& outSlot ) const;
        /** @brief 위와 같지만 처음 등록한 기본 바인딩(`resetActionToDefault` 가 되돌리는 대상)의 키를 반환합니다. */
        [[nodiscard]] bool findDefaultRebindSlot( const hashed_string& action, uint32 bindIndex, InputSlot& outSlot ) const;
        /** @brief 바인딩 @p bindIndex 가 속한 레이어 이름을 반환합니다. 없으면 빈 이름입니다. */
        hashed_string findBindingLayer( const hashed_string& action, uint32 bindIndex ) const;
        /** @brief @p layer 와 함께 발화할 수 있는 레이어(`canLayersFireTogether`)에서 다른 액션이 이미 그 슬롯을 점유하고 있는지 검사합니다. */
        bool hasBindingConflict( const InputSlot& slot, const hashed_string& layer, string& outConflictingAction ) const;
        /** @brief 지정 액션의 바인딩을 처음 등록한 기본 바인딩으로 되돌립니다. */
        bool resetActionToDefault( const hashed_string& action );
        /** @brief 모든 액션의 바인딩을 기본값으로 한꺼번에 되돌립니다. 플레이어 리매핑 파일의 쓰기 · 읽기는 `InputRemapSerializer` 가 맡는다. */
        void resetAllBindingsToDefault();

        // ------------------------------------------------------------------------------
        // 8) 입력 장치 연결, 임계값, 디버그
        // ------------------------------------------------------------------------------
        void setInputManager( InputManager* pInput ) { _pInput = pInput; }
        /**
         * @brief true 면 이 맵은 키보드 포커스(`InputManager::setKeyboardFocus`)와 상관없이 키를 읽습니다. 기본은 false 이며 게임 쪽 맵이 이 값을 씁니다.
         * @details 디버그 키 맵(개발 콘솔, 리로드 단축키)만 켠다. 콘솔이 키보드를 가진 동안에도 닫는 키와 편집 키를 받아야 하기 때문이다.
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
        void               setDoubleTapTime( float32 seconds ) { _triggerRules._doubleTapTime = seconds; }
        [[nodiscard]] bool setHoldThreshold( float32 seconds );
        void               setNavRepeatTiming( float32 delaySeconds, float32 rateSeconds )
        {
            _triggerRules._navRepeatDelay = delaySeconds;
            _triggerRules._navRepeatRate  = rateSeconds;
        }
        /** @brief 이 맵이 켜는 가상 커서입니다. `InputManager` 가 매 프레임 읽어 켜고 끕니다. */
        InputMapVirtualCursor getVirtualCursorStick() const { return _virtualCursorStick; }
        void                  setVirtualCursorStick( InputMapVirtualCursor stick ) { _virtualCursorStick = stick; }
        InputManager*         getInputManager() const { return _pInput; }
        float32               getDoubleClickTime() const { return _triggerRules._doubleClickTime; }
        float32               getDragThreshold() const { return _triggerRules._dragThreshold; }
        float32               getDoubleClickMaxDistance() const { return _triggerRules._doubleClickMaxDistance; }
        float32               getDoubleTapTime() const { return _triggerRules._doubleTapTime; }
        float32               getHoldThreshold() const { return _triggerRules._holdThreshold; }
        float32               getNavRepeatDelay() const { return _triggerRules._navRepeatDelay; }
        float32               getNavRepeatRate() const { return _triggerRules._navRepeatRate; }

        /** @brief 실시간 액션 평가 상태를 덤프합니다(HUD, 에디터 인스펙터용). */
        void getDebugActionStates( vector<DebugActionState>& outListState ) const;

        // ------------------------------------------------------------------------------
        // 9) 조회와 프레임 상태(통합 쿼리와 O(1) ActionHandle 핫패스)
        // ------------------------------------------------------------------------------
        ActionHandle getActionHandle( const hashed_string& action ) const;

        bool hasLayer( const hashed_string& layer ) const;
        bool isLayerEnabled( const hashed_string& layer ) const;
        /**
         * @brief 두 레이어의 바인딩이 한 번의 누름에 함께 발화할 수 있으면 true 입니다 — 같은 레이어이거나, 둘 다 지금 켜져 있습니다.
         * @details 묶음의 레이어는 파일의 겹침을 나누려고 있어서 게임에서는 함께 켜져 있다. 그래서 플레이어 재배치의 겹침은 같은 레이어만 보면 놓친다(Interact 를 W 로 옮기면 이동과 함께 돈다).
         */
        bool                         canLayersFireTogether( const hashed_string& layerA, const hashed_string& layerB ) const;
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
        /** @brief 바인딩 @p bindIndex 를 이미 있는 레이어 @p layer 로 옮깁니다. 기본 바인딩에도 같이 적습니다. 액션 · 바인딩 · 레이어가 없으면 false 입니다. */
        [[nodiscard]] bool setBindingLayer( const hashed_string& action, uint32 bindIndex, const hashed_string& layer );
        /**
         * @brief 바인딩 @p bindIndex 를 지웁니다. 뒤 바인딩의 번호가 당겨집니다. 없으면 false 입니다.
         * @details 맵 정의를 고치는 일이라 현재 · 기본 · 상태 세 목록에서 함께 지운다(`commitBinding` 의 짝). 키만 비우면 파일이 이름 붙이지 못하는 `Unknown` 코드가 저장되어 다음 읽기가 맵 전체를 거절한다.
         */
        [[nodiscard]] bool removeBinding( const hashed_string& action, uint32 bindIndex );
        /**
         * @brief 액션을 지웁니다. 없으면 false 입니다.
         * @details 저장소 칸은 번호가 안정되도록 빈 채로 남기고 세대를 올린다 — 들고 있던 `ActionHandle` 은 무효가 되고, 같은 이름을 다시 만들면 새 칸을 쓴다.
         */
        [[nodiscard]] bool removeAction( const hashed_string& action );
        /** @brief 바인딩의 트리거가 실제로 쓰는 시간(초)입니다. 바인딩 값이 0 이면 트리거별 맵 값입니다. 시간을 쓰지 않는 트리거는 0 입니다. */
        float32              getEffectiveTriggerTime( const ActionBinding& binding ) const { return _triggerRules.getEffectiveTriggerTime( binding ); }
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

    private:
        /** @brief 입력을 읽는 눈입니다. 게임이 보는 값과 장치 값은 개발 콘솔 · 포커스 · 캡처가 가린 키에서 달라진다. */
        enum class SlotRead : uint8
        {
            Game,  ///< 게임이 보는 값. 콘솔이 쥔 키는 "안 눌림" 이다(`isSlotHiddenByConsole`).
            Device ///< 가림과 상관없는 장치 값. 바인딩이 "보이게 되는 순간" 이미 눌려 있었는지 추적하는 데만 쓴다.
        };

        struct ActionEntry
        {
            InputActionValueType                   _valueType{ InputActionValueType::Boolean };
            vector<ActionBinding>                  _listBinding{};
            vector<InputBindingState>              _listBindingState{};
            vector<ActionBinding>                  _listDefaultBinding{};
            MulticastDelegate<void()>              _onTriggered{}; ///< `bindActionCallback`
            MulticastDelegate<void( ActionPhase )> _onPhase{};     ///< `bindPhaseCallback` — 항목이 자기 단계만 거른다
            MulticastDelegate<void( float2 )>      _onVector2D{};  ///< `bindVector2DCallback`
            float2                                 _currentValue{ 0.0f, 0.0f };
            float32                                _holdDuration{ 0.0f };
            ActionPhase                            _currentPhase{ ActionPhase::None };
            uint32                                 _handleIndex{ ActionHandle::kInvalidIndex };
            uint32                                 _generation{ 1 };
            uint32                                 _sourceIndex{ 0 }; ///< 이 액션을 정한 묶음 파일 번호(`_listSource`)
            uint8                                  _bOverride    : 1; ///< 앞 파일의 액션을 `<override>` 로 덮은 것이다
            uint8                                  _bDown        : 1;
            uint8                                  _bPressed     : 1;
            uint8                                  _bReleased    : 1;
            uint8                                  _bTriggered   : 1;
            uint8                                  _bToggleState : 1; ///< 토글 액션의 켜짐. 토글 여부는 `InputAccessibilitySettings` 가 가진다(맵을 다시 읽어도 남게)

            ActionEntry()
                : _valueType{ InputActionValueType::Boolean }
                , _listBinding{}
                , _listBindingState{}
                , _listDefaultBinding{}
                , _onTriggered{}
                , _onPhase{}
                , _onVector2D{}
                , _currentValue{ 0.0f, 0.0f }
                , _holdDuration{ 0.0f }
                , _currentPhase{ ActionPhase::None }
                , _handleIndex{ ActionHandle::kInvalidIndex }
                , _generation{ 1 }
                , _sourceIndex{ 0 }
                , _bOverride{ SW_FALSE }
                , _bDown{ SW_FALSE }
                , _bPressed{ SW_FALSE }
                , _bReleased{ SW_FALSE }
                , _bTriggered{ SW_FALSE }
                , _bToggleState{ SW_FALSE } {}
        };

        /** @brief 뒤 파일이 덮거나 지운 정의입니다. 맵에서는 사라지지만 파일에는 남아야 하므로 정의한 파일의 기록이 든다. */
        struct SourceActionSnapshot
        {
            hashed_string         _action{};
            InputActionValueType  _valueType{ InputActionValueType::Boolean };
            vector<ActionBinding> _listBinding{};
        };

        /** @brief 묶음이 읽은 파일 하나의 기록입니다. 패널 저장이 파일마다 원래 몫을 돌려보내는 데 쓴다. */
        struct SourceRecord
        {
            string                       _assetPath;
            vector<SourceActionSnapshot> _listShadowed;       ///< 이 파일이 정의했는데 뒤 파일이 덮거나 지운 액션
            vector<hashed_string>        _listRemove;         ///< 이 파일의 `<remove>`
            uint8                        _bBase          : 1; ///< 기본 맵이다 — 맵 전체 값을 쓴다
            uint8                        _bVirtualCursor : 1; ///< `virtualCursor` 를 이 파일이 적었다
            [[maybe_unused]] uint8       _reserved       : 6;

            SourceRecord( string_view assetPath, bool bBase, bool bVirtualCursor )
                : _assetPath{ assetPath }
                , _listShadowed{}
                , _listRemove{}
                , _bBase{ static_cast<uint8>( bBase ? SW_TRUE : SW_FALSE ) }
                , _bVirtualCursor{ static_cast<uint8>( bVirtualCursor ? SW_TRUE : SW_FALSE ) }
                , _reserved{ 0 }
            {
            }
        };

        const ActionEntry* getActionFromHandle( ActionHandle handle ) const;

        bool evaluateBindingDown( const ActionBinding& binding, float2& outValue, SlotRead read = SlotRead::Game ) const;
        /** @brief 단일 키 바인딩이 수정 키(Ctrl · Alt — 또는 같은 키의 Shift · Super 조합 바인딩이 지금 맞을 때)가 눌린 동안 물러나야 하는지 봅니다(`_bSuppressBaseActionOnChord`). */
        bool isBaseKeySuppressedByModifier( const ActionBinding& binding, SlotRead read ) const;
        /** @brief 게임패드 스틱 바인딩의 값을 데드존 · 응답 곡선 · 감도를 적용해 읽습니다. 연결이 없으면 false 입니다. */
        bool evaluateGamepadStick( const ActionBinding& binding, float2& outValue ) const;
        /** @brief 가상 조이스틱 바인딩의 값을 읽습니다. 활성화 버튼을 처음 누른 지점이 앵커입니다. */
        bool evaluateVirtualJoystick( const ActionBinding& binding, float2& outValue, SlotRead read ) const;
        /** @brief 눌린 수정 키(Ctrl · Shift · Alt · Super)가 단축키 바인딩의 마스크와 정확히 같은지 봅니다. 방아쇠 키 자신이 수정 키면 그 키는 비교에서 뺍니다. */
        bool areShortcutModifiersExact( const ActionBinding& binding, SlotRead read ) const;
        /**
         * @brief `loadFromAsset` 와 `appendFromAsset` 가 함께 쓰는 본문입니다. @p bAppend 면 맵을 비우지 않고, 맵 전체 값을 받지 않고, 이미 있는 이름을 오류로 봅니다.
         * @details 두 입구가 각자 읽으면 레이어 · 액션 읽기 규칙이 둘로 갈라진다. @p bCheckConflicts 가 거짓이면 겹침 · 가상 커서 검사를 부른 쪽이 다 읽은 뒤에 한다(`loadBundle`).
         */
        [[nodiscard]] bool loadDefinition( const AssetID& assetID, bool bAppend, bool bCheckConflicts );
        /** @brief `loadDefinition` 의 맵 전체 값 단계입니다. 시간 문턱 · 가상 커서 · 기본 레이어를 읽고, 더하지 않을 때만 맵을 비우고 적용합니다. 깨졌거나 거절되면 false 입니다. */
        [[nodiscard]] bool readMapSettings( const XMLNode& root, string_view absPath, bool bAppend );
        /** @brief `loadDefinition` 의 레이어 단계입니다. `<layers>` 블록의 레이어를 등록하고 기본 레이어를 보장합니다. 깨진 값은 @p inoutbValid 를 내립니다. */
        void readLayerDefinitions( const XMLNode& root, string_view absPath, bool bAppend, bool& inoutbValid );
        /**
         * @brief `<layer>` 원소 하나를 스키마(`InputMapLayerParams`)로 읽어 등록합니다. @p listChild 는 그 원소가 품을 수 있는 자식 이름입니다(`<layers>` 안은 없음, 맨 위는 `action`).
         * @details 맨 위 `<layer>` 는 이미 있는 레이어면 설정을 바꾸지 않고 액션만 담는다. 더하는 파일이 `<layers>` 로 있는 레이어를 다시 정의하면 오류다.
         */
        [[nodiscard]] bool readLayer( const XMLNode& layerNode, string_view absPath, bool bAppend, std::initializer_list<string_view> listChild, hashed_string& outName );
        /** @brief `loadDefinition` 의 액션 단계입니다. 최상위 `<layer>` 와 `<action>` 을 읽고 더한 액션 이름을 @p outListLoadedAction 에 모읍니다. */
        void readActionDefinitions( const XMLNode& root, string_view absPath, bool bAppend, vector<hashed_string>& outListLoadedAction, bool& inoutbValid );
        /** @brief 액션 @p action 의 바인딩 중 같은 레이어에서 같은 발화 규칙 · 같은 입력을 다른 액션이 이미 쓰는 것을 알리고 그 수를 반환합니다(`appendFromAsset`). */
        [[nodiscard]] uint32 countSameLayerInputConflicts( string_view sourceName, const hashed_string& action ) const;
        /** @brief 맵 전체의 같은 레이어 겹침을 짝마다 한 번 알리고 그 수를 반환합니다(`loadBundle`). */
        [[nodiscard]] uint32 countAllSameLayerInputConflicts( string_view sourceName ) const;
        /** @brief 차이 원소(`<override>` · `<remove>`)가 가리키는 액션을 찾습니다. 앞 파일에 없거나 이미 덮였거나 이 파일의 것이면 알리고 nullptr 입니다. */
        [[nodiscard]] ActionEntry* findDiffTarget( const XMLNode& node, const hashed_string& action, string_view absPath );
        /** @brief 지금 정의를 정의한 파일의 기록에 남깁니다(뒤 파일이 덮거나 지우기 직전). */
        void shadowActionDefinition( const ActionEntry& entry, const hashed_string& action );
        /** @brief `<override>` 하나를 읽습니다 — 앞 파일 액션의 바인딩을 이 원소의 것으로 바꿉니다. */
        void readOverride( const XMLNode& node, string_view absPath, vector<hashed_string>& outListLoadedAction, bool& inoutbValid );
        /** @brief `<remove>` 하나를 읽습니다 — 앞 파일의 액션을 지웁니다. */
        void readRemove( const XMLNode& node, string_view absPath, bool& inoutbValid );
        /** @brief 레이어 하나를 `<layer>` 원소로 씁니다. */
        void writeLayerDef( const XMLNode& layersNode, const hashed_string& layerName ) const;
        /** @brief `saveSourceToAsset` · `writeSourceText` 의 본문입니다. */
        [[nodiscard]] bool fillSourceDocument( uint32 sourceIndex, XMLDocument& outDoc ) const;

        /** @brief 한 프레임 동안 액션 하나의 바인딩들을 합산한 값입니다. */
        struct ActionFrameAccum
        {
            bool    _bAnyDown{ false };        ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
            bool    _bAnyPressed{ false };     ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
            bool    _bAnyReleased{ false };    ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
            bool    _bAnyTriggered{ false };   ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
            bool    _bAnyHoldReached{ false }; ///< 어떤 바인딩이든 자기 홀드 시간을 채웠는지입니다. NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
            float32 _maxHold{ 0.0f };
            float2  _totalValue{ 0.0f, 0.0f };
            float2  _relativeValue{ 0.0f, 0.0f }; ///< 마우스 이동량입니다. 묶지 않습니다.
        };
        /** @brief 바인딩 하나의 프레임 상태(눌림 · 홀드 · 연타)를 갱신하고 액션 합산에 더합니다. */
        void updateBinding( const ActionBinding& binding, InputBindingState& state, float32 deltaSeconds, const int2& mousePos, ActionFrameAccum& accum ) const;
        /** @brief 이번 프레임 합산으로 ActionPhase 를 전이합니다. */
        static void advanceActionPhase( ActionEntry& actionEntry, bool bPrevDown, bool bAnyHoldReached );
        /** @brief Triggered · Phase · 2D 벡터 델리게이트를 부릅니다. */
        static void dispatchActionCallbacks( ActionEntry& actionEntry, bool bPrevDown );
        /**
         * @brief 새로 만드는 바인딩 상태입니다. 지금 장치가 눌려 있으면 `_bPhysicalDown` 을 미리 채워, 첫 `update` 에서 Pressed 가 아니라 Held 로 시작하게 합니다.
         * @details 바인딩 상태를 만드는 길(`commitBinding`, 리바인딩, 기본값 복원)은 모두 이 함수 하나를 거친다 — 빈 `InputBindingState{}` 를 직접 넣는 길이 남으면
         *          이미 눌린 키가 한 번 새 누름으로 읽힌다.
         */
        InputBindingState makeBindingState( const ActionBinding& binding ) const;
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
        /** @brief 개발 콘솔이 입력을 쥐어 이 맵이 슬롯을 "안 눌림" 으로 봐야 하면 true 입니다(키보드와 패드 슬롯). 키보드 포커스를 무시하는 맵(디버그 키 맵, UI 맵)은 늘 false 입니다. */
        bool isSlotHiddenByConsole( const InputSlot& slot ) const;
        /**
         * @brief 바인딩 값을 합쳐 액션 값을 계산합니다.
         * @details 축, 버튼, 스틱 몫(@p bounded)은 축 반전을 적용한 뒤 [-1, 1](또는 원)에 묶는다.
         *          마우스 이동량 몫(@p relative)은 묶지 않고 그대로 더한다. 이동량은 픽셀 단위 상대값이고, 반전은 바인딩 평가에서 이미 적용했기 때문이다.
         */
        float2 composeActionValue( const float2& bounded, const float2& relative ) const;
        bool   isBindingLayerActive( const ActionBinding& binding ) const;
        /**
         * @brief 활성 레이어의 코드(chord) 바인딩 중 수식 키가 눌려 있고 방아쇠 키가 조건을 만족하는 것이 있는지 봅니다.
         * @param bTriggerJustPressed true 면 방아쇠 키가 이번 프레임에 눌렸는지(`wasChordTriggered`), false 면 눌려 있는지(`isChordDown`)를 봅니다.
         * @details 두 질의(`wasChordTriggered`, `isChordDown`)는 방아쇠 키를 묻는 한 줄만 다르다.
         */
        bool         hasActiveChord( const hashed_string& action, bool bTriggerJustPressed ) const;
        bool         isLayerActiveInternal( const hashed_string& layer ) const;
        void         ensureActionListed( const hashed_string& action );
        LayerDef&    ensureLayer( const hashed_string& name, int32 priority = 0, bool enabled = true, bool blockLower = false, bool alwaysOn = false );
        ActionEntry& getOrCreateAction( const hashed_string& action, InputActionValueType valueType = InputActionValueType::Boolean );
        /**
         * @brief 바인딩 하나를 만들 때 공통으로 거치는 앞부분입니다. 레이어를 보장하고 액션을 목록에 올린 뒤, 레이어 인덱스를 캐시한 빈 바인딩을 반환합니다.
         * @details 모든 `bind*` 가 이 함수와 `commitBinding` 을 거친다. 세 목록(현재, 기본값, 상태) 중 하나만 빠져도 바인딩과 상태의 인덱스가 어긋나기 때문이다.
         *          종류별 필드(슬롯, 데드존, 배율 등)는 이 함수를 부르는 쪽이 채운다.
         */
        ActionBinding beginBinding( const hashed_string& action, BindingKind kind, ActionTrigger trigger, const hashed_string& layer );
        /** @brief 바인딩을 액션의 세 목록(현재, 기본값, 상태)에 함께 넣습니다. 그래서 세 목록의 인덱스가 항상 같습니다. */
        void               commitBinding( const hashed_string& action, InputActionValueType valueType, const ActionBinding& binding );
        LayerDef*          findLayer( const hashed_string& name );
        const LayerDef*    findLayer( const hashed_string& name ) const;
        ActionEntry*       findAction( const hashed_string& action );
        const ActionEntry* findAction( const hashed_string& action ) const;

    private:
        /**
         * @brief 가상 커서(`_virtualCursorStick`)가 마우스로 바꾸는 스틱 · 패드 버튼(기본 설정의 A · B, 0 번 패드)을 따로 묶은 바인딩마다 알리고 그 수를 반환합니다.
         * @details 가상 커서가 꺼져 있으면 0 이다. 읽기(`loadFromAsset`)가 0 이 아니면 실패로 돌린다.
         */
        [[nodiscard]] uint32 countVirtualCursorConflicts( string_view sourceName ) const;
        /**
         * @brief 바인딩 하나가 가상 커서가 마우스로 바꾸는 스틱 · 패드 버튼을 묶는지 보고, 묶으면 알리고 true 를 반환합니다.
         * @details 맵 읽기(`countVirtualCursorConflicts`)와 사용자 바인딩 읽기(`InputRemapSerializer::load`)가 같은 검사를 쓴다 — 한쪽만 고치면 다른 길로 같은 충돌이 들어온다.
         */
        [[nodiscard]] bool reportVirtualCursorConflict( string_view sourceName, const hashed_string& action, const ActionBinding& binding ) const;
        /**
         * @brief 액션 @p action 의 바인딩 중 @p firstIndex 부터 가상 커서와 겹치는 것(`reportVirtualCursorConflict`)을 알리고 지운 뒤 지운 수를 반환합니다.
         * @details 바인딩 · 기본값 · 상태 세 목록을 함께 지운다 — 세 목록의 인덱스가 같다는 규칙은 맵이 지킨다(`commitBinding`).
         */
        uint32 removeVirtualCursorConflicts( string_view sourceName, const hashed_string& action, uint32 firstIndex );

    private:
        friend struct InputMapXML;          ///< `<action>` 원소 하나를 읽고 쓰는 도구(맵 정의 · 사용자 바인딩 공용)
        friend struct InputRemapSerializer; ///< 사용자 바인딩 읽기가 가상 커서와 겹친 바인딩을 지운다(`removeVirtualCursorConflicts`)

    private:
        InputManager*                        _pInput;
        unordered_map<hashed_string, uint32> _mapAction;       /**< 액션 이름에서 `_listActionEntry` 인덱스를 찾는 맵입니다. */
        unordered_map<hashed_string, uint32> _mapLayer;        /**< 레이어 이름에서 `_listLayerEntry` 인덱스를 찾는 맵입니다. */
        vector<ActionEntry>                  _listActionEntry; /**< 액션 슬롯을 소유하는 저장소입니다. 안정된 인덱스로만 접근하며 포인터를 보관하지 않으므로 재할당되어도 안전합니다. */
        vector<LayerDef>                     _listLayerEntry;  /**< 레이어를 소유하는 저장소입니다. 안정된 인덱스로만 접근하고, `ActionBinding::_cachedLayerIndex` 가 이 인덱스를 참조합니다. */
        vector<hashed_string>                _listActionName;
        vector<hashed_string>                _listLayerName;
        vector<hashed_string>                _listLayerStack;
        vector<SourceRecord>                 _listSource; /**< 읽은 파일(묶음 순서). 맵을 비우면 함께 비운다. */
        InputActionBuffer                    _actionBuffer;
        uint32                               _nextGeneration;
        uint32                               _currentSourceIndex; /**< 새로 만드는 액션 · 레이어가 속할 파일입니다. 읽기가 끝나면 마지막 파일 — 패널이 새로 만든 것은 묶음의 차이 파일로 간다. */
        hashed_string                        _defaultLayerName;
        InputAccessibilitySettings           _accessibility;
        InputTriggerRules                    _triggerRules; ///< 시간 · 거리 문턱과 바인딩 상태 진행(홀드 · 탭 · 연타 · 끌기 · 반복)
        float32                              _totalElapsedTime;
        float32                              _lastDeltaSeconds;   ///< 가장 최근 `update` 의 프레임 시간. 프레임 시간 곱 바인딩이 쓴다.
        InputMapVirtualCursor                _virtualCursorStick; ///< 이 맵이 켜는 가상 커서(`<InputMap virtualCursor>`)
        uint8                                _bSuppressBaseActionOnChord : 1;
        uint8                                _bKeyboardFocusIgnored      : 1; /**< 키보드 포커스와 상관없이 키를 읽습니다(디버그 키 맵). */
        uint8                                _bReadsToolKeyboard         : 1; /**< 도구 포커스 동안에도 키를 읽습니다(에디터 맵). */
        [[maybe_unused]] uint8               _reservedFlags              : 5;
    };
} // namespace sw

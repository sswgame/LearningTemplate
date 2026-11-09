#include "pch.h"

#include "Engine/Input/Map/InputMap.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Content/AssetPaths.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Input/GamepadButtonUtil.h"
#include "Engine/Input/IInputDevice.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/KeyCodeUtil.h"
#include "Engine/Input/Map/InputMapParams.h"
#include "Engine/Input/Map/InputMapXML.h"
#include "Engine/Input/Map/InputRemapSerializer.h"
#include "Engine/Input/RawInputEvent.h"
#include "Engine/Input/Virtual/VirtualCursor.h"
#include "Engine/Input/Virtual/VirtualJoystick.h"
#include "Engine/Reflection/EnumName.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLFields.h"
#include "Engine/Serialization/XML/XMLNameCheck.h"

/**
 * @file InputMap.cpp
 * @brief InputMap 의 핵심 로직(수명 주기, 바인딩 등록, 레이어 스택, 리바인드, 조회 API)을 구현합니다.
 *
 * InputMap 의 로직은 여러 파일에 나뉘어 있습니다. 찾는 내용에 따라 아래 파일을 보십시오.
 *  - 생성자, bind*() 등록 함수, 레이어 스택(pushLayer/popLayer), 리바인드, is/wasActionXxx() 조회.
 *  - update() 가 매 프레임 부르는 상태 머신(액션이 눌렸는지, 트리거됐는지 판정).
 *  - InputMap XML 정의의 로드와 저장.
 *  맵이 들고 있거나 맵을 읽는 협력 클래스는 따로 있다(언리얼 Enhanced Input 이 매핑 컨텍스트와 플레이어 설정 · 아이콘을 가르는 나눔).
 *  - InputActionBuffer: 선입력 버퍼링과 격투 게임식 커맨드 시퀀스, 패턴 판정(맵이 들고 프레임마다 진행).
 *  - InputGlyphResolver: 액션을 UI 프롬프트 문자열("[ E ]" 등)로 바꾸는 글리프 조회(상태 없음).
 *  - InputAccessibilitySettings: 감도 · 축 반전 · 데드존 · 토글 액션(맵이 들고, 맵을 다시 읽어도 남는다).
 *  - InputRemapSerializer: 플레이어 리매핑 파일(`<UserBindings>`)의 쓰기 · 읽기. 두 형식이 함께 쓰는 `<action>` 원소 읽기 · 쓰기는 InputMapXML(스키마 InputMapParams.h).
 */

// InputAccessibilitySettings
namespace sw
{
    InputAccessibilitySettings::InputAccessibilitySettings()
        : _mouseSensitivity{ 1.0f, 1.0f }
        , _gamepadSensitivity{ 1.0f, 1.0f }
        , _stickDeadzoneOverride{ -1.0f }
        , _deadzoneShape{ DeadzoneShape::Radial }
        , _digitalNormalization{ DigitalNormalization::Circular }
        , _bInvertX{ SW_FALSE }
        , _bInvertY{ SW_FALSE }
        , _listToggleAction{}
    {
    }

    void InputAccessibilitySettings::setToggleAction( const hashed_string& action, bool bToggle )
    {
        if ( action.empty() )
            return;
        if ( bToggle == false )
        {
            (void)VectorUtil::removeFirst( _listToggleAction, action ); // 없으면 지울 것이 없다
            return;
        }
        if ( VectorUtil::contains( _listToggleAction, action ) == false )
            _listToggleAction.push_back( action );
    }

    bool InputAccessibilitySettings::isToggleAction( const hashed_string& action ) const
    {
        return VectorUtil::contains( _listToggleAction, action );
    }
} // namespace sw

namespace sw
{
    namespace
    {
        /**
         * @brief `BindingKind` 한 종류에 딸린 값입니다.
         * @details 이 테이블에 없는 값은 종류마다 실제로 다른 것(어떤 특성을 읽고 쓰는가, 어떻게 평가하는가)이고,
         *          그것들은 각자의 switch 에 남는다. 이 저장소는 모든 switch 에 `default:` 를
         *          요구하므로(-Wswitch-default) 컴파일러가 그 switch 에서 빠진 종류를 알려 주지 못한다.
         *          대신 `default:` 에서 알린다(저장과 로드는 오류 로그, 평가는 단언).
         */
        struct BindingKindInfo
        {
            BindingKind _kind;              ///< 테이블 위치와 열거자가 어긋나지 않는지 확인하려고 가진 자기 값입니다.
            const utf8* _pXMLName;          ///< XML `kind` 특성에 적는 이름입니다.
            uint32      _conflictSlotCount; ///< 키 충돌 검사가 훑을 슬롯 수입니다(0 이면 특정 키를 점유하지 않음).
            uint32      _rebindSlotIndex;   ///< 키 하나로 다시 잡을 때 바뀌는 슬롯입니다(`BindingKinds::kNoRebindSlot` 이면 키 하나로는 못 바꿈).
            bool        _bRelative;         ///< 이번 프레임의 이동량인지 나타냅니다. 묶지 않고, 액션 값 단계에서 반전하지 않습니다(`BindingKinds::isRelative`). NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
        };

        constexpr uint32 kNoRebind = BindingKinds::kNoRebindSlot;

        // 종류를 추가하면 이 테이블에 한 줄을 더한다. 빠뜨리면 아래 static_assert 가 컴파일을 막는다.
        constexpr BindingKindInfo kArrBindingKindInfo[] = {
            {       BindingKind::SingleSlot,          "single", 1,         0, false},
            {  BindingKind::Axis1DComposite,          "axis1d", 2, kNoRebind, false},
            {BindingKind::Vector2DComposite,        "vector2d", 4, kNoRebind, false},
            {   BindingKind::GamepadStick2D,           "stick", 0, kNoRebind, false},
            {            BindingKind::Chord,           "chord", 2,         1, false},
            {     BindingKind::MouseDelta2D,      "mouseDelta", 0, kNoRebind,  true},
            {         BindingKind::Shortcut,        "shortcut", 1,         0, false},
            {           BindingKind::AnyKey,          "anyKey", 0, kNoRebind, false},
            {BindingKind::VirtualJoystick2D, "virtualJoystick", 1, kNoRebind, false},
            {     BindingKind::MouseWheel1D,      "mouseWheel", 0, kNoRebind,  true},
        };

        static_assert( sizeof( kArrBindingKindInfo ) / sizeof( kArrBindingKindInfo[0] ) == static_cast<size_t>( BindingKind::Count ),
                       "BindingKind 를 늘렸으면 kArrBindingKindInfo 에도 줄을 더할 것 — 이름과 충돌 슬롯 수가 여기서 온다." );

        /** @brief 테이블에서 종류에 해당하는 줄을 찾습니다. 범위 밖이면 nullptr 입니다. */
        const BindingKindInfo* findBindingKindInfo( BindingKind kind )
        {
            for ( const BindingKindInfo& info : kArrBindingKindInfo )
            {
                if ( info._kind == kind )
                    return &info;
            }
            return nullptr;
        }

        /**
         * @brief 다시 잡으려는 슬롯과 겹치는 이 바인딩의 슬롯 번호입니다. 겹치지 않으면 `BindingKinds::kNoRebindSlot` 입니다.
         * @details 키 하나로 다시 잡는 슬롯이 있는 종류(단일 슬롯, Chord 의 방아쇠, Shortcut)는 그 슬롯만 본다. Chord 의 수식 키(Ctrl)는
         *          다른 액션이 단독으로 써도 겹침이 아니다. 그런 슬롯이 없는 합성 축과 2D 합성은 키를 점유하는 모든 슬롯을 본다(그 부분이 맞바뀌거나 비워진다).
         */
        uint32 findConflictingSlot( const ActionBinding& binding, const InputSlot& slot )
        {
            const uint32 rebindSlot = BindingKinds::getRebindSlotIndex( binding._kind );
            if ( rebindSlot != BindingKinds::kNoRebindSlot )
                return binding._arrSlot[rebindSlot] == slot ? rebindSlot : BindingKinds::kNoRebindSlot;
            const uint32 slotCount = BindingKinds::getConflictSlotCount( binding._kind );
            for ( uint32 slotIndex = 0; slotIndex < slotCount; ++slotIndex )
            {
                if ( binding._arrSlot[slotIndex] == slot )
                    return slotIndex;
            }
            return BindingKinds::kNoRebindSlot;
        }

        /** @brief 데드존 밖 크기(0 ~ 1)를 [안쪽 데드존, 바깥 데드존] 구간으로 펴고 응답 지수를 적용합니다. */
        float32 shapeStickMagnitude( float32 magnitude, float32 inDeadzone, float32 deadRange, float32 responseExponent )
        {
            float32 norm = deadRange > 0.0001f ? MathUtil::clamp( ( magnitude - inDeadzone ) / deadRange, 0.0f, 1.0f ) : 1.0f;
            if ( responseExponent != 1.0f && responseExponent > 0.0f )
                norm = MathUtil::pow( norm, responseExponent );
            return norm;
        }

        /** @brief 축마다 따로 데드존을 적용합니다(Axial). */
        float32 applyAxialDeadzone( float32 val, float32 inDeadzone, float32 deadRange, float32 responseExponent )
        {
            const float32 absVal = MathUtil::abs( val );
            if ( absVal <= inDeadzone )
                return 0.0f;
            return ( val > 0.0f ? 1.0f : -1.0f ) * shapeStickMagnitude( absVal, inDeadzone, deadRange, responseExponent );
        }
    } // namespace

    const utf8* BindingKinds::toName( BindingKind kind )
    {
        const BindingKindInfo* pInfo = findBindingKindInfo( kind );
        // 이름이 없으면 저장이 조용히 망가지므로, 모르는 종류는 빈 문자열로 눈에 띄게 둔다.
        return ( pInfo != nullptr ) ? pInfo->_pXMLName : "";
    }

    BindingKind BindingKinds::fromName( string_view name )
    {
        for ( const BindingKindInfo& info : kArrBindingKindInfo )
        {
            // string_view 오버로드를 쓴다. `name.data()` 는 널 종료가 보장되지 않는다.
            if ( StringUtil::equals( name, string_view{ info._pXMLName } ) )
                return info._kind;
        }
        return BindingKind::Count;
    }

    uint32 BindingKinds::getRebindSlotIndex( BindingKind kind )
    {
        const BindingKindInfo* pInfo = findBindingKindInfo( kind );
        return ( pInfo != nullptr ) ? pInfo->_rebindSlotIndex : kNoRebindSlot;
    }

    uint32 BindingKinds::getConflictSlotCount( BindingKind kind )
    {
        const BindingKindInfo* pInfo = findBindingKindInfo( kind );
        return ( pInfo != nullptr ) ? pInfo->_conflictSlotCount : 0;
    }

    bool BindingKinds::isRelative( BindingKind kind )
    {
        const BindingKindInfo* pInfo = findBindingKindInfo( kind );
        return pInfo != nullptr && pInfo->_bRelative;
    }

    bool BindingKinds::isRelative( const ActionBinding& binding )
    {
        if ( isRelative( binding._kind ) )
            return true;
        return binding._kind == BindingKind::GamepadStick2D && ( binding._bScaleByDelta || binding._axisScale._x != 1.0f || binding._axisScale._y != 1.0f );
    }

    InputMap::InputMap()
        : _pInput{ nullptr }
        , _mapAction{}
        , _mapLayer{}
        , _listActionEntry{}
        , _listLayerEntry{}
        , _listActionName{}
        , _listLayerName{}
        , _listLayerStack{}
        , _listSource{}
        , _actionBuffer{}
        , _nextGeneration{ 1 }
        , _currentSourceIndex{ 0 }
        , _defaultLayerName{ InputMapDefaults::kDefaultLayerName }
        , _accessibility{}
        , _triggerRules{}
        , _totalElapsedTime{ 0.0f }
        , _lastDeltaSeconds{ 0.0f }
        , _virtualCursorStick{ InputMapVirtualCursor::Off }
        , _bSuppressBaseActionOnChord{ SW_TRUE }
        , _bKeyboardFocusIgnored{ SW_FALSE }
        , _bReadsToolKeyboard{ SW_FALSE }
        , _reservedFlags{ 0 }
    {
    }

    void InputMap::clear()
    {
        _mapAction.clear();
        _mapLayer.clear();
        _listActionEntry.clear();
        _listLayerEntry.clear();
        _listActionName.clear();
        _listLayerName.clear();
        _listLayerStack.clear();
        _listSource.clear();
        _currentSourceIndex = 0;
        _actionBuffer.clear();
        _defaultLayerName                     = hashed_string( InputMapDefaults::kDefaultLayerName );
        _triggerRules._doubleClickTime        = InputMapDefaults::kDoubleClickTime;
        _triggerRules._doubleClickMaxDistance = InputMapDefaults::kDoubleClickMaxDistance;
        _triggerRules._dragThreshold          = InputMapDefaults::kDragThreshold;
        _triggerRules._holdThreshold          = InputMapDefaults::kHoldThreshold;
        _totalElapsedTime                     = 0.0f;
        _lastDeltaSeconds                     = 0.0f;
        _virtualCursorStick                   = InputMapVirtualCursor::Off;
    }

    void InputMap::createAction( const hashed_string& action, InputActionValueType valueType )
    {
        if ( action.empty() )
            return;
        getOrCreateAction( hashed_string( action ), valueType );
    }

    ActionBinding InputMap::beginBinding( const hashed_string& action, BindingKind kind, ActionTrigger trigger, const hashed_string& layer )
    {
        const hashed_string layerName = layer.empty() ? _defaultLayerName : layer;
        ensureLayer( layerName );
        ensureActionListed( action );

        ActionBinding binding{};
        binding._layer            = layerName;
        binding._kind             = kind;
        binding._trigger          = trigger;
        binding._cachedLayerIndex = _mapLayer.find( layerName )->second;
        return binding;
    }

    void InputMap::commitBinding( const hashed_string& action, InputActionValueType valueType, const ActionBinding& binding )
    {
        ActionEntry& entry = getOrCreateAction( action, valueType );
        entry._listBinding.push_back( binding );
        entry._listDefaultBinding.push_back( binding );
        entry._listBindingState.push_back( makeBindingState( binding ) );
    }

    void InputMap::bind( const hashed_string& action, InputSlot slot, ActionTrigger trigger, const hashed_string& layer )
    {
        if ( action.empty() )
            return;

        ActionBinding binding = beginBinding( action, BindingKind::SingleSlot, trigger, layer );
        binding._arrSlot[0]   = slot;
        commitBinding( action, InputActionValueType::Boolean, binding );
    }

    void InputMap::bind( const hashed_string& action, Key key, ActionTrigger trigger, const hashed_string& layer )
    {
        if ( key == Key::Unknown )
            return;
        bind( action, InputSlot::fromKey( key ), trigger, layer );
    }

    void InputMap::bind( const hashed_string& action, GamepadButton button, ActionTrigger trigger, const hashed_string& layer )
    {
        if ( button == GamepadButton::Count )
            return;
        bind( action, InputSlot::fromGamepadButton( button ), trigger, layer );
    }

    void InputMap::bind( const hashed_string& action, MouseButton mouse, ActionTrigger trigger, const hashed_string& layer )
    {
        if ( mouse == MouseButton::Count )
            return;
        bind( action, InputSlot::fromMouseButton( mouse ), trigger, layer );
    }

    void InputMap::bindAxis1DComposite( const hashed_string& action, Key negativeKey, Key positiveKey, const hashed_string& layer, ActionTrigger trigger )
    {
        bindAxis1DComposite( action, InputSlot::fromKey( negativeKey ), InputSlot::fromKey( positiveKey ), layer, trigger );
    }

    void InputMap::bindAxis1DComposite( const hashed_string& action, InputSlot negativeSlot, InputSlot positiveSlot, const hashed_string& layer, ActionTrigger trigger )
    {
        if ( action.empty() )
            return;

        ActionBinding binding = beginBinding( action, BindingKind::Axis1DComposite, trigger, layer );
        binding._arrSlot[0]   = negativeSlot;
        binding._arrSlot[1]   = positiveSlot;
        commitBinding( action, InputActionValueType::Axis1D, binding );
    }

    float32 InputMap::getAxis1D( const hashed_string& action ) const
    {
        const ActionEntry* pEntry = findAction( action );
        if ( pEntry == nullptr )
            return 0.0f;
        float32 bounded  = 0.0f;
        float32 relative = 0.0f;
        for ( const ActionBinding& binding : pEntry->_listBinding )
        {
            float2 bVal{ 0.0f, 0.0f };
            if ( isBindingLayerActive( binding ) == false || evaluateBindingDown( binding, bVal ) == false )
                continue;
            float32& accumulator = BindingKinds::isRelative( binding ) ? relative : bounded;
            accumulator += binding._bNegate ? -bVal._x : bVal._x; // 프레임 평가(InputMapEvaluate)와 같은 부호 뒤집기
        }
        if ( _accessibility._bInvertX )
            bounded = -bounded;
        return MathUtil::clamp( bounded, -1.0f, 1.0f ) + relative;
    }

    void InputMap::bindVector2D( const hashed_string& action, Key up, Key down, Key left, Key right, float32 deadzone, const hashed_string& layer )
    {
        if ( action.empty() )
            return;

        ActionBinding binding = beginBinding( action, BindingKind::Vector2DComposite, ActionTrigger::Down, layer );
        binding._arrSlot[0]   = InputSlot::fromKey( up );
        binding._arrSlot[1]   = InputSlot::fromKey( down );
        binding._arrSlot[2]   = InputSlot::fromKey( left );
        binding._arrSlot[3]   = InputSlot::fromKey( right );
        binding._deadzone     = deadzone;
        commitBinding( action, InputActionValueType::Axis2D, binding );
    }

    void InputMap::bindGamepadStick2D( const hashed_string& action, GamepadStick stick, float32 deadzone, const hashed_string& layer, uint8 padIndex, float32 outerDeadzone, float32 responseExponent,
                                       float2 axisScale, bool bScaleByDelta, GamepadStickAxis axis )
    {
        if ( action.empty() )
            return;

        ActionBinding binding     = beginBinding( action, BindingKind::GamepadStick2D, ActionTrigger::Down, layer );
        binding._deviceIndex      = padIndex;
        binding._stick            = stick;
        binding._stickAxis        = axis;
        binding._deadzone         = deadzone;
        binding._outerDeadzone    = outerDeadzone;
        binding._responseExponent = responseExponent;
        binding._axisScale        = axisScale;
        binding._bScaleByDelta    = bScaleByDelta;
        commitBinding( action, axis == GamepadStickAxis::Both ? InputActionValueType::Axis2D : InputActionValueType::Axis1D, binding );
    }

    float2 InputMap::getVector2D( const hashed_string& action ) const
    {
        const ActionEntry* pEntry = findAction( action );
        if ( pEntry == nullptr )
            return float2{ 0.0f, 0.0f };

        float2 bounded{ 0.0f, 0.0f };
        float2 relative{ 0.0f, 0.0f };
        for ( const ActionBinding& binding : pEntry->_listBinding )
        {
            float2 bindingValue{ 0.0f, 0.0f };
            if ( isBindingLayerActive( binding ) && evaluateBindingDown( binding, bindingValue ) )
            {
                float2& accumulator = BindingKinds::isRelative( binding ) ? relative : bounded;
                accumulator._x += binding._bNegate ? -bindingValue._x : bindingValue._x; // 프레임 평가(InputMapEvaluate)와 같은 부호 뒤집기
                accumulator._y += binding._bNegate ? -bindingValue._y : bindingValue._y;
            }
        }
        return composeActionValue( bounded, relative );
    }

    void InputMap::bindMouseDelta( const hashed_string& action, float32 sensitivity, const hashed_string& layer )
    {
        if ( action.empty() )
            return;

        ActionBinding binding = beginBinding( action, BindingKind::MouseDelta2D, ActionTrigger::Down, layer );
        binding._scale        = sensitivity;
        commitBinding( action, InputActionValueType::Axis2D, binding );
    }

    void InputMap::bindMouseWheel( const hashed_string& action, float32 scale, const hashed_string& layer )
    {
        if ( action.empty() )
            return;

        ActionBinding binding = beginBinding( action, BindingKind::MouseWheel1D, ActionTrigger::Down, layer );
        binding._scale        = scale;
        commitBinding( action, InputActionValueType::Axis1D, binding );
    }

    void InputMap::bindVirtualJoystick2D( const hashed_string& action, MouseButton activationButton, float32 radius, float32 deadzone, const hashed_string& layer, float32 outerDeadzone )
    {
        if ( action.empty() || activationButton == MouseButton::Count )
            return;

        ActionBinding binding  = beginBinding( action, BindingKind::VirtualJoystick2D, ActionTrigger::Down, layer );
        binding._arrSlot[0]    = InputSlot::fromMouseButton( activationButton );
        binding._deadzone      = deadzone;
        binding._outerDeadzone = outerDeadzone;
        binding._scale         = radius;
        commitBinding( action, InputActionValueType::Axis2D, binding );
    }

    void InputMap::bindShortcut( const hashed_string& action, Key key, uint8 modifierMask, ActionTrigger trigger, const hashed_string& layer )
    {
        if ( action.empty() || key == Key::Unknown )
            return;

        ActionBinding binding = beginBinding( action, BindingKind::Shortcut, trigger, layer );
        binding._modifierMask = modifierMask;
        binding._arrSlot[0]   = InputSlot::fromKey( key );
        commitBinding( action, InputActionValueType::Boolean, binding );
    }

    void InputMap::bindAnyKey( const hashed_string& action, const hashed_string& layer )
    {
        if ( action.empty() )
            return;

        ActionBinding binding = beginBinding( action, BindingKind::AnyKey, ActionTrigger::Pressed, layer );
        commitBinding( action, InputActionValueType::Boolean, binding );
    }

    void InputMap::bindChord( const hashed_string& action, Key modifierKey, Key triggerKey, ActionTrigger trigger, const hashed_string& layer )
    {
        const bool bInvalidChordArguments = action.empty() || modifierKey == Key::Unknown || triggerKey == Key::Unknown;
        if ( bInvalidChordArguments )
            return;

        ActionBinding binding = beginBinding( action, BindingKind::Chord, trigger, layer );
        binding._arrSlot[0]   = InputSlot::fromKey( modifierKey );
        binding._arrSlot[1]   = InputSlot::fromKey( triggerKey );
        commitBinding( action, InputActionValueType::Boolean, binding );
    }

    bool InputMap::isChordDown( const hashed_string& action ) const
    {
        return hasActiveChord( action, false );
    }

    bool InputMap::wasChordTriggered( const hashed_string& action ) const
    {
        return hasActiveChord( action, true );
    }

    bool InputMap::hasActiveChord( const hashed_string& action, bool bTriggerJustPressed ) const
    {
        const ActionEntry* pEntry = findAction( action );
        if ( pEntry == nullptr || _pInput == nullptr )
            return false;

        for ( const ActionBinding& binding : pEntry->_listBinding )
        {
            if ( binding._kind == BindingKind::Chord && isBindingLayerActive( binding ) )
            {
                const Key  modifierKey = static_cast<Key>( binding._arrSlot[0]._controlIndex );
                const Key  triggerKey  = static_cast<Key>( binding._arrSlot[1]._controlIndex );
                const bool bTriggerOk  = bTriggerJustPressed ? wasKeyboardKeyPressed( triggerKey ) : isKeyboardKeyDown( triggerKey );
                if ( isKeyboardKeyDown( modifierKey ) && bTriggerOk )
                    return true;
            }
        }
        return false;
    }

    Subscription InputMap::bindActionCallback( const hashed_string& action, ActionCallbackDelegate callback )
    {
        if ( action.empty() || callback.isBound() == false )
            return {};
        return getOrCreateAction( action )._onTriggered.add( callback );
    }

    Subscription InputMap::bindPhaseCallback( const hashed_string& action, ActionPhase phase, ActionCallbackDelegate callback )
    {
        if ( action.empty() || callback.isBound() == false )
            return {};
        return getOrCreateAction( action )._onPhase.add( PhaseCallbackDelegate::create( [phase, callback]( ActionPhase currentPhase )
        {
            if ( currentPhase == phase )
                callback();
        } ) );
    }

    Subscription InputMap::bindVector2DCallback( const hashed_string& action, Vector2DCallbackDelegate callback )
    {
        if ( action.empty() || callback.isBound() == false )
            return {};
        return getOrCreateAction( action, InputActionValueType::Axis2D )._onVector2D.add( callback );
    }

    void InputMap::registerLayer( const hashed_string& name, int32 priority, bool enabled, bool blockLower, bool alwaysOn )
    {
        ensureLayer( name, priority, enabled, blockLower, alwaysOn );
    }

    void InputMap::setLayerEnabled( const hashed_string& layer, bool enabled )
    {
        LayerDef* pLayer = findLayer( layer );
        if ( pLayer != nullptr )
            pLayer->_bEnabled = enabled ? SW_TRUE : SW_FALSE;
    }

    void InputMap::pushLayer( const hashed_string& layer, bool blockLower, bool showCursor )
    {
        const hashed_string hLayer( layer );
        ensureLayer( hLayer, 0, true, blockLower );
        LayerDef* pLayer = findLayer( hLayer );
        if ( pLayer != nullptr )
        {
            pLayer->_bEnabled    = SW_TRUE;
            pLayer->_bBlockLower = blockLower ? SW_TRUE : SW_FALSE;
        }

        for ( auto it = _listLayerStack.begin(); it != _listLayerStack.end(); ++it )
        {
            if ( *it == hLayer )
            {
                _listLayerStack.erase( it );
                break;
            }
        }
        _listLayerStack.push_back( hLayer );
        if ( _pInput != nullptr && showCursor )
            _pInput->setCursorVisible( true );
    }

    void InputMap::popLayer()
    {
        if ( _listLayerStack.empty() == false )
            _listLayerStack.pop_back();
    }

    void InputMap::popLayer( const hashed_string& layer )
    {
        const hashed_string hLayer( layer );
        for ( auto it = _listLayerStack.rbegin(); it != _listLayerStack.rend(); ++it )
        {
            if ( *it == hLayer )
            {
                _listLayerStack.erase( std::next( it ).base() );
                break;
            }
        }
    }

    string_view InputMap::getCurrentTopLayer() const
    {
        if ( _listLayerStack.empty() == false )
            return _listLayerStack.back().view();
        return _defaultLayerName.view();
    }

    void InputMap::enableOnlyLayer( const hashed_string& layer )
    {
        const hashed_string hLayer( layer );
        for ( auto& [name, index] : _mapLayer )
        {
            LayerDef& def = _listLayerEntry[index];
            if ( def._bAlwaysOn == SW_TRUE )
                continue;
            def._bEnabled = ( name == hLayer ) ? SW_TRUE : SW_FALSE;
        }
        _listLayerStack.clear();
        _listLayerStack.push_back( hLayer );
    }

    bool InputMap::isActionToggled( const hashed_string& action ) const
    {
        const ActionEntry* pEntry = findAction( action );
        return pEntry != nullptr && pEntry->_bToggleState == SW_TRUE;
    }

    bool InputMap::rebindKey( const hashed_string& action, Key newKey, uint32 bindIndex )
    {
        return rebindSlot( action, InputSlot::fromKey( newKey ), bindIndex );
    }

    bool InputMap::rebindSlot( const hashed_string& action, InputSlot slot, uint32 bindIndex )
    {
        ActionEntry* pEntry = findAction( action );
        if ( pEntry == nullptr || bindIndex >= pEntry->_listBinding.size() )
            return false;

        // 종류는 그대로 두고 그 종류에서 키가 들어가는 슬롯만 바꾼다(`rebindKey` 설명 참고). 단일 키로 바꾸면 수식 키와 축이 사라진다.
        ActionBinding& binding   = pEntry->_listBinding[bindIndex];
        const uint32   slotIndex = BindingKinds::getRebindSlotIndex( binding._kind );
        if ( slotIndex == BindingKinds::kNoRebindSlot )
        {
            SW_LOG_WARNING_ID( "InputMap.RebindBindingBindingOne", "rebind: binding %# of '%#' is a '%#' binding - one key cannot replace it (bind it again, or edit its parts)", bindIndex,
                               action.c_str(), BindingKinds::toName( binding._kind ) );
            return false;
        }
        // 맵 읽기와 같은 검사 — 가상 커서가 마우스로 바꾸는 스틱 · 버튼으로는 다시 묶지 않는다(사용자 설정의 키 바인딩도 이 길이다).
        ActionBinding candidate       = binding;
        candidate._arrSlot[slotIndex] = slot;
        if ( reportVirtualCursorConflict( "rebind", action, candidate ) )
            return false;
        binding = candidate;
        reseedBindingState( *pEntry, bindIndex );
        return true;
    }

    bool InputMap::rebindKeyShortcut( const hashed_string& action, uint32 bindIndex, Key key, uint8 modifierMask )
    {
        ActionEntry* pEntry      = findAction( action );
        const bool   bValidChord = key != Key::Unknown && ( modifierMask & ~ModifierKey::All ) == 0;
        const bool   bNoBinding  = pEntry == nullptr || bindIndex >= pEntry->_listBinding.size();
        if ( bNoBinding || bValidChord == false )
            return false;
        ActionBinding& binding        = pEntry->_listBinding[bindIndex];
        const bool     bKeyboardSlot  = binding._arrSlot[0]._deviceKind == InputDeviceKind::Keyboard;
        const bool     bKeyOrShortcut = binding._kind == BindingKind::SingleSlot || binding._kind == BindingKind::Shortcut;
        if ( bKeyboardSlot == false || bKeyOrShortcut == false )
        {
            SW_LOG_WARNING_ID( "InputMap.RebindBindingNotKeyboard", "rebind: binding %# of '%#' is not a keyboard key or shortcut - a key with modifiers cannot replace it", bindIndex, action.c_str() );
            return false;
        }
        // 수정 키가 없으면 단일 키, 있으면 단축키다 — 읽기(`<bind modifier>`)와 같은 규칙이라 저장 · 다시 읽기에서 종류가 바뀌지 않는다.
        binding._kind         = modifierMask == ModifierKey::None ? BindingKind::SingleSlot : BindingKind::Shortcut;
        binding._modifierMask = modifierMask;
        binding._arrSlot[0]   = InputSlot::fromKey( key );
        reseedBindingState( *pEntry, bindIndex );
        return true;
    }

    bool InputMap::swapKeyShortcut( const hashed_string& action, uint32 bindIndex, const hashed_string& otherAction, Key key, uint8 modifierMask )
    {
        const ActionBinding* pBefore = getBinding( action, bindIndex );
        if ( pBefore == nullptr )
            return false;
        const ActionBinding before         = *pBefore;
        const InputSlot     keySlot        = InputSlot::fromKey( key );
        uint32              otherBindIndex = 0;
        const uint32        otherCount     = getBindingCount( otherAction );
        while ( otherBindIndex < otherCount )
        {
            const ActionBinding* pOther = getBinding( otherAction, otherBindIndex );
            if ( pOther->_arrSlot[0] == keySlot && pOther->_modifierMask == modifierMask )
                break;
            ++otherBindIndex;
        }
        if ( otherBindIndex == otherCount || rebindKeyShortcut( action, bindIndex, key, modifierMask ) == false )
            return false;
        if ( rebindKeyShortcut( otherAction, otherBindIndex, static_cast<Key>( before._arrSlot[0]._controlIndex ), before._modifierMask ) )
            return true;
        (void)rebindKeyShortcut( action, bindIndex, static_cast<Key>( before._arrSlot[0]._controlIndex ), before._modifierMask ); // 방금 있던 조합이다
        return false;
    }

    bool InputMap::findSameLayerInputConflict( const hashed_string& action, uint32 bindIndex, hashed_string& outOtherAction ) const
    {
        const ActionBinding* pBinding = getBinding( action, bindIndex );
        if ( pBinding == nullptr )
            return false;
        const uint32 slotCount = BindingKinds::getConflictSlotCount( pBinding->_kind );
        if ( slotCount == 0 )
            return false;
        for ( const hashed_string& otherName : _listActionName )
        {
            const ActionEntry* pOther = otherName == action ? nullptr : findAction( otherName );
            if ( pOther == nullptr )
                continue;
            for ( const ActionBinding& other : pOther->_listBinding )
            {
                bool bSameInput = other._kind == pBinding->_kind && other._layer == pBinding->_layer && other._trigger == pBinding->_trigger &&
                                  other._modifierMask == pBinding->_modifierMask;
                for ( uint32 slotIndex = 0; bSameInput && slotIndex < slotCount; ++slotIndex )
                {
                    bSameInput = other._arrSlot[slotIndex] == pBinding->_arrSlot[slotIndex];
                }
                if ( bSameInput )
                {
                    outOtherAction = otherName;
                    return true;
                }
            }
        }
        return false;
    }

    const ActionBinding* InputMap::getDefaultBinding( const hashed_string& action, uint32 bindIndex ) const
    {
        const ActionEntry* pEntry = findAction( action );
        if ( pEntry == nullptr || bindIndex >= pEntry->_listDefaultBinding.size() )
            return nullptr;
        return &pEntry->_listDefaultBinding[bindIndex];
    }

    bool InputMap::rebindWithResolution( const hashed_string& action, InputSlot newSlot, ConflictResolution strategy, uint32 bindIndex )
    {
        ActionEntry* pEntry = findAction( action );
        if ( pEntry == nullptr || bindIndex >= pEntry->_listBinding.size() )
            return false;

        // 대상의 어느 슬롯을 바꾸는지는 `rebindSlot` 과 같은 규칙이다(Chord 는 방아쇠, 키 하나로 못 바꾸는 종류는 거절). 늘 0 번에 쓰면
        // Chord 의 수식 키를 덮어쓰고, 겹침도 다른 액션의 0 번 슬롯만 보면 Chord 의 방아쇠나 축의 양의 키와 겹쳐도 알 수 없다.
        const uint32 targetSlot = BindingKinds::getRebindSlotIndex( pEntry->_listBinding[bindIndex]._kind );
        if ( targetSlot == BindingKinds::kNoRebindSlot )
        {
            SW_LOG_WARNING_ID( "InputMap.RebindBindingBindingOne", "rebind: binding %# of '%#' is a '%#' binding - one key cannot replace it (bind it again, or edit its parts)", bindIndex,
                               action.c_str(), BindingKinds::toName( pEntry->_listBinding[bindIndex]._kind ) );
            return false;
        }
        ActionBinding candidate        = pEntry->_listBinding[bindIndex];
        candidate._arrSlot[targetSlot] = newSlot;
        if ( reportVirtualCursorConflict( "rebind", action, candidate ) )
            return false;
        const hashed_string targetLayer = pEntry->_listBinding[bindIndex]._layer;

        hashed_string conflictingAction{};
        uint32        conflictingBindIndex = 0;
        uint32        conflictingSlot      = BindingKinds::kNoRebindSlot;
        for ( auto& [actName, actIndex] : _mapAction )
        {
            if ( actName == action )
                continue;
            const ActionEntry& entry = _listActionEntry[actIndex];
            for ( uint32 bIdx = 0; bIdx < entry._listBinding.size(); ++bIdx )
            {
                if ( entry._listBinding[bIdx]._layer != targetLayer )
                    continue;
                conflictingSlot = findConflictingSlot( entry._listBinding[bIdx], newSlot );
                if ( conflictingSlot != BindingKinds::kNoRebindSlot )
                {
                    conflictingAction    = actName;
                    conflictingBindIndex = bIdx;
                    break;
                }
            }
            if ( conflictingAction.empty() == false )
                break;
        }

        if ( conflictingAction.empty() == false )
        {
            ActionEntry* pConflictEntry = findAction( conflictingAction );
            if ( strategy == ConflictResolution::Swap && pConflictEntry != nullptr )
            {
                const InputSlot oldSlot                                                      = pEntry->_listBinding[bindIndex]._arrSlot[targetSlot];
                pConflictEntry->_listBinding[conflictingBindIndex]._arrSlot[conflictingSlot] = oldSlot;
                pEntry->_listBinding[bindIndex]._arrSlot[targetSlot]                         = newSlot;
                reseedBindingState( *pEntry, bindIndex );
                reseedBindingState( *pConflictEntry, conflictingBindIndex );
                return true;
            }
            else if ( strategy == ConflictResolution::Override && pConflictEntry != nullptr )
            {
                pConflictEntry->_listBinding[conflictingBindIndex]._arrSlot[conflictingSlot] = InputSlot{};
                pEntry->_listBinding[bindIndex]._arrSlot[targetSlot]                         = newSlot;
                reseedBindingState( *pEntry, bindIndex );
                reseedBindingState( *pConflictEntry, conflictingBindIndex );
                return true;
            }
            else if ( strategy == ConflictResolution::AddSecondary )
            {
                ActionBinding newBinding        = pEntry->_listBinding[bindIndex];
                newBinding._arrSlot[targetSlot] = newSlot;
                pEntry->_listBinding.push_back( newBinding );
                pEntry->_listBindingState.push_back( makeBindingState( newBinding ) );
                return true;
            }
        }

        pEntry->_listBinding[bindIndex]._arrSlot[targetSlot] = newSlot;
        reseedBindingState( *pEntry, bindIndex );
        return true;
    }

    bool InputMap::findRebindSlot( const hashed_string& action, uint32 bindIndex, InputSlot& outSlot ) const
    {
        const ActionEntry* pEntry = findAction( action );
        if ( pEntry == nullptr || bindIndex >= pEntry->_listBinding.size() )
            return false;
        const ActionBinding& binding   = pEntry->_listBinding[bindIndex];
        const uint32         slotIndex = BindingKinds::getRebindSlotIndex( binding._kind );
        if ( slotIndex == BindingKinds::kNoRebindSlot )
            return false;
        outSlot = binding._arrSlot[slotIndex];
        return true;
    }

    bool InputMap::findDefaultRebindSlot( const hashed_string& action, uint32 bindIndex, InputSlot& outSlot ) const
    {
        const ActionEntry* pEntry = findAction( action );
        if ( pEntry == nullptr || bindIndex >= pEntry->_listDefaultBinding.size() )
            return false;
        const ActionBinding& binding   = pEntry->_listDefaultBinding[bindIndex];
        const uint32         slotIndex = BindingKinds::getRebindSlotIndex( binding._kind );
        if ( slotIndex == BindingKinds::kNoRebindSlot )
            return false;
        outSlot = binding._arrSlot[slotIndex];
        return true;
    }

    hashed_string InputMap::findBindingLayer( const hashed_string& action, uint32 bindIndex ) const
    {
        const ActionEntry* pEntry = findAction( action );
        if ( pEntry == nullptr || bindIndex >= pEntry->_listBinding.size() )
            return {};
        return pEntry->_listBinding[bindIndex]._layer;
    }

    bool InputMap::hasBindingConflict( const InputSlot& slot, const hashed_string& layer, string& outConflictingAction ) const
    {
        const hashed_string targetLayer = layer.empty() ? _defaultLayerName : hashed_string( layer );

        for ( const auto& [actionName, actIndex] : _mapAction )
        {
            const ActionEntry& entry = _listActionEntry[actIndex];
            for ( const ActionBinding& binding : entry._listBinding )
            {
                if ( canLayersFireTogether( binding._layer, targetLayer ) == false )
                    continue;

                // 슬롯 수는 테이블에서 가져온다. switch 의 `default:` 로 두면 새 종류를 추가했을 때 충돌 검사가 그 바인딩을
                // 보지 못한 채 지나가고, 같은 키를 두 번 바인딩해도 조용하다.
                const uint32 slotCount = BindingKinds::getConflictSlotCount( binding._kind );

                for ( uint32 slotIndex = 0; slotIndex < slotCount; ++slotIndex )
                {
                    if ( binding._arrSlot[slotIndex] == slot )
                    {
                        outConflictingAction = actionName.c_str();
                        return true;
                    }
                }
            }
        }
        return false;
    }

    bool InputMap::resetActionToDefault( const hashed_string& action )
    {
        ActionEntry* pEntry = findAction( action );
        if ( pEntry == nullptr || pEntry->_listDefaultBinding.empty() )
            return false;

        pEntry->_listBinding = pEntry->_listDefaultBinding;
        rebuildBindingStates( *pEntry );
        return true;
    }

    void InputMap::resetAllBindingsToDefault()
    {
        for ( ActionEntry& entry : _listActionEntry )
        {
            if ( entry._listDefaultBinding.empty() == false )
            {
                entry._listBinding = entry._listDefaultBinding;
                rebuildBindingStates( entry );
            }
        }
    }

    bool InputMap::setDoubleClickTime( float32 seconds )
    {
        const bool bValid = seconds >= InputMapDefaults::kDoubleClickTimeMin && seconds <= InputMapDefaults::kDoubleClickTimeMax;
        if ( bValid == false )
        {
            SW_LOG_ERROR_ID( "InputMap.InputMapRefusedDoubleClick", "InputMap: refused double click time %# - it must be within [%#, %#]", seconds, InputMapDefaults::kDoubleClickTimeMin, InputMapDefaults::kDoubleClickTimeMax );
            return false;
        }
        _triggerRules._doubleClickTime = seconds;
        return true;
    }

    bool InputMap::setDoubleClickMaxDistance( float32 pixels )
    {
        const bool bValid = pixels >= 0.0f && pixels <= InputMapDefaults::kDoubleClickDistanceMax;
        if ( bValid == false )
        {
            SW_LOG_ERROR_ID( "InputMap.InputMapRefusedDoubleClick2", "InputMap: refused double click distance %# - it must be within [0, %#]", pixels, InputMapDefaults::kDoubleClickDistanceMax );
            return false;
        }
        _triggerRules._doubleClickMaxDistance = pixels;
        return true;
    }

    bool InputMap::setDragThreshold( float32 pixels )
    {
        const bool bValid = pixels >= 0.0f && pixels <= InputMapDefaults::kDragThresholdMax;
        if ( bValid == false )
        {
            SW_LOG_ERROR_ID( "InputMap.InputMapRefusedDragThreshold", "InputMap: refused drag threshold %# - it must be within [0, %#]", pixels, InputMapDefaults::kDragThresholdMax );
            return false;
        }
        _triggerRules._dragThreshold = pixels;
        return true;
    }

    bool InputMap::setHoldThreshold( float32 seconds )
    {
        const bool bValid = seconds >= InputMapDefaults::kHoldThresholdMin && seconds <= InputMapDefaults::kHoldThresholdMax;
        if ( bValid == false )
        {
            SW_LOG_ERROR_ID( "InputMap.InputMapRefusedHoldThreshold", "InputMap: refused hold threshold %# - it must be within [%#, %#]", seconds, InputMapDefaults::kHoldThresholdMin, InputMapDefaults::kHoldThresholdMax );
            return false;
        }
        _triggerRules._holdThreshold = seconds;
        return true;
    }

    bool InputMap::hasLayer( const hashed_string& layer ) const
    {
        return _mapLayer.find( layer ) != _mapLayer.end();
    }

    bool InputMap::isLayerEnabled( const hashed_string& layer ) const
    {
        return isLayerActiveInternal( layer );
    }

    bool InputMap::canLayersFireTogether( const hashed_string& layerA, const hashed_string& layerB ) const
    {
        return layerA == layerB || ( isLayerActiveInternal( layerA ) && isLayerActiveInternal( layerB ) );
    }

    uint32 InputMap::getActionSourceIndex( const hashed_string& action ) const
    {
        const ActionEntry* pEntry = findAction( action );
        return pEntry != nullptr ? pEntry->_sourceIndex : 0;
    }

    bool InputMap::isActionOverride( const hashed_string& action ) const
    {
        const ActionEntry* pEntry = findAction( action );
        return pEntry != nullptr && pEntry->_bOverride == SW_TRUE;
    }

    int32 InputMap::getLayerPriority( const hashed_string& layer ) const
    {
        const LayerDef* pLayer = findLayer( layer );
        return pLayer != nullptr ? pLayer->_priority : 0;
    }

    bool InputMap::hasAction( const hashed_string& action ) const
    {
        return _mapAction.find( action ) != _mapAction.end();
    }

    ActionTrigger InputMap::getBindingTrigger( const hashed_string& action, uint32 bindIndex ) const
    {
        const ActionEntry* pEntry = findAction( action );
        if ( pEntry == nullptr || bindIndex >= pEntry->_listBinding.size() )
            return ActionTrigger::Pressed;
        return pEntry->_listBinding[bindIndex]._trigger;
    }

    bool InputMap::setBindingTrigger( const hashed_string& action, uint32 bindIndex, ActionTrigger trigger )
    {
        ActionEntry* pEntry                = findAction( action );
        const bool   bInvalidTriggerRebind = pEntry == nullptr || bindIndex >= pEntry->_listBinding.size() || trigger >= ActionTrigger::Count;
        if ( bInvalidTriggerRebind )
            return false;
        pEntry->_listBinding[bindIndex]._trigger = trigger;
        return true;
    }

    bool InputMap::setBindingTriggerTime( const hashed_string& action, uint32 bindIndex, float32 seconds )
    {
        ActionEntry* pEntry = findAction( action );
        if ( pEntry == nullptr || bindIndex >= pEntry->_listBinding.size() )
            return false;
        const float32 triggerTime                    = ( seconds > 0.0f ) ? seconds : 0.0f;
        pEntry->_listBinding[bindIndex]._triggerTime = triggerTime;
        if ( bindIndex < pEntry->_listDefaultBinding.size() )
            pEntry->_listDefaultBinding[bindIndex]._triggerTime = triggerTime;
        return true;
    }

    bool InputMap::setBindingNegate( const hashed_string& action, uint32 bindIndex, bool bNegate )
    {
        ActionEntry* pEntry = findAction( action );
        if ( pEntry == nullptr || bindIndex >= pEntry->_listBinding.size() )
            return false;
        pEntry->_listBinding[bindIndex]._bNegate = bNegate;
        if ( bindIndex < pEntry->_listDefaultBinding.size() )
            pEntry->_listDefaultBinding[bindIndex]._bNegate = bNegate;
        return true;
    }

    bool InputMap::setBindingTapCount( const hashed_string& action, uint32 bindIndex, uint8 tapCount )
    {
        ActionEntry* pEntry = findAction( action );
        if ( pEntry == nullptr || bindIndex >= pEntry->_listBinding.size() )
            return false;
        const uint8 clampedCount                  = ( tapCount < 2 ) ? static_cast<uint8>( 2 ) : tapCount;
        pEntry->_listBinding[bindIndex]._tapCount = clampedCount;
        if ( bindIndex < pEntry->_listDefaultBinding.size() )
            pEntry->_listDefaultBinding[bindIndex]._tapCount = clampedCount;
        return true;
    }

    bool InputMap::setBindingLayer( const hashed_string& action, uint32 bindIndex, const hashed_string& layer )
    {
        ActionEntry* pEntry  = findAction( action );
        const auto   itLayer = _mapLayer.find( layer );
        if ( pEntry == nullptr || bindIndex >= pEntry->_listBinding.size() || itLayer == _mapLayer.end() )
            return false;
        pEntry->_listBinding[bindIndex]._layer            = layer;
        pEntry->_listBinding[bindIndex]._cachedLayerIndex = itLayer->second;
        if ( bindIndex < pEntry->_listDefaultBinding.size() )
        {
            pEntry->_listDefaultBinding[bindIndex]._layer            = layer;
            pEntry->_listDefaultBinding[bindIndex]._cachedLayerIndex = itLayer->second;
        }
        reseedBindingState( *pEntry, bindIndex );
        return true;
    }

    bool InputMap::removeBinding( const hashed_string& action, uint32 bindIndex )
    {
        ActionEntry* pEntry = findAction( action );
        if ( pEntry == nullptr || bindIndex >= pEntry->_listBinding.size() )
            return false;
        pEntry->_listBinding.erase( pEntry->_listBinding.begin() + bindIndex );
        pEntry->_listBindingState.erase( pEntry->_listBindingState.begin() + bindIndex );
        if ( bindIndex < pEntry->_listDefaultBinding.size() )
            pEntry->_listDefaultBinding.erase( pEntry->_listDefaultBinding.begin() + bindIndex );
        return true;
    }

    bool InputMap::removeAction( const hashed_string& action )
    {
        const auto it = _mapAction.find( action );
        if ( it == _mapAction.end() )
            return false;
        ActionEntry& entry = _listActionEntry[it->second];
        // 덮은 액션을 지우면 앞 파일의 정의가 다시 읽힌다 — 저장이 덮던 파일에 지우기로 남긴다. 앞 파일에는 원래 정의가 남아 있다(`_listShadowed`).
        if ( entry._bOverride == SW_TRUE && entry._sourceIndex < _listSource.size() )
            _listSource[entry._sourceIndex]._listRemove.push_back( action );
        entry._listBinding.clear();
        entry._listBindingState.clear();
        entry._listDefaultBinding.clear();
        entry._bOverride  = SW_FALSE;
        entry._generation = ++_nextGeneration;
        _mapAction.erase( it );
        (void)VectorUtil::removeFirst( _listActionName, action ); // 목록 순서가 저장 순서라 순서를 지키며 지운다
        return true;
    }

    uint32 InputMap::getBindingCount( const hashed_string& action ) const
    {
        const ActionEntry* pEntry = findAction( action );
        return pEntry != nullptr ? static_cast<uint32>( pEntry->_listBinding.size() ) : 0;
    }

    const ActionBinding* InputMap::getBinding( const hashed_string& action, uint32 bindIndex ) const
    {
        const ActionEntry* pEntry = findAction( action );
        if ( pEntry == nullptr || bindIndex >= pEntry->_listBinding.size() )
            return nullptr;
        return &pEntry->_listBinding[bindIndex];
    }

    bool InputMap::wasActionTriggered( const hashed_string& action ) const
    {
        const ActionEntry* pEntry = findAction( action );
        return pEntry != nullptr && pEntry->_bTriggered == SW_TRUE;
    }

    bool InputMap::isActionDown( const hashed_string& action ) const
    {
        const ActionEntry* pEntry = findAction( action );
        return pEntry != nullptr && pEntry->_bDown == SW_TRUE;
    }

    ActionHandle InputMap::getActionHandle( const hashed_string& action ) const
    {
        const ActionEntry* pEntry = findAction( action );
        if ( pEntry != nullptr && pEntry->_handleIndex != ActionHandle::kInvalidIndex )
            return ActionHandle{ pEntry->_handleIndex, pEntry->_generation };
        return ActionHandle{};
    }

    const InputMap::ActionEntry* InputMap::getActionFromHandle( ActionHandle handle ) const
    {
        if ( handle._index < _listActionEntry.size() )
        {
            const ActionEntry& entry = _listActionEntry[handle._index];
            if ( entry._generation == handle._generation )
                return &entry;
        }
        return nullptr;
    }

    bool InputMap::wasActionTriggered( ActionHandle handle ) const
    {
        const ActionEntry* pEntry = getActionFromHandle( handle );
        return pEntry != nullptr && pEntry->_bTriggered == SW_TRUE;
    }

    bool InputMap::isActionDown( ActionHandle handle ) const
    {
        const ActionEntry* pEntry = getActionFromHandle( handle );
        return pEntry != nullptr && pEntry->_bDown == SW_TRUE;
    }

    bool InputMap::wasActionPressed( const hashed_string& action ) const
    {
        const ActionEntry* pEntry = findAction( action );
        return pEntry != nullptr && pEntry->_bPressed == SW_TRUE;
    }

    bool InputMap::wasActionPressed( ActionHandle handle ) const
    {
        const ActionEntry* pEntry = getActionFromHandle( handle );
        return pEntry != nullptr && pEntry->_bPressed == SW_TRUE;
    }

    bool InputMap::tryGetActionPressInstant( ActionHandle handle, InputPressInstant& outInstant ) const
    {
        const ActionEntry* pEntry      = getActionFromHandle( handle );
        const bool         bNotPressed = pEntry == nullptr || pEntry->_bPressed == SW_FALSE || _pInput == nullptr;
        if ( bNotPressed )
            return false;
        bool bFound = false;
        for ( size_t bindingIndex = 0; bindingIndex < pEntry->_listBinding.size(); ++bindingIndex )
        {
            if ( pEntry->_listBindingState[bindingIndex]._bPressed == SW_FALSE )
                continue;
            const ActionBinding& binding = pEntry->_listBinding[bindingIndex];
            // 바인딩이 완성된 순간 — 단일 슬롯은 그 슬롯, 조합 키(수정자 + 트리거)·단축키는 이번 프레임에 눌린 슬롯 중 늦은 것이다.
            int32 slotCount = 0;
            if ( binding._kind == BindingKind::SingleSlot || binding._kind == BindingKind::Shortcut )
                slotCount = 1;
            else if ( binding._kind == BindingKind::Chord )
                slotCount = 2;
            bool              bBindingFound = false;
            InputPressInstant bindingInstant{};
            for ( int32 slotIndex = 0; slotIndex < slotCount; ++slotIndex )
            {
                InputPressInstant slotInstant{};
                if ( _pInput->tryGetPressInstant( binding._arrSlot[slotIndex], slotInstant ) == false )
                    continue;
                if ( bBindingFound == false || slotInstant._frameFraction > bindingInstant._frameFraction )
                    bindingInstant = slotInstant;
                bBindingFound = true;
            }
            if ( bBindingFound == false )
                continue;
            if ( bFound == false || bindingInstant._frameFraction < outInstant._frameFraction )
                outInstant = bindingInstant;
            bFound = true;
        }
        return bFound;
    }

    bool InputMap::wasActionReleased( const hashed_string& action ) const
    {
        const ActionEntry* pEntry = findAction( action );
        return pEntry != nullptr && pEntry->_bReleased == SW_TRUE;
    }

    bool InputMap::wasActionReleased( ActionHandle handle ) const
    {
        const ActionEntry* pEntry = getActionFromHandle( handle );
        return pEntry != nullptr && pEntry->_bReleased == SW_TRUE;
    }

    float32 InputMap::getActionHoldDuration( const hashed_string& action ) const
    {
        const ActionEntry* pEntry = findAction( action );
        return pEntry != nullptr ? pEntry->_holdDuration : 0.0f;
    }

    float32 InputMap::getActionHoldDuration( ActionHandle handle ) const
    {
        const ActionEntry* pEntry = getActionFromHandle( handle );
        return pEntry != nullptr ? pEntry->_holdDuration : 0.0f;
    }

    float2 InputMap::getVector2D( ActionHandle handle ) const
    {
        const ActionEntry* pEntry = getActionFromHandle( handle );
        return pEntry != nullptr ? pEntry->_currentValue : float2{ 0.0f, 0.0f };
    }

    ActionPhase InputMap::getActionPhase( const hashed_string& action ) const
    {
        const ActionEntry* pEntry = findAction( action );
        return pEntry != nullptr ? pEntry->_currentPhase : ActionPhase::None;
    }

    ActionPhase InputMap::getActionPhase( ActionHandle handle ) const
    {
        const ActionEntry* pEntry = getActionFromHandle( handle );
        return pEntry != nullptr ? pEntry->_currentPhase : ActionPhase::None;
    }

    void InputMap::ensureActionListed( const hashed_string& action )
    {
        for ( const hashed_string& name : _listActionName )
        {
            if ( name == action )
                return;
        }
        _listActionName.push_back( action );
    }

    LayerDef& InputMap::ensureLayer( const hashed_string& name, int32 priority, bool enabled, bool blockLower, bool alwaysOn )
    {
        auto it = _mapLayer.find( name );
        if ( it != _mapLayer.end() )
            return _listLayerEntry[it->second];

        LayerDef def{};
        def._name        = name;
        def._priority    = priority;
        def._bEnabled    = enabled ? SW_TRUE : SW_FALSE;
        def._bBlockLower = blockLower ? SW_TRUE : SW_FALSE;
        def._bAlwaysOn   = alwaysOn ? SW_TRUE : SW_FALSE;
        def._sourceIndex = _currentSourceIndex;

        _listLayerName.push_back( def._name );
        const uint32 index = static_cast<uint32>( _listLayerEntry.size() );
        _listLayerEntry.push_back( def );
        _mapLayer.emplace( name, index );
        return _listLayerEntry[index];
    }

    InputMap::ActionEntry& InputMap::getOrCreateAction( const hashed_string& action, InputActionValueType valueType )
    {
        ensureActionListed( action );
        auto it = _mapAction.find( action );
        if ( it == _mapAction.end() )
        {
            const uint32 index = static_cast<uint32>( _listActionEntry.size() );

            ActionEntry entry{};
            entry._valueType   = valueType;
            entry._handleIndex = index;
            entry._generation  = ++_nextGeneration;
            entry._sourceIndex = _currentSourceIndex;
            _listActionEntry.push_back( std::move( entry ) );
            _mapAction.emplace( action, index );
            return _listActionEntry[index];
        }

        ActionEntry& entry = _listActionEntry[it->second];
        if ( entry._valueType == InputActionValueType::Boolean && valueType != InputActionValueType::Boolean )
            entry._valueType = valueType;
        return entry;
    }

    LayerDef* InputMap::findLayer( const hashed_string& name )
    {
        auto it = _mapLayer.find( name );
        return it != _mapLayer.end() ? &_listLayerEntry[it->second] : nullptr;
    }

    const LayerDef* InputMap::findLayer( const hashed_string& name ) const
    {
        auto it = _mapLayer.find( name );
        return it != _mapLayer.end() ? &_listLayerEntry[it->second] : nullptr;
    }

    InputMap::ActionEntry* InputMap::findAction( const hashed_string& action )
    {
        auto it = _mapAction.find( action );
        return it != _mapAction.end() ? &_listActionEntry[it->second] : nullptr;
    }

    const InputMap::ActionEntry* InputMap::findAction( const hashed_string& action ) const
    {
        auto it = _mapAction.find( action );
        return it != _mapAction.end() ? &_listActionEntry[it->second] : nullptr;
    }

    void InputMap::getDebugActionStates( vector<DebugActionState>& outListState ) const
    {
        outListState.clear();
        outListState.reserve( _mapAction.size() );
        for ( const auto& [actName, actIndex] : _mapAction )
        {
            const ActionEntry& entry = _listActionEntry[actIndex];
            DebugActionState   state{};
            state._action       = actName;
            state._layer        = entry._listBinding.empty() ? _defaultLayerName : entry._listBinding[0]._layer;
            state._valueType    = entry._valueType;
            state._phase        = entry._currentPhase;
            state._value        = entry._currentValue;
            state._holdDuration = entry._holdDuration;
            state._bTriggered   = entry._bTriggered;
            state._bDown        = entry._bDown;
            outListState.push_back( state );
        }
    }

    void InputMap::update( float32 deltaSeconds )
    {
        _totalElapsedTime += deltaSeconds;
        _lastDeltaSeconds = deltaSeconds;

        _actionBuffer.update( deltaSeconds ); // 선입력 만료 · 오래된 커맨드 이력 정리

        if ( _pInput == nullptr )
            return;

        const int2 curMousePos = _pInput->getMousePosition();

        // 액션마다 런타임 평가와 ActionPhase 상태 머신을 돌린다
        for ( auto& [actionName, actIndex] : _mapAction )
        {
            ActionEntry& actionEntry = _listActionEntry[actIndex];

            const size_t bindCount = actionEntry._listBinding.size();
            if ( actionEntry._listBindingState.size() != bindCount )
                rebuildBindingStates( actionEntry );

            ActionFrameAccum accum{};
            for ( size_t bindIndex = 0; bindIndex < bindCount; ++bindIndex )
            {
                updateBinding( actionEntry._listBinding[bindIndex], actionEntry._listBindingState[bindIndex], deltaSeconds, curMousePos, accum );
            }

            const bool bPrevDown      = actionEntry._bDown == SW_TRUE;
            actionEntry._bDown        = accum._bAnyDown ? SW_TRUE : SW_FALSE;
            actionEntry._bPressed     = accum._bAnyPressed ? SW_TRUE : SW_FALSE;
            actionEntry._bReleased    = accum._bAnyReleased ? SW_TRUE : SW_FALSE;
            actionEntry._bTriggered   = accum._bAnyTriggered ? SW_TRUE : SW_FALSE;
            actionEntry._holdDuration = accum._maxHold;

            // 모디파이어를 적용한다(축 반전, 클램핑 또는 원형 정규화 — 마우스 이동량은 빼고).
            actionEntry._currentValue = composeActionValue( accum._totalValue, accum._relativeValue );

            advanceActionPhase( actionEntry, bPrevDown, accum._bAnyHoldReached );

            // 접근성 토글 — 누른 프레임이나 켜진 동안만 목록을 본다. 토글에서 빠진 액션은 꺼진다.
            const bool bToggleCandidate = actionEntry._bPressed == SW_TRUE || actionEntry._bToggleState == SW_TRUE;
            if ( bToggleCandidate && _accessibility.isToggleAction( actionName ) == false )
                actionEntry._bToggleState = SW_FALSE;
            else if ( actionEntry._bPressed == SW_TRUE )
                actionEntry._bToggleState = ( actionEntry._bToggleState == SW_TRUE ) ? SW_FALSE : SW_TRUE;

            // 커맨드 이력 기록
            if ( actionEntry._bTriggered == SW_TRUE )
                _actionBuffer.recordTriggered( actionName );

            dispatchActionCallbacks( actionEntry, bPrevDown );
        }
    }

    void InputMap::updateBinding( const ActionBinding& binding, InputBindingState& state, float32 deltaSeconds, const int2& mousePos, ActionFrameAccum& accum ) const
    {
        // 꺼진 레이어의 바인딩이나 콘솔이 가린 키도 장치를 읽어 눌림을 추적한다. 다시 보이는 프레임에 이미 눌려 있던 입력을 새 누름으로 읽지 않기 위해서다(`InputTriggerRules::advance`).
        const bool bLayerActive = isBindingLayerActive( binding );
        float2     deviceValue{ 0.0f, 0.0f };
        float2     bindingValue{ 0.0f, 0.0f };
        const bool bPhysicalDown = evaluateBindingDown( binding, deviceValue, SlotRead::Device );
        const bool bRawDown      = bLayerActive && evaluateBindingDown( binding, bindingValue, SlotRead::Game );
        if ( binding._bNegate )
        {
            bindingValue._x = -bindingValue._x;
            bindingValue._y = -bindingValue._y;
        }
        InputBindingSample sample;
        sample._mousePos      = mousePos;
        sample._bDown         = bRawDown;
        sample._bPhysicalDown = bPhysicalDown;
        // 장치는 눌려 있는데 레이어가 켜진 게임이 안 눌림으로 보면 콘솔 · 포커스가 눌린 키를 가린 것이다.
        sample._bLive = bLayerActive && ( bPhysicalDown == false || bRawDown );
        _triggerRules.advance( binding, state, deltaSeconds, sample );

        if ( state._bDown == SW_TRUE )
            accum._bAnyDown = true;
        if ( state._bPressed == SW_TRUE )
            accum._bAnyPressed = true;
        if ( state._bReleased == SW_TRUE )
            accum._bAnyReleased = true;
        if ( state._bTriggered == SW_TRUE )
            accum._bAnyTriggered = true;
        if ( state._holdDuration > accum._maxHold )
            accum._maxHold = state._holdDuration;
        // 뗀 순간 Completed/Canceled 판정은 바인딩마다 시간(`getEffectiveTriggerTime`)으로 본다. 맵 값과 비교하면 짧은 차지가 Canceled 로 나온다.
        if ( state._holdDuration >= _triggerRules.getHoldTime( binding ) )
            accum._bAnyHoldReached = true;
        InputTriggerRules::endFrame( state );

        if ( bRawDown )
        {
            float2& accumulator = BindingKinds::isRelative( binding ) ? accum._relativeValue : accum._totalValue;
            accumulator._x += bindingValue._x;
            accumulator._y += bindingValue._y;
        }
    }

    void InputMap::advanceActionPhase( ActionEntry& actionEntry, bool bPrevDown, bool bAnyHoldReached )
    {
        if ( actionEntry._bTriggered == SW_TRUE )
        {
            actionEntry._currentPhase = ActionPhase::Triggered;
        }
        else if ( actionEntry._bPressed == SW_TRUE || ( bPrevDown == false && actionEntry._bDown == SW_TRUE ) )
        {
            actionEntry._currentPhase = ActionPhase::Started;
        }
        else if ( actionEntry._bDown == SW_TRUE )
        {
            actionEntry._currentPhase = ActionPhase::Ongoing;
        }
        else if ( actionEntry._bReleased == SW_TRUE )
        {
            actionEntry._currentPhase = ( actionEntry._currentPhase == ActionPhase::Triggered || actionEntry._currentPhase == ActionPhase::Ongoing || bAnyHoldReached )
                                          ? ActionPhase::Completed
                                          : ActionPhase::Canceled;
        }
        else
        {
            actionEntry._currentPhase = ActionPhase::None;
        }
    }

    void InputMap::dispatchActionCallbacks( ActionEntry& actionEntry, bool bPrevDown )
    {
        // 델리게이트 이벤트 디스패치(Triggered)
        if ( actionEntry._bTriggered == SW_TRUE )
            actionEntry._onTriggered.broadcast();

        // 페이즈 델리게이트 디스패치(Phase) — 항목이 자기 단계만 거른다
        if ( actionEntry._currentPhase != ActionPhase::None )
            actionEntry._onPhase.broadcast( actionEntry._currentPhase );

        // 2D 벡터 델리게이트 디스패치
        if ( ( actionEntry._currentValue._x != 0.0f || actionEntry._currentValue._y != 0.0f ) || ( bPrevDown && actionEntry._bDown == SW_FALSE ) )
            actionEntry._onVector2D.broadcast( actionEntry._currentValue );
    }

    float2 InputMap::composeActionValue( const float2& bounded, const float2& relative ) const
    {
        float2 value = bounded;
        if ( _accessibility._bInvertX )
            value._x = -value._x;
        if ( _accessibility._bInvertY )
            value._y = -value._y;
        if ( _accessibility._digitalNormalization == DigitalNormalization::Circular )
        {
            const float32 lenSq = value._x * value._x + value._y * value._y;
            if ( lenSq > 1.0f )
            {
                const float32 invLen = 1.0f / MathUtil::sqrt( lenSq );
                value._x *= invLen;
                value._y *= invLen;
            }
        }
        else
        {
            value._x = MathUtil::clamp( value._x, -1.0f, 1.0f );
            value._y = MathUtil::clamp( value._y, -1.0f, 1.0f );
        }
        return float2{ value._x + relative._x, value._y + relative._y };
    }

    InputBindingState InputMap::makeBindingState( const ActionBinding& binding ) const
    {
        float2            unused{ 0.0f, 0.0f };
        InputBindingState state{};
        state._bPhysicalDown = evaluateBindingDown( binding, unused, SlotRead::Device ) ? SW_TRUE : SW_FALSE;
        return state;
    }

    void InputMap::reseedBindingState( ActionEntry& entry, uint32 bindIndex ) const
    {
        entry._listBindingState[bindIndex] = makeBindingState( entry._listBinding[bindIndex] );
    }

    void InputMap::rebuildBindingStates( ActionEntry& entry ) const
    {
        entry._listBindingState.clear();
        entry._listBindingState.reserve( entry._listBinding.size() );
        for ( const ActionBinding& binding : entry._listBinding )
        {
            entry._listBindingState.push_back( makeBindingState( binding ) );
        }
    }

    bool InputMap::isKeyboardKeyDown( Key key, SlotRead read ) const
    {
        if ( _pInput == nullptr )
            return false;
        if ( read == SlotRead::Device )
            return _pInput->getKeyboard() != nullptr && _pInput->getKeyboard()->isKeyDown( key );
        if ( _bKeyboardFocusIgnored == SW_TRUE )
            return isToolKeyboardHidden() == false && _pInput->getKeyboard() != nullptr && _pInput->getKeyboard()->isKeyDown( key );
        return _pInput->isKeyDown( key );
    }

    bool InputMap::wasKeyboardKeyPressed( Key key ) const
    {
        if ( _pInput == nullptr )
            return false;
        if ( _bKeyboardFocusIgnored == SW_TRUE )
            return isToolKeyboardHidden() == false && _pInput->getKeyboard() != nullptr && _pInput->getKeyboard()->wasKeyPressed( key );
        return _pInput->wasKeyPressed( key );
    }

    bool InputMap::isToolKeyboardHidden() const
    {
        return _bReadsToolKeyboard == SW_FALSE && _pInput->getKeyboardFocus() == InputKeyboardFocus::Tool;
    }

    bool InputMap::isSlotDown( const InputSlot& slot, SlotRead read ) const
    {
        const IInputDevice* pDevice = _pInput != nullptr ? _pInput->getDevice( slot._deviceKind, slot._deviceIndex ) : nullptr;
        if ( pDevice == nullptr )
            return false;
        if ( read == SlotRead::Game && isSlotHiddenByConsole( slot ) )
            return false;
        return pDevice->isControlDown( slot._controlIndex );
    }

    bool InputMap::wasSlotPressed( const InputSlot& slot, SlotRead read ) const
    {
        const IInputDevice* pDevice = _pInput != nullptr ? _pInput->getDevice( slot._deviceKind, slot._deviceIndex ) : nullptr;
        if ( pDevice == nullptr )
            return false;
        if ( read == SlotRead::Game && isSlotHiddenByConsole( slot ) )
            return false;
        return pDevice->wasControlPressed( slot._controlIndex );
    }

    bool InputMap::isSlotHeldThisFrame( const InputSlot& slot, SlotRead read ) const
    {
        return isSlotDown( slot, read ) || wasSlotPressed( slot, read );
    }

    bool InputMap::isSlotHiddenByConsole( const InputSlot& slot ) const
    {
        // 포커스를 무시하는 맵(셸 · UI · 에디터)도 도구 포커스 동안의 키는 도구 자신의 맵만 본다.
        if ( _bKeyboardFocusIgnored == SW_TRUE )
            return slot._deviceKind == InputDeviceKind::Keyboard && isToolKeyboardHidden();
        // 개발 콘솔이 입력을 쥔 동안 게임 맵은 키보드 바인딩과 패드 바인딩을 모두 "안 눌림" 으로 본다(패드는 디버그 키 맵이 콘솔 조작에 쓴다).
        if ( slot._deviceKind == InputDeviceKind::Keyboard )
            return _pInput->isKeyVisibleToGame( static_cast<Key>( slot._controlIndex ) ) == false;
        if ( slot._deviceKind == InputDeviceKind::Gamepad )
            return _pInput->isGamepadControlVisibleToGame( slot._deviceIndex, slot._controlIndex ) == false;
        return false;
    }

    bool InputMap::isBaseKeySuppressedByModifier( const ActionBinding& binding, SlotRead read ) const
    {
        if ( _bSuppressBaseActionOnChord == SW_FALSE || binding._arrSlot[0]._deviceKind != InputDeviceKind::Keyboard )
            return false;
        const Key  key            = static_cast<Key>( binding._arrSlot[0]._controlIndex );
        const bool bIsModifierKey = ( key == Key::LeftControl || key == Key::RightControl || key == Key::LeftShift || key == Key::RightShift || key == Key::LeftAlt || key == Key::RightAlt || key == Key::LeftSuper || key == Key::RightSuper );
        if ( bIsModifierKey )
            return false;
        const bool bCtrlHeld  = isKeyboardKeyDown( Key::LeftControl, read ) || isKeyboardKeyDown( Key::RightControl, read );
        const bool bAltHeld   = isKeyboardKeyDown( Key::LeftAlt, read ) || isKeyboardKeyDown( Key::RightAlt, read );
        const bool bShiftHeld = isKeyboardKeyDown( Key::LeftShift, read ) || isKeyboardKeyDown( Key::RightShift, read );
        const bool bSuperHeld = isKeyboardKeyDown( Key::LeftSuper, read ) || isKeyboardKeyDown( Key::RightSuper, read );
        if ( bCtrlHeld || bAltHeld )
            return true;
        if ( ( bShiftHeld == false && bSuperHeld == false ) || binding._modifierMask != 0 )
            return false; // 조합 바인딩 자신은 정확 일치(`areShortcutModifiersExact`)가 맡는다
        // Shift · Super 는 혼자서는 맨 키를 막지 않는다(Shift 달리기 + WASD). 다만 같은 키에 지금 눌린 수정 키와 꼭 맞는 조합 바인딩이 있으면 그것이 이긴다 —
        // 맨 Z 와 Shift+Z 가 함께 발화해 서로 지웠다(배치 회전). 언리얼 Enhanced Input 의 코드 막기(Chord Blocker)와 같은 자리다.
        const uint8 heldMask = static_cast<uint8>( ( bShiftHeld ? ModifierKey::Shift : 0 ) | ( bSuperHeld ? ModifierKey::Super : 0 ) );
        for ( const ActionEntry& entry : _listActionEntry )
        {
            for ( const ActionBinding& other : entry._listBinding )
            {
                const bool bSameKey = other._arrSlot[0]._deviceKind == InputDeviceKind::Keyboard && other._arrSlot[0]._controlIndex == binding._arrSlot[0]._controlIndex;
                if ( bSameKey && other._modifierMask == heldMask )
                    return true;
            }
        }
        return false;
    }

    bool InputMap::evaluateGamepadStick( const ActionBinding& binding, float2& outValue ) const
    {
        GamepadDevice* pPad = _pInput->getGamepad( binding._deviceIndex );
        if ( pPad == nullptr || pPad->isConnected() == false )
            return false;
        const float2 stick = ( binding._stick == GamepadStick::Left ) ? pPad->getLeftStick() : pPad->getRightStick();
        // 한 성분만 내는 바인딩은 다른 성분을 읽지 않는다 — 원형 데드존이 두 성분을 함께 정규화하면 다른 성분이 1D 값에 섞인다.
        float32 stickX = binding._stickAxis == GamepadStickAxis::Y ? 0.0f : stick._x;
        float32 stickY = binding._stickAxis == GamepadStickAxis::X ? 0.0f : stick._y;

        const float32 inDeadzone  = _accessibility._stickDeadzoneOverride >= 0.0f ? _accessibility._stickDeadzoneOverride : binding._deadzone;
        const float32 outDeadzone = binding._outerDeadzone > inDeadzone ? binding._outerDeadzone : 1.0f;
        const float32 deadRange   = outDeadzone - inDeadzone;

        if ( _accessibility._deadzoneShape == DeadzoneShape::Radial )
        {
            const float32 mag = MathUtil::sqrt( stickX * stickX + stickY * stickY );
            if ( mag <= inDeadzone )
            {
                stickX = 0.0f;
                stickY = 0.0f;
            }
            else
            {
                const float32 norm = shapeStickMagnitude( mag, inDeadzone, deadRange, binding._responseExponent );
                stickX             = ( stickX / mag ) * norm;
                stickY             = ( stickY / mag ) * norm;
            }
        }
        else // Axial
        {
            stickX = applyAxialDeadzone( stickX, inDeadzone, deadRange, binding._responseExponent );
            stickY = applyAxialDeadzone( stickY, inDeadzone, deadRange, binding._responseExponent );
        }

        stickX *= _accessibility._gamepadSensitivity._x;
        stickY *= _accessibility._gamepadSensitivity._y;
        if ( BindingKinds::isRelative( binding ) )
        {
            // 이동량으로 쓰는 스틱: 배율과 프레임 시간을 곱하고, 반전은 마우스 이동처럼 여기서 한 번만 적용한다(액션 값 단계는 이동량을 반전하지 않는다).
            // 한 성분만 내는 스틱(확대 같은 1D 액션)은 시점 반전을 받지 않는다 — 반전 설정은 2D 시점 이동의 것이다.
            const float32 frameScale = binding._bScaleByDelta ? _lastDeltaSeconds : 1.0f;
            const bool    bLook      = binding._stickAxis == GamepadStickAxis::Both;
            stickX *= binding._axisScale._x * frameScale * ( bLook && _accessibility._bInvertX ? -1.0f : 1.0f );
            stickY *= binding._axisScale._y * frameScale * ( bLook && _accessibility._bInvertY ? -1.0f : 1.0f );
        }
        // 한 성분만 내는 바인딩은 그 성분이 1D 값(x)이다 — getAxis1D 가 x 를 읽는다.
        if ( binding._stickAxis == GamepadStickAxis::X )
        {
            stickY = 0.0f;
        }
        else if ( binding._stickAxis == GamepadStickAxis::Y )
        {
            stickX = stickY;
            stickY = 0.0f;
        }
        outValue = float2{ stickX, stickY };
        return ( stickX != 0.0f || stickY != 0.0f );
    }

    bool InputMap::evaluateVirtualJoystick( const ActionBinding& binding, float2& outValue, SlotRead read ) const
    {
        const bool bActivationDown = isSlotDown( binding._arrSlot[0], read );

        if ( bActivationDown == false )
        {
            binding._bJoystickAnchored = SW_FALSE;
            return false;
        }

        const int2   curTilePos = _pInput->getMousePosition();
        const float2 curPos     = curTilePos.toFloat2();

        // 앵커는 고정 좌표가 아니라 활성화 버튼을 처음 누른 지점에 놓는다(모바일 온스크린 스틱에서 흔한 방식).
        if ( binding._bJoystickAnchored == false )
        {
            binding._joystickAnchor    = curPos;
            binding._bJoystickAnchored = SW_TRUE;
        }

        outValue = VirtualJoystick::computeVector( binding._joystickAnchor, curPos, binding._scale, binding._deadzone, binding._outerDeadzone );
        return ( outValue._x != 0.0f || outValue._y != 0.0f );
    }

    bool InputMap::areShortcutModifiersExact( const ActionBinding& binding, SlotRead read ) const
    {
        struct ModifierFamily
        {
            uint8 _bit;
            Key   _left;
            Key   _right;
        };
        static constexpr ModifierFamily kArrFamily[] = {
            { ModifierKey::Ctrl, Key::LeftControl, Key::RightControl},
            {ModifierKey::Shift,   Key::LeftShift,   Key::RightShift},
            {  ModifierKey::Alt,     Key::LeftAlt,     Key::RightAlt},
            {ModifierKey::Super,   Key::LeftSuper,   Key::RightSuper},
        };
        // 요구한 키만 보면 Ctrl+Shift+Z 가 Ctrl+Z 도 함께 발화시킨다. 눌린 수정 키 집합이 마스크와 같아야 한다.
        // 방아쇠 키 자신이 수정 키(Ctrl+LeftShift)면 그 키는 늘 눌려 있으므로 비교에서 뺀다.
        const bool bKeyboardTrigger = binding._arrSlot[0]._deviceKind == InputDeviceKind::Keyboard;
        const Key  triggerKey       = static_cast<Key>( binding._arrSlot[0]._controlIndex );
        for ( const ModifierFamily& family : kArrFamily )
        {
            if ( bKeyboardTrigger && ( triggerKey == family._left || triggerKey == family._right ) )
                continue;
            const bool bRequired = ( binding._modifierMask & family._bit ) != 0;
            const bool bHeld     = isKeyboardKeyDown( family._left, read ) || isKeyboardKeyDown( family._right, read );
            if ( bRequired != bHeld )
                return false;
        }
        return true;
    }

    // 긴 케이스(스틱 · 가상 조이스틱 · 단축키 수정 키)는 함수로 뺐고, 남은 것은 종류마다 짧은 switch 디스패처라 나누지 않는다.
    bool InputMap::evaluateBindingDown( const ActionBinding& binding, float2& outValue, SlotRead read ) const
    {
        if ( _pInput == nullptr )
            return false;
        // 이동량과 "아무 키" 신호는 눌려 있는 상태가 없어 이어 갈 장치 눌림이 없다. 있는 척하면 다음 신호가 새 누름이 아닌 것으로 읽힌다.
        if ( read == SlotRead::Device && ( binding._kind == BindingKind::MouseDelta2D || binding._kind == BindingKind::MouseWheel1D || binding._kind == BindingKind::AnyKey ) )
            return false;

        switch ( binding._kind )
        {
            case BindingKind::SingleSlot:
            {
                if ( isSlotHeldThisFrame( binding._arrSlot[0], read ) && isBaseKeySuppressedByModifier( binding, read ) == false )
                {
                    outValue = float2{ 1.0f, 0.0f };
                    return true;
                }
                return false;
            }
            case BindingKind::Axis1DComposite:
            {
                float32 axisValue = 0.0f;
                if ( isSlotHeldThisFrame( binding._arrSlot[0], read ) )
                    axisValue -= 1.0f;
                if ( isSlotHeldThisFrame( binding._arrSlot[1], read ) )
                    axisValue += 1.0f;
                outValue = float2{ axisValue, 0.0f };
                return axisValue != 0.0f;
            }
            case BindingKind::Vector2DComposite:
            {
                float2 kbdVec{ 0.0f, 0.0f };
                if ( isSlotHeldThisFrame( binding._arrSlot[0], read ) )
                    kbdVec._y += 1.0f;
                if ( isSlotHeldThisFrame( binding._arrSlot[1], read ) )
                    kbdVec._y -= 1.0f;
                if ( isSlotHeldThisFrame( binding._arrSlot[2], read ) )
                    kbdVec._x -= 1.0f;
                if ( isSlotHeldThisFrame( binding._arrSlot[3], read ) )
                    kbdVec._x += 1.0f;

                const float32 lenSq = kbdVec._x * kbdVec._x + kbdVec._y * kbdVec._y;
                if ( _accessibility._digitalNormalization == DigitalNormalization::Circular && lenSq > 1.0f )
                {
                    const float32 invLen = 1.0f / MathUtil::sqrt( lenSq );
                    kbdVec._x *= invLen;
                    kbdVec._y *= invLen;
                }
                outValue = kbdVec;
                return ( kbdVec._x != 0.0f || kbdVec._y != 0.0f );
            }
            case BindingKind::GamepadStick2D:
            {
                return evaluateGamepadStick( binding, outValue );
            }
            case BindingKind::Chord:
            {
                if ( isSlotDown( binding._arrSlot[0], read ) && isSlotDown( binding._arrSlot[1], read ) )
                {
                    outValue = float2{ 1.0f, 0.0f };
                    return true;
                }
                return false;
            }
            case BindingKind::MouseDelta2D:
            {
                const float2  movement = _pInput->getMouse() != nullptr ? _pInput->getMouse()->getMovementDelta() : float2{};
                const float32 rdx      = movement._x;
                const float32 rdy      = movement._y;
                outValue._x            = rdx * binding._scale * _accessibility._mouseSensitivity._x * ( _accessibility._bInvertX ? -1.0f : 1.0f );
                outValue._y            = rdy * binding._scale * _accessibility._mouseSensitivity._y * ( _accessibility._bInvertY ? -1.0f : 1.0f );
                return ( rdx != 0.0f || rdy != 0.0f );
            }
            case BindingKind::MouseWheel1D:
            {
                const float32 wheel = _pInput->getMouseWheel();
                outValue            = float2{ wheel * binding._scale, 0.0f };
                return wheel != 0.0f;
            }
            case BindingKind::VirtualJoystick2D:
            {
                return evaluateVirtualJoystick( binding, outValue, read );
            }
            case BindingKind::Shortcut:
            {
                if ( areShortcutModifiersExact( binding, read ) == false )
                    return false;

                if ( isSlotDown( binding._arrSlot[0], read ) )
                {
                    outValue = float2{ 1.0f, 0.0f };
                    return true;
                }
                return false;
            }
            case BindingKind::AnyKey:
            {
                const bool bAny = _pInput->wasAnyInputPressed();
                if ( bAny )
                    outValue = float2{ 1.0f, 0.0f };
                return bAny;
            }
            case BindingKind::Count:
            {
                // 이 저장소는 모든 switch 에 `default:` 를 요구한다(-Wswitch-default). 그래서 컴파일러는
                // 이 switch 에서 빠진 종류를 알려 주지 못한다. 컴파일 시점에 잡히는 것은 테이블에서 빠진 줄뿐이다
                // (`kArrBindingKindInfo` 의 static_assert). 여기까지 왔다면 종류를 추가하고
                // 이 switch 를 빠뜨린 것이고, 그 액션은 영원히 발동하지 않는다.
                // 매 프레임 도는 경로라 로그는 남기지 않고, 개발 빌드에서 단언으로 멈춘다.
                SW_ASSERT( false );
                return false;
            }
        }
    }

    bool InputMap::isBindingLayerActive( const ActionBinding& binding ) const
    {
        if ( binding._cachedLayerIndex == ActionBinding::kInvalidLayerIndex )
        {
            auto it = _mapLayer.find( binding._layer );
            if ( it != _mapLayer.end() )
                binding._cachedLayerIndex = it->second;
        }

        if ( binding._cachedLayerIndex != ActionBinding::kInvalidLayerIndex && _listLayerStack.empty() )
        {
            if ( _listLayerEntry[binding._cachedLayerIndex]._bEnabled == SW_FALSE )
                return false;
            return true;
        }

        return isLayerActiveInternal( binding._layer );
    }

    bool InputMap::isLayerActiveInternal( const hashed_string& layer ) const
    {
        const LayerDef* pLayer = findLayer( layer );
        if ( pLayer == nullptr || pLayer->_bEnabled == SW_FALSE )
            return false;
        if ( pLayer->_bAlwaysOn == SW_TRUE )
            return true;

        if ( _listLayerStack.empty() == false )
        {
            for ( auto it = _listLayerStack.rbegin(); it != _listLayerStack.rend(); ++it )
            {
                if ( *it == layer )
                    return true;
                const LayerDef* pTopLayer = findLayer( *it );
                if ( pTopLayer != nullptr && pTopLayer->_bBlockLower == SW_TRUE )
                    return false;
            }
            return false;
        }

        return true;
    }

} // namespace sw

namespace sw
{
    bool InputMap::loadFromAsset( const AssetID& assetID )
    {
        const string_view relativePath = assetID.view();
        return loadDefinition( AssetID{ relativePath }, false, true );
    }

    bool InputMap::appendFromAsset( const AssetID& assetID )
    {
        return loadDefinition( assetID, true, true );
    }

    bool InputMap::loadBundle( const vector<AssetID>& listAsset )
    {
        if ( listAsset.empty() )
        {
            SW_LOG_ERROR_ID( "InputMap.BundleEmpty", "InputMap: a bundle needs at least its base map" );
            clear();
            return false;
        }
        bool bValid = true;
        for ( size_t index = 0; index < listAsset.size() && bValid; ++index )
        {
            bValid = loadDefinition( listAsset[index], index > 0, false );
        }
        // 뒤 파일의 `virtualCursor` 는 앞 파일의 바인딩과도 겹칠 수 있고, 앞 파일끼리의 겹침은 뒤 파일이 풀 수 있다 — 검사는 다 읽은 맵에 한 번 한다.
        const string_view sourceName = listAsset.back().view();
        if ( bValid )
            bValid = countVirtualCursorConflicts( sourceName ) == 0 && countAllSameLayerInputConflicts( sourceName ) == 0;
        if ( bValid == false )
        {
            SW_LOG_ERROR_ID( "InputMap.BundleRejected", "InputMap: the bundle ending in %# was rejected (listed above) - no actions are bound", sourceName );
            clear();
        }
        return bValid;
    }

    bool InputMap::loadDefinition( const AssetID& assetID, bool bAppend, bool bCheckConflicts )
    {
        const string_view relativePath = assetID.view();
        XMLDocument       doc;
        string            absPath;
        if ( doc.loadResource( assetID, &absPath ) == false )
        {
            SW_LOG_WARNING_ID( "InputMap.FailedLoadInputMap", "Failed to load InputMap %#", relativePath );
            return false;
        }

        XMLNode root = doc.getRoot( "InputMap" );
        if ( root.isValid() == false )
        {
            SW_LOG_WARNING_ID( "InputMap.MissingInputMap", "Missing <InputMap> in %#", absPath );
            return false;
        }

        // 읽기 단계: 맵 전체 값 → 레이어 → 액션 → 충돌 검사. 단계가 실패하면 거기서 끝낸다.
        // 더하는 파일은 맵 전체 값을 정하지 않는다 — 두 파일이 각자 정하면 어느 값이 이기는지가 읽는 순서에 달린다.
        // `virtualCursor` 만 받는다. 시간 값과 달리 게임마다 다른 기능이고, 기본 맵을 둘로 두면 시간 값이 두 벌이 된다. 두 번 적으면 `readMapSettings` 가 거절한다.
        for ( XMLAttribute attribute = root.getFirstAttribute(); bAppend && attribute.isValid(); attribute = attribute.getNext() )
        {
            if ( string_view{ attribute.getName() } == "virtualCursor" )
                continue;
            SW_LOG_ERROR_ID( "InputMap.InputMapAppendedAnotherMap", "InputMap %# is appended to another map but sets map-wide attributes - only the base map sets them", absPath );
            return false;
        }
        if ( readMapSettings( root, absPath, bAppend ) == false )
            return false;
        // 맵을 비운(`readMapSettings`) 뒤에 올린다 — 이 파일의 레이어 · 액션이 이 번호를 받는다.
        _listSource.emplace_back( relativePath, bAppend == false, root.hasAttribute( "virtualCursor" ) );
        _currentSourceIndex = static_cast<uint32>( _listSource.size() - 1 );

        bool bValid = true;
        readLayerDefinitions( root, absPath, bAppend, bValid );
        vector<hashed_string> listLoadedAction;
        readActionDefinitions( root, absPath, bAppend, listLoadedAction, bValid );

        if ( bValid == false )
        {
            SW_LOG_ERROR_ID( "InputMap.InputMapInvalidAttributeValues", "InputMap %# has invalid attribute values (listed above)", absPath );
            return false;
        }
        if ( bCheckConflicts == false )
            return true;
        // 가상 커서가 마우스로 바꾸는 스틱 · 버튼을 액션에 따로 묶으면 한 입력이 두 입력이 된다 — 읽기를 실패로 돌려 데이터를 고치게 한다.
        if ( countVirtualCursorConflicts( absPath ) > 0 )
            return false;
        uint32 sameLayerConflictCount = 0;
        for ( const hashed_string& actionName : listLoadedAction )
        {
            sameLayerConflictCount += countSameLayerInputConflicts( absPath, actionName );
        }
        return sameLayerConflictCount == 0;
    }

    bool InputMap::readMapSettings( const XMLNode& root, string_view absPath, bool bAppend )
    {
        static constexpr const utf8* kArrRootChild[] = { "layers", "layer", "action", "override", "remove" };
        InputMapRootParams           params;
        const bool                   bFields   = XMLFields::readFields( root, params, absPath );
        const bool                   bChildren = XMLNameCheck::reportUnknownChildren( root, kArrRootChild, absPath );
        if ( bFields == false || bChildren == false )
            return false;
        if ( bAppend )
        {
            if ( params._virtualCursor == InputMapVirtualCursor::Off )
                return true;
            if ( _virtualCursorStick != InputMapVirtualCursor::Off )
            {
                SW_LOG_ERROR_ID( "InputMap.VirtualCursorSetTwice", "InputMap %# sets virtualCursor but an earlier map of the bundle already did", absPath );
                return false;
            }
            _virtualCursorStick = params._virtualCursor;
            return true;
        }
        clear();
        const bool bTimingRefused = setDoubleClickTime( params._doubleClickTime ) == false || setDoubleClickMaxDistance( params._doubleClickMaxDistance ) == false ||
                                    setHoldThreshold( params._holdThreshold ) == false || setDragThreshold( params._dragThreshold ) == false;
        if ( bTimingRefused )
            return false;
        _virtualCursorStick = params._virtualCursor;
        _defaultLayerName   = params._defaultLayer;
        return true;
    }

    bool InputMap::readLayer( const XMLNode& layerNode, string_view absPath, bool bAppend, std::initializer_list<string_view> listChild, hashed_string& outName )
    {
        InputMapLayerParams params;
        const bool          bFields   = XMLFields::readFields( layerNode, params, absPath );
        bool                bChildren = true;
        for ( XMLNode child = layerNode.findChild(); child.isValid(); child = child.findNextSibling() )
        {
            if ( VectorUtil::contains( listChild, string_view{ child.getName() } ) )
                continue;
            SW_LOG_ERROR_ID( "InputMap.LayerUnknownElement", "InputMap %#: <layer name=\"%#\"> has unknown element <%#>", absPath, params._name.c_str(), child.getName() );
            bChildren = false;
        }
        if ( bFields == false || bChildren == false )
            return false;
        outName = params._name;
        if ( bAppend && hasLayer( params._name ) && listChild.size() == 0 )
        {
            SW_LOG_ERROR_ID( "InputMap.InputMapRedefinesLayerMap", "InputMap %# redefines layer '%#' that the map already has - put actions in it with <layer name=...> instead", absPath, params._name.c_str() );
            return false;
        }
        if ( hasLayer( params._name ) == false || listChild.size() == 0 )
            registerLayer( params._name, params._priority, params._bEnabled, params._bBlockLower, params._bAlwaysOn );
        return true;
    }

    void InputMap::readLayerDefinitions( const XMLNode& root, string_view absPath, bool bAppend, bool& inoutbValid )
    {
        const XMLNode layersNode = root.findChild( "layers" );
        for ( XMLNode layerNode = layersNode.findChild(); layerNode.isValid(); layerNode = layerNode.findNextSibling() )
        {
            hashed_string name;
            if ( string_view{ layerNode.getName() } != "layer" )
            {
                SW_LOG_ERROR_ID( "InputMap.LayersHoldsOnlyLayer", "InputMap %#: <layers> holds only <layer> elements (found <%#>)", absPath, layerNode.getName() );
                inoutbValid = false;
                continue;
            }
            inoutbValid = readLayer( layerNode, absPath, bAppend, {}, name ) && inoutbValid;
        }
        ensureLayer( _defaultLayerName, 0, true, false );
    }

    void InputMap::readActionDefinitions( const XMLNode& root, string_view absPath, bool bAppend, vector<hashed_string>& outListLoadedAction, bool& inoutbValid )
    {
        // 더할 때는 이미 있는 액션을 다시 정의하지 못한다(두 파일이 같은 이름을 쓰면 바인딩이 섞인다). 더한 액션은 끝에서 같은 레이어 충돌을 본다.
        const auto loadAction = [&]( const XMLNode& actionNode, const hashed_string& inheritedLayer )
        {
            const hashed_string actionName( actionNode.getAttributeText( "name" ) );
            if ( bAppend && actionName.empty() == false )
            {
                if ( hasAction( actionName ) )
                {
                    SW_LOG_ERROR_ID( "InputMap.InputMapDefinesActionMap", "InputMap %# defines action '%#' that the map already has", absPath, actionName.c_str() );
                    inoutbValid = false;
                    return;
                }
                outListLoadedAction.push_back( actionName );
            }
            InputMapXML::readAction( *this, actionNode, inheritedLayer, absPath, inoutbValid );
        };

        for ( XMLNode layerNode = root.findChild( "layer" ); layerNode.isValid(); layerNode = layerNode.findNextSibling( "layer" ) )
        {
            hashed_string layerName;
            if ( readLayer( layerNode, absPath, bAppend, { "action" }, layerName ) == false )
            {
                inoutbValid = false;
                continue;
            }
            for ( XMLNode actionNode = layerNode.findChild( "action" ); actionNode.isValid(); actionNode = actionNode.findNextSibling( "action" ) )
            {
                loadAction( actionNode, layerName );
            }
        }

        for ( XMLNode actionNode = root.findChild( "action" ); actionNode.isValid(); actionNode = actionNode.findNextSibling( "action" ) )
        {
            loadAction( actionNode, _defaultLayerName );
        }

        // 차이는 앞 파일이 있어야 뜻이 있다. 기본 맵에 적은 것은 틀린 파일이다.
        if ( bAppend == false && ( root.findChild( "override" ).isValid() || root.findChild( "remove" ).isValid() ) )
        {
            SW_LOG_ERROR_ID( "InputMap.DiffInBaseMap", "InputMap %# is a base map but has <override> or <remove> - only a later map of a bundle can", absPath );
            inoutbValid = false;
            return;
        }
        for ( XMLNode node = root.findChild( "override" ); node.isValid(); node = node.findNextSibling( "override" ) )
        {
            readOverride( node, absPath, outListLoadedAction, inoutbValid );
        }
        for ( XMLNode node = root.findChild( "remove" ); node.isValid(); node = node.findNextSibling( "remove" ) )
        {
            readRemove( node, absPath, inoutbValid );
        }
    }

    InputMap::ActionEntry* InputMap::findDiffTarget( const XMLNode& node, const hashed_string& action, string_view absPath )
    {
        ActionEntry* pEntry = findAction( action );
        if ( pEntry == nullptr )
        {
            SW_LOG_ERROR_ID( "InputMap.DiffUnknownAction", "InputMap %#: <%#> names action '%#' that no earlier map of the bundle defines", absPath, node.getName(), action.c_str() );
            return nullptr;
        }
        if ( pEntry->_bOverride == SW_TRUE )
        {
            SW_LOG_ERROR_ID( "InputMap.DiffTwice", "InputMap %#: <%#> names action '%#' that %# already overrode - one diff per action, or the winner depends on the load order", absPath, node.getName(),
                             action.c_str(), _listSource[pEntry->_sourceIndex]._assetPath.c_str() );
            return nullptr;
        }
        if ( pEntry->_sourceIndex == _currentSourceIndex )
        {
            SW_LOG_ERROR_ID( "InputMap.DiffSameFile", "InputMap %#: <%#> names action '%#' that the same file defines - edit the definition instead", absPath, node.getName(), action.c_str() );
            return nullptr;
        }
        return pEntry;
    }

    void InputMap::shadowActionDefinition( const ActionEntry& entry, const hashed_string& action )
    {
        SourceActionSnapshot snapshot;
        snapshot._action      = action;
        snapshot._valueType   = entry._valueType;
        snapshot._listBinding = entry._listBinding;
        _listSource[entry._sourceIndex]._listShadowed.push_back( std::move( snapshot ) );
    }

    void InputMap::readOverride( const XMLNode& node, string_view absPath, vector<hashed_string>& outListLoadedAction, bool& inoutbValid )
    {
        const hashed_string action( node.getAttributeText( "name" ) );
        ActionEntry* const  pEntry = findDiffTarget( node, action, absPath );
        if ( pEntry == nullptr )
        {
            inoutbValid = false;
            return;
        }
        shadowActionDefinition( *pEntry, action );
        // `layer` 를 적지 않은 덮기는 덮는 액션의 레이어에 남는다 — 차이는 키만 적으면 된다.
        const hashed_string inheritedLayer = pEntry->_listBinding.empty() ? _defaultLayerName : pEntry->_listBinding.front()._layer;
        pEntry->_listBinding.clear();
        pEntry->_listDefaultBinding.clear();
        pEntry->_listBindingState.clear();
        pEntry->_sourceIndex = _currentSourceIndex;
        pEntry->_bOverride   = SW_TRUE;
        outListLoadedAction.push_back( action );
        InputMapXML::readAction( *this, node, inheritedLayer, absPath, inoutbValid );
    }

    void InputMap::readRemove( const XMLNode& node, string_view absPath, bool& inoutbValid )
    {
        InputMapRemoveParams params;
        const bool           bFields   = XMLFields::readFields( node, params, absPath );
        const bool           bChildren = XMLNameCheck::reportUnknownChildren( node, nullptr, 0, absPath );
        if ( bFields == false || bChildren == false )
        {
            inoutbValid = false;
            return;
        }
        ActionEntry* const pEntry = findDiffTarget( node, params._name, absPath );
        if ( pEntry == nullptr )
        {
            inoutbValid = false;
            return;
        }
        shadowActionDefinition( *pEntry, params._name );
        (void)removeAction( params._name ); // 바로 위에서 찾았다
        _listSource[_currentSourceIndex]._listRemove.push_back( params._name );
    }

    uint32 InputMap::countAllSameLayerInputConflicts( string_view sourceName ) const
    {
        uint32                conflictCount = 0;
        vector<hashed_string> listVisited;
        for ( const hashed_string& action : _listActionName )
        {
            const uint32 bindCount = getBindingCount( action );
            for ( uint32 bindIndex = 0; bindIndex < bindCount; ++bindIndex )
            {
                hashed_string other{};
                if ( findSameLayerInputConflict( action, bindIndex, other ) == false || VectorUtil::contains( listVisited, other ) )
                    continue; // 짝의 앞쪽에서 이미 알렸다
                SW_LOG_ERROR_ID( "InputMap.InputMapActionUsesSame", "InputMap %#: action '%#' uses the same input as '%#' in layer '%#' - one press would fire both", sourceName, action.c_str(), other.c_str(),
                                 findBindingLayer( action, bindIndex ).c_str() );
                ++conflictCount;
            }
            listVisited.push_back( action );
        }
        return conflictCount;
    }

    uint32 InputMap::countSameLayerInputConflicts( string_view sourceName, const hashed_string& action ) const
    {
        const ActionEntry* pEntry = findAction( action );
        if ( pEntry == nullptr )
            return 0;
        uint32 conflictCount = 0;
        for ( uint32 bindIndex = 0; bindIndex < static_cast<uint32>( pEntry->_listBinding.size() ); ++bindIndex )
        {
            hashed_string otherName{};
            if ( findSameLayerInputConflict( action, bindIndex, otherName ) == false )
                continue;
            SW_LOG_ERROR_ID( "InputMap.InputMapActionUsesSame", "InputMap %#: action '%#' uses the same input as '%#' in layer '%#' - one press would fire both", sourceName, action.c_str(), otherName.c_str(),
                             pEntry->_listBinding[bindIndex]._layer.c_str() );
            ++conflictCount;
        }
        return conflictCount;
    }

    uint32 InputMap::countVirtualCursorConflicts( string_view sourceName ) const
    {
        uint32 conflictCount = 0;
        for ( const hashed_string& actionName : _listActionName )
        {
            const ActionEntry* pEntry = findAction( actionName );
            if ( pEntry == nullptr )
                continue;
            for ( const ActionBinding& binding : pEntry->_listBinding )
            {
                if ( reportVirtualCursorConflict( sourceName, actionName, binding ) )
                    ++conflictCount;
            }
        }
        return conflictCount;
    }

    uint32 InputMap::removeVirtualCursorConflicts( string_view sourceName, const hashed_string& action, uint32 firstIndex )
    {
        ActionEntry* pEntry = findAction( action );
        if ( pEntry == nullptr )
            return 0;
        uint32 removedCount = 0;
        for ( uint32 bindIndex = static_cast<uint32>( pEntry->_listBinding.size() ); bindIndex > firstIndex; --bindIndex )
        {
            const uint32 removeIndex = bindIndex - 1;
            if ( reportVirtualCursorConflict( sourceName, action, pEntry->_listBinding[removeIndex] ) == false )
                continue;
            pEntry->_listBinding.erase( pEntry->_listBinding.begin() + removeIndex );
            pEntry->_listDefaultBinding.erase( pEntry->_listDefaultBinding.begin() + removeIndex );
            pEntry->_listBindingState.erase( pEntry->_listBindingState.begin() + removeIndex );
            ++removedCount;
        }
        return removedCount;
    }

    bool InputMap::reportVirtualCursorConflict( string_view sourceName, const hashed_string& action, const ActionBinding& binding ) const
    {
        if ( _virtualCursorStick == InputMapVirtualCursor::Off )
            return false;
        const VirtualCursorSettings cursor{};
        const GamepadStick          cursorStick       = _virtualCursorStick == InputMapVirtualCursor::RightStick ? GamepadStick::Right : GamepadStick::Left;
        const InputSlot             arrCursorButton[] = { InputSlot::fromGamepadButton( cursor._primaryButton, cursor._padIndex ),
                                                          InputSlot::fromGamepadButton( cursor._secondaryButton, cursor._padIndex ) };
        const bool                  bCursorStick      = binding._kind == BindingKind::GamepadStick2D && binding._stick == cursorStick && binding._deviceIndex == cursor._padIndex;
        bool                        bCursorButton     = false;
        for ( const InputSlot& slot : binding._arrSlot )
        {
            for ( const InputSlot& button : arrCursorButton )
            {
                bCursorButton = bCursorButton || slot == button;
            }
        }
        if ( bCursorStick == false && bCursorButton == false )
            return false;
        SW_LOG_ERROR_ID( "InputMap.InputMapActionBindsVirtual", "InputMap %#: action '%#' binds the %# the virtual cursor (%#) turns into the mouse - one press would fire twice; bind the mouse only",
                         sourceName, action.c_str(), bCursorStick ? "stick" : "gamepad button", getEnumName( _virtualCursorStick ) );
        return true;
    }

    bool InputMap::saveToAsset( const AbsolutePath& absPath ) const
    {
        if ( absPath.str().empty() )
            return false;

        XMLDocument        doc;
        const XMLNode      root = doc.appendRoot( "InputMap" );
        InputMapRootParams rootParams;
        rootParams._defaultLayer           = _defaultLayerName;
        rootParams._doubleClickTime        = _triggerRules._doubleClickTime;
        rootParams._doubleClickMaxDistance = _triggerRules._doubleClickMaxDistance;
        rootParams._holdThreshold          = _triggerRules._holdThreshold;
        rootParams._dragThreshold          = _triggerRules._dragThreshold;
        rootParams._virtualCursor          = _virtualCursorStick;
        XMLFields::writeFields( root, rootParams );

        const XMLNode layersNode = root.appendChild( "layers" );
        for ( const hashed_string& layerName : _listLayerName )
        {
            writeLayerDef( layersNode, layerName );
        }

        // 액션은 등록 순서대로 쓴다. 그래야 다시 읽은 맵의 액션 순서가 같다.
        bool bAllWritten = true;
        for ( const hashed_string& actionName : _listActionName )
        {
            bAllWritten = InputMapXML::writeAction( root, *this, actionName ) && bAllWritten;
        }
        if ( bAllWritten == false )
            return false;
        if ( doc.saveToFile( absPath.str() ) == false )
        {
            SW_LOG_ERROR_ID( "InputMap.CouldNotWriteInputMap", "Could not write InputMap %#", absPath.str() );
            return false;
        }
        return true;
    }

    void InputMap::writeLayerDef( const XMLNode& layersNode, const hashed_string& layerName ) const
    {
        const LayerDef* pLayer = findLayer( layerName );
        if ( pLayer == nullptr )
            return;
        InputMapLayerParams layerParams;
        layerParams._name        = layerName;
        layerParams._priority    = pLayer->_priority;
        layerParams._bEnabled    = pLayer->_bEnabled != SW_FALSE;
        layerParams._bBlockLower = pLayer->_bBlockLower != SW_FALSE;
        layerParams._bAlwaysOn   = pLayer->_bAlwaysOn != SW_FALSE;
        XMLFields::writeFields( layersNode.appendChild( "layer" ), layerParams );
    }

    bool InputMap::fillSourceDocument( uint32 sourceIndex, XMLDocument& outDoc ) const
    {
        const SourceRecord& source = _listSource[sourceIndex];
        const XMLNode       root   = outDoc.appendRoot( "InputMap" );
        if ( source._bBase == SW_TRUE )
        {
            InputMapRootParams rootParams;
            rootParams._defaultLayer           = _defaultLayerName;
            rootParams._doubleClickTime        = _triggerRules._doubleClickTime;
            rootParams._doubleClickMaxDistance = _triggerRules._doubleClickMaxDistance;
            rootParams._holdThreshold          = _triggerRules._holdThreshold;
            rootParams._dragThreshold          = _triggerRules._dragThreshold;
            rootParams._virtualCursor          = source._bVirtualCursor == SW_TRUE ? _virtualCursorStick : InputMapVirtualCursor::Off;
            XMLFields::writeFields( root, rootParams );
        }
        else if ( source._bVirtualCursor == SW_TRUE )
        {
            root.appendAttribute( "virtualCursor", getEnumName( _virtualCursorStick ) );
        }

        vector<hashed_string> listOwnLayer;
        for ( const hashed_string& layerName : _listLayerName )
        {
            const LayerDef* pLayer = findLayer( layerName );
            if ( pLayer != nullptr && pLayer->_sourceIndex == sourceIndex )
                listOwnLayer.push_back( layerName );
        }
        if ( listOwnLayer.empty() == false )
        {
            const XMLNode layersNode = root.appendChild( "layers" );
            for ( const hashed_string& layerName : listOwnLayer )
            {
                writeLayerDef( layersNode, layerName );
            }
        }

        // 원래 차례대로 — 자기 정의(지금 맵에 있는 것, 뒤 파일이 덮거나 지운 것), 덮기, 지우기.
        bool bAllWritten = true;
        for ( const hashed_string& actionName : _listActionName )
        {
            const ActionEntry* pEntry = findAction( actionName );
            if ( pEntry != nullptr && pEntry->_sourceIndex == sourceIndex && pEntry->_bOverride == SW_FALSE )
                bAllWritten = InputMapXML::writeActionElement( root, "action", actionName, pEntry->_valueType, pEntry->_listBinding ) && bAllWritten;
        }
        for ( const SourceActionSnapshot& snapshot : source._listShadowed )
        {
            bAllWritten = InputMapXML::writeActionElement( root, "action", snapshot._action, snapshot._valueType, snapshot._listBinding ) && bAllWritten;
        }
        for ( const hashed_string& actionName : _listActionName )
        {
            const ActionEntry* pEntry = findAction( actionName );
            if ( pEntry != nullptr && pEntry->_sourceIndex == sourceIndex && pEntry->_bOverride == SW_TRUE )
                bAllWritten = InputMapXML::writeActionElement( root, "override", actionName, pEntry->_valueType, pEntry->_listBinding ) && bAllWritten;
        }
        for ( const hashed_string& actionName : source._listRemove )
        {
            InputMapRemoveParams removeFields;
            removeFields._name = actionName;
            XMLFields::writeFields( root.appendChild( "remove" ), removeFields );
        }
        return bAllWritten;
    }

    bool InputMap::writeSourceText( uint32 sourceIndex, string& outText ) const
    {
        XMLDocument doc;
        if ( fillSourceDocument( sourceIndex, doc ) == false )
            return false;
        outText = doc.saveToString();
        return true;
    }

    bool InputMap::saveSourceToAsset( uint32 sourceIndex, const AbsolutePath& absPath ) const
    {
        XMLDocument doc;
        if ( fillSourceDocument( sourceIndex, doc ) == false )
            return false;
        if ( doc.saveToFile( absPath.str() ) == false )
        {
            SW_LOG_ERROR_ID( "InputMap.CouldNotWriteInputMap", "Could not write InputMap %#", absPath.str() );
            return false;
        }
        return true;
    }
} // namespace sw

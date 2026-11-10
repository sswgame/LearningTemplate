#include "pch.h"

#include "Engine/Input/Map/InputMap.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Foundation/StdHeaders.h"

#include "Engine/Input/GamepadButtonUtil.h"
#include "Engine/Input/IInputDevice.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/KeyCodeUtil.h"
#include "Engine/Input/Map/InputMapParams.h"
#include "Engine/Input/Virtual/VirtualCursor.h"
#include "Engine/Input/Virtual/VirtualJoystick.h"

/**
 * @file InputMap.cpp
 * @brief InputMap 의 정의 데이터(수명 주기, 바인딩 등록, 레이어 스택, 리바인드, 정의 조회)를 구현합니다.
 *
 * InputMap 은 책임마다 파일이 나뉜다(언리얼 Enhanced Input 이 매핑 컨텍스트 데이터 · 플레이어 입력 평가 · 에셋 직렬화를 가르는 나눔).
 *  - InputMap.cpp: 정의 데이터 — 생성자, bind*() 등록, 레이어 스택(pushLayer/popLayer), 리바인드, 정의 조회.
 *  - InputMapEvaluate.cpp: 평가기 · 상태 — update() 가 매 프레임 도는 상태 머신과 is/wasActionXxx() 상태 조회.
 *  - InputMapAsset.cpp: InputMap XML 정의 · 묶음의 로드와 저장.
 *  - InputBinding.cpp: 바인딩 종류 표(`BindingKinds`).
 * 맵이 들고 있거나 맵을 읽는 협력 클래스는 따로 있다.
 *  - InputActionBuffer: 선입력 버퍼링과 격투 게임식 커맨드 시퀀스, 패턴 판정(맵이 들고 프레임마다 진행).
 *  - InputGlyphResolver: 액션을 UI 프롬프트 문자열("[ E ]" 등)로 바꾸는 글리프 조회(상태 없음).
 *  - InputAccessibilitySettings: 감도 · 축 반전 · 데드존 · 토글 액션(맵이 들고, 맵을 다시 읽어도 남는다).
 *  - InputRemapSerializer: 플레이어 리매핑 파일(`<UserBindings>`)의 쓰기 · 읽기. 두 형식이 함께 쓰는 `<action>` 원소 읽기 · 쓰기는 InputMapXML(스키마 InputMapParams.h).
 */

namespace sw
{
    namespace
    {
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
    } // namespace

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
} // namespace sw

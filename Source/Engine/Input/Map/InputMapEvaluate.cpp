#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Input/GamepadButtonUtil.h"
#include "Engine/Input/IInputDevice.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/KeyCodeUtil.h"
#include "Engine/Input/Map/InputMap.h"
#include "Engine/Input/RawInputEvent.h"
#include "Engine/Input/Virtual/VirtualJoystick.h"

/**
 * @file InputMapEvaluate.cpp
 * @brief InputMap 의 평가기 · 상태(매 프레임 update 상태 머신, 바인딩 평가, 액션 상태 조회)를 구현합니다.
 * @details 파일 나눔은 InputMap.cpp 머리말을 본다.
 */

namespace sw
{
    namespace
    {
        /** @brief 이보다 좁은 데드존 구간은 0 으로 나누지 않으려고 구간 없음(가득 참)으로 본다. */
        constexpr float32 kMinStickDeadRange = 0.0001f;

        /** @brief 데드존 밖 크기(0 ~ 1)를 [안쪽 데드존, 바깥 데드존] 구간으로 펴고 응답 지수를 적용합니다. */
        float32 shapeStickMagnitude( float32 magnitude, float32 inDeadzone, float32 deadRange, float32 responseExponent )
        {
            float32 norm = deadRange > kMinStickDeadRange ? MathUtil::clamp( ( magnitude - inDeadzone ) / deadRange, 0.0f, 1.0f ) : 1.0f;
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

    bool InputMap::isActionToggled( const hashed_string& action ) const
    {
        const ActionEntry* pEntry = findAction( action );
        return pEntry != nullptr && pEntry->_bToggleState == SW_TRUE;
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

#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Input/IInputDevice.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/Map/InputMap.h"
#include "Engine/Input/Virtual/VirtualJoystick.h"

/**
 * @file InputMapEvaluate.cpp
 * @brief InputMap 의 매 프레임 평가 로직을 구현합니다. `update()` 가 매 틱 부르는 상태 머신입니다.
 *
 * 액션이 지금 눌려 있는지, 막 트리거됐는지가 어떻게 정해지는지 보려면 이 파일부터 읽으십시오.
 *  1) `update()`: 모든 액션을 돌며 아래 함수를 부르고, ActionPhase(Started/Ongoing/...)를 갱신합니다.
 *  2) `evaluateBindingDown()`: 바인딩 하나(키, 스틱, 조합 키, 가상 조이스틱 등)가 지금 얼마나 눌려 있는지 원시 장치에서 읽습니다.
 *  3) `evaluateTrigger()`: ActionTrigger(Pressed/Released/Hold/Tap/...)가 이번 프레임에 발화했는지 판정합니다.
 *  4) `isBindingLayerActive()`: 그 바인딩이 속한 레이어가 지금 활성인지(LIFO 스택, enabled) 확인합니다.
 */

namespace sw
{
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

            // 접근성 토글 처리
            if ( actionEntry._bPressed == SW_TRUE && actionEntry._bToggleMode == SW_TRUE )
                actionEntry._bToggleState = ( actionEntry._bToggleState == SW_TRUE ) ? SW_FALSE : SW_TRUE;

            // 커맨드 이력 기록
            if ( actionEntry._bTriggered == SW_TRUE )
                _actionBuffer.recordTriggered( actionName );

            dispatchActionCallbacks( actionEntry, bPrevDown );
        }
    }

    void InputMap::updateBinding( const ActionBinding& binding, ActionBindingState& state, float32 deltaSeconds, const int2& mousePos, ActionFrameAccum& accum ) const
    {
        // 꺼진 레이어의 바인딩이나 콘솔이 가린 키도 장치를 읽어 눌림을 추적한다. 다시 보이는 프레임에 이미 눌려 있던 입력을 새 누름으로 읽지 않기 위해서다(아래 `_bWasDown`).
        const bool bLayerActive = isBindingLayerActive( binding );
        float2     deviceValue{ 0.0f, 0.0f };
        float2     bindingValue{ 0.0f, 0.0f };
        const bool bPhysicalDown = evaluateBindingDown( binding, deviceValue, SlotRead::Device );
        const bool bRawDown      = bLayerActive && evaluateBindingDown( binding, bindingValue, SlotRead::Game );
        // 장치는 눌려 있는데 레이어가 켜진 게임이 안 눌림으로 보면 콘솔 · 포커스가 눌린 키를 가린 것이다.
        const bool bLive = bLayerActive && ( bPhysicalDown == false || bRawDown );
        if ( binding._bNegate )
        {
            bindingValue._x = -bindingValue._x;
            bindingValue._y = -bindingValue._y;
        }
        // 바인딩마다 시간(`_triggerTime`)이 있으면 홀드 판정과 더블 판정 창도 그 시간을 쓴다. 다른 트리거는 맵 값 그대로다.
        const bool    bHoldTrigger   = binding._trigger == ActionTrigger::HoldThreshold || binding._trigger == ActionTrigger::HoldAndRelease;
        const uint32  requiredTaps   = getRequiredTapCount( binding );
        const bool    bDoubleTrigger = requiredTaps > 0;
        const float32 holdTime       = bHoldTrigger ? getEffectiveTriggerTime( binding ) : _holdThreshold;
        const float32 doubleWindow   = bDoubleTrigger ? getEffectiveTriggerTime( binding ) : _doubleClickTime;

        state._bWasDown = state._bDown;
        state._bDown    = bRawDown ? SW_TRUE : SW_FALSE;
        // 바인딩이 막 보이게 됐고(레이어가 켜짐, 콘솔이 닫혀 키가 다시 보임, 리바인딩 · 복원 · 새 바인딩으로 상태가 새로 만들어짐) 그 전부터 눌려 있었으면
        // Pressed 가 아니라 Held 로 시작한다(눌린 채 켜진 입력을 새 누름으로 읽으면 일시정지를 연 Esc 가 층 안 Back 이 된다).
        // 눌린 채 보이게 된 프레임에 뗐으면 아무 일도 아니다. 보인 뒤 뗐다 다시 누른 입력은 새 누름이다.
        const bool bJustBecameLive = bLive && state._bLive == SW_FALSE;
        if ( bJustBecameLive && state._bPhysicalDown == SW_TRUE )
            state._bWasDown = state._bDown;
        state._bPhysicalDown = bPhysicalDown ? SW_TRUE : SW_FALSE;
        state._bLive         = bLive ? SW_TRUE : SW_FALSE;

        state._bPressed       = ( state._bDown == SW_TRUE && state._bWasDown == SW_FALSE ) ? SW_TRUE : SW_FALSE;
        state._bReleased      = ( state._bDown == SW_FALSE && state._bWasDown == SW_TRUE ) ? SW_TRUE : SW_FALSE;
        state._bDoubleClicked = SW_FALSE;
        state._bHoldThreshold = SW_FALSE;

        if ( state._timeSinceLastPress < InputMapDefaults::kNeverPressedSentinel * 0.5f )
            state._timeSinceLastPress += deltaSeconds;

        if ( state._bDown == SW_TRUE )
        {
            state._holdDuration += deltaSeconds;
            if ( state._holdDuration >= holdTime )
                state._bHoldThreshold = SW_TRUE;
            state._pulseTimer += deltaSeconds;
        }

        if ( state._bPressed == SW_TRUE )
            advanceTapStreak( binding, state, requiredTaps, doubleWindow, mousePos );

        const bool bTriggerFired = evaluateTrigger( binding, state, deltaSeconds );
        state._bTriggered        = bTriggerFired ? SW_TRUE : SW_FALSE;

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
        if ( state._holdDuration >= holdTime )
            accum._bAnyHoldReached = true;

        if ( state._bDown == SW_FALSE )
        {
            state._holdDuration = 0.0f;
            state._pulseTimer   = 0.0f;
        }

        if ( bRawDown )
        {
            float2& accumulator = BindingKinds::isRelative( binding ) ? accum._relativeValue : accum._totalValue;
            accumulator._x += bindingValue._x;
            accumulator._y += bindingValue._y;
        }
    }

    void InputMap::advanceTapStreak( const ActionBinding& binding, ActionBindingState& state, uint32 requiredTaps, float32 doubleWindow, const int2& mousePos ) const
    {
        const bool bIsMouseBinding  = ( binding._arrSlot[0]._deviceKind == InputDeviceKind::Mouse );
        bool       bContinuesStreak = false;

        if ( state._timeSinceLastPress <= doubleWindow )
        {
            if ( bIsMouseBinding )
            {
                const int2    pressDelta = mousePos - state._lastPress;
                const float32 distSq     = static_cast<float32>( pressDelta._x * pressDelta._x + pressDelta._y * pressDelta._y );
                if ( distSq <= ( _doubleClickMaxDistance * _doubleClickMaxDistance ) )
                    bContinuesStreak = true;
            }
            else
            {
                bContinuesStreak = true;
            }
        }

        // 간격 안에 이어진 누름을 센다. 필요한 수(DoubleTap · DoubleClicked 는 2, MultiTap 은 `_tapCount`)에 닿으면
        // 발화하고 처음부터 다시 센다. 2 일 때는 옛 더블 판정과 같다. 다른 트리거도 `_bDoubleClicked` 는 2 연타로 둔다.
        const uint32 streakGoal = ( requiredTaps > 0 ) ? requiredTaps : 2u;
        state._tapStreak        = bContinuesStreak ? static_cast<uint8>( state._tapStreak + 1 ) : static_cast<uint8>( 1 );
        if ( state._tapStreak >= streakGoal )
        {
            state._bDoubleClicked     = SW_TRUE;
            state._timeSinceLastPress = InputMapDefaults::kNeverPressedSentinel;
            state._tapStreak          = 0;
        }
        else
        {
            state._timeSinceLastPress = 0.0f;
        }
        state._lastPress = mousePos;
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

    void InputMap::dispatchActionCallbacks( const ActionEntry& actionEntry, bool bPrevDown )
    {
        // 델리게이트 이벤트 디스패치(Triggered)
        if ( actionEntry._bTriggered == SW_TRUE )
        {
            for ( const ActionCallbackEntry& cbEntry : actionEntry._listActionCallback )
            {
                if ( cbEntry._callback.isBound() )
                    cbEntry._callback();
            }
        }

        // 페이즈 델리게이트 디스패치(Phase)
        if ( actionEntry._currentPhase != ActionPhase::None )
        {
            for ( const PhaseCallbackEntry& phaseEntry : actionEntry._listPhaseCallback )
            {
                if ( phaseEntry._phase == actionEntry._currentPhase && phaseEntry._callback.isBound() )
                    phaseEntry._callback();
            }
        }

        // 2D 벡터 델리게이트 디스패치
        if ( ( actionEntry._currentValue._x != 0.0f || actionEntry._currentValue._y != 0.0f ) || ( bPrevDown && actionEntry._bDown == SW_FALSE ) )
        {
            for ( const Vector2DCallbackDelegate& vecCb : actionEntry._listVector2DCallback )
            {
                if ( vecCb.isBound() )
                    vecCb( actionEntry._currentValue );
            }
        }
    }

    float2 InputMap::composeActionValue( const float2& bounded, const float2& relative ) const
    {
        float2 value = bounded;
        if ( _playerSettings._bInvertX )
            value._x = -value._x;
        if ( _playerSettings._bInvertY )
            value._y = -value._y;
        if ( _playerSettings._digitalNormalization == DigitalNormalization::Circular )
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

    InputMap::ActionBindingState InputMap::makeBindingState( const ActionBinding& binding ) const
    {
        float2             unused{ 0.0f, 0.0f };
        ActionBindingState state{};
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
        if ( read == SlotRead::Device || _bKeyboardFocusIgnored == SW_TRUE )
            return _pInput->getKeyboard() != nullptr && _pInput->getKeyboard()->isKeyDown( key );
        return _pInput->isKeyDown( key );
    }

    bool InputMap::wasKeyboardKeyPressed( Key key ) const
    {
        if ( _pInput == nullptr )
            return false;
        if ( _bKeyboardFocusIgnored == SW_TRUE )
            return _pInput->getKeyboard() != nullptr && _pInput->getKeyboard()->wasKeyPressed( key );
        return _pInput->wasKeyPressed( key );
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
        if ( _bKeyboardFocusIgnored == SW_TRUE )
            return false;
        // 개발 콘솔이 입력을 쥔 동안 게임 맵은 키보드 바인딩과 패드 바인딩을 모두 "안 눌림" 으로 본다(패드는 셸 맵이 콘솔 조작에 쓴다).
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
        const bool bCtrlHeld = isKeyboardKeyDown( Key::LeftControl, read ) || isKeyboardKeyDown( Key::RightControl, read );
        const bool bAltHeld  = isKeyboardKeyDown( Key::LeftAlt, read ) || isKeyboardKeyDown( Key::RightAlt, read );
        return bCtrlHeld || bAltHeld;
    }

    namespace
    {
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

    bool InputMap::evaluateGamepadStick( const ActionBinding& binding, float2& outValue ) const
    {
        GamepadDevice* pPad = _pInput->getGamepad( binding._deviceIndex );
        if ( pPad == nullptr || pPad->isConnected() == false )
            return false;
        const float2 stick = ( binding._stick == GamepadStick::Left ) ? pPad->getLeftStick() : pPad->getRightStick();
        // 한 성분만 내는 바인딩은 다른 성분을 읽지 않는다 — 원형 데드존이 두 성분을 함께 정규화하면 다른 성분이 1D 값에 섞인다.
        float32 stickX = binding._stickAxis == GamepadStickAxis::Y ? 0.0f : stick._x;
        float32 stickY = binding._stickAxis == GamepadStickAxis::X ? 0.0f : stick._y;

        const float32 inDeadzone  = _playerSettings._stickDeadzoneOverride >= 0.0f ? _playerSettings._stickDeadzoneOverride : binding._deadzone;
        const float32 outDeadzone = binding._outerDeadzone > inDeadzone ? binding._outerDeadzone : 1.0f;
        const float32 deadRange   = outDeadzone - inDeadzone;

        if ( _playerSettings._deadzoneShape == DeadzoneShape::Radial )
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

        stickX *= _playerSettings._gamepadSensitivity._x;
        stickY *= _playerSettings._gamepadSensitivity._y;
        if ( BindingKinds::isRelative( binding ) )
        {
            // 이동량으로 쓰는 스틱: 배율과 프레임 시간을 곱하고, 반전은 마우스 이동처럼 여기서 한 번만 적용한다(액션 값 단계는 이동량을 반전하지 않는다).
            // 한 성분만 내는 스틱(확대 같은 1D 액션)은 시점 반전을 받지 않는다 — 반전 설정은 2D 시점 이동의 것이다.
            const float32 frameScale = binding._bScaleByDelta ? _lastDeltaSeconds : 1.0f;
            const bool    bLook      = binding._stickAxis == GamepadStickAxis::Both;
            stickX *= binding._axisScale._x * frameScale * ( bLook && _playerSettings._bInvertX ? -1.0f : 1.0f );
            stickY *= binding._axisScale._y * frameScale * ( bLook && _playerSettings._bInvertY ? -1.0f : 1.0f );
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
            binding._bJoystickAnchored = false;
            return false;
        }

        const int2   curTilePos = _pInput->getMousePosition();
        const float2 curPos     = curTilePos.toFloat2();

        // 앵커는 고정 좌표가 아니라 활성화 버튼을 처음 누른 지점에 놓는다(모바일 온스크린 스틱에서 흔한 방식).
        if ( binding._bJoystickAnchored == false )
        {
            binding._joystickAnchor    = curPos;
            binding._bJoystickAnchored = true;
        }

        outValue = VirtualJoystick::computeVector( binding._joystickAnchor, curPos, binding._scale, binding._deadzone, binding._outerDeadzone );
        return ( outValue._x != 0.0f || outValue._y != 0.0f );
    }

    bool InputMap::areShortcutModifiersHeld( const ActionBinding& binding, SlotRead read ) const
    {
        bool bModifierMatch = true;
        if ( ( binding._modifierMask & ModifierKey::Ctrl ) != 0 )
            bModifierMatch = bModifierMatch && ( isKeyboardKeyDown( Key::LeftControl, read ) || isKeyboardKeyDown( Key::RightControl, read ) );
        if ( ( binding._modifierMask & ModifierKey::Shift ) != 0 )
            bModifierMatch = bModifierMatch && ( isKeyboardKeyDown( Key::LeftShift, read ) || isKeyboardKeyDown( Key::RightShift, read ) );
        if ( ( binding._modifierMask & ModifierKey::Alt ) != 0 )
            bModifierMatch = bModifierMatch && ( isKeyboardKeyDown( Key::LeftAlt, read ) || isKeyboardKeyDown( Key::RightAlt, read ) );
        if ( ( binding._modifierMask & ModifierKey::Super ) != 0 )
            bModifierMatch = bModifierMatch && ( isKeyboardKeyDown( Key::LeftSuper, read ) || isKeyboardKeyDown( Key::RightSuper, read ) );
        return bModifierMatch;
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
                if ( _playerSettings._digitalNormalization == DigitalNormalization::Circular && lenSq > 1.0f )
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
                outValue._x            = rdx * binding._scale * _playerSettings._mouseSensitivity._x * ( _playerSettings._bInvertX ? -1.0f : 1.0f );
                outValue._y            = rdy * binding._scale * _playerSettings._mouseSensitivity._y * ( _playerSettings._bInvertY ? -1.0f : 1.0f );
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
                if ( areShortcutModifiersHeld( binding, read ) == false )
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

    bool InputMap::evaluateTrigger( const ActionBinding& binding, const ActionBindingState& state, float32 deltaSeconds ) const
    {
        switch ( binding._trigger )
        {
            case ActionTrigger::Pressed:
            {
                return state._bPressed == SW_TRUE;
            }
            case ActionTrigger::Released:
            {
                return state._bReleased == SW_TRUE;
            }
            case ActionTrigger::Down:
            {
                return state._bDown == SW_TRUE;
            }
            case ActionTrigger::DoubleClicked:
            case ActionTrigger::DoubleTap:
            case ActionTrigger::MultiTap:
            {
                return state._bDoubleClicked == SW_TRUE;
            }
            case ActionTrigger::HoldThreshold:
            {
                return state._bHoldThreshold == SW_TRUE;
            }
            case ActionTrigger::HoldAndRelease:
            {
                return state._bReleased == SW_TRUE && state._holdDuration >= getEffectiveTriggerTime( binding );
            }
            case ActionTrigger::Tap:
            {
                return state._bReleased == SW_TRUE && state._holdDuration < getEffectiveTriggerTime( binding );
            }
            case ActionTrigger::Pulse:
            {
                // 누른 뒤 간격마다 한 번 발화한다. `타이머 >= 간격` 만 보면 첫 간격이 지난 뒤 매 프레임 발화해서 연사 속도가 프레임률을
                // 따라간다. 타이머는 뗄 때만 0 으로 돌아가므로, 이번 프레임에 간격 경계를 넘었는지로 판정한다(Repeat 과 같은 방식).
                // 한 프레임이 간격보다 길어도 한 번만 발화한다.
                if ( state._bDown == SW_FALSE )
                    return false;
                const float32 pulseInterval = getEffectiveTriggerTime( binding );
                const int32   stepNow       = static_cast<int32>( state._pulseTimer / pulseInterval );
                const int32   stepPrev      = static_cast<int32>( MathUtil::max( state._pulseTimer - deltaSeconds, 0.0f ) / pulseInterval );
                return stepNow > stepPrev;
            }
            case ActionTrigger::Repeat:
            {
                if ( state._bPressed == SW_TRUE )
                    return true;
                if ( state._bDown == SW_TRUE && state._holdDuration >= _navRepeatDelay )
                {
                    const float32 repeatTime     = state._holdDuration - _navRepeatDelay;
                    const float32 prevRepeatTime = ( state._holdDuration - deltaSeconds ) - _navRepeatDelay;
                    if ( prevRepeatTime < 0.0f )
                        return true;
                    const int32 stepNow  = static_cast<int32>( repeatTime / _navRepeatRate );
                    const int32 stepPrev = static_cast<int32>( prevRepeatTime / _navRepeatRate );
                    return stepNow > stepPrev;
                }
                return false;
            }
            case ActionTrigger::Count:
            {
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
        const LayerDefinition* pLayer = findLayer( layer );
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
                const LayerDefinition* pTopLayer = findLayer( *it );
                if ( pTopLayer != nullptr && pTopLayer->_bBlockLower == SW_TRUE )
                    return false;
            }
            return false;
        }

        return true;
    }
} // namespace sw

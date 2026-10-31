#include "pch.h"

#include "Engine/Input/Map/InputTriggerRules.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    void InputTriggerRules::advance( const ActionBinding& binding, InputBindingState& state, float32 deltaSeconds, const InputBindingSample& sample ) const
    {
        const int2& mousePos = sample._mousePos;
        // 바인딩마다 시간(`_triggerTime`)이 있으면 홀드 판정과 더블 판정 창도 그 시간을 쓴다. 다른 트리거는 맵 값 그대로다.
        const bool    bHoldTrigger   = binding._trigger == ActionTrigger::HoldThreshold || binding._trigger == ActionTrigger::HoldAndRelease;
        const uint32  requiredTaps   = getRequiredTapCount( binding );
        const bool    bDoubleTrigger = requiredTaps > 0;
        const float32 holdTime       = bHoldTrigger ? getEffectiveTriggerTime( binding ) : _holdThreshold;
        const float32 doubleWindow   = bDoubleTrigger ? getEffectiveTriggerTime( binding ) : _doubleClickTime;

        state._bWasDown = state._bDown;
        state._bDown    = sample._bDown ? SW_TRUE : SW_FALSE;
        // 바인딩이 막 보이게 됐고(레이어가 켜짐, 콘솔이 닫혀 키가 다시 보임, 리바인딩 · 복원 · 새 바인딩으로 상태가 새로 만들어짐) 그 전부터 눌려 있었으면
        // Pressed 가 아니라 Held 로 시작한다(눌린 채 켜진 입력을 새 누름으로 읽으면 일시정지를 연 Esc 가 층 안 Back 이 된다).
        // 눌린 채 보이게 된 프레임에 뗐으면 아무 일도 아니다. 보인 뒤 뗐다 다시 누른 입력은 새 누름이다.
        const bool bJustBecameLive = sample._bLive && state._bLive == SW_FALSE;
        if ( bJustBecameLive && state._bPhysicalDown == SW_TRUE )
            state._bWasDown = state._bDown;
        state._bPhysicalDown = sample._bPhysicalDown ? SW_TRUE : SW_FALSE;
        state._bLive         = sample._bLive ? SW_TRUE : SW_FALSE;

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
        advanceDrag( binding, state, mousePos );

        const bool bTriggerFired = evaluateTrigger( binding, state, deltaSeconds );
        state._bTriggered        = bTriggerFired ? SW_TRUE : SW_FALSE;
        // 뗀 프레임까지는 `Click` 이 끌렸는지 봐야 하므로 끌기 표시는 판정 뒤에 지운다.
        if ( state._bDown == SW_FALSE )
            state._bDragged = SW_FALSE;
    }

    float32 InputTriggerRules::getHoldTime( const ActionBinding& binding ) const
    {
        const bool bHoldTrigger = binding._trigger == ActionTrigger::HoldThreshold || binding._trigger == ActionTrigger::HoldAndRelease;
        return bHoldTrigger ? getEffectiveTriggerTime( binding ) : _holdThreshold;
    }

    void InputTriggerRules::endFrame( InputBindingState& state )
    {
        if ( state._bDown == SW_FALSE )
        {
            state._holdDuration = 0.0f;
            state._pulseTimer   = 0.0f;
        }
    }

    uint32 InputTriggerRules::getRequiredTapCount( const ActionBinding& binding )
    {
        if ( binding._trigger == ActionTrigger::MultiTap )
            return ( binding._tapCount < 2 ) ? 2u : static_cast<uint32>( binding._tapCount );
        if ( binding._trigger == ActionTrigger::DoubleTap || binding._trigger == ActionTrigger::DoubleClicked )
            return 2u;
        return 0u;
    }

    float32 InputTriggerRules::getEffectiveTriggerTime( const ActionBinding& binding ) const
    {
        float32 mapTime{ 0.0f };
        switch ( binding._trigger )
        {
            case ActionTrigger::HoldThreshold:
            case ActionTrigger::HoldAndRelease:
            {
                mapTime = _holdThreshold;
                break;
            }
            case ActionTrigger::Tap:
            {
                mapTime = InputMapDefaults::kTapMaxTime;
                break;
            }
            case ActionTrigger::Pulse:
            {
                mapTime = InputMapDefaults::kPulseInterval;
                break;
            }
            case ActionTrigger::DoubleClicked:
            case ActionTrigger::DoubleTap:
            case ActionTrigger::MultiTap:
            {
                mapTime = _doubleClickTime;
                break;
            }
            case ActionTrigger::Pressed:
            case ActionTrigger::Released:
            case ActionTrigger::Down:
            case ActionTrigger::Repeat: // 지연 · 간격 두 값이라 맵 값(`setNavRepeatTiming`)만 쓴다
            case ActionTrigger::Click:  // 시간이 아니라 거리(`_dragThreshold`)를 본다
            case ActionTrigger::Drag:
            case ActionTrigger::Count:
            {
                return 0.0f;
            }
        }
        return ( binding._triggerTime > 0.0f ) ? binding._triggerTime : mapTime;
    }

    bool InputTriggerRules::isPointerBinding( const ActionBinding& binding )
    {
        return binding._arrSlot[0]._deviceKind == InputDeviceKind::Mouse;
    }

    void InputTriggerRules::advanceDrag( const ActionBinding& binding, InputBindingState& state, const int2& mousePos ) const
    {
        state._bDragStarted = SW_FALSE;
        // 포인터가 없는 슬롯(키 · 패드)은 끌 수 없다 — 누름 위치는 그 누름의 마우스 위치가 아니다.
        const bool bUndraggedPointerHeld = state._bDown == SW_TRUE && state._bDragged == SW_FALSE && isPointerBinding( binding );
        if ( bUndraggedPointerHeld == false )
            return;
        const int2    moved  = mousePos - state._lastPress;
        const float32 distSq = static_cast<float32>( moved._x * moved._x + moved._y * moved._y );
        if ( distSq <= _dragThreshold * _dragThreshold )
            return;
        state._bDragged     = SW_TRUE;
        state._bDragStarted = SW_TRUE;
    }

    void InputTriggerRules::advanceTapStreak( const ActionBinding& binding, InputBindingState& state, uint32 requiredTaps, float32 doubleWindow, const int2& mousePos ) const
    {
        const bool bIsMouseBinding  = isPointerBinding( binding );
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

    bool InputTriggerRules::evaluateTrigger( const ActionBinding& binding, const InputBindingState& state, float32 deltaSeconds ) const
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
            case ActionTrigger::Click:
            {
                return state._bReleased == SW_TRUE && state._bDragged == SW_FALSE;
            }
            case ActionTrigger::Drag:
            {
                return state._bDragStarted == SW_TRUE;
            }
            case ActionTrigger::Count:
            {
                return false;
            }
        }
    }
} // namespace sw

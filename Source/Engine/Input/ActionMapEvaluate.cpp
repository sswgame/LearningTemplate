#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Input/ActionMap.h"
#include "Engine/Input/IInputDevice.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/Utils/VirtualJoystick.h"

/**
 * @file ActionMapEvaluate.cpp
 * @brief ActionMap의 매 프레임 평가 로직 — update()가 매 틱 호출하는 상태 머신입니다.
 *
 * 초심자 가이드: 액션이 지금 눌려 있는지, 막 트리거됐는지가 어떻게 결정되는지 보려면 이 파일부터 보세요.
 *  1) update() : 모든 액션을 순회하며 아래 두 함수를 호출하고, ActionPhase(Started/Ongoing/...)를 갱신합니다.
 *  2) evaluateBindingDown() : 바인딩 하나(키/스틱/조합키/가상 조이스틱 등)가 지금 얼마나 눌려 있는지 원시 장치에서 읽어옵니다.
 *  3) evaluateTrigger() : ActionTrigger(Pressed/Released/Hold/Tap/...)가 이번 프레임에 발화했는지 판정합니다.
 *  4) isBindingLayerActive() : 해당 바인딩이 속한 레이어가 지금 활성 상태인지(LIFO 스택/enabled) 확인합니다.
 */

namespace sw
{
	void ActionMap::update( float32 deltaSeconds )
	{
		_totalElapsedTime += deltaSeconds;

		// 1) 선입력 버퍼 갱신 (Ring Buffer O(1) 정리)
		for ( uint32 index = 0; index < _bufferedActionCount; ++index )
		{
			const uint32 idx = ( _bufferedActionHead + index ) % kMaxBufferedActions;
			_arrBufferedAction[idx]._remainingTime -= deltaSeconds;
		}
		while ( _bufferedActionCount > 0 && _arrBufferedAction[_bufferedActionHead]._remainingTime <= 0.0f )
		{
			_bufferedActionHead = ( _bufferedActionHead + 1 ) % kMaxBufferedActions;
			--_bufferedActionCount;
		}

		// 2) 커맨드 이력 만료 제거 (Ring Buffer O(1) 정리, 2.0초 초과)
		while ( _commandHistoryCount > 0 && ( _totalElapsedTime - _arrCommandHistory[_commandHistoryHead]._timestamp ) > 2.0f )
		{
			_commandHistoryHead = ( _commandHistoryHead + 1 ) % kMaxCommandHistory;
			--_commandHistoryCount;
		}

		if ( _pInput == nullptr )
			return;

		int32 curMouseX{ 0 };
		int32 curMouseY{ 0 };
		_pInput->getMousePosition( curMouseX, curMouseY );

		// 3) 통합 액션 런타임 평가 및 ActionPhase 상태 머신
		for ( auto& [actionName, actIndex] : _mapAction )
		{
			ActionEntry& actionEntry = _listActionEntry[actIndex];

			const size_t bindCount = actionEntry._listBinding.size();
			if ( actionEntry._listBindingState.size() != bindCount )
				actionEntry._listBindingState.resize( bindCount );

			bool	anyDown{ false };
			bool	anyPressed{ false };
			bool	anyReleased{ false };
			bool	anyDoubleClicked{ false };
			bool	anyHoldThreshold{ false };
			bool	anyTriggered{ false };
			float32 maxHold{ 0.0f };
			float2	totalAccumValue{ 0.0f, 0.0f };

			for ( size_t bindIndex = 0; bindIndex < bindCount; ++bindIndex )
			{
				const ActionBinding& binding = actionEntry._listBinding[bindIndex];
				ActionBindingState&	 state	 = actionEntry._listBindingState[bindIndex];

				const bool bLayerActive = isBindingLayerActive( binding );
				float2	   bindingValue{ 0.0f, 0.0f };
				const bool bRawDown = bLayerActive && evaluateBindingDown( binding, bindingValue );

				state._bWasDown = state._bDown;
				state._bDown	= bRawDown ? SW_TRUE : SW_FALSE;

				state._bPressed		  = ( state._bDown == SW_TRUE && state._bWasDown == SW_FALSE ) ? SW_TRUE : SW_FALSE;
				state._bReleased	  = ( state._bDown == SW_FALSE && state._bWasDown == SW_TRUE ) ? SW_TRUE : SW_FALSE;
				state._bDoubleClicked = SW_FALSE;
				state._bHoldThreshold = SW_FALSE;

				if ( state._timeSinceLastPress < ActionMapDefaults::kNeverPressedSentinel * 0.5f )
					state._timeSinceLastPress += deltaSeconds;

				if ( state._bDown == SW_TRUE )
				{
					state._holdDuration += deltaSeconds;
					if ( state._holdDuration >= _holdThreshold )
						state._bHoldThreshold = SW_TRUE;
					state._pulseTimer += deltaSeconds;
				}

				if ( state._bPressed == SW_TRUE )
				{
					const bool bIsMouseBinding = ( binding._arrSlot[0]._deviceKind == InputDeviceKind::Mouse );
					bool	   bDoubleDetected = false;

					if ( state._timeSinceLastPress <= _doubleClickTime )
					{
						if ( bIsMouseBinding )
						{
							const float32 distSq = static_cast<float32>( ( curMouseX - state._lastPressX ) * ( curMouseX - state._lastPressX ) +
																		 ( curMouseY - state._lastPressY ) * ( curMouseY - state._lastPressY ) );
							if ( distSq <= ( _doubleClickMaxDistance * _doubleClickMaxDistance ) )
								bDoubleDetected = true;
						}
						else
						{
							bDoubleDetected = true;
						}
					}

					if ( bDoubleDetected )
					{
						state._bDoubleClicked	  = SW_TRUE;
						state._timeSinceLastPress = ActionMapDefaults::kNeverPressedSentinel;
					}
					else
					{
						state._timeSinceLastPress = 0.0f;
					}
					state._lastPressX = curMouseX;
					state._lastPressY = curMouseY;
				}

				const bool bTriggerFired = evaluateTrigger( binding._trigger, state, deltaSeconds );
				state._bTriggered		 = bTriggerFired ? SW_TRUE : SW_FALSE;

				if ( state._bDown == SW_TRUE )
					anyDown = true;
				if ( state._bPressed == SW_TRUE )
					anyPressed = true;
				if ( state._bReleased == SW_TRUE )
					anyReleased = true;
				if ( state._bDoubleClicked == SW_TRUE )
					anyDoubleClicked = true;
				if ( state._bHoldThreshold == SW_TRUE )
					anyHoldThreshold = true;
				if ( state._bTriggered == SW_TRUE )
					anyTriggered = true;
				if ( state._holdDuration > maxHold )
					maxHold = state._holdDuration;

				if ( state._bDown == SW_FALSE )
				{
					state._holdDuration = 0.0f;
					state._pulseTimer	= 0.0f;
				}

				if ( bRawDown )
				{
					totalAccumValue._x += bindingValue._x;
					totalAccumValue._y += bindingValue._y;
				}
			}

			const bool bPrevDown		= actionEntry._bDown == SW_TRUE;
			actionEntry._bDown			= anyDown ? SW_TRUE : SW_FALSE;
			actionEntry._bPressed		= anyPressed ? SW_TRUE : SW_FALSE;
			actionEntry._bReleased		= anyReleased ? SW_TRUE : SW_FALSE;
			actionEntry._bDoubleClicked = anyDoubleClicked ? SW_TRUE : SW_FALSE;
			actionEntry._bHoldThreshold = anyHoldThreshold ? SW_TRUE : SW_FALSE;
			actionEntry._bTriggered		= anyTriggered ? SW_TRUE : SW_FALSE;
			actionEntry._holdDuration	= maxHold;

			// 모디파이어 적용 (축 반전 및 클램핑/원형 정규화)
			if ( _bInvertX == SW_TRUE )
				totalAccumValue._x = -totalAccumValue._x;
			if ( _bInvertY == SW_TRUE )
				totalAccumValue._y = -totalAccumValue._y;

			if ( _digitalNormalization == DigitalNormalization::Circular )
			{
				const float32 lenSq = totalAccumValue._x * totalAccumValue._x + totalAccumValue._y * totalAccumValue._y;
				if ( lenSq > 1.0f )
				{
					const float32 invLen = 1.0f / MathUtil::sqrt( lenSq );
					totalAccumValue._x *= invLen;
					totalAccumValue._y *= invLen;
				}
			}
			else
			{
				totalAccumValue._x = totalAccumValue._x < -1.0f ? -1.0f : ( totalAccumValue._x > 1.0f ? 1.0f : totalAccumValue._x );
				totalAccumValue._y = totalAccumValue._y < -1.0f ? -1.0f : ( totalAccumValue._y > 1.0f ? 1.0f : totalAccumValue._y );
			}
			actionEntry._currentValue = totalAccumValue;

			// --------------------------------------------------------------------------
			// 상용 엔진 표준 ActionPhase 상태 머신 전이
			// --------------------------------------------------------------------------
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
				actionEntry._currentPhase = ( actionEntry._currentPhase == ActionPhase::Triggered || actionEntry._currentPhase == ActionPhase::Ongoing || maxHold >= _holdThreshold )
											  ? ActionPhase::Completed
											  : ActionPhase::Canceled;
			}
			else
			{
				actionEntry._currentPhase = ActionPhase::None;
			}

			// 접근성 토글 처리
			if ( actionEntry._bPressed == SW_TRUE && actionEntry._bToggleMode == SW_TRUE )
			{
				actionEntry._bToggleState = ( actionEntry._bToggleState == SW_TRUE ) ? SW_FALSE : SW_TRUE;
			}

			// 커맨드 이력 기록 (Ring Buffer)
			if ( actionEntry._bTriggered == SW_TRUE )
			{
				const uint32 insertIdx					 = ( _commandHistoryHead + _commandHistoryCount ) % kMaxCommandHistory;
				_arrCommandHistory[insertIdx]._action	 = actionName;
				_arrCommandHistory[insertIdx]._timestamp = _totalElapsedTime;
				if ( _commandHistoryCount < kMaxCommandHistory )
					++_commandHistoryCount;
				else
					_commandHistoryHead = ( _commandHistoryHead + 1 ) % kMaxCommandHistory;
			}

			// 델리게이트 이벤트 디스패치 (Triggered)
			if ( actionEntry._bTriggered == SW_TRUE )
			{
				for ( const ActionCallbackEntry& cbEntry : actionEntry._listActionCallback )
				{
					if ( cbEntry._callback.isBound() )
						cbEntry._callback();
				}
			}

			// 페이즈 델리게이트 디스패치 (Phase)
			if ( actionEntry._currentPhase != ActionPhase::None )
			{
				for ( const PhaseCallbackEntry& phaseEntry : actionEntry._listPhaseCallback )
				{
					if ( phaseEntry._phase == actionEntry._currentPhase && phaseEntry._callback.isBound() )
						phaseEntry._callback();
				}
			}

			// 2D 벡터 델리게이트 디스패치
			if ( ( actionEntry._currentValue._x != 0.0f || actionEntry._currentValue._y != 0.0f ) ||
				 ( bPrevDown && actionEntry._bDown == SW_FALSE ) )
			{
				for ( const Vector2DCallbackDelegate& vecCb : actionEntry._listVector2DCallback )
				{
					if ( vecCb.isBound() )
						vecCb( actionEntry._currentValue );
				}
			}
		}
	}

	bool ActionMap::evaluateBindingDown( const ActionBinding& binding, float2& outValue ) const
	{
		if ( _pInput == nullptr )
			return false;

		switch ( binding._kind )
		{
			case BindingKind::SingleSlot:
			{
				IInputDevice* pDevice = _pInput->getDevice( binding._arrSlot[0]._deviceKind, binding._arrSlot[0]._deviceIndex );
				// isControlDown()만 보면 같은 프레임 안에서 Down+Up이 모두 처리된 순간 탭(예: 매크로 주입, 초고속 입력)을
				// 놓칩니다. wasControlPressed()를 함께 확인해 그 프레임엔 "눌렸었다"로 취급합니다.
				if ( pDevice != nullptr && ( pDevice->isControlDown( binding._arrSlot[0]._controlIndex ) || pDevice->wasControlPressed( binding._arrSlot[0]._controlIndex ) ) )
				{
					if ( _bSuppressBaseActionOnChord == SW_TRUE && binding._arrSlot[0]._deviceKind == InputDeviceKind::Keyboard )
					{
						const Key  key		 = static_cast<Key>( binding._arrSlot[0]._controlIndex );
						const bool bIsModKey = ( key == Key::LeftControl || key == Key::RightControl || key == Key::LeftShift || key == Key::RightShift || key == Key::LeftAlt || key == Key::RightAlt || key == Key::LeftSuper || key == Key::RightSuper );
						if ( bIsModKey == false )
						{
							const bool bCtrlHeld = _pInput->isKeyDown( Key::LeftControl ) || _pInput->isKeyDown( Key::RightControl );
							const bool bAltHeld	 = _pInput->isKeyDown( Key::LeftAlt ) || _pInput->isKeyDown( Key::RightAlt );
							if ( bCtrlHeld || bAltHeld )
								return false;
						}
					}
					outValue = float2{ 1.0f, 0.0f };
					return true;
				}
				return false;
			}
			case BindingKind::Axis1DComposite:
			{
				IInputDevice* pNegDev = _pInput->getDevice( binding._arrSlot[0]._deviceKind, binding._arrSlot[0]._deviceIndex );
				IInputDevice* pPosDev = _pInput->getDevice( binding._arrSlot[1]._deviceKind, binding._arrSlot[1]._deviceIndex );
				float32		  v		  = 0.0f;
				if ( pNegDev != nullptr && pNegDev->isControlDown( binding._arrSlot[0]._controlIndex ) )
					v -= 1.0f;
				if ( pPosDev != nullptr && pPosDev->isControlDown( binding._arrSlot[1]._controlIndex ) )
					v += 1.0f;
				outValue = float2{ v, 0.0f };
				return v != 0.0f;
			}
			case BindingKind::Vector2DComposite:
			{
				IInputDevice* pUpDev	= _pInput->getDevice( binding._arrSlot[0]._deviceKind, binding._arrSlot[0]._deviceIndex );
				IInputDevice* pDownDev	= _pInput->getDevice( binding._arrSlot[1]._deviceKind, binding._arrSlot[1]._deviceIndex );
				IInputDevice* pLeftDev	= _pInput->getDevice( binding._arrSlot[2]._deviceKind, binding._arrSlot[2]._deviceIndex );
				IInputDevice* pRightDev = _pInput->getDevice( binding._arrSlot[3]._deviceKind, binding._arrSlot[3]._deviceIndex );

				float2 kbdVec{ 0.0f, 0.0f };
				if ( pUpDev != nullptr && pUpDev->isControlDown( binding._arrSlot[0]._controlIndex ) )
					kbdVec._y += 1.0f;
				if ( pDownDev != nullptr && pDownDev->isControlDown( binding._arrSlot[1]._controlIndex ) )
					kbdVec._y -= 1.0f;
				if ( pLeftDev != nullptr && pLeftDev->isControlDown( binding._arrSlot[2]._controlIndex ) )
					kbdVec._x -= 1.0f;
				if ( pRightDev != nullptr && pRightDev->isControlDown( binding._arrSlot[3]._controlIndex ) )
					kbdVec._x += 1.0f;

				const float32 lenSq = kbdVec._x * kbdVec._x + kbdVec._y * kbdVec._y;
				if ( _digitalNormalization == DigitalNormalization::Circular && lenSq > 1.0f )
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
				GamepadDevice* pPad = _pInput->getGamepad( binding._deviceIndex );
				if ( pPad != nullptr && pPad->isConnected() )
				{
					float32 sx{ 0.0f };
					float32 sy{ 0.0f };
					if ( binding._stick == GamepadStick::Left )
						pPad->getLeftStick( sx, sy );
					else
						pPad->getRightStick( sx, sy );

					const float32 inDeadzone  = binding._deadzone;
					const float32 outDeadzone = binding._outerDeadzone > inDeadzone ? binding._outerDeadzone : 1.0f;
					const float32 deadRange	  = outDeadzone - inDeadzone;

					if ( _deadzoneShape == DeadzoneShape::Radial )
					{
						const float32 mag = MathUtil::sqrt( sx * sx + sy * sy );
						if ( mag <= inDeadzone )
						{
							sx = 0.0f;
							sy = 0.0f;
						}
						else
						{
							float32 norm = deadRange > 0.0001f ? MathUtil::clamp( ( mag - inDeadzone ) / deadRange, 0.0f, 1.0f ) : 1.0f;
							if ( binding._responseExponent != 1.0f && binding._responseExponent > 0.0f )
							{
								norm = MathUtil::pow( norm, binding._responseExponent );
							}
							sx = ( sx / mag ) * norm;
							sy = ( sy / mag ) * norm;
						}
					}
					else // Axial
					{
						auto applyAxialDeadzone = [&]( float32 val ) -> float32
						{
							const float32 absVal = MathUtil::abs( val );
							if ( absVal <= inDeadzone )
								return 0.0f;
							float32 norm = deadRange > 0.0001f ? MathUtil::clamp( ( absVal - inDeadzone ) / deadRange, 0.0f, 1.0f ) : 1.0f;
							if ( binding._responseExponent != 1.0f && binding._responseExponent > 0.0f )
							{
								norm = MathUtil::pow( norm, binding._responseExponent );
							}
							return ( val > 0.0f ? 1.0f : -1.0f ) * norm;
						};
						sx = applyAxialDeadzone( sx );
						sy = applyAxialDeadzone( sy );
					}

					sx *= _gamepadSensitivity._x;
					sy *= _gamepadSensitivity._y;
					outValue = float2{ sx, sy };
					return ( sx != 0.0f || sy != 0.0f );
				}
				return false;
			}
			case BindingKind::Chord:
			{
				IInputDevice* pModDev  = _pInput->getDevice( binding._arrSlot[0]._deviceKind, binding._arrSlot[0]._deviceIndex );
				IInputDevice* pTrigDev = _pInput->getDevice( binding._arrSlot[1]._deviceKind, binding._arrSlot[1]._deviceIndex );
				if ( pModDev != nullptr && pTrigDev != nullptr )
				{
					const bool bModDown	 = pModDev->isControlDown( binding._arrSlot[0]._controlIndex );
					const bool bTrigDown = pTrigDev->isControlDown( binding._arrSlot[1]._controlIndex );
					if ( bModDown && bTrigDown )
					{
						outValue = float2{ 1.0f, 0.0f };
						return true;
					}
				}
				return false;
			}
			case BindingKind::MouseDelta2D:
			{
				float32 rdx{ 0.0f };
				float32 rdy{ 0.0f };
				_pInput->getRawMouseDelta( rdx, rdy );
				if ( rdx == 0.0f && rdy == 0.0f )
				{
					int32 dx{ 0 };
					int32 dy{ 0 };
					_pInput->getMouseDelta( dx, dy );
					rdx = static_cast<float32>( dx );
					rdy = static_cast<float32>( dy );
				}
				outValue._x = rdx * binding._scale * _mouseSensitivity._x * ( _bInvertX == SW_TRUE ? -1.0f : 1.0f );
				outValue._y = rdy * binding._scale * _mouseSensitivity._y * ( _bInvertY == SW_TRUE ? -1.0f : 1.0f );
				return ( rdx != 0.0f || rdy != 0.0f );
			}
			case BindingKind::VirtualJoystick2D:
			{
				IInputDevice* pActivationDevice = _pInput->getDevice( binding._arrSlot[0]._deviceKind, binding._arrSlot[0]._deviceIndex );
				const bool	  bActivationDown	= pActivationDevice != nullptr && pActivationDevice->isControlDown( binding._arrSlot[0]._controlIndex );

				if ( bActivationDown == false )
				{
					binding._bJoystickAnchored = false;
					return false;
				}

				int32 curX{ 0 };
				int32 curY{ 0 };
				_pInput->getMousePosition( curX, curY );
				const float2 curPos{ static_cast<float32>( curX ), static_cast<float32>( curY ) };

				// 앵커는 고정 좌표가 아니라 활성화 버튼을 처음 누른 지점에서 플로팅됩니다 (모바일 온스크린 스틱 표준 UX).
				if ( binding._bJoystickAnchored == false )
				{
					binding._joystickAnchor	   = curPos;
					binding._bJoystickAnchored = true;
				}

				outValue = VirtualJoystick::calculateVector( binding._joystickAnchor, curPos, binding._scale, binding._deadzone, binding._outerDeadzone );
				return ( outValue._x != 0.0f || outValue._y != 0.0f );
			}
			case BindingKind::Shortcut:
			{
				bool bModMatch = true;
				if ( ( binding._modifierMask & ModifierKey::Ctrl ) != 0 )
					bModMatch = bModMatch && ( _pInput->isKeyDown( Key::LeftControl ) || _pInput->isKeyDown( Key::RightControl ) );
				if ( ( binding._modifierMask & ModifierKey::Shift ) != 0 )
					bModMatch = bModMatch && ( _pInput->isKeyDown( Key::LeftShift ) || _pInput->isKeyDown( Key::RightShift ) );
				if ( ( binding._modifierMask & ModifierKey::Alt ) != 0 )
					bModMatch = bModMatch && ( _pInput->isKeyDown( Key::LeftAlt ) || _pInput->isKeyDown( Key::RightAlt ) );
				if ( ( binding._modifierMask & ModifierKey::Super ) != 0 )
					bModMatch = bModMatch && ( _pInput->isKeyDown( Key::LeftSuper ) || _pInput->isKeyDown( Key::RightSuper ) );

				if ( bModMatch == false )
					return false;

				IInputDevice* pDev = _pInput->getDevice( binding._arrSlot[0]._deviceKind, binding._arrSlot[0]._deviceIndex );
				if ( pDev != nullptr && pDev->isControlDown( binding._arrSlot[0]._controlIndex ) )
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
			default:
				return false;
		}
	}

	bool ActionMap::evaluateTrigger( ActionTrigger trigger, const ActionBindingState& state, float32 deltaSeconds ) const
	{
		switch ( trigger )
		{
			case ActionTrigger::Pressed:
				return state._bPressed == SW_TRUE;
			case ActionTrigger::Released:
				return state._bReleased == SW_TRUE;
			case ActionTrigger::Down:
				return state._bDown == SW_TRUE;
			case ActionTrigger::DoubleClicked:
			case ActionTrigger::DoubleTap:
				return state._bDoubleClicked == SW_TRUE;
			case ActionTrigger::HoldThreshold:
				return state._bHoldThreshold == SW_TRUE;
			case ActionTrigger::HoldAndRelease:
				return state._bReleased == SW_TRUE && state._holdDuration >= _holdThreshold;
			case ActionTrigger::Tap:
				return state._bReleased == SW_TRUE && state._holdDuration < ActionMapDefaults::kTapMaxTime;
			case ActionTrigger::Pulse:
				return state._bDown == SW_TRUE && state._pulseTimer >= ActionMapDefaults::kPulseInterval;
			case ActionTrigger::Repeat:
			{
				if ( state._bPressed == SW_TRUE )
					return true;
				if ( state._bDown == SW_TRUE && state._holdDuration >= _navRepeatDelay )
				{
					const float32 repeatTime	 = state._holdDuration - _navRepeatDelay;
					const float32 prevRepeatTime = ( state._holdDuration - deltaSeconds ) - _navRepeatDelay;
					if ( prevRepeatTime < 0.0f )
						return true;
					const int32 stepNow	 = static_cast<int32>( repeatTime / _navRepeatRate );
					const int32 stepPrev = static_cast<int32>( prevRepeatTime / _navRepeatRate );
					return stepNow > stepPrev;
				}
				return false;
			}
			case ActionTrigger::Count:
			default:
				return false;
		}
	}

	bool ActionMap::isBindingLayerActive( const ActionBinding& binding ) const
	{
		if ( binding._pCachedLayer == nullptr )
		{
			binding._pCachedLayer = findLayer( binding._layer );
		}

		if ( binding._pCachedLayer != nullptr && _listLayerStack.empty() )
		{
			if ( binding._pCachedLayer->_bEnabled == SW_FALSE )
				return false;
			return true;
		}

		return isLayerActiveInternal( binding._layer );
	}

	bool ActionMap::isLayerActiveInternal( const hashed_string& layer ) const
	{
		const LayerDef* pDef = findLayer( layer );
		if ( pDef == nullptr || pDef->_bEnabled == SW_FALSE )
			return false;
		if ( pDef->_bAlwaysOn == SW_TRUE )
			return true;

		if ( _listLayerStack.empty() == false )
		{
			for ( auto it = _listLayerStack.rbegin(); it != _listLayerStack.rend(); ++it )
			{
				if ( *it == layer )
					return true;
				const LayerDef* pTopDef = findLayer( *it );
				if ( pTopDef != nullptr && pTopDef->_bBlockLower == SW_TRUE )
					return false;
			}
			return false;
		}

		return true;
	}
} // namespace sw

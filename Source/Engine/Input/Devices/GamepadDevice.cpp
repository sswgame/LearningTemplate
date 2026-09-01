#include "pch.h"

#include "Engine/Input/Devices/GamepadDevice.h"

namespace sw
{
	GamepadDevice::GamepadDevice( uint32 deviceIndex )
		: _deviceIndex{ deviceIndex }
		, _buttonMask{ 0 }
		, _prevButtonMask{ 0 }
		, _leftStickX{ 0.0f }
		, _leftStickY{ 0.0f }
		, _rightStickX{ 0.0f }
		, _rightStickY{ 0.0f }
		, _leftTrigger{ 0.0f }
		, _rightTrigger{ 0.0f }
	{
	}

	void GamepadDevice::onFrameBegin( [[maybe_unused]] float32 deltaTime )
	{
		_prevButtonMask = _buttonMask;
	}

	void GamepadDevice::onFrameEnd()
	{
	}

	void GamepadDevice::resetState()
	{
		_buttonMask		= 0;
		_prevButtonMask = 0;
		_leftStickX		= 0.0f;
		_leftStickY		= 0.0f;
		_rightStickX	= 0.0f;
		_rightStickY	= 0.0f;
		_leftTrigger	= 0.0f;
		_rightTrigger	= 0.0f;
		stopVibration();
	}

	bool GamepadDevice::isControlDown( uint16 controlIndex ) const
	{
		if ( controlIndex >= static_cast<uint16>( GamepadButton::Count ) )
			return false;
		return isButtonDown( static_cast<GamepadButton>( controlIndex ) );
	}

	bool GamepadDevice::wasControlPressed( uint16 controlIndex ) const
	{
		if ( controlIndex >= static_cast<uint16>( GamepadButton::Count ) )
			return false;
		return wasButtonPressed( static_cast<GamepadButton>( controlIndex ) );
	}

	bool GamepadDevice::wasControlReleased( uint16 controlIndex ) const
	{
		if ( controlIndex >= static_cast<uint16>( GamepadButton::Count ) )
			return false;
		return wasButtonReleased( static_cast<GamepadButton>( controlIndex ) );
	}

	float32 GamepadDevice::getControlValue( uint16 controlIndex ) const
	{
		if ( controlIndex == 100 ) // Left Trigger
			return _leftTrigger;
		if ( controlIndex == 101 ) // Right Trigger
			return _rightTrigger;
		return isControlDown( controlIndex ) ? 1.0f : 0.0f;
	}

	bool GamepadDevice::isButtonDown( GamepadButton button ) const
	{
		const uint32 bit = 1u << static_cast<uint32>( button );
		return ( _buttonMask & bit ) != 0;
	}

	bool GamepadDevice::wasButtonPressed( GamepadButton button ) const
	{
		const uint32 bit = 1u << static_cast<uint32>( button );
		return ( ( _buttonMask & bit ) != 0 ) && ( ( _prevButtonMask & bit ) == 0 );
	}

	bool GamepadDevice::wasButtonReleased( GamepadButton button ) const
	{
		const uint32 bit = 1u << static_cast<uint32>( button );
		return ( ( _buttonMask & bit ) == 0 ) && ( ( _prevButtonMask & bit ) != 0 );
	}

	void GamepadDevice::setButtonDown( GamepadButton button, bool bDown )
	{
		const size_t index = static_cast<size_t>( button );
		if ( index >= static_cast<size_t>( GamepadButton::Count ) )
			return;

		const uint32 bit = 1u << index;
		if ( bDown )
			_buttonMask |= bit;
		else
			_buttonMask &= ~bit;
	}

	void GamepadDevice::setAxis( uint16 axisIndex, float32 value )
	{
		switch ( axisIndex )
		{
			case 0:
				_leftStickX = value;
				break;
			case 1:
				_leftStickY = value;
				break;
			case 2:
				_rightStickX = value;
				break;
			case 3:
				_rightStickY = value;
				break;
			case 4:
				_leftTrigger = value;
				break;
			case 5:
				_rightTrigger = value;
				break;
			default:
				break;
		}
	}
} // namespace sw

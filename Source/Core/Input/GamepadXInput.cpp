/**
 * @file GamepadXInput.cpp
 * @brief XInput poll implementation / non-Windows stub.
 */
#include "GamepadXInput.h"

#if defined( _WIN32 )
	#include "Core/Common/PlatformHeaders.h"
	#include <Xinput.h>
#endif

namespace sw
{
	namespace
	{
		constexpr float32 kStickDeadzone = 0.2f;

		float32 applyDeadzone( float32 value, float32 deadzone )
		{
			const float32 absValue = value < 0.0f ? -value : value;
			if ( absValue < deadzone )
				return 0.0f;
			const float32 sign		= value < 0.0f ? -1.0f : 1.0f;
			const float32 normalized = ( absValue - deadzone ) / ( 1.0f - deadzone );
			return sign * ( normalized > 1.0f ? 1.0f : normalized );
		}
	} // namespace

	GamepadXInput::GamepadXInput()
		: _bConnected{ 0 }
		, _reserved{ 0 }
	{
	}

	void GamepadXInput::setButtonDown( GamepadButton button, bool bDown )
	{
		if ( button >= GamepadButton::Count )
			return;
		_buttons[static_cast<size_t>( button )] = bDown;
	}

	bool GamepadXInput::isButtonDown( GamepadButton button ) const
	{
		if ( button >= GamepadButton::Count )
			return false;
		return _buttons[static_cast<size_t>( button )];
	}

	bool GamepadXInput::wasButtonPressed( GamepadButton button ) const
	{
		if ( button >= GamepadButton::Count )
			return false;
		const size_t i = static_cast<size_t>( button );
		return _buttons[i] && ( _prevButtons[i] == false );
	}

	void GamepadXInput::getLeftStick( float32& outX, float32& outY ) const
	{
		outX = _leftStickX;
		outY = _leftStickY;
	}

	void GamepadXInput::getRightStick( float32& outX, float32& outY ) const
	{
		outX = _rightStickX;
		outY = _rightStickY;
	}

#if defined( _WIN32 )
	void GamepadXInput::poll( uint32 userIndex )
	{
		std::memcpy( _prevButtons, _buttons, sizeof( _buttons ) );

		XINPUT_STATE state{};
		const DWORD	 result = XInputGetState( userIndex, &state );
		if ( result != ERROR_SUCCESS )
		{
			_bConnected	 = 0;
			_leftStickX	 = 0.0f;
			_leftStickY	 = 0.0f;
			_rightStickX = 0.0f;
			_rightStickY = 0.0f;
			std::memset( _buttons, 0, sizeof( _buttons ) );
			return;
		}

		_bConnected			= 1;
		const WORD buttons	= state.Gamepad.wButtons;
		setButtonDown( GamepadButton::A, ( buttons & XINPUT_GAMEPAD_A ) != 0 );
		setButtonDown( GamepadButton::B, ( buttons & XINPUT_GAMEPAD_B ) != 0 );
		setButtonDown( GamepadButton::X, ( buttons & XINPUT_GAMEPAD_X ) != 0 );
		setButtonDown( GamepadButton::Y, ( buttons & XINPUT_GAMEPAD_Y ) != 0 );
		setButtonDown( GamepadButton::DPadUp, ( buttons & XINPUT_GAMEPAD_DPAD_UP ) != 0 );
		setButtonDown( GamepadButton::DPadDown, ( buttons & XINPUT_GAMEPAD_DPAD_DOWN ) != 0 );
		setButtonDown( GamepadButton::DPadLeft, ( buttons & XINPUT_GAMEPAD_DPAD_LEFT ) != 0 );
		setButtonDown( GamepadButton::DPadRight, ( buttons & XINPUT_GAMEPAD_DPAD_RIGHT ) != 0 );
		setButtonDown( GamepadButton::Start, ( buttons & XINPUT_GAMEPAD_START ) != 0 );
		setButtonDown( GamepadButton::Back, ( buttons & XINPUT_GAMEPAD_BACK ) != 0 );
		setButtonDown( GamepadButton::LeftShoulder, ( buttons & XINPUT_GAMEPAD_LEFT_SHOULDER ) != 0 );
		setButtonDown( GamepadButton::RightShoulder, ( buttons & XINPUT_GAMEPAD_RIGHT_SHOULDER ) != 0 );
		setButtonDown( GamepadButton::LeftThumb, ( buttons & XINPUT_GAMEPAD_LEFT_THUMB ) != 0 );
		setButtonDown( GamepadButton::RightThumb, ( buttons & XINPUT_GAMEPAD_RIGHT_THUMB ) != 0 );

		const float32 lx = static_cast<float32>( state.Gamepad.sThumbLX ) / 32767.0f;
		const float32 ly = static_cast<float32>( state.Gamepad.sThumbLY ) / 32767.0f;
		const float32 rx = static_cast<float32>( state.Gamepad.sThumbRX ) / 32767.0f;
		const float32 ry = static_cast<float32>( state.Gamepad.sThumbRY ) / 32767.0f;
		_leftStickX		 = applyDeadzone( lx, kStickDeadzone );
		_leftStickY		 = applyDeadzone( ly, kStickDeadzone );
		_rightStickX	 = applyDeadzone( rx, kStickDeadzone );
		_rightStickY	 = applyDeadzone( ry, kStickDeadzone );
	}
#else
	void GamepadXInput::poll( uint32 )
	{
		std::memcpy( _prevButtons, _buttons, sizeof( _buttons ) );
		_bConnected	 = 0;
		_leftStickX	 = 0.0f;
		_leftStickY	 = 0.0f;
		_rightStickX = 0.0f;
		_rightStickY = 0.0f;
		std::memset( _buttons, 0, sizeof( _buttons ) );
	}
#endif
} // namespace sw

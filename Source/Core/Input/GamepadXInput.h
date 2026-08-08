#pragma once
/**
 * @file GamepadXInput.h
 * @brief Thin Windows XInput gamepad wrapper (stub on non-Windows).
 */
#include "Core/Common/CommonHeaders.h"
#include "Core/Common/CommonMacros.h"
#include "Core/Common/Types.h"

namespace sw
{
	/** @brief Digital gamepad buttons used by ActionMap. */
	enum class GamepadButton : uint8
	{
		A = 0,
		B,
		X,
		Y,
		DPadUp,
		DPadDown,
		DPadLeft,
		DPadRight,
		Start,
		Back,
		LeftShoulder,
		RightShoulder,
		LeftThumb,
		RightThumb,
		Count
	};

	/**
	 * @class GamepadXInput
	 * @brief Polls one XInput user index; no-op stub when XInput is unavailable.
	 */
	class SW_API GamepadXInput
	{
	public:
		GamepadXInput();
		~GamepadXInput() = default;

		GamepadXInput( const GamepadXInput& )			 = delete;
		GamepadXInput& operator=( const GamepadXInput& ) = delete;

		/** @brief Poll controller @p userIndex (0..3). Call once per frame. */
		void poll( uint32 userIndex = 0 );

		bool isConnected() const { return _bConnected != 0; }
		bool isButtonDown( GamepadButton button ) const;
		bool wasButtonPressed( GamepadButton button ) const;

		/** @brief Left stick [-1,1]; deadzone applied. */
		void getLeftStick( float32& outX, float32& outY ) const;
		/** @brief Right stick [-1,1]; deadzone applied. */
		void getRightStick( float32& outX, float32& outY ) const;

	private:
		void setButtonDown( GamepadButton button, bool bDown );

		bool	_buttons[static_cast<size_t>( GamepadButton::Count )]{};
		bool	_prevButtons[static_cast<size_t>( GamepadButton::Count )]{};
		float32 _leftStickX	 = 0.0f;
		float32 _leftStickY	 = 0.0f;
		float32 _rightStickX = 0.0f;
		float32 _rightStickY = 0.0f;
		uint8	_bConnected : 1;
		[[maybe_unused]] uint8 _reserved : 7;
	};
} // namespace sw

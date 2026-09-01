/**
 * @file GamepadDevice.h
 * @brief 모든 플랫폼 게임패드 구현체의 공통 추상 기본 클래스
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#include "Engine/Input/GamepadButtons.h"
#include "Engine/Input/IInputDevice.h"

namespace sw
{
	/**
	 * @class GamepadDevice
	 * @brief 게임패드 버튼, 아날로그 스틱, 트리거 압력 및 럼블 진동 인터페이스를 정의하는 추상 기본 클래스
	 */
	class SW_API GamepadDevice : public IInputDevice
	{
	public:
		explicit GamepadDevice( uint32 deviceIndex = 0 );
		virtual ~GamepadDevice() override = default;

		GamepadDevice( const GamepadDevice& )			 = delete;
		GamepadDevice& operator=( const GamepadDevice& ) = delete;

		// ------------------------------------------------------------------------------
		// 1) IInputDevice 수명주기
		// ------------------------------------------------------------------------------
		InputDeviceKind getDeviceKind() const override { return InputDeviceKind::Gamepad; }
		string_view		getDeviceName() const override { return "Gamepad"; }
		uint32			getDeviceIndex() const override { return _deviceIndex; }

		void onFrameBegin( float32 deltaTime ) override;
		void onFrameEnd() override;
		void resetState() override;

		bool	isControlDown( uint16 controlIndex ) const override;
		bool	wasControlPressed( uint16 controlIndex ) const override;
		bool	wasControlReleased( uint16 controlIndex ) const override;
		float32 getControlValue( uint16 controlIndex ) const override;

		// ------------------------------------------------------------------------------
		// 2) 게임패드 전용 쿼리 & 햅틱 진동
		// ------------------------------------------------------------------------------
		bool isButtonDown( GamepadButton button ) const;
		bool wasButtonPressed( GamepadButton button ) const;
		bool wasButtonReleased( GamepadButton button ) const;

		void getLeftStick( float32& outX, float32& outY ) const
		{
			outX = _leftStickX;
			outY = _leftStickY;
		}
		void getRightStick( float32& outX, float32& outY ) const
		{
			outX = _rightStickX;
			outY = _rightStickY;
		}
		float32 getLeftTrigger() const { return _leftTrigger; }
		float32 getRightTrigger() const { return _rightTrigger; }

		virtual bool setVibration( float32 leftMotor, float32 rightMotor )
		{
			(void)leftMotor;
			(void)rightMotor;
			return false;
		}
		virtual void stopVibration() {}

		void setButtonDown( GamepadButton button, bool bDown );
		void setAxis( uint16 axisIndex, float32 value );

	protected:
		uint32	_deviceIndex;
		uint32	_buttonMask;
		uint32	_prevButtonMask;
		float32 _leftStickX;
		float32 _leftStickY;
		float32 _rightStickX;
		float32 _rightStickY;
		float32 _leftTrigger;
		float32 _rightTrigger;
	};
} // namespace sw

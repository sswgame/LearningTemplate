/**
 * @file GamepadDevice.h
 * @brief 모든 플랫폼 게임패드 구현체의 공통 추상 기본 클래스
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Delegate/Delegate.h"

#include "Engine/Input/GamepadButtons.h"
#include "Engine/Input/IInputDevice.h"

namespace sw
{
	enum class GamepadBatteryType : uint8
	{
		Disconnected = 0,
		Wired,
		Alkaline,
		Nimh,
		Unknown
	};

	enum class GamepadBatteryLevel : uint8
	{
		Empty = 0,
		Low,
		Medium,
		Full
	};

	struct GamepadBatteryInfo
	{
		GamepadBatteryType	_type{ GamepadBatteryType::Unknown };
		GamepadBatteryLevel _level{ GamepadBatteryLevel::Empty };
	};

	/**
	 * @class GamepadDevice
	 * @brief 게임패드 버튼, 아날로그 스틱, 트리거 압력 및 럼블 진동 인터페이스를 정의하는 추상 기본 클래스
	 */
	class SW_API GamepadDevice : public IInputDevice
	{
	public:
		using GamepadConnectionDelegate = Delegate<void( uint32, bool )>;

		explicit GamepadDevice( uint32 deviceIndex = 0 );
		virtual ~GamepadDevice() override = default;

		GamepadDevice( const GamepadDevice& )			 = delete;
		GamepadDevice& operator=( const GamepadDevice& ) = delete;

		// ------------------------------------------------------------------------------
		// 1) IInputDevice 수명주기
		// ------------------------------------------------------------------------------
		InputDeviceKind			   getDeviceKind() const override { return InputDeviceKind::Gamepad; }
		string_view				   getDeviceName() const override { return "Gamepad"; }
		uint32					   getDeviceIndex() const override { return _deviceIndex; }
		virtual GamepadBatteryInfo getBatteryInfo() const { return GamepadBatteryInfo{}; }

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
		bool wasAnyButtonPressed() const { return ( _buttonMask & ~_prevButtonMask ) != 0; }

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
		float32 getLeftMotorVibration() const { return _leftMotorSpeed; }
		float32 getRightMotorVibration() const { return _rightMotorSpeed; }

		virtual bool setVibration( float32 leftMotor, float32 rightMotor )
		{
			_leftMotorSpeed	 = leftMotor;
			_rightMotorSpeed = rightMotor;
			return true;
		}
		virtual void stopVibration()
		{
			_bTimedVibrationActive	= SW_FALSE;
			_vibrationDurationTimer = 0.0f;
			setVibration( 0.0f, 0.0f );
		}

		bool playVibration( float32 leftMotor, float32 rightMotor, float32 durationSeconds )
		{
			if ( durationSeconds <= 0.0f )
			{
				stopVibration();
				return true;
			}
			const bool bOk = setVibration( leftMotor, rightMotor );
			if ( bOk )
			{
				_vibrationDurationTimer = durationSeconds;
				_bTimedVibrationActive	= SW_TRUE;
			}
			return bOk;
		}

		void	setTriggerDeadzone( float32 deadzone ) { _triggerDeadzone = deadzone; }
		float32 getTriggerDeadzone() const { return _triggerDeadzone; }
		bool	isLeftTriggerDown( float32 threshold = 0.5f ) const { return _leftTrigger >= threshold; }
		bool	isRightTriggerDown( float32 threshold = 0.5f ) const { return _rightTrigger >= threshold; }

		void setButtonDown( GamepadButton button, bool bDown );
		void setAxis( uint16 axisIndex, float32 value );
		void setConnectionCallback( GamepadConnectionDelegate callback ) { _onConnectionChanged = std::move( callback ); }

	protected:
		GamepadConnectionDelegate _onConnectionChanged;
		uint32					  _deviceIndex;
		uint32					  _buttonMask;
		uint32					  _prevButtonMask;
		float32					  _leftStickX;
		float32					  _leftStickY;
		float32					  _rightStickX;
		float32					  _rightStickY;
		float32					  _leftTrigger;
		float32					  _rightTrigger;
		float32					  _leftMotorSpeed;
		float32					  _rightMotorSpeed;
		float32					  _vibrationDurationTimer;
		float32					  _triggerDeadzone;
		uint8					  _bTimedVibrationActive : 1;
		[[maybe_unused]] uint8	  _reserved				 : 7;
	};
} // namespace sw

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
		uint32					  _deviceIndex;				  /**< 컨트롤러 슬롯 인덱스 (로컬 멀티플레이어 0~3번 패드). */
		uint32					  _buttonMask;				  /**< 이번 프레임의 디지털 버튼 눌림 비트마스크 (GamepadButton 인덱스로 비트 조회). */
		uint32					  _prevButtonMask;			  /**< 직전 프레임의 버튼 비트마스크. wasButtonPressed/Released의 엣지 판정에 사용. */
		float32					  _leftStickX;				  /**< 왼쪽 스틱 X축 [-1.0, 1.0] (데드존 미적용 원시값). */
		float32					  _leftStickY;				  /**< 왼쪽 스틱 Y축 [-1.0, 1.0]. */
		float32					  _rightStickX;				  /**< 오른쪽 스틱 X축 [-1.0, 1.0]. */
		float32					  _rightStickY;				  /**< 오른쪽 스틱 Y축 [-1.0, 1.0]. */
		float32					  _leftTrigger;				  /**< 왼쪽 트리거 압력 [0.0, 1.0] (_triggerDeadzone 필터 적용됨). */
		float32					  _rightTrigger;			  /**< 오른쪽 트리거 압력 [0.0, 1.0]. */
		float32					  _prevLeftTrigger;			  /**< 직전 프레임의 왼쪽 트리거 값. wasControlPressed/Released 임계값(0.5) 판정에 사용. */
		float32					  _prevRightTrigger;		  /**< 직전 프레임의 오른쪽 트리거 값. */
		float32					  _leftMotorSpeed;			  /**< 마지막으로 설정한 왼쪽(저주파) 진동 모터 세기 [0.0, 1.0]. */
		float32					  _rightMotorSpeed;			  /**< 마지막으로 설정한 오른쪽(고주파) 진동 모터 세기 [0.0, 1.0]. */
		float32					  _vibrationDurationTimer;	  /**< playVibration()으로 시작한 타이머 진동의 잔여 시간(초). 0 이하가 되면 자동 정지. */
		float32					  _triggerDeadzone;			  /**< 트리거 축 노이즈 필터용 데드존. 이 값 미만이면 0으로 취급 (디지털 눌림 판정용 0.5 임계값과는 별개). */
		uint8					  _bTimedVibrationActive : 1; /**< playVibration()으로 시작된 타이머 진동이 진행 중인지. */
		[[maybe_unused]] uint8	  _reserved				 : 7;
	};
} // namespace sw

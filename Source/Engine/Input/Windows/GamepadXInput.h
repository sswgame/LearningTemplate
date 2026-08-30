/**
 * @file GamepadXInput.h
 * @brief Windows XInput 게임패드 얇은 래퍼 (비-Windows에서는 스텁).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#include "Engine/Input/GamepadButtons.h"

namespace sw
{
	/**
	 * @class GamepadXInput
	 * @brief XInput 사용자 인덱스 하나를 폴링합니다. XInput이 없으면 no-op 스텁입니다.
	 */
	class SW_API GamepadXInput
	{
	public:
		/** @brief 빈 패드 상태. */
		GamepadXInput();
		/** @brief 가상 소멸. */
		~GamepadXInput() = default;

		/** @brief 복사를 금지합니다. */
		GamepadXInput( const GamepadXInput& ) = delete;
		/** @brief 대입을 금지합니다. */
		GamepadXInput& operator=( const GamepadXInput& ) = delete;

		/** @brief 컨트롤러 @p userIndex (0..3)를 폴링합니다. 프레임당 한 번 호출하세요. */
		void poll( uint32 userIndex = 0 );

		/** @brief 컨트롤러 연결 여부를 반환합니다. */
		bool isConnected() const { return _bConnected == SW_TRUE; }
		/** @brief 버튼이 눌린 상태인지 반환합니다. */
		bool isButtonDown( GamepadButton button ) const;
		/** @brief 이번 프레임에 버튼이 눌렸는지 반환합니다. */
		bool wasButtonPressed( GamepadButton button ) const;
		/** @brief 이번 프레임에 버튼이 떼어졌는지 반환합니다. */
		bool wasButtonReleased( GamepadButton button ) const;

		/** @brief 왼쪽 스틱 [-1,1]; 데드존 적용. */
		void getLeftStick( float32& outX, float32& outY ) const;
		/** @brief 오른쪽 스틱 [-1,1]; 데드존 적용. */
		void getRightStick( float32& outX, float32& outY ) const;

	private:
		/** @brief 버튼 눌림 상태를 설정합니다. */
		void setButtonDown( GamepadButton button, bool bDown );

		bool				   _arrButton[static_cast<size_t>( GamepadButton::Count )];
		bool				   _arrPrevButton[static_cast<size_t>( GamepadButton::Count )];
		float32				   _leftStickX;
		float32				   _leftStickY;
		float32				   _rightStickX;
		float32				   _rightStickY;
		uint8				   _bConnected : 1;
		[[maybe_unused]] uint8 _reserved   : 7;
	};
} // namespace sw

/**
 * @file GamepadButtons.h
 * @brief 디지털 게임패드 버튼 열거형과 이름 변환 (플랫폼 래퍼와 분리).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

namespace sw
{
	/** @brief ActionMap이 사용하는 디지털 게임패드 버튼입니다. */
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

	/** @brief GamepadButton 이름 변환 */
	struct SW_API GamepadButtons
	{
		/** @brief 대소문자 무시 열거형 이름 → GamepadButton (미인식이면 Count). */
		static GamepadButton fromName( string_view name );
		/** @brief GamepadButton → 안정 열거형 이름 (Count/범위 밖이면 nullptr). */
		static const utf8* toName( GamepadButton button );
	};
} // namespace sw

/**
 * @file KeyCodes.h
 * @brief 키보드/마우스 바인드 ID. ENUM() → KeyCodes.gen.cpp 등록.
 */
#pragma once
#include "Core/Common/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
	/// @brief Key 종류를 정의하는 열거형입니다.
	ENUM( Invalid = Unknown, Count = Count )
	enum class Key : uint8
	{
		Unknown = 0,

		// 영문 키
		A,
		B,
		C,
		D,
		E,
		F,
		G,
		H,
		I,
		J,
		K,
		L,
		M,
		N,
		O,
		P,
		Q,
		R,
		S,
		T,
		U,
		V,
		W,
		X,
		Y,
		Z,

		// 상단 숫자열
		Digit0,
		Digit1,
		Digit2,
		Digit3,
		Digit4,
		Digit5,
		Digit6,
		Digit7,
		Digit8,
		Digit9,

		// 기능 키
		F1,
		F2,
		F3,
		F4,
		F5,
		F6,
		F7,
		F8,
		F9,
		F10,
		F11,
		F12,

		// 탐색 / 편집
		Escape,
		Tab,
		CapsLock,
		Enter,
		Space,
		Backspace,
		Insert,
		Delete,
		Home,
		End,
		PageUp,
		PageDown,
		Left,
		Right,
		Up,
		Down,
		PrintScreen,
		ScrollLock,
		Pause,

		// 수정 키
		LeftShift,
		RightShift,
		LeftControl,
		RightControl,
		LeftAlt,
		RightAlt,
		LeftSuper, // Win / Cmd 키
		RightSuper,
		Menu,

		// 구두점 (US)
		Minus,
		Equal,
		LeftBracket,
		RightBracket,
		Backslash,
		Semicolon,
		Apostrophe,
		Grave,
		Comma,
		Period,
		Slash,

		// 숫자 패드
		NumLock,
		Numpad0,
		Numpad1,
		Numpad2,
		Numpad3,
		Numpad4,
		Numpad5,
		Numpad6,
		Numpad7,
		Numpad8,
		Numpad9,
		NumpadDivide,
		NumpadMultiply,
		NumpadSubtract,
		NumpadAdd,
		NumpadDecimal,
		NumpadEnter,

		Count
	};

	/// @brief 마우스 버튼 종류를 정의하는 열거형입니다.
	ENUM( Invalid = Count, Count = Count )
	enum class MouseButton : uint8
	{
		Left = 0,
		Right,
		Middle,
		X1,
		X2,
		Count
	};

	/** @brief Key 이름 변환 */
	struct SW_API KeyCodes
	{
		/** @brief 이름에서 Key를 해석합니다. */
		static Key fromName( string_view name );
		/** @brief Key의 안정 이름을 반환합니다. */
		static const utf8* toName( Key key );
	};

	/** @brief MouseButton 이름 변환 */
	struct SW_API MouseButtons
	{
		/** @brief 이름에서 MouseButton을 해석합니다. */
		static MouseButton fromName( string_view name );
		/** @brief MouseButton의 안정 이름을 반환합니다. */
		static const utf8* toName( MouseButton button );
	};
} // namespace sw

#include "pch.h"

#include "Engine/Input/InputKeyMap.h"

#if defined( SW_PLATFORM_WINDOWS )

namespace sw
{
	namespace
	{
		struct InputKeyMapWin32Internal
		{
			static Key resolveShiftKey()
			{
				const bool left	 = ( GetAsyncKeyState( VK_LSHIFT ) & 0x8000 ) != 0;
				const bool right = ( GetAsyncKeyState( VK_RSHIFT ) & 0x8000 ) != 0;
				if ( left && right == false )
					return Key::LeftShift;
				if ( right && left == false )
					return Key::RightShift;
				if ( left )
					return Key::LeftShift;
				return Key::RightShift;
			}

			static Key resolveControlKey()
			{
				const bool left	 = ( GetAsyncKeyState( VK_LCONTROL ) & 0x8000 ) != 0;
				const bool right = ( GetAsyncKeyState( VK_RCONTROL ) & 0x8000 ) != 0;
				if ( left && right == false )
					return Key::LeftControl;
				if ( right && left == false )
					return Key::RightControl;
				if ( left )
					return Key::LeftControl;
				return Key::RightControl;
			}

			static Key resolveAltKey()
			{
				const bool left	 = ( GetAsyncKeyState( VK_LMENU ) & 0x8000 ) != 0;
				const bool right = ( GetAsyncKeyState( VK_RMENU ) & 0x8000 ) != 0;
				if ( left && right == false )
					return Key::LeftAlt;
				if ( right && left == false )
					return Key::RightAlt;
				if ( left )
					return Key::LeftAlt;
				return Key::RightAlt;
			}

			inline static const InputKeyMap::VkKeyPair kArrPollKeys[] = {
				{		  'A',			   Key::A},
				{		  'B',			   Key::B},
				{		  'C',			   Key::C},
				{		  'D',			   Key::D},
				{		  'E',			   Key::E},
				{		  'F',			   Key::F},
				{		  'G',			   Key::G},
				{		  'H',			   Key::H},
				{		  'I',			   Key::I},
				{		  'J',			   Key::J},
				{		  'K',			   Key::K},
				{		  'L',			   Key::L},
				{		  'M',			   Key::M},
				{		  'N',			   Key::N},
				{		  'O',			   Key::O},
				{		  'P',			   Key::P},
				{		  'Q',			   Key::Q},
				{		  'R',			   Key::R},
				{		  'S',			   Key::S},
				{		  'T',			   Key::T},
				{		  'U',			   Key::U},
				{		  'V',			   Key::V},
				{		  'W',			   Key::W},
				{		  'X',			   Key::X},
				{		  'Y',			   Key::Y},
				{		  'Z',			   Key::Z},

				{		  '0',		   Key::Digit0},
				{		  '1',		   Key::Digit1},
				{		  '2',		   Key::Digit2},
				{		  '3',		   Key::Digit3},
				{		  '4',		   Key::Digit4},
				{		  '5',		   Key::Digit5},
				{		  '6',		   Key::Digit6},
				{		  '7',		   Key::Digit7},
				{		  '8',		   Key::Digit8},
				{		  '9',		   Key::Digit9},

				{		  VK_F1,			 Key::F1},
				{		  VK_F2,			 Key::F2},
				{		  VK_F3,			 Key::F3},
				{		  VK_F4,			 Key::F4},
				{		  VK_F5,			 Key::F5},
				{		  VK_F6,			 Key::F6},
				{		  VK_F7,			 Key::F7},
				{		  VK_F8,			 Key::F8},
				{		  VK_F9,			 Key::F9},
				{		  VK_F10,			  Key::F10},
				{		  VK_F11,			  Key::F11},
				{		  VK_F12,			  Key::F12},

				{	  VK_ESCAPE,		 Key::Escape},
				{		  VK_TAB,			  Key::Tab},
				{	  VK_CAPITAL,		  Key::CapsLock},
				{	  VK_RETURN,			 Key::Enter},
				{	  VK_SPACE,			Key::Space},
				{	  VK_BACK,	   Key::Backspace},
				{	  VK_INSERT,		 Key::Insert},
				{	  VK_DELETE,		 Key::Delete},
				{	  VK_HOME,		   Key::Home},
				{		  VK_END,			  Key::End},
				{	  VK_PRIOR,			Key::PageUp},
				{	  VK_NEXT,	   Key::PageDown},
				{	  VK_LEFT,		   Key::Left},
				{	  VK_RIGHT,			Key::Right},
				{		  VK_UP,			 Key::Up},
				{	  VK_DOWN,		   Key::Down},
				{  VK_SNAPSHOT,	   Key::PrintScreen},
				{	  VK_SCROLL,	 Key::ScrollLock},
				{	  VK_PAUSE,			Key::Pause},

				{	  VK_LSHIFT,		 Key::LeftShift},
				{	  VK_RSHIFT,	 Key::RightShift},
				{  VK_LCONTROL,	   Key::LeftControl},
				{  VK_RCONTROL,   Key::RightControl},
				{	  VK_LMENU,		Key::LeftAlt},
				{	  VK_RMENU,		Key::RightAlt},
				{	  VK_LWIN,	   Key::LeftSuper},
				{	  VK_RWIN,	   Key::RightSuper},
				{	  VK_APPS,		   Key::Menu},

				{ VK_OEM_MINUS,			Key::Minus},
				{  VK_OEM_PLUS,		   Key::Equal},
				{	  VK_OEM_4,	Key::LeftBracket},
				{	  VK_OEM_6,	Key::RightBracket},
				{	  VK_OEM_5,		Key::Backslash},
				{	  VK_OEM_1,		Key::Semicolon},
				{	  VK_OEM_7,		Key::Apostrophe},
				{	  VK_OEM_3,			Key::Grave},
				{ VK_OEM_COMMA,			Key::Comma},
				{VK_OEM_PERIOD,		 Key::Period},
				{	  VK_OEM_2,			Key::Slash},

				{	  VK_NUMLOCK,		  Key::NumLock},
				{	  VK_NUMPAD0,		  Key::Numpad0},
				{	  VK_NUMPAD1,		  Key::Numpad1},
				{	  VK_NUMPAD2,		  Key::Numpad2},
				{	  VK_NUMPAD3,		  Key::Numpad3},
				{	  VK_NUMPAD4,		  Key::Numpad4},
				{	  VK_NUMPAD5,		  Key::Numpad5},
				{	  VK_NUMPAD6,		  Key::Numpad6},
				{	  VK_NUMPAD7,		  Key::Numpad7},
				{	  VK_NUMPAD8,		  Key::Numpad8},
				{	  VK_NUMPAD9,		  Key::Numpad9},
				{	  VK_DIVIDE,	 Key::NumpadDivide},
				{  VK_MULTIPLY, Key::NumpadMultiply},
				{  VK_SUBTRACT, Key::NumpadSubtract},
				{		  VK_ADD,	  Key::NumpadAdd},
				{	  VK_DECIMAL,  Key::NumpadDecimal},
				// Numpad Enter shares VK_RETURN; poll cannot distinguish — rely on Key::Enter.
			};
		};
	} // namespace
} // namespace sw

namespace sw
{
	Key InputKeyMap::mapWin32VirtualKey( uintptr_t vk )
	{
		if ( vk >= 'A' && vk <= 'Z' )
			return static_cast<Key>( static_cast<uint8>( Key::A ) + static_cast<uint8>( vk - 'A' ) );
		if ( vk >= 'a' && vk <= 'z' )
			return static_cast<Key>( static_cast<uint8>( Key::A ) + static_cast<uint8>( vk - 'a' ) );
		if ( vk >= '0' && vk <= '9' )
			return static_cast<Key>( static_cast<uint8>( Key::Digit0 ) + static_cast<uint8>( vk - '0' ) );

		switch ( vk )
		{
			case VK_F1:
				return Key::F1;
			case VK_F2:
				return Key::F2;
			case VK_F3:
				return Key::F3;
			case VK_F4:
				return Key::F4;
			case VK_F5:
				return Key::F5;
			case VK_F6:
				return Key::F6;
			case VK_F7:
				return Key::F7;
			case VK_F8:
				return Key::F8;
			case VK_F9:
				return Key::F9;
			case VK_F10:
				return Key::F10;
			case VK_F11:
				return Key::F11;
			case VK_F12:
				return Key::F12;

			case VK_ESCAPE:
				return Key::Escape;
			case VK_TAB:
				return Key::Tab;
			case VK_CAPITAL:
				return Key::CapsLock;
			case VK_RETURN:
				return Key::Enter;
			case VK_SPACE:
				return Key::Space;
			case VK_BACK:
				return Key::Backspace;
			case VK_INSERT:
				return Key::Insert;
			case VK_DELETE:
				return Key::Delete;
			case VK_HOME:
				return Key::Home;
			case VK_END:
				return Key::End;
			case VK_PRIOR:
				return Key::PageUp;
			case VK_NEXT:
				return Key::PageDown;
			case VK_LEFT:
				return Key::Left;
			case VK_RIGHT:
				return Key::Right;
			case VK_UP:
				return Key::Up;
			case VK_DOWN:
				return Key::Down;
			case VK_SNAPSHOT:
				return Key::PrintScreen;
			case VK_SCROLL:
				return Key::ScrollLock;
			case VK_PAUSE:
				return Key::Pause;

			case VK_LSHIFT:
				return Key::LeftShift;
			case VK_RSHIFT:
				return Key::RightShift;
			case VK_SHIFT:
				return InputKeyMapWin32Internal::resolveShiftKey();
			case VK_LCONTROL:
				return Key::LeftControl;
			case VK_RCONTROL:
				return Key::RightControl;
			case VK_CONTROL:
				return InputKeyMapWin32Internal::resolveControlKey();
			case VK_LMENU:
				return Key::LeftAlt;
			case VK_RMENU:
				return Key::RightAlt;
			case VK_MENU:
				return InputKeyMapWin32Internal::resolveAltKey();
			case VK_LWIN:
				return Key::LeftSuper;
			case VK_RWIN:
				return Key::RightSuper;
			case VK_APPS:
				return Key::Menu;

			case VK_OEM_MINUS:
				return Key::Minus;
			case VK_OEM_PLUS:
				return Key::Equal;
			case VK_OEM_4:
				return Key::LeftBracket;
			case VK_OEM_6:
				return Key::RightBracket;
			case VK_OEM_5:
				return Key::Backslash;
			case VK_OEM_1:
				return Key::Semicolon;
			case VK_OEM_7:
				return Key::Apostrophe;
			case VK_OEM_3:
				return Key::Grave;
			case VK_OEM_COMMA:
				return Key::Comma;
			case VK_OEM_PERIOD:
				return Key::Period;
			case VK_OEM_2:
				return Key::Slash;

			case VK_NUMLOCK:
				return Key::NumLock;
			case VK_NUMPAD0:
				return Key::Numpad0;
			case VK_NUMPAD1:
				return Key::Numpad1;
			case VK_NUMPAD2:
				return Key::Numpad2;
			case VK_NUMPAD3:
				return Key::Numpad3;
			case VK_NUMPAD4:
				return Key::Numpad4;
			case VK_NUMPAD5:
				return Key::Numpad5;
			case VK_NUMPAD6:
				return Key::Numpad6;
			case VK_NUMPAD7:
				return Key::Numpad7;
			case VK_NUMPAD8:
				return Key::Numpad8;
			case VK_NUMPAD9:
				return Key::Numpad9;
			case VK_DIVIDE:
				return Key::NumpadDivide;
			case VK_MULTIPLY:
				return Key::NumpadMultiply;
			case VK_SUBTRACT:
				return Key::NumpadSubtract;
			case VK_ADD:
				return Key::NumpadAdd;
			case VK_DECIMAL:
				return Key::NumpadDecimal;

			default:
				return Key::Unknown;
		}
	}

	MouseButton InputKeyMap::mapWin32MouseButton( uint32 message, uintptr_t wParam )
	{
		switch ( message )
		{
			case WM_LBUTTONDOWN:
			case WM_LBUTTONUP:
			case WM_LBUTTONDBLCLK:
				return MouseButton::Left;
			case WM_RBUTTONDOWN:
			case WM_RBUTTONUP:
			case WM_RBUTTONDBLCLK:
				return MouseButton::Right;
			case WM_MBUTTONDOWN:
			case WM_MBUTTONUP:
			case WM_MBUTTONDBLCLK:
				return MouseButton::Middle;
			case WM_XBUTTONDOWN:
			case WM_XBUTTONUP:
			case WM_XBUTTONDBLCLK:
				return ( GET_XBUTTON_WPARAM( wParam ) == XBUTTON1 ) ? MouseButton::X1 : MouseButton::X2;
			default:
				return MouseButton::Count;
		}
	}

	const InputKeyMap::VkKeyPair* InputKeyMap::getWin32PollKeyTable( uint32& outCount )
	{
		outCount = static_cast<uint32>( sizeof( InputKeyMapWin32Internal::kArrPollKeys ) / sizeof( InputKeyMapWin32Internal::kArrPollKeys[0] ) );
		return InputKeyMapWin32Internal::kArrPollKeys;
	}
} // namespace sw

#else

namespace sw
{
	Key InputKeyMap::mapWin32VirtualKey( uintptr_t )
	{
		return Key::Unknown;
	}

	MouseButton InputKeyMap::mapWin32MouseButton( uint32, uintptr_t )
	{
		return MouseButton::Count;
	}

	const InputKeyMap::VkKeyPair* InputKeyMap::getWin32PollKeyTable( uint32& outCount )
	{
		outCount = 0;
		return nullptr;
	}
} // namespace sw

#endif

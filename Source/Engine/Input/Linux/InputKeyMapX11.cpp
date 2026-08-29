#include "pch.h"

#include "Engine/Input/InputKeyMap.h"

#if defined( SW_PLATFORM_LINUX )

	#include <X11/keysym.h>

namespace sw
{
	Key InputKeyMap::mapX11KeySym( uint64 keySym )
	{
		switch ( keySym )
		{
			case XK_a:
			case XK_A:
				return Key::A;
			case XK_b:
			case XK_B:
				return Key::B;
			case XK_c:
			case XK_C:
				return Key::C;
			case XK_d:
			case XK_D:
				return Key::D;
			case XK_e:
			case XK_E:
				return Key::E;
			case XK_f:
			case XK_F:
				return Key::F;
			case XK_g:
			case XK_G:
				return Key::G;
			case XK_h:
			case XK_H:
				return Key::H;
			case XK_i:
			case XK_I:
				return Key::I;
			case XK_j:
			case XK_J:
				return Key::J;
			case XK_k:
			case XK_K:
				return Key::K;
			case XK_l:
			case XK_L:
				return Key::L;
			case XK_m:
			case XK_M:
				return Key::M;
			case XK_n:
			case XK_N:
				return Key::N;
			case XK_o:
			case XK_O:
				return Key::O;
			case XK_p:
			case XK_P:
				return Key::P;
			case XK_q:
			case XK_Q:
				return Key::Q;
			case XK_r:
			case XK_R:
				return Key::R;
			case XK_s:
			case XK_S:
				return Key::S;
			case XK_t:
			case XK_T:
				return Key::T;
			case XK_u:
			case XK_U:
				return Key::U;
			case XK_v:
			case XK_V:
				return Key::V;
			case XK_w:
			case XK_W:
				return Key::W;
			case XK_x:
			case XK_X:
				return Key::X;
			case XK_y:
			case XK_Y:
				return Key::Y;
			case XK_z:
			case XK_Z:
				return Key::Z;

			case XK_0:
				return Key::Digit0;
			case XK_1:
				return Key::Digit1;
			case XK_2:
				return Key::Digit2;
			case XK_3:
				return Key::Digit3;
			case XK_4:
				return Key::Digit4;
			case XK_5:
				return Key::Digit5;
			case XK_6:
				return Key::Digit6;
			case XK_7:
				return Key::Digit7;
			case XK_8:
				return Key::Digit8;
			case XK_9:
				return Key::Digit9;

			case XK_F1:
				return Key::F1;
			case XK_F2:
				return Key::F2;
			case XK_F3:
				return Key::F3;
			case XK_F4:
				return Key::F4;
			case XK_F5:
				return Key::F5;
			case XK_F6:
				return Key::F6;
			case XK_F7:
				return Key::F7;
			case XK_F8:
				return Key::F8;
			case XK_F9:
				return Key::F9;
			case XK_F10:
				return Key::F10;
			case XK_F11:
				return Key::F11;
			case XK_F12:
				return Key::F12;

			case XK_Escape:
				return Key::Escape;
			case XK_Tab:
			case XK_ISO_Left_Tab:
				return Key::Tab;
			case XK_Caps_Lock:
				return Key::CapsLock;
			case XK_Return:
				return Key::Enter;
			case XK_space:
				return Key::Space;
			case XK_BackSpace:
				return Key::Backspace;
			case XK_Insert:
				return Key::Insert;
			case XK_Delete:
				return Key::Delete;
			case XK_Home:
				return Key::Home;
			case XK_End:
				return Key::End;
			case XK_Page_Up:
				return Key::PageUp;
			case XK_Page_Down:
				return Key::PageDown;
			case XK_Left:
				return Key::Left;
			case XK_Right:
				return Key::Right;
			case XK_Up:
				return Key::Up;
			case XK_Down:
				return Key::Down;
			case XK_Print:
				return Key::PrintScreen;
			case XK_Scroll_Lock:
				return Key::ScrollLock;
			case XK_Pause:
				return Key::Pause;

			case XK_Shift_L:
				return Key::LeftShift;
			case XK_Shift_R:
				return Key::RightShift;
			case XK_Control_L:
				return Key::LeftControl;
			case XK_Control_R:
				return Key::RightControl;
			case XK_Alt_L:
			case XK_Meta_L:
				return Key::LeftAlt;
			case XK_Alt_R:
			case XK_Meta_R:
			case XK_ISO_Level3_Shift:
				return Key::RightAlt;
			case XK_Super_L:
				return Key::LeftSuper;
			case XK_Super_R:
				return Key::RightSuper;
			case XK_Menu:
				return Key::Menu;

			case XK_minus:
				return Key::Minus;
			case XK_equal:
				return Key::Equal;
			case XK_bracketleft:
				return Key::LeftBracket;
			case XK_bracketright:
				return Key::RightBracket;
			case XK_backslash:
				return Key::Backslash;
			case XK_semicolon:
				return Key::Semicolon;
			case XK_apostrophe:
				return Key::Apostrophe;
			case XK_grave:
				return Key::Grave;
			case XK_comma:
				return Key::Comma;
			case XK_period:
				return Key::Period;
			case XK_slash:
				return Key::Slash;

			case XK_Num_Lock:
				return Key::NumLock;
			case XK_KP_0:
				return Key::Numpad0;
			case XK_KP_1:
				return Key::Numpad1;
			case XK_KP_2:
				return Key::Numpad2;
			case XK_KP_3:
				return Key::Numpad3;
			case XK_KP_4:
				return Key::Numpad4;
			case XK_KP_5:
				return Key::Numpad5;
			case XK_KP_6:
				return Key::Numpad6;
			case XK_KP_7:
				return Key::Numpad7;
			case XK_KP_8:
				return Key::Numpad8;
			case XK_KP_9:
				return Key::Numpad9;
			case XK_KP_Divide:
				return Key::NumpadDivide;
			case XK_KP_Multiply:
				return Key::NumpadMultiply;
			case XK_KP_Subtract:
				return Key::NumpadSubtract;
			case XK_KP_Add:
				return Key::NumpadAdd;
			case XK_KP_Decimal:
			case XK_KP_Separator:
				return Key::NumpadDecimal;
			case XK_KP_Enter:
				return Key::NumpadEnter;

			default:
				return Key::Unknown;
		}
	}
} // namespace sw

#else

namespace sw
{
	Key InputKeyMap::mapX11KeySym( uint64 )
	{
		return Key::Unknown;
	}
} // namespace sw

#endif

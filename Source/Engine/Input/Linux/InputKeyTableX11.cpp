#include "pch.h"

#include "Core/Foundation/PlatformOSHeaders.h"

#include "Engine/Input/Map/InputKeyTable.h"

#if defined( SW_WITH_CLIENT_CODE )
    #include "Core/Foundation/X11Headers.h"

    #include <linux/input-event-codes.h>

namespace sw
{
    namespace
    {
        /** @brief 레이아웃에 따라 키심이 달라지는 글자 · 숫자 · 구두점 키를 evdev 번호(자판의 물리 자리, 미국 배열 이름)로 읽습니다. 그 밖의 키는 `Key::Unknown` 입니다. */
        Key mapPhysicalKeyCode( uint32 evdevCode )
        {
            switch ( evdevCode )
            {
                case KEY_A:
                    return Key::A;
                case KEY_B:
                    return Key::B;
                case KEY_C:
                    return Key::C;
                case KEY_D:
                    return Key::D;
                case KEY_E:
                    return Key::E;
                case KEY_F:
                    return Key::F;
                case KEY_G:
                    return Key::G;
                case KEY_H:
                    return Key::H;
                case KEY_I:
                    return Key::I;
                case KEY_J:
                    return Key::J;
                case KEY_K:
                    return Key::K;
                case KEY_L:
                    return Key::L;
                case KEY_M:
                    return Key::M;
                case KEY_N:
                    return Key::N;
                case KEY_O:
                    return Key::O;
                case KEY_P:
                    return Key::P;
                case KEY_Q:
                    return Key::Q;
                case KEY_R:
                    return Key::R;
                case KEY_S:
                    return Key::S;
                case KEY_T:
                    return Key::T;
                case KEY_U:
                    return Key::U;
                case KEY_V:
                    return Key::V;
                case KEY_W:
                    return Key::W;
                case KEY_X:
                    return Key::X;
                case KEY_Y:
                    return Key::Y;
                case KEY_Z:
                    return Key::Z;
                case KEY_0:
                    return Key::Digit0;
                case KEY_1:
                    return Key::Digit1;
                case KEY_2:
                    return Key::Digit2;
                case KEY_3:
                    return Key::Digit3;
                case KEY_4:
                    return Key::Digit4;
                case KEY_5:
                    return Key::Digit5;
                case KEY_6:
                    return Key::Digit6;
                case KEY_7:
                    return Key::Digit7;
                case KEY_8:
                    return Key::Digit8;
                case KEY_9:
                    return Key::Digit9;
                case KEY_MINUS:
                    return Key::Minus;
                case KEY_EQUAL:
                    return Key::Equal;
                case KEY_LEFTBRACE:
                    return Key::LeftBracket;
                case KEY_RIGHTBRACE:
                    return Key::RightBracket;
                case KEY_BACKSLASH:
                    return Key::Backslash;
                case KEY_SEMICOLON:
                    return Key::Semicolon;
                case KEY_APOSTROPHE:
                    return Key::Apostrophe;
                case KEY_GRAVE:
                    return Key::Grave;
                case KEY_COMMA:
                    return Key::Comma;
                case KEY_DOT:
                    return Key::Period;
                case KEY_SLASH:
                    return Key::Slash;
                default:
                    return Key::Unknown;
            }
        }
    } // namespace

    Key InputKeyTable::mapX11Key( uint64 keySym, uint32 keyCode )
    {
        const Key key = mapX11KeySym( keySym );
        if ( key != Key::Unknown )
            return key;
        // X 키 코드 · xkb 키 코드는 evdev 번호 + 8 이다(xkbcommon 규약, evdev 규칙을 쓰는 X 서버 전부).
        constexpr uint32 kEvdevOffset = 8;
        return keyCode >= kEvdevOffset ? mapPhysicalKeyCode( keyCode - kEvdevOffset ) : Key::Unknown;
    }

    // X11 KeySym 에서 Key 로 가는 switch 표이고 케이스마다 한 줄이라 나누지 않는다.
    Key InputKeyTable::mapX11KeySym( uint64 keySym )
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
            // 숫자 패드 키의 0 번 칸은 NumLock 이 꺼진 쪽(XK_KP_Home …)이고, 숫자는 NumLock 칸이다. 키 이름은 두 칸 모두 같은 물리 키다(Win32 의 VK_NUMPAD 와 스캔코드처럼).
            case XK_KP_0:
            case XK_KP_Insert:
                return Key::Numpad0;
            case XK_KP_1:
            case XK_KP_End:
                return Key::Numpad1;
            case XK_KP_2:
            case XK_KP_Down:
                return Key::Numpad2;
            case XK_KP_3:
            case XK_KP_Page_Down:
                return Key::Numpad3;
            case XK_KP_4:
            case XK_KP_Left:
                return Key::Numpad4;
            case XK_KP_5:
            case XK_KP_Begin:
                return Key::Numpad5;
            case XK_KP_6:
            case XK_KP_Right:
                return Key::Numpad6;
            case XK_KP_7:
            case XK_KP_Home:
                return Key::Numpad7;
            case XK_KP_8:
            case XK_KP_Up:
                return Key::Numpad8;
            case XK_KP_9:
            case XK_KP_Page_Up:
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
            case XK_KP_Delete:
                return Key::NumpadDecimal;
            case XK_KP_Enter:
                return Key::NumpadEnter;

            default:
                return Key::Unknown;
        }
    }
} // namespace sw
#endif

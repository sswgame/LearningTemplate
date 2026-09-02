#include "pch.h"

#include "Engine/Input/InputKeyMap.h"

namespace sw
{
    Key InputKeyMap::mapScanCodeToKey( uint32 scanCode, bool bExtended )
    {
        if ( bExtended )
        {
            switch ( scanCode )
            {
                case 0x1C:
                    return Key::NumpadEnter;
                case 0x1D:
                    return Key::RightControl;
                case 0x38:
                    return Key::RightAlt;
                case 0x47:
                    return Key::Home;
                case 0x48:
                    return Key::Up;
                case 0x49:
                    return Key::PageUp;
                case 0x4B:
                    return Key::Left;
                case 0x4D:
                    return Key::Right;
                case 0x4F:
                    return Key::End;
                case 0x50:
                    return Key::Down;
                case 0x51:
                    return Key::PageDown;
                case 0x52:
                    return Key::Insert;
                case 0x53:
                    return Key::Delete;
                case 0x5B:
                    return Key::LeftSuper;
                case 0x5C:
                    return Key::RightSuper;
                default:
                    break;
            }
        }

        switch ( scanCode )
        {
            case 0x01:
                return Key::Escape;
            case 0x02:
                return Key::Digit1;
            case 0x03:
                return Key::Digit2;
            case 0x04:
                return Key::Digit3;
            case 0x05:
                return Key::Digit4;
            case 0x06:
                return Key::Digit5;
            case 0x07:
                return Key::Digit6;
            case 0x08:
                return Key::Digit7;
            case 0x09:
                return Key::Digit8;
            case 0x0A:
                return Key::Digit9;
            case 0x0B:
                return Key::Digit0;
            case 0x0E:
                return Key::Backspace;
            case 0x0F:
                return Key::Tab;
            case 0x10:
                return Key::Q;
            case 0x11:
                return Key::W;
            case 0x12:
                return Key::E;
            case 0x13:
                return Key::R;
            case 0x14:
                return Key::T;
            case 0x15:
                return Key::Y;
            case 0x16:
                return Key::U;
            case 0x17:
                return Key::I;
            case 0x18:
                return Key::O;
            case 0x19:
                return Key::P;
            case 0x1C:
                return Key::Enter;
            case 0x1D:
                return Key::LeftControl;
            case 0x1E:
                return Key::A;
            case 0x1F:
                return Key::S;
            case 0x20:
                return Key::D;
            case 0x21:
                return Key::F;
            case 0x22:
                return Key::G;
            case 0x23:
                return Key::H;
            case 0x24:
                return Key::J;
            case 0x25:
                return Key::K;
            case 0x26:
                return Key::L;
            case 0x2A:
                return Key::LeftShift;
            case 0x2C:
                return Key::Z;
            case 0x2D:
                return Key::X;
            case 0x2E:
                return Key::C;
            case 0x2F:
                return Key::V;
            case 0x30:
                return Key::B;
            case 0x31:
                return Key::N;
            case 0x32:
                return Key::M;
            case 0x36:
                return Key::RightShift;
            case 0x38:
                return Key::LeftAlt;
            case 0x39:
                return Key::Space;
            case 0x3A:
                return Key::CapsLock;
            case 0x3B:
                return Key::F1;
            case 0x3C:
                return Key::F2;
            case 0x3D:
                return Key::F3;
            case 0x3E:
                return Key::F4;
            case 0x3F:
                return Key::F5;
            case 0x40:
                return Key::F6;
            case 0x41:
                return Key::F7;
            case 0x42:
                return Key::F8;
            case 0x43:
                return Key::F9;
            case 0x44:
                return Key::F10;
            case 0x57:
                return Key::F11;
            case 0x58:
                return Key::F12;
            default:
                return Key::Unknown;
        }
    }
} // namespace sw

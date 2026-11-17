#include "pch.h"

#include "Engine/Input/Map/InputKeyTable.h"

#include "Engine/Input/Map/AndroidKeyCode.h"

namespace sw
{
    // 스캔 코드에서 Key 로 가는 switch 표(확장 여부별)이고 케이스마다 한 줄이라 나누지 않는다.
    Key InputKeyTable::mapScanCodeToKey( uint32 scanCode, bool bExtended )
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
    Key InputKeyTable::mapAndroidKeyCode( int32 code )
    {
        const auto isInRange = [code]( int32 first, int32 last )
        { return code >= first && code <= last; };
        const auto offsetKey = [code]( Key firstKey, int32 firstCode )
        { return static_cast<Key>( static_cast<uint32>( firstKey ) + static_cast<uint32>( code - firstCode ) ); };
        if ( isInRange( AndroidKeyCode::kA, AndroidKeyCode::kZ ) )
            return offsetKey( Key::A, AndroidKeyCode::kA );
        if ( isInRange( AndroidKeyCode::kDigit0, AndroidKeyCode::kDigit9 ) )
            return offsetKey( Key::Digit0, AndroidKeyCode::kDigit0 );
        if ( isInRange( AndroidKeyCode::kF1, AndroidKeyCode::kF12 ) )
            return offsetKey( Key::F1, AndroidKeyCode::kF1 );
        if ( isInRange( AndroidKeyCode::kNumpad0, AndroidKeyCode::kNumpad9 ) )
            return offsetKey( Key::Numpad0, AndroidKeyCode::kNumpad0 );
        struct CodeKey
        {
            int32 _code;
            Key   _key;
        };
        // 뒤로 가기(Back)는 Escape 다 — 일시 정지 메뉴 · 화면 닫기가 같은 액션을 받는다(언리얼 Android 의 Back → Escape 와 같다, 기억).
        static constexpr CodeKey kArrCode[] = {
            {      AndroidKeyCode::kBack,       Key::Escape},
            {    AndroidKeyCode::kEscape,       Key::Escape},
            {       AndroidKeyCode::kTab,          Key::Tab},
            {     AndroidKeyCode::kEnter,        Key::Enter},
            {     AndroidKeyCode::kSpace,        Key::Space},
            {       AndroidKeyCode::kDel,    Key::Backspace},
            {AndroidKeyCode::kForwardDel,       Key::Delete},
            {  AndroidKeyCode::kDPadLeft,         Key::Left},
            { AndroidKeyCode::kDPadRight,        Key::Right},
            {    AndroidKeyCode::kDPadUp,           Key::Up},
            {  AndroidKeyCode::kDPadDown,         Key::Down},
            { AndroidKeyCode::kShiftLeft,    Key::LeftShift},
            {AndroidKeyCode::kShiftRight,   Key::RightShift},
            {  AndroidKeyCode::kCtrlLeft,  Key::LeftControl},
            { AndroidKeyCode::kCtrlRight, Key::RightControl},
            {   AndroidKeyCode::kAltLeft,      Key::LeftAlt},
            {  AndroidKeyCode::kAltRight,     Key::RightAlt},
            {     AndroidKeyCode::kMinus,        Key::Minus},
            {    AndroidKeyCode::kEquals,        Key::Equal},
            {     AndroidKeyCode::kComma,        Key::Comma},
            {    AndroidKeyCode::kPeriod,       Key::Period},
            {     AndroidKeyCode::kSlash,        Key::Slash},
        };
        for ( const CodeKey& entry : kArrCode )
        {
            if ( entry._code == code )
                return entry._key;
        }
        return Key::Unknown;
    }

    GamepadButton InputKeyTable::mapAndroidGamepadKeyCode( int32 code )
    {
        struct CodeButton
        {
            int32         _code;
            GamepadButton _button;
        };
        static constexpr CodeButton kArrCode[] = {
            {     AndroidKeyCode::kButtonA,             GamepadButton::A},
            {     AndroidKeyCode::kButtonB,             GamepadButton::B},
            {     AndroidKeyCode::kButtonX,             GamepadButton::X},
            {     AndroidKeyCode::kButtonY,             GamepadButton::Y},
            {    AndroidKeyCode::kButtonL1,  GamepadButton::LeftShoulder},
            {    AndroidKeyCode::kButtonR1, GamepadButton::RightShoulder},
            {AndroidKeyCode::kButtonThumbL,     GamepadButton::LeftThumb},
            {AndroidKeyCode::kButtonThumbR,    GamepadButton::RightThumb},
            { AndroidKeyCode::kButtonStart,         GamepadButton::Start},
            {AndroidKeyCode::kButtonSelect,          GamepadButton::Back},
            {      AndroidKeyCode::kDPadUp,        GamepadButton::DPadUp},
            {    AndroidKeyCode::kDPadDown,      GamepadButton::DPadDown},
            {    AndroidKeyCode::kDPadLeft,      GamepadButton::DPadLeft},
            {   AndroidKeyCode::kDPadRight,     GamepadButton::DPadRight},
        };
        for ( const CodeButton& entry : kArrCode )
        {
            if ( entry._code == code )
                return entry._button;
        }
        return GamepadButton::Count;
    }

    Key InputKeyTable::mapWebKeyCode( string_view domCode )
    {
        struct CodeName
        {
            const utf8* _pDomCode;
            const utf8* _pKeyName;
        };
        static constexpr CodeName kArrCodeName[] = {
            {   "ArrowLeft",         "Left"},
            {  "ArrowRight",        "Right"},
            {     "ArrowUp",           "Up"},
            {   "ArrowDown",         "Down"},
            {   "ShiftLeft",    "LeftShift"},
            {  "ShiftRight",   "RightShift"},
            { "ControlLeft",  "LeftControl"},
            {"ControlRight", "RightControl"},
            {     "AltLeft",      "LeftAlt"},
            {    "AltRight",     "RightAlt"},
            {    "MetaLeft",    "LeftSuper"},
            {   "MetaRight",   "RightSuper"},
            { "ContextMenu",         "Menu"},
            { "BracketLeft",  "LeftBracket"},
            {"BracketRight", "RightBracket"},
            {       "Quote",   "Apostrophe"},
            {   "Backquote",        "Grave"},
        };
        if ( domCode.empty() )
            return Key::Unknown;
        for ( const CodeName& entry : kArrCodeName )
        {
            if ( domCode == entry._pDomCode )
                return KeyCodeUtil::fromName( entry._pKeyName );
        }
        constexpr string_view kLetterPrefix = "Key"; // "KeyA" → "A"
        if ( domCode.size() == kLetterPrefix.size() + 1 && domCode.substr( 0, kLetterPrefix.size() ) == kLetterPrefix )
            return KeyCodeUtil::fromName( domCode.substr( kLetterPrefix.size() ) );
        return KeyCodeUtil::fromName( domCode );
    }
} // namespace sw

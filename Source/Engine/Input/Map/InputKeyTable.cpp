#include "pch.h"

#include "Engine/Input/Map/InputKeyTable.h"

#include "Engine/Input/Map/AndroidKeyCode.h"

namespace sw
{
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

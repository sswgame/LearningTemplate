#include "pch.h"

#include "Engine/Input/Map/InputKeyTable.h"

#include <android/keycodes.h>

namespace sw
{
    Key InputKeyTable::mapAndroidKeyCode( int32 code )
    {
        if ( code >= AKEYCODE_A && code <= AKEYCODE_Z )
            return static_cast<Key>( static_cast<uint32>( Key::A ) + static_cast<uint32>( code - AKEYCODE_A ) );
        if ( code >= AKEYCODE_0 && code <= AKEYCODE_9 )
            return static_cast<Key>( static_cast<uint32>( Key::Digit0 ) + static_cast<uint32>( code - AKEYCODE_0 ) );
        if ( code >= AKEYCODE_F1 && code <= AKEYCODE_F12 )
            return static_cast<Key>( static_cast<uint32>( Key::F1 ) + static_cast<uint32>( code - AKEYCODE_F1 ) );
        if ( code >= AKEYCODE_NUMPAD_0 && code <= AKEYCODE_NUMPAD_9 )
            return static_cast<Key>( static_cast<uint32>( Key::Numpad0 ) + static_cast<uint32>( code - AKEYCODE_NUMPAD_0 ) );
        struct CodeKey
        {
            int32 _code;
            Key   _key;
        };
        // 뒤로 가기(AKEYCODE_BACK)는 Escape 다 — 일시 정지 메뉴 · 화면 닫기가 같은 액션을 받는다(언리얼 Android 의 Back → Escape 와 같다, 기억).
        static constexpr CodeKey kArrCode[] = {
            {       AKEYCODE_BACK,       Key::Escape},
            {     AKEYCODE_ESCAPE,       Key::Escape},
            {        AKEYCODE_TAB,          Key::Tab},
            {      AKEYCODE_ENTER,        Key::Enter},
            {      AKEYCODE_SPACE,        Key::Space},
            {        AKEYCODE_DEL,    Key::Backspace},
            {AKEYCODE_FORWARD_DEL,       Key::Delete},
            {  AKEYCODE_DPAD_LEFT,         Key::Left},
            { AKEYCODE_DPAD_RIGHT,        Key::Right},
            {    AKEYCODE_DPAD_UP,           Key::Up},
            {  AKEYCODE_DPAD_DOWN,         Key::Down},
            { AKEYCODE_SHIFT_LEFT,    Key::LeftShift},
            {AKEYCODE_SHIFT_RIGHT,   Key::RightShift},
            {  AKEYCODE_CTRL_LEFT,  Key::LeftControl},
            { AKEYCODE_CTRL_RIGHT, Key::RightControl},
            {   AKEYCODE_ALT_LEFT,      Key::LeftAlt},
            {  AKEYCODE_ALT_RIGHT,     Key::RightAlt},
            {      AKEYCODE_MINUS,        Key::Minus},
            {     AKEYCODE_EQUALS,        Key::Equal},
            {      AKEYCODE_COMMA,        Key::Comma},
            {     AKEYCODE_PERIOD,       Key::Period},
            {      AKEYCODE_SLASH,        Key::Slash},
        };
        for ( const CodeKey& entry : kArrCode )
        {
            if ( entry._code == code )
                return entry._key;
        }
        return Key::Unknown;
    }
} // namespace sw

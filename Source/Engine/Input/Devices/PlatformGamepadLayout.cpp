#include "pch.h"

#include "Engine/Input/Devices/PlatformGamepadLayout.h"

namespace sw
{
    GamepadButton PlatformGamepadLayout::mapStandardButton( uint32 index )
    {
        // https://w3c.github.io/gamepad/#remapping — 0 아래 · 1 오른쪽 · 2 왼쪽 · 3 위 얼굴 버튼, 4 · 5 어깨, 6 · 7 방아쇠, 8 · 9 가운데 왼쪽 · 오른쪽, 10 · 11 스틱 누름, 12 ~ 15 D패드 위 · 아래 · 왼쪽 · 오른쪽, 16 홈.
        static constexpr GamepadButton kArrButton[kStandardButtonCount] = {
            GamepadButton::A,
            GamepadButton::B,
            GamepadButton::X,
            GamepadButton::Y,
            GamepadButton::LeftShoulder,
            GamepadButton::RightShoulder,
            GamepadButton::Count,
            GamepadButton::Count,
            GamepadButton::Back,
            GamepadButton::Start,
            GamepadButton::LeftThumb,
            GamepadButton::RightThumb,
            GamepadButton::DPadUp,
            GamepadButton::DPadDown,
            GamepadButton::DPadLeft,
            GamepadButton::DPadRight,
            GamepadButton::Count,
        };
        return index < kStandardButtonCount ? kArrButton[index] : GamepadButton::Count;
    }

    uint32 PlatformGamepadLayout::makeStandardButtonMask( const bool* pDigital, uint32 count )
    {
        uint32 mask = 0;
        for ( uint32 index = 0; pDigital != nullptr && index < count; ++index )
        {
            const GamepadButton button = mapStandardButton( index );
            if ( pDigital[index] && button != GamepadButton::Count )
                mask |= 1u << static_cast<uint32>( button );
        }
        return mask;
    }

    void PlatformGamepadLayout::splitHatAxis( float32 value, bool& outNegative, bool& outPositive )
    {
        outNegative = value <= -kHatThreshold;
        outPositive = value >= kHatThreshold;
    }
} // namespace sw

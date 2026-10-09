#include "pch.h"

#include "Engine/Input/InputSlotUtil.h"

#include "Core/Container/StringUtil.h"

namespace sw
{
    namespace
    {
        struct InputSlotUtilInternal
        {
            static constexpr string_view kKeyPrefix     = "Key";
            static constexpr string_view kMousePrefix   = "Mouse";
            static constexpr string_view kGamepadPrefix = "Gamepad";

            /** @brief 열거자 이름으로 읽은 값이 실제로 그 이름인지 봅니다 — 리플렉션 조회는 모르는 이름에 기본값을 돌려줄 수 있다. */
            static bool isSameName( const utf8* pCanonical, string_view name ) { return pCanonical != nullptr && StringUtil::equals( string_view( pCanonical ), name, true ); }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool InputSlotUtil::tryParse( string_view text, InputSlot& outSlot )
    {
        const size_t dotPos = text.find( '.' );
        if ( dotPos == string_view::npos || dotPos == 0 || dotPos + 1 >= text.size() )
            return false;
        const string_view device = text.substr( 0, dotPos );
        const string_view name   = text.substr( dotPos + 1 );

        if ( StringUtil::equals( device, InputSlotUtilInternal::kKeyPrefix, true ) )
        {
            const Key key = KeyCodeUtil::fromName( name );
            if ( key == Key::Unknown || InputSlotUtilInternal::isSameName( KeyCodeUtil::toName( key ), name ) == false )
                return false;
            outSlot = InputSlot::fromKey( key );
            return true;
        }
        if ( StringUtil::equals( device, InputSlotUtilInternal::kMousePrefix, true ) )
        {
            const MouseButton button = MouseButtonUtil::fromName( name );
            if ( button == MouseButton::Count || InputSlotUtilInternal::isSameName( MouseButtonUtil::toName( button ), name ) == false )
                return false;
            outSlot = InputSlot::fromMouseButton( button );
            return true;
        }
        if ( StringUtil::startsWith( device, InputSlotUtilInternal::kGamepadPrefix, true ) )
        {
            int32             padIndex   = 0;
            const string_view padNumber  = device.substr( InputSlotUtilInternal::kGamepadPrefix.size() );
            const bool        bHasNumber = padNumber.empty() == false;
            if ( bHasNumber && StringUtil::parseInt( padNumber, padIndex ) == false )
                return false;
            const bool bPadInRange = 0 <= padIndex && padIndex < static_cast<int32>( kMaxGamepadSlot );
            if ( bPadInRange == false )
                return false;
            const GamepadButton button = GamepadButtonUtil::fromName( name );
            if ( button == GamepadButton::Count )
                return false;
            outSlot = InputSlot::fromGamepadButton( button, static_cast<uint8>( padIndex ) );
            return true;
        }
        return false;
    }

    string InputSlotUtil::toText( const InputSlot& slot )
    {
        switch ( slot._deviceKind )
        {
            case InputDeviceKind::Keyboard:
            {
                return string( InputSlotUtilInternal::kKeyPrefix ) + "." + KeyCodeUtil::toName( static_cast<Key>( slot._controlIndex ) );
            }
            case InputDeviceKind::Mouse:
            {
                return string( InputSlotUtilInternal::kMousePrefix ) + "." + MouseButtonUtil::toName( static_cast<MouseButton>( slot._controlIndex ) );
            }
            case InputDeviceKind::Gamepad:
            {
                string text( InputSlotUtilInternal::kGamepadPrefix );
                if ( slot._deviceIndex != 0 )
                {
                    utf8 arrDigit[constant::kMaxBuffer16]{};
                    StringUtil::formatNumber( arrDigit, constant::kMaxBuffer16, static_cast<uint32>( slot._deviceIndex ) );
                    text += arrDigit;
                }
                const utf8* pName = GamepadButtonUtil::toName( static_cast<GamepadButton>( slot._controlIndex ) );
                return text + "." + ( pName != nullptr ? pName : "" );
            }
            case InputDeviceKind::Touch:
            case InputDeviceKind::Custom:
            case InputDeviceKind::Count:
            {
                return {};
            }
        }
        return {};
    }
} // namespace sw

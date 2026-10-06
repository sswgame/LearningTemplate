#include "pch.h"

#include "Engine/Input/GamepadButtonUtil.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/InputMap.h"
#include "Engine/Input/KeyCodeUtil.h"

/**
 * @file InputMapGlyph.cpp
 * @brief 바인딩을 UI 프롬프트 문자열("[ E ]" 등)로 바꾸는 글리프 조회입니다.
 *
 * getGlyphForAction() 은 현재 활성 장치(키보드 · Xbox · PlayStation · Switch)에 맞는 표기를 고릅니다.
 * previewDevice 를 직접 넘기는 오버로드는 실제 장치와 상관없이 특정 플랫폼 표기를 미리 볼 때 씁니다
 * (에디터의 Glyph Previewer 탭이 이것으로 플랫폼별 표기를 나란히 비교해 보여 줍니다).
 */

namespace sw
{
    namespace
    {
        struct InputMapGlyphInternal
        {
            static string slotToGlyph( const InputSlot& slot, InputGlyphStyle device )
            {
                if ( slot._deviceKind == InputDeviceKind::Keyboard )
                {
                    const Key key = static_cast<Key>( slot._controlIndex );
                    if ( key != Key::Unknown )
                    {
                        const utf8* pName = KeyCodeUtil::toName( key );
                        return pName != nullptr ? pName : "?";
                    }
                }
                else if ( slot._deviceKind == InputDeviceKind::Mouse )
                {
                    const MouseButton btn = static_cast<MouseButton>( slot._controlIndex );
                    if ( btn != MouseButton::Count )
                    {
                        const utf8* pName = MouseButtonUtil::toName( btn );
                        return pName != nullptr ? pName : "?";
                    }
                }
                else if ( slot._deviceKind == InputDeviceKind::Gamepad )
                {
                    const GamepadButton btn = static_cast<GamepadButton>( slot._controlIndex );
                    if ( btn != GamepadButton::Count )
                    {
                        if ( device == InputGlyphStyle::GamepadPlayStation )
                        {
                            if ( btn == GamepadButton::A )
                                return "X";
                            if ( btn == GamepadButton::B )
                                return "Circle";
                            if ( btn == GamepadButton::X )
                                return "Square";
                            if ( btn == GamepadButton::Y )
                                return "Triangle";
                        }
                        else if ( device == InputGlyphStyle::GamepadSwitch )
                        {
                            // 닌텐도 배치: Xbox 기준으로 A/B, X/Y 위치가 서로 뒤바뀐다.
                            if ( btn == GamepadButton::A )
                                return "B";
                            if ( btn == GamepadButton::B )
                                return "A";
                            if ( btn == GamepadButton::X )
                                return "Y";
                            if ( btn == GamepadButton::Y )
                                return "X";
                        }
                        const utf8* pName = GamepadButtonUtil::toName( btn );
                        return pName != nullptr ? pName : "?";
                    }
                }
                return "?";
            }

            /** @brief 슬롯이 키보드 · 마우스 장치인지 반환합니다. */
            static bool isKeyboardOrMouseSlot( const InputSlot& slot )
            {
                return slot._deviceKind == InputDeviceKind::Keyboard || slot._deviceKind == InputDeviceKind::Mouse;
            }

            /**
             * @brief 키보드 · 마우스 장치에서 바인딩 하나의 표기입니다. 이 장치로 보일 것이 없으면 빈 문자열입니다(다음 바인딩을 본다).
             * @details `BindingKind` 를 빠짐없이 다룹니다(파일 머리의 -Wswitch-enum 오류가 빠진 종류를 짚는다).
             */
            static string makeKeyboardMouseGlyph( const ActionBinding& binding, InputGlyphStyle device )
            {
                switch ( binding._kind )
                {
                    case BindingKind::SingleSlot:
                    {
                        if ( isKeyboardOrMouseSlot( binding._arrSlot[0] ) == false )
                            return {};
                        const string glyph = slotToGlyph( binding._arrSlot[0], device );
                        return glyph != "?" ? string( "[ " ) + glyph + " ]" : string{};
                    }
                    case BindingKind::Axis1DComposite:
                    {
                        if ( isKeyboardOrMouseSlot( binding._arrSlot[0] ) == false )
                            return {};
                        return string( "[ " ) + slotToGlyph( binding._arrSlot[0], device ) + " / " + slotToGlyph( binding._arrSlot[1], device ) + " ]";
                    }
                    case BindingKind::Vector2DComposite:
                    {
                        return string( "[ " ) + slotToGlyph( binding._arrSlot[0], device ) + slotToGlyph( binding._arrSlot[1], device ) +
                               slotToGlyph( binding._arrSlot[2], device ) + slotToGlyph( binding._arrSlot[3], device ) + " ]";
                    }
                    case BindingKind::Chord:
                    {
                        return string( "[ " ) + slotToGlyph( binding._arrSlot[0], device ) + " + " + slotToGlyph( binding._arrSlot[1], device ) + " ]";
                    }
                    case BindingKind::MouseDelta2D:
                    {
                        return "[ Mouse Look ]";
                    }
                    case BindingKind::MouseWheel1D:
                    {
                        return "[ Mouse Wheel ]";
                    }
                    case BindingKind::VirtualJoystick2D:
                    {
                        return string( "[ Drag " ) + slotToGlyph( binding._arrSlot[0], device ) + " ]";
                    }
                    case BindingKind::Shortcut:
                    {
                        string modifierText;
                        if ( ( binding._modifierMask & ModifierKey::Ctrl ) != 0 )
                            modifierText += "Ctrl + ";
                        if ( ( binding._modifierMask & ModifierKey::Shift ) != 0 )
                            modifierText += "Shift + ";
                        if ( ( binding._modifierMask & ModifierKey::Alt ) != 0 )
                            modifierText += "Alt + ";
                        if ( ( binding._modifierMask & ModifierKey::Super ) != 0 )
                            modifierText += "Win + ";
                        return string( "[ " ) + modifierText + slotToGlyph( binding._arrSlot[0], device ) + " ]";
                    }
                    case BindingKind::AnyKey:
                    {
                        return "[ Any Key ]";
                    }
                    case BindingKind::GamepadStick2D: // 게임패드 전용 — 키보드 표기가 없다
                    case BindingKind::Count:
                    {
                        return {};
                    }
                }
            }

            /**
             * @brief 게임패드 장치에서 바인딩 하나의 표기입니다. 이 장치로 보일 것이 없으면 빈 문자열입니다(다음 바인딩을 본다).
             * @details `BindingKind` 를 빠짐없이 다룹니다(파일 머리의 -Wswitch-enum 오류가 빠진 종류를 짚는다).
             */
            static string makeGamepadGlyph( const ActionBinding& binding, InputGlyphStyle device )
            {
                switch ( binding._kind )
                {
                    case BindingKind::SingleSlot:
                    {
                        if ( binding._arrSlot[0]._deviceKind != InputDeviceKind::Gamepad )
                            return {};
                        const string glyph = slotToGlyph( binding._arrSlot[0], device );
                        return glyph != "?" ? string( "[ " ) + glyph + " ]" : string{};
                    }
                    case BindingKind::GamepadStick2D:
                    {
                        return ( binding._stick == GamepadStick::Left ) ? "[ L-Stick ]" : "[ R-Stick ]";
                    }
                    case BindingKind::Chord:
                    {
                        return string( "[ " ) + slotToGlyph( binding._arrSlot[0], device ) + " + " + slotToGlyph( binding._arrSlot[1], device ) + " ]";
                    }
                    case BindingKind::AnyKey:
                    {
                        return "[ Any Button ]";
                    }
                    case BindingKind::Axis1DComposite:
                    {
                        if ( binding._arrSlot[0]._deviceKind != InputDeviceKind::Gamepad )
                            return {};
                        return string( "[ " ) + slotToGlyph( binding._arrSlot[0], device ) + " / " + slotToGlyph( binding._arrSlot[1], device ) + " ]";
                    }
                    case BindingKind::Vector2DComposite: // 키보드 · 마우스 전용 종류 — 게임패드 표기가 없다
                    case BindingKind::MouseDelta2D:
                    case BindingKind::MouseWheel1D:
                    case BindingKind::VirtualJoystick2D:
                    case BindingKind::Shortcut:
                    case BindingKind::Count:
                    {
                        return {};
                    }
                }
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    string InputMap::getGlyphForAction( const hashed_string& action ) const
    {
        const InputGlyphStyle device = _pInput != nullptr ? _pInput->getActiveGlyphStyle() : InputGlyphStyle::KeyboardMouse;
        return getGlyphForActionInternal( action, device );
    }

    string InputMap::getGlyphForAction( const hashed_string& action, InputGlyphStyle previewDevice ) const
    {
        return getGlyphForActionInternal( action, previewDevice );
    }

    string InputMap::getGlyphForSlot( const InputSlot& slot, InputGlyphStyle device )
    {
        return string( "[ " ) + InputMapGlyphInternal::slotToGlyph( slot, device ) + " ]";
    }

    string InputMap::getGlyphForActionInternal( const hashed_string& action, InputGlyphStyle device ) const
    {
        const ActionEntry* pEntry = findAction( action );
        if ( pEntry == nullptr || pEntry->_listBinding.empty() )
            return "[ ? ]";

        for ( const ActionBinding& binding : pEntry->_listBinding )
        {
            const string glyph = ( device == InputGlyphStyle::KeyboardMouse ) ? InputMapGlyphInternal::makeKeyboardMouseGlyph( binding, device )
                                                                              : InputMapGlyphInternal::makeGamepadGlyph( binding, device );
            if ( glyph.empty() == false )
                return glyph;
        }
        return "[ ? ]";
    }
} // namespace sw

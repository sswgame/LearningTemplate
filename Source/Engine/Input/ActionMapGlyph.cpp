#include "pch.h"

#include "Engine/Input/ActionMap.h"
#include "Engine/Input/GamepadButtons.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/KeyCodes.h"

/**
 * @file ActionMapGlyph.cpp
 * @brief 바인딩을 UI 프롬프트 문자열("[ E ]" 등)로 바꾸는 글리프 조회 기능입니다.
 *
 * 초심자 가이드: getGlyphForAction()은 현재 활성 장치(키보드/Xbox/PlayStation/Switch)에 맞는 표기를 고릅니다.
 * previewDevice를 직접 넘기는 오버로드는 실제 장치와 무관하게 특정 플랫폼 표기를 강제로 미리보기할 때 씁니다
 * (에디터의 Glyph Previewer 탭이 이걸로 각 플랫폼 표기를 동시에 비교해서 보여줍니다).
 */

namespace sw
{
    namespace
    {
        struct ActionMapGlyphInternal
        {
            static string slotToGlyph( const InputSlot& slot, InputDeviceType device )
            {
                if ( slot._deviceKind == InputDeviceKind::Keyboard )
                {
                    const Key key = static_cast<Key>( slot._controlIndex );
                    if ( key != Key::Unknown )
                    {
                        const utf8* pName = KeyCodes::toName( key );
                        return pName != nullptr ? pName : "?";
                    }
                }
                else if ( slot._deviceKind == InputDeviceKind::Mouse )
                {
                    const MouseButton btn = static_cast<MouseButton>( slot._controlIndex );
                    if ( btn != MouseButton::Count )
                    {
                        const utf8* pName = MouseButtons::toName( btn );
                        return pName != nullptr ? pName : "?";
                    }
                }
                else if ( slot._deviceKind == InputDeviceKind::Gamepad )
                {
                    const GamepadButton btn = static_cast<GamepadButton>( slot._controlIndex );
                    if ( btn != GamepadButton::Count )
                    {
                        if ( device == InputDeviceType::GamepadPlayStation )
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
                        else if ( device == InputDeviceType::GamepadSwitch )
                        {
                            // 닌텐도 배치: Xbox 기준 A/B, X/Y 위치가 서로 뒤바뀝니다.
                            if ( btn == GamepadButton::A )
                                return "B";
                            if ( btn == GamepadButton::B )
                                return "A";
                            if ( btn == GamepadButton::X )
                                return "Y";
                            if ( btn == GamepadButton::Y )
                                return "X";
                        }
                        const utf8* pName = GamepadButtons::toName( btn );
                        return pName != nullptr ? pName : "?";
                    }
                }
                return "?";
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    string ActionMap::getGlyphForAction( string_view action ) const
    {
        return getGlyphForAction( hashed_string( action ) );
    }

    string ActionMap::getGlyphForAction( const hashed_string& action ) const
    {
        const InputDeviceType device = _pInput != nullptr ? _pInput->getActiveDeviceType() : InputDeviceType::KeyboardMouse;
        return getGlyphForActionInternal( action, device );
    }

    string ActionMap::getGlyphForAction( string_view action, InputDeviceType previewDevice ) const
    {
        return getGlyphForAction( hashed_string( action ), previewDevice );
    }

    string ActionMap::getGlyphForAction( const hashed_string& action, InputDeviceType previewDevice ) const
    {
        return getGlyphForActionInternal( action, previewDevice );
    }

    string ActionMap::getGlyphForActionInternal( const hashed_string& action, InputDeviceType device ) const
    {
        const ActionEntry* pEntry = findAction( action );
        if ( pEntry == nullptr || pEntry->_listBinding.empty() )
            return "[ ? ]";

        for ( const ActionBinding& b : pEntry->_listBinding )
        {
            if ( device == InputDeviceType::KeyboardMouse )
            {
                if ( b._kind == BindingKind::SingleSlot )
                {
                    if ( b._arrSlot[0]._deviceKind == InputDeviceKind::Keyboard || b._arrSlot[0]._deviceKind == InputDeviceKind::Mouse )
                    {
                        const string glyph = ActionMapGlyphInternal::slotToGlyph( b._arrSlot[0], device );
                        if ( glyph != "?" )
                            return string( "[ " ) + glyph + " ]";
                    }
                }
                else if ( b._kind == BindingKind::Axis1DComposite )
                {
                    return string( "[ " ) + ActionMapGlyphInternal::slotToGlyph( b._arrSlot[0], device ) + " / " + ActionMapGlyphInternal::slotToGlyph( b._arrSlot[1], device ) + " ]";
                }
                else if ( b._kind == BindingKind::Vector2DComposite )
                {
                    return string( "[ " ) + ActionMapGlyphInternal::slotToGlyph( b._arrSlot[0], device ) + ActionMapGlyphInternal::slotToGlyph( b._arrSlot[1], device ) + ActionMapGlyphInternal::slotToGlyph( b._arrSlot[2], device ) + ActionMapGlyphInternal::slotToGlyph( b._arrSlot[3], device ) + " ]";
                }
                else if ( b._kind == BindingKind::Chord )
                {
                    return string( "[ " ) + ActionMapGlyphInternal::slotToGlyph( b._arrSlot[0], device ) + " + " + ActionMapGlyphInternal::slotToGlyph( b._arrSlot[1], device ) + " ]";
                }
                else if ( b._kind == BindingKind::MouseDelta2D )
                {
                    return "[ Mouse Look ]";
                }
                else if ( b._kind == BindingKind::VirtualJoystick2D )
                {
                    return string( "[ Drag " ) + ActionMapGlyphInternal::slotToGlyph( b._arrSlot[0], device ) + " ]";
                }
                else if ( b._kind == BindingKind::Shortcut )
                {
                    string modStr;
                    if ( ( b._modifierMask & ModifierKey::Ctrl ) != 0 )
                        modStr += "Ctrl + ";
                    if ( ( b._modifierMask & ModifierKey::Shift ) != 0 )
                        modStr += "Shift + ";
                    if ( ( b._modifierMask & ModifierKey::Alt ) != 0 )
                        modStr += "Alt + ";
                    if ( ( b._modifierMask & ModifierKey::Super ) != 0 )
                        modStr += "Win + ";
                    return string( "[ " ) + modStr + ActionMapGlyphInternal::slotToGlyph( b._arrSlot[0], device ) + " ]";
                }
                else if ( b._kind == BindingKind::AnyKey )
                {
                    return "[ Any Key ]";
                }
            }
            else
            {
                if ( b._kind == BindingKind::SingleSlot && b._arrSlot[0]._deviceKind == InputDeviceKind::Gamepad )
                {
                    const string glyph = ActionMapGlyphInternal::slotToGlyph( b._arrSlot[0], device );
                    if ( glyph != "?" )
                        return string( "[ " ) + glyph + " ]";
                }
                else if ( b._kind == BindingKind::GamepadStick2D )
                {
                    return ( b._stick == GamepadStick::Left ) ? "[ L-Stick ]" : "[ R-Stick ]";
                }
                else if ( b._kind == BindingKind::Chord )
                {
                    return string( "[ " ) + ActionMapGlyphInternal::slotToGlyph( b._arrSlot[0], device ) + " + " + ActionMapGlyphInternal::slotToGlyph( b._arrSlot[1], device ) + " ]";
                }
                else if ( b._kind == BindingKind::AnyKey )
                {
                    return "[ Any Button ]";
                }
            }
        }
        return "[ ? ]";
    }
} // namespace sw

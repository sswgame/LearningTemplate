#include "pch.h"

// 금지 헤더(EditorInputBan.h)를 받지 않는 두 파일 중 하나다 — ImGui 입력을 직접 읽는 유일한 자리(유니티 묶음에서 빠진다, CMake `sw_applyEditorInputBan`).
#define SW_EDITOR_INPUT_ADAPTER

#include "Editor/Common/Backend/ImGuiKeyForward.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/string.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct ImGuiKeyForwardInternal
        {
            /** @brief ImGui 키 하나와 엔진 키 하나의 대응입니다. 두 열거형에서 연속이 아닌 키만 표에 둡니다. */
            struct ImGuiKeyPair
            {
                ImGuiKey _imguiKey;
                Key      _key;
            };

            /** @brief ImGui 키의 연속 구간과 엔진 키의 연속 구간의 대응입니다(영문 · 윗줄 숫자 · 기능 키 · 숫자 패드). */
            struct ImGuiKeyRange
            {
                ImGuiKey _firstImGuiKey;
                Key      _firstKey;
                int32    _count;
            };

            static constexpr ImGuiKeyRange kArrRange[] = {
                {      ImGuiKey_A,       Key::A, 26},
                {      ImGuiKey_0,  Key::Digit0, 10},
                {     ImGuiKey_F1,      Key::F1, 12},
                {ImGuiKey_Keypad0, Key::Numpad0, 10},
            };

            static constexpr ImGuiKeyPair kArrNamedKey[] = {
                {           ImGuiKey_Tab,            Key::Tab},
                {     ImGuiKey_LeftArrow,           Key::Left},
                {    ImGuiKey_RightArrow,          Key::Right},
                {       ImGuiKey_UpArrow,             Key::Up},
                {     ImGuiKey_DownArrow,           Key::Down},
                {        ImGuiKey_PageUp,         Key::PageUp},
                {      ImGuiKey_PageDown,       Key::PageDown},
                {          ImGuiKey_Home,           Key::Home},
                {           ImGuiKey_End,            Key::End},
                {        ImGuiKey_Insert,         Key::Insert},
                {        ImGuiKey_Delete,         Key::Delete},
                {     ImGuiKey_Backspace,      Key::Backspace},
                {         ImGuiKey_Space,          Key::Space},
                {         ImGuiKey_Enter,          Key::Enter},
                {        ImGuiKey_Escape,         Key::Escape},
                {      ImGuiKey_LeftCtrl,    Key::LeftControl},
                {     ImGuiKey_LeftShift,      Key::LeftShift},
                {       ImGuiKey_LeftAlt,        Key::LeftAlt},
                {     ImGuiKey_LeftSuper,      Key::LeftSuper},
                {     ImGuiKey_RightCtrl,   Key::RightControl},
                {    ImGuiKey_RightShift,     Key::RightShift},
                {      ImGuiKey_RightAlt,       Key::RightAlt},
                {    ImGuiKey_RightSuper,     Key::RightSuper},
                {          ImGuiKey_Menu,           Key::Menu},
                {    ImGuiKey_Apostrophe,     Key::Apostrophe},
                {         ImGuiKey_Comma,          Key::Comma},
                {         ImGuiKey_Minus,          Key::Minus},
                {        ImGuiKey_Period,         Key::Period},
                {         ImGuiKey_Slash,          Key::Slash},
                {     ImGuiKey_Semicolon,      Key::Semicolon},
                {         ImGuiKey_Equal,          Key::Equal},
                {   ImGuiKey_LeftBracket,    Key::LeftBracket},
                {     ImGuiKey_Backslash,      Key::Backslash},
                {  ImGuiKey_RightBracket,   Key::RightBracket},
                {   ImGuiKey_GraveAccent,          Key::Grave},
                {      ImGuiKey_CapsLock,       Key::CapsLock},
                {    ImGuiKey_ScrollLock,     Key::ScrollLock},
                {       ImGuiKey_NumLock,        Key::NumLock},
                {   ImGuiKey_PrintScreen,    Key::PrintScreen},
                {         ImGuiKey_Pause,          Key::Pause},
                { ImGuiKey_KeypadDecimal,  Key::NumpadDecimal},
                {  ImGuiKey_KeypadDivide,   Key::NumpadDivide},
                {ImGuiKey_KeypadMultiply, Key::NumpadMultiply},
                {ImGuiKey_KeypadSubtract, Key::NumpadSubtract},
                {     ImGuiKey_KeypadAdd,      Key::NumpadAdd},
                {   ImGuiKey_KeypadEnter,    Key::NumpadEnter},
            };

            /** @brief 엔진 키에 짝인 ImGui 키입니다. 짝이 없으면 `ImGuiKey_None` 입니다. */
            static ImGuiKey toImGuiKey( Key key )
            {
                const int32 keyIndex = static_cast<int32>( key );
                for ( const ImGuiKeyRange& range : kArrRange )
                {
                    const int32 firstIndex = static_cast<int32>( range._firstKey );
                    if ( firstIndex <= keyIndex && keyIndex < firstIndex + range._count )
                        return static_cast<ImGuiKey>( range._firstImGuiKey + ( keyIndex - firstIndex ) );
                }
                for ( const ImGuiKeyPair& pair : kArrNamedKey )
                {
                    if ( pair._key == key )
                        return pair._imguiKey;
                }
                return ImGuiKey_None;
            }

            /** @brief 지금 눌려 있는 수정 키의 `ModifierKey` 마스크입니다. ImGui 의 `io.KeyCtrl` 은 입력 큐를 비운 뒤에야 갱신되어 한 프레임 늦으므로 좌우 키 상태를 직접 본다. */
            static uint8 makeModifierMask()
            {
                uint8 mask = 0;
                if ( ImGui::IsKeyDown( ImGuiKey_LeftCtrl ) || ImGui::IsKeyDown( ImGuiKey_RightCtrl ) )
                    mask = static_cast<uint8>( mask | ModifierKey::Ctrl );
                if ( ImGui::IsKeyDown( ImGuiKey_LeftShift ) || ImGui::IsKeyDown( ImGuiKey_RightShift ) )
                    mask = static_cast<uint8>( mask | ModifierKey::Shift );
                if ( ImGui::IsKeyDown( ImGuiKey_LeftAlt ) || ImGui::IsKeyDown( ImGuiKey_RightAlt ) )
                    mask = static_cast<uint8>( mask | ModifierKey::Alt );
                if ( ImGui::IsKeyDown( ImGuiKey_LeftSuper ) || ImGui::IsKeyDown( ImGuiKey_RightSuper ) )
                    mask = static_cast<uint8>( mask | ModifierKey::Super );
                return mask;
            }
        };
    } // namespace

    bool ImGuiKeyForward::isKeyDown( Key key )
    {
        const ImGuiKey imguiKey = ImGuiKeyForwardInternal::toImGuiKey( key );
        return imguiKey != ImGuiKey_None && ImGui::IsKeyDown( imguiKey );
    }

    Key ImGuiKeyForward::findPressedKey()
    {
        for ( int32 keyIndex = static_cast<int32>( Key::Unknown ) + 1; keyIndex < static_cast<int32>( Key::Count ); ++keyIndex )
        {
            const Key      key      = static_cast<Key>( keyIndex );
            const ImGuiKey imguiKey = ImGuiKeyForwardInternal::toImGuiKey( key );
            if ( imguiKey != ImGuiKey_None && ImGui::IsKeyPressed( imguiKey, false ) )
                return key;
        }
        return Key::Unknown;
    }

    bool ImGuiKeyForward::findPressedChord( Key& outKey, uint8& outModifierMask )
    {
        for ( int32 keyIndex = static_cast<int32>( Key::Unknown ) + 1; keyIndex < static_cast<int32>( Key::Count ); ++keyIndex )
        {
            const Key  key       = static_cast<Key>( keyIndex );
            const bool bModifier = key == Key::LeftControl || key == Key::RightControl || key == Key::LeftShift || key == Key::RightShift || key == Key::LeftAlt ||
                                   key == Key::RightAlt || key == Key::LeftSuper || key == Key::RightSuper;
            if ( bModifier )
                continue;
            const ImGuiKey imguiKey = ImGuiKeyForwardInternal::toImGuiKey( key );
            const bool     bPressed = imguiKey != ImGuiKey_None && ImGui::IsKeyPressed( imguiKey, false );
            if ( bPressed == false )
                continue;
            outKey          = key;
            outModifierMask = ImGuiKeyForwardInternal::makeModifierMask();
            return true;
        }
        return false;
    }

    void ImGuiKeyForward::appendKeyChanges( vector<RawInputEvent>& outListEvent )
    {
        const uint8 modifierMask = ImGuiKeyForwardInternal::makeModifierMask();
        for ( int32 keyIndex = static_cast<int32>( Key::Unknown ) + 1; keyIndex < static_cast<int32>( Key::Count ); ++keyIndex )
        {
            const Key      key      = static_cast<Key>( keyIndex );
            const ImGuiKey imguiKey = ImGuiKeyForwardInternal::toImGuiKey( key );
            if ( imguiKey == ImGuiKey_None )
                continue;
            if ( ImGui::IsKeyPressed( imguiKey, false ) )
                outListEvent.push_back( RawInputEvent::makeKeyDown( key, 0, false, modifierMask ) );
            else if ( ImGui::IsKeyPressed( imguiKey, true ) )
                outListEvent.push_back( RawInputEvent::makeKeyDown( key, 0, true, modifierMask ) );
            else if ( ImGui::IsKeyReleased( imguiKey ) )
                outListEvent.push_back( RawInputEvent::makeKeyUp( key, 0, modifierMask ) );
        }
    }

    void ImGuiKeyForward::appendTextInput( vector<RawInputEvent>& outListEvent )
    {
        const ImGuiIO& io = ImGui::GetIO();
        for ( int32 index = 0; index < io.InputQueueCharacters.Size; ++index )
        {
            string character;
            StringUtil::appendUTF8( character, static_cast<uint32>( io.InputQueueCharacters[index] ) );
            outListEvent.push_back( RawInputEvent::makeTextInput( character ) );
        }
    }
} // namespace sw::editor

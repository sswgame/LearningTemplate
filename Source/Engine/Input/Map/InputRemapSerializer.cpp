#include "pch.h"

#include "Engine/Input/Map/InputRemapSerializer.h"

#include "Core/String/StringUtil.h"

#include "Engine/Input/Map/InputMap.h"
#include "Engine/Input/Map/InputMapXML.h"
#include "Engine/Input/RawInputEvent.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLNameCheck.h"

/**
 * @file InputRemapSerializer.cpp
 * @brief 플레이어 키 리매핑 파일(`<UserBindings>`)의 쓰기 · 읽기와 수정 키 이름 표를 구현합니다.
 * @details 파일은 `<action name>` 아래 바인딩 원소들이고, 맵 정의와 같은 원소 · 같은 스키마다(`InputMapXML`). 바인딩마다 레이어와 트리거를 적는다.
 */

namespace sw
{
    namespace
    {
        struct InputRemapSerializerInternal
        {
            struct ModifierNameEntry
            {
                const utf8* _pName;
                uint8       _bit;
            };
            /** @brief `<bind modifier="Ctrl+Shift">` 의 이름과 수정 키 비트입니다. 읽기와 쓰기가 이 표 하나를 쓴다. */
            static constexpr ModifierNameEntry kArrModifierNames[] = {
                { "Ctrl",  ModifierKey::Ctrl},
                {"Shift", ModifierKey::Shift},
                {  "Alt",   ModifierKey::Alt},
                {"Super", ModifierKey::Super},
            };
            static constexpr const utf8* kArrRootChild[] = { "action" };
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool InputRemapSerializer::save( const InputMap& map, string_view filePath )
    {
        if ( filePath.empty() )
            return false;

        XMLDocument   doc;
        const XMLNode root        = doc.appendRoot( "UserBindings" );
        bool          bAllWritten = true;
        for ( const hashed_string& actionName : map.getActionNames() )
        {
            bAllWritten = InputMapXML::writeAction( root, map, actionName ) && bAllWritten;
        }
        return bAllWritten && doc.saveToFile( filePath );
    }

    bool InputRemapSerializer::load( InputMap& map, string_view filePath )
    {
        if ( filePath.empty() )
            return false;

        // 사용자 바인딩은 `save` 가 디스크에 쓴 플레이어 파일이다 — 리소스 id 가 아니므로 디스크 입구로 읽는다.
        XMLDocument doc;
        if ( doc.loadFromFile( filePath ) == false )
            return false;

        const XMLNode root = doc.getRoot( "UserBindings" );
        if ( root.isValid() == false )
            return false;

        bool bCursorConflict = false;
        bool bValid          = XMLNameCheck::reportUnknownChildren( root, InputRemapSerializerInternal::kArrRootChild, filePath ); // 깨진 칸이 있는 바인딩은 넣지 않고 읽기를 실패로 돌린다
        for ( XMLNode actionNode = root.findChild( "action" ); actionNode.isValid(); actionNode = actionNode.findNextSibling( "action" ) )
        {
            const hashed_string actionName( actionNode.getAttributeText( "name" ) );
            const uint32        previousCount = map.getBindingCount( actionName );
            InputMapXML::readAction( map, actionNode, map.getDefaultLayerName(), filePath, bValid );
            // 맵 읽기와 같은 검사 — 가상 커서가 마우스로 바꾸는 스틱 · 버튼을 다시 묶은 바인딩은 넣지 않고 읽기를 실패로 돌린다.
            bCursorConflict = map.removeVirtualCursorConflicts( filePath, actionName, previousCount ) > 0 || bCursorConflict;
        }
        if ( bValid == false )
            SW_LOG_ERROR_ID( "InputRemapSerializer.UserBindingsInvalidAttribute", "user bindings %# have invalid attribute values (listed above)", filePath );
        return bCursorConflict == false && bValid;
    }

    bool InputRemapSerializer::tryParseModifierNames( string_view text, uint8& outModifierMask )
    {
        outModifierMask = ModifierKey::None;
        while ( true )
        {
            const size_t      plusIndex = text.find( '+' );
            const string_view token     = text.substr( 0, plusIndex );
            uint8             bit       = ModifierKey::None;
            for ( const InputRemapSerializerInternal::ModifierNameEntry& entry : InputRemapSerializerInternal::kArrModifierNames )
            {
                if ( StringUtil::equals( token, entry._pName, true ) )
                    bit = entry._bit;
            }
            if ( bit == ModifierKey::None || ( outModifierMask & bit ) != 0 )
                return false;
            outModifierMask = static_cast<uint8>( outModifierMask | bit );
            if ( plusIndex == string_view::npos )
                return true;
            text = text.substr( plusIndex + 1 );
        }
    }

    string InputRemapSerializer::formatModifierNames( uint8 modifierMask )
    {
        string text;
        for ( const InputRemapSerializerInternal::ModifierNameEntry& entry : InputRemapSerializerInternal::kArrModifierNames )
        {
            if ( ( modifierMask & entry._bit ) == 0 )
                continue;
            if ( text.empty() == false )
                text += "+";
            text += entry._pName;
        }
        return text;
    }
} // namespace sw

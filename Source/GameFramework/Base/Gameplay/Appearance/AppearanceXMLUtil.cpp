#include "pch.h"

#include "GameFramework/Base/Gameplay/Appearance/AppearanceXMLUtil.h"

#include "Core/Container/StringUtil.h"

#include "Engine/Object/Component/TagSystem.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLNameCheck.h"

#include "GameFramework/Base/Foundation/Data/GameDataXML.h"
#include "GameFramework/Base/Gameplay/Appearance/AppearanceTypes.h"

namespace sw
{
    SW_LOG_CALLER( "Appearance" );

    void AppearanceLoadReport::addMessage( string_view message )
    {
        _listError.push_back( string( message ) );
        SW_LOG_WARNING( "%#", message );
    }

    uint32 AppearanceLoadReport::countContaining( string_view text ) const
    {
        uint32 count = 0;
        for ( const string& error : _listError )
        {
            if ( string_view( error.c_str(), error.size() ).find( text ) != string_view::npos )
                ++count;
        }
        return count;
    }

    string AppearanceLoadReport::joined() const
    {
        string result;
        for ( const string& error : _listError )
        {
            result += "\n  ";
            result += error;
        }
        return result;
    }

    bool AppearanceXMLUtil::reportUnknownAttributes( const XMLNode& node, const utf8* const* ppKnownName, uint32 knownCount, AppearanceLoadReport& report, string_view sourceName )
    {
        vector<const utf8*> listUnknown;
        if ( XMLNameCheck::collectUnknownAttributes( node, ppKnownName, knownCount, listUnknown ) == 0 )
            return true;
        for ( const utf8* pName : listUnknown )
        {
            report.addError( "%#: <%#> has unknown attribute '%#'", sourceName, node.getName(), pName );
        }
        return false;
    }

    void AppearanceXMLUtil::reportUnknownChild( const XMLNode& parent, const XMLNode& child, AppearanceLoadReport& report, string_view sourceName )
    {
        report.addError( "%#: <%#> has unknown element <%#>", sourceName, parent.getName(), child.getName() );
    }

    hashed_string AppearanceXMLUtil::readName( const XMLNode& node, const utf8* pAttribute )
    {
        const string_view text = StringUtil::trim( node.getAttributeText( pAttribute ) );
        return text.empty() ? hashed_string{} : hashed_string( text );
    }

    void AppearanceXMLUtil::readNameList( const XMLNode& node, const utf8* pAttribute, vector<hashed_string>& outListName )
    {
        GameDataXML::forEachToken( node.getAttributeText( pAttribute ), ",; ", [&]( string_view token )
        {
            outListName.push_back( hashed_string( token ) );
        } );
    }

    float3 AppearanceXMLUtil::readFloat3( const XMLNode& node, const utf8* pAttribute, const float3& fallback )
    {
        return GameDataXML::parseFloat3( node.getAttributeText( pAttribute ), fallback );
    }

    float4 AppearanceXMLUtil::readFloat4( const XMLNode& node, const utf8* pAttribute, const float4& fallback )
    {
        return GameDataXML::parseFloat4( node.getAttributeText( pAttribute ), fallback );
    }

    void AppearanceXMLUtil::readPlacement( const XMLNode& node, const utf8* pSocketAttribute, AppearancePlacement& outPlacement )
    {
        outPlacement._listSocket.clear();
        readNameList( node, pSocketAttribute, outPlacement._listSocket );
        outPlacement._offset   = readFloat3( node, "offset", float3::Zero );
        outPlacement._rotation = readFloat3( node, "rotation", float3::Zero );
    }

    void AppearanceXMLUtil::readTags( const XMLNode& node, const utf8* pAttribute, TagContainer& outTags )
    {
        GameDataXML::forEachToken( node.getAttributeText( pAttribute ), ",; ", [&]( string_view token )
        {
            outTags.addTag( TagID::request( token ) );
        } );
    }

    bool AppearanceXMLUtil::containsName( const vector<hashed_string>& listName, const hashed_string& name )
    {
        for ( const hashed_string& entry : listName )
        {
            if ( entry == name )
                return true;
        }
        return false;
    }
} // namespace sw

#include "pch.h"

#include "GameFramework/Appearance/AppearanceXmlUtil.h"

#include "Core/String/StringUtil.h"

#include "Engine/Object/Component/TagSystem.h"
#include "Engine/Utility/Xml/XmlDocument.h"

#include "GameFramework/Appearance/AppearanceTypes.h"
#include "GameFramework/Data/GameDataXml.h"

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

    bool AppearanceXmlUtil::reportUnknownAttributes( const XmlNode& node, const utf8* const* ppKnownName, uint32 knownCount, AppearanceLoadReport& report, string_view sourceName )
    {
        bool bAllKnown = true;
        for ( XmlAttribute attribute = node.getFirstAttribute(); attribute; attribute = attribute.getNext() )
        {
            bool bKnown = false;
            for ( uint32 index = 0; index < knownCount && bKnown == false; ++index )
            {
                bKnown = StringUtil::equals( attribute.getName(), ppKnownName[index], true );
            }
            if ( bKnown == false )
            {
                report.addError( "%#: <%#> has unknown attribute '%#'", sourceName, node.getName(), attribute.getName() );
                bAllKnown = false;
            }
        }
        return bAllKnown;
    }

    void AppearanceXmlUtil::reportUnknownChild( const XmlNode& parent, const XmlNode& child, AppearanceLoadReport& report, string_view sourceName )
    {
        report.addError( "%#: <%#> has unknown element <%#>", sourceName, parent.getName(), child.getName() );
    }

    hashed_string AppearanceXmlUtil::readName( const XmlNode& node, const utf8* pAttribute )
    {
        const string_view text = StringUtil::trim( node.getAttributeText( pAttribute ) );
        return text.empty() ? hashed_string{} : hashed_string( text );
    }

    void AppearanceXmlUtil::readNameList( const XmlNode& node, const utf8* pAttribute, vector<hashed_string>& outListName )
    {
        GameDataXml::forEachToken( node.getAttributeText( pAttribute ), ",; ", [&]( string_view token )
        {
            outListName.push_back( hashed_string( token ) );
        } );
    }

    float3 AppearanceXmlUtil::readFloat3( const XmlNode& node, const utf8* pAttribute, const float3& fallback )
    {
        return GameDataXml::parseFloat3( node.getAttributeText( pAttribute ), fallback );
    }

    float4 AppearanceXmlUtil::readFloat4( const XmlNode& node, const utf8* pAttribute, const float4& fallback )
    {
        return GameDataXml::parseFloat4( node.getAttributeText( pAttribute ), fallback );
    }

    void AppearanceXmlUtil::readPlacement( const XmlNode& node, const utf8* pSocketAttribute, AppearancePlacement& outPlacement )
    {
        outPlacement._listSocket.clear();
        readNameList( node, pSocketAttribute, outPlacement._listSocket );
        outPlacement._offset   = readFloat3( node, "offset", float3::Zero );
        outPlacement._rotation = readFloat3( node, "rotation", float3::Zero );
    }

    void AppearanceXmlUtil::readTags( const XmlNode& node, const utf8* pAttribute, TagContainer& outTags )
    {
        GameDataXml::forEachToken( node.getAttributeText( pAttribute ), ",; ", [&]( string_view token )
        {
            outTags.addTag( TagID::request( token ) );
        } );
    }

    bool AppearanceXmlUtil::containsName( const vector<hashed_string>& listName, const hashed_string& name )
    {
        for ( const hashed_string& entry : listName )
        {
            if ( entry == name )
                return true;
        }
        return false;
    }
} // namespace sw

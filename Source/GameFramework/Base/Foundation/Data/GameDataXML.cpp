#include "pch.h"

#include "GameFramework/Base/Foundation/Data/GameDataXML.h"

#include "Core/Container/StringUtil.h"

#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLReadUtil.h"

namespace sw
{
    SW_LOG_CALLER( "GameDataXML" );

    namespace
    {
        struct GameDataXMLInternal
        {
            static constexpr const utf8* kNumberSeparators = ", \t;";
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool GameDataXML::loadRoot( XMLDocument& doc, string_view path, const utf8* pRootName, XMLNode& outRoot, string& outSourceName )
    {
        outSourceName = string( path );
        switch ( XMLReadUtil::loadRoot( doc, path, pRootName, outRoot, &outSourceName ) )
        {
            case XMLRootStatus::Found:
            {
                return true;
            }
            case XMLRootStatus::NotLoaded:
            {
                SW_LOG_WARNING( "Failed to read %#", path );
                return false;
            }
            case XMLRootStatus::RootMissing:
            {
                SW_LOG_WARNING( "Missing <%#> root in %#", pRootName, outSourceName );
                return false;
            }
        }
        return false;
    }

    bool GameDataXML::parseRoot( XMLDocument& doc, string_view xmlText, string_view sourceName, const utf8* pRootName, XMLNode& outRoot )
    {
        switch ( XMLReadUtil::parseRoot( doc, xmlText, sourceName, pRootName, outRoot ) )
        {
            case XMLRootStatus::Found:
            {
                return true;
            }
            case XMLRootStatus::NotLoaded:
            {
                SW_LOG_WARNING( "Failed to parse %#", sourceName );
                return false;
            }
            case XMLRootStatus::RootMissing:
            {
                SW_LOG_WARNING( "Missing <%#> root in %#", pRootName, sourceName );
                return false;
            }
        }
        return false;
    }

    bool GameDataXML::loadFileWith( void* pCall, ReadRootFunction pReadRoot, string_view path, const utf8* pRootName )
    {
        XMLDocument doc;
        XMLNode     root;
        string      sourceName;
        return loadRoot( doc, path, pRootName, root, sourceName ) && pReadRoot( pCall, root, sourceName );
    }

    bool GameDataXML::loadTextWith( void* pCall, ReadRootFunction pReadRoot, string_view xmlText, string_view sourceName, const utf8* pRootName )
    {
        XMLDocument doc;
        XMLNode     root;
        return parseRoot( doc, xmlText, sourceName, pRootName, root ) && pReadRoot( pCall, root, sourceName );
    }

    const utf8* GameDataXML::findRequiredID( const XMLNode& node, string_view sourceName )
    {
        const utf8* pID = node.findAttribute( "id" );
        if ( StringUtil::isNullOrEmpty( pID ) )
        {
            SW_LOG_WARNING( "%#: <%#> without an id - skipped", sourceName, node.getName() );
            return nullptr;
        }
        return pID;
    }

    hashed_string GameDataXML::readName( const XMLNode& node, const utf8* pAttribute )
    {
        return XMLReadUtil::readName( node, pAttribute );
    }

    void GameDataXML::parseNameList( string_view text, string_view separators, vector<hashed_string>& outListName )
    {
        outListName.clear();
        appendNameList( text, separators, outListName );
    }

    void GameDataXML::appendNameList( string_view text, string_view separators, vector<hashed_string>& outListName )
    {
        StringUtil::forEachToken( text, separators, [&]( string_view token )
        { outListName.push_back( hashed_string( token ) ); } );
    }

    uint32 GameDataXML::parseFloats( string_view text, float32* pOutValue, uint32 maxCount )
    {
        if ( pOutValue == nullptr )
            return 0;
        uint32 tokenIndex = 0;
        uint32 readCount  = 0;
        StringUtil::forEachToken( text, GameDataXMLInternal::kNumberSeparators, [&]( string_view token )
        {
            if ( tokenIndex >= maxCount )
                return;
            float32 value = 0.0f;
            if ( StringUtil::parseFloat( token, value ) )
            {
                pOutValue[tokenIndex] = value;
                ++readCount;
            }
            ++tokenIndex;
        } );
        return readCount;
    }

    float4 GameDataXML::parseFloat4( string_view text, const float4& fallback )
    {
        float32 arrValue[4] = { fallback._x, fallback._y, fallback._z, fallback._w };
        (void)parseFloats( text, arrValue, 4 ); // 빠진 성분은 기본값 그대로
        return float4{ arrValue[0], arrValue[1], arrValue[2], arrValue[3] };
    }

    float3 GameDataXML::parseFloat3( string_view text, const float3& fallback )
    {
        float32 arrValue[3] = { fallback._x, fallback._y, fallback._z };
        (void)parseFloats( text, arrValue, 3 ); // 못 읽은 칸은 대체값이 남는다
        return float3{ arrValue[0], arrValue[1], arrValue[2] };
    }

    bool GameDataXML::parseNumber( string_view token, int32& outValue ) { return StringUtil::parseInt( token, outValue ); }

    bool GameDataXML::parseNumber( string_view token, float32& outValue ) { return StringUtil::parseFloat( token, outValue ); }
} // namespace sw

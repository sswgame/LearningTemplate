#include "pch.h"

#include "GameFramework/Base/Data/GameDataXml.h"

#include "Core/String/StringUtil.h"

#include "Engine/Utility/Xml/XmlDocument.h"

namespace sw
{
    SW_LOG_CALLER( "GameDataXml" );

    namespace
    {
        struct GameDataXmlInternal
        {
            static constexpr const utf8* kNumberSeparators = ", \t;";
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool GameDataXml::loadRoot( XmlDocument& doc, string_view path, const utf8* pRootName, XmlNode& outRoot, string& outSourceName )
    {
        outSourceName = string( path );
        if ( doc.loadPath( path, &outSourceName ) == false )
        {
            SW_LOG_WARNING( "Failed to read %#", path );
            return false;
        }
        outRoot = doc.getRoot( pRootName );
        if ( outRoot.isValid() == false )
        {
            SW_LOG_WARNING( "Missing <%#> root in %#", pRootName, outSourceName );
            return false;
        }
        return true;
    }

    bool GameDataXml::parseRoot( XmlDocument& doc, string_view xmlText, string_view sourceName, const utf8* pRootName, XmlNode& outRoot )
    {
        if ( doc.parse( xmlText, sourceName ) == false )
        {
            SW_LOG_WARNING( "Failed to parse %#", sourceName );
            return false;
        }
        outRoot = doc.getRoot( pRootName );
        if ( outRoot.isValid() == false )
        {
            SW_LOG_WARNING( "Missing <%#> root in %#", pRootName, sourceName );
            return false;
        }
        return true;
    }

    const utf8* GameDataXml::findRequiredId( const XmlNode& node, string_view sourceName )
    {
        const utf8* pId = node.findAttribute( "id" );
        if ( StringUtil::isNullOrEmpty( pId ) )
        {
            SW_LOG_WARNING( "%#: <%#> without an id - skipped", sourceName, node.getName() );
            return nullptr;
        }
        return pId;
    }

    uint32 GameDataXml::parseFloats( string_view text, float32* pOutValue, uint32 maxCount )
    {
        if ( pOutValue == nullptr )
            return 0;
        uint32 tokenIndex = 0;
        uint32 readCount  = 0;
        forEachToken( text, GameDataXmlInternal::kNumberSeparators, [&]( string_view token )
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

    float4 GameDataXml::parseFloat4( string_view text, const float4& fallback )
    {
        float32 arrValue[4] = { fallback._x, fallback._y, fallback._z, fallback._w };
        (void)parseFloats( text, arrValue, 4 ); // 빠진 성분은 기본값 그대로
        return float4{ arrValue[0], arrValue[1], arrValue[2], arrValue[3] };
    }

    float3 GameDataXml::parseFloat3( string_view text, const float3& fallback )
    {
        float32 arrValue[3] = { fallback._x, fallback._y, fallback._z };
        (void)parseFloats( text, arrValue, 3 ); // 못 읽은 칸은 대체값이 남는다
        return float3{ arrValue[0], arrValue[1], arrValue[2] };
    }
} // namespace sw

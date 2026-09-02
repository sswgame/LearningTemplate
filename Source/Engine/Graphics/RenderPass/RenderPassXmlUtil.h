/**
 * @file RenderPassXmlUtil.h
 * @brief Shared XML string-list & attachment helpers for RenderPass / RenderPipeline resources.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"
#include "Core/String/string_splitter.h"

#include "Engine/Graphics/RenderPass/RenderPassResource.h"
#include "Engine/Utility/Xml/XmlDocument.h"

namespace sw
{
    /** @brief RenderPass 및 RenderPipeline XML 리소스 공통 직렬화/역직렬화 유틸 */
    struct RenderPassXmlUtil
    {
        /** @brief XML 자식 목록을 문자열 벡터로 읽습니다. */
        static void parseStringList( XmlNode parent, const utf8* pListName, vector<string>& outList )
        {
            outList.clear();
            if ( parent.isValid() == false )
                return;
            XmlNode list = parent.child( pListName );
            if ( list.isValid() == false )
                return;
            for ( XmlNode item = list.child( "item" ); item.isValid(); item = item.next( "item" ) )
            {
                if ( item.text() != nullptr && item.text()[0] != '\0' )
                    outList.emplace_back( item.text() );
                else
                {
                    const utf8* pAttr = item.attr( "name" );
                    if ( pAttr != nullptr )
                        outList.emplace_back( pAttr );
                }
            }
        }

        /** @brief 문자열 목록을 `<listName><item>…</item></listName>`로 씁니다. */
        static void appendStringList( XmlNode parent, const utf8* pListName, const vector<string>& listValue )
        {
            XmlNode list = parent.appendChild( pListName );
            for ( const string& valueStr : listValue )
                list.appendChild( "item", valueStr );
        }

        /** @brief `<_attachments>` XML 노드에서 RenderPassAttachment 목록을 파싱합니다. */
        static void parseAttachmentList( XmlNode attachsNode, vector<RenderPassAttachment>& outListAttachment )
        {
            outListAttachment.clear();
            if ( attachsNode.isValid() == false )
                return;

            for ( XmlNode attNode = attachsNode.child( "item" ); attNode.isValid(); attNode = attNode.next( "item" ) )
            {
                RenderPassAttachment att{};

                const utf8* pAttName = attNode.childText( "_name" );
                if ( pAttName != nullptr )
                    att._name = pAttName;
                const utf8* pAttFormat = attNode.childText( "_format" );
                if ( pAttFormat != nullptr )
                    att._format = pAttFormat;
                att._bClear                = attNode.childBool( "_bClear", false );
                const utf8* pAttClearColor = attNode.childText( "_clearColor" );
                if ( pAttClearColor != nullptr && pAttClearColor[0] != '\0' )
                {
                    string_splitter tokens( pAttClearColor, { ",", " " } );
                    const auto&     listToken = tokens.getSplitList();
                    float32         arrVal[4] = { att._clearColor._x, att._clearColor._y, att._clearColor._z, att._clearColor._w };
                    for ( size_t index = 0; index < listToken.size() && index < 4; ++index )
                    {
                        if ( listToken[index].empty() == false )
                            StringUtil::parseFloat( listToken[index], arrVal[index] );
                    }
                    att._clearColor = float4( arrVal[0], arrVal[1], arrVal[2], arrVal[3] );
                }

                outListAttachment.push_back( std::move( att ) );
            }
        }

        /** @brief RenderPassAttachment 목록을 `<_attachments>` XML 노드로 씁니다. */
        static void appendAttachmentList( XmlNode parentNode, const vector<RenderPassAttachment>& listAttachment )
        {
            XmlNode attachsNode = parentNode.appendChild( "_attachments" );
            for ( const RenderPassAttachment& att : listAttachment )
            {
                XmlNode attNode = attachsNode.appendChild( "item" );
                attNode.appendChild( "_name", att._name );
                attNode.appendChild( "_format", att._format );
                attNode.appendChild( "_bClear", att._bClear );

                StringBuilder<constant::kMaxBuffer128> colorSS;
                constexpr Format                       colorFmt( 4 );
                colorSS.appendFormat( "%#,%#,%#,%#",
                                      Fmt( att._clearColor._x, colorFmt ),
                                      Fmt( att._clearColor._y, colorFmt ),
                                      Fmt( att._clearColor._z, colorFmt ),
                                      Fmt( att._clearColor._w, colorFmt ) );
                attNode.appendChild( "_clearColor", colorSS.view() );
            }
        }
    };
} // namespace sw

#include "pch.h"

#include "Engine/Serialization/XML/XMLReadUtil.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/Log/Logger.h"

#include "Engine/Serialization/XML/XMLDocument.h"

namespace sw
{
    SW_LOG_CALLER( "XMLReadUtil" );

    XMLRootStatus XMLReadUtil::loadRoot( XMLDocument& outDocument, string_view path, const utf8* pRootName, XMLNode& outRoot, string* pOutAbsPath )
    {
        if ( outDocument.loadResource( path, pOutAbsPath ) == false )
            return XMLRootStatus::NotLoaded;
        outRoot = outDocument.getRoot( pRootName );
        return outRoot.isValid() ? XMLRootStatus::Found : XMLRootStatus::RootMissing;
    }

    XMLRootStatus XMLReadUtil::parseRoot( XMLDocument& outDocument, string_view xmlText, string_view sourceName, const utf8* pRootName,
                                          XMLNode& outRoot )
    {
        if ( outDocument.parse( xmlText, sourceName ) == false )
            return XMLRootStatus::NotLoaded;
        outRoot = outDocument.getRoot( pRootName );
        return outRoot.isValid() ? XMLRootStatus::Found : XMLRootStatus::RootMissing;
    }

    hashed_string XMLReadUtil::readOptionalName( const XMLNode& node, const utf8* pAttribute )
    {
        const string_view text = StringUtil::trim( node.getAttributeText( pAttribute ) );
        if ( text.empty() )
            return hashed_string{};
        const hashed_string name( text );
        // 빈 이름의 철자(`None`, 대소문자 무시)는 이름 값이 될 수 없다 — 담으면 "적지 않음"과 같아진다(언리얼 `NAME_None` 과 같은 함정).
        // 그 글을 값으로 쓰는 속성(열거형 `align="none"`)은 글 그대로 읽어야 하므로, 이름으로 읽은 것을 오류로 알린다.
        if ( name.empty() )
            SW_LOG_ERROR( "<%# %#=\"%#\">: '%#' spells the empty name, so a name cannot hold it - read this attribute as text",
                          node.getName() != nullptr ? node.getName() : "?", pAttribute, text, text );
        return name;
    }

    bool XMLReadUtil::hasOnlyKnownNames( const XMLNode& node, std::initializer_list<string_view> listAttribute, std::initializer_list<string_view> listChild, string_view context )
    {
        bool bKnown = true;
        for ( XMLAttribute attribute = node.getFirstAttribute(); attribute.isValid(); attribute = attribute.getNext() )
        {
            if ( VectorUtil::contains( listAttribute, string_view{ attribute.getName() } ) )
                continue;
            SW_LOG_ERROR( "%#: unknown attribute '%#'", context, attribute.getName() );
            bKnown = false;
        }
        for ( XMLNode child = node.findChild(); child.isValid(); child = child.findNextSibling() )
        {
            if ( VectorUtil::contains( listChild, string_view{ child.getName() } ) )
                continue;
            SW_LOG_ERROR( "%#: unknown element '%#'", context, child.getName() );
            bKnown = false;
        }
        return bKnown;
    }

    bool XMLReadUtil::readFloatList( string_view text, float32* pOutValue, uint32 count )
    {
        return StringUtil::parseExactFloats( text, ", \t\r\n", pOutValue, count );
    }

    string XMLReadUtil::makeFloatListText( const float32* pValue, uint32 count )
    {
        string text;
        for ( uint32 index = 0; index < count; ++index )
        {
            if ( index > 0 )
                text.push_back( ',' );
            text.append( to_string( pValue[index] ) );
        }
        return text;
    }
} // namespace sw

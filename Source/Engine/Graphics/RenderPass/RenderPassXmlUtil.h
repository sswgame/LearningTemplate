/**
 * @file RenderPassXmlUtil.h
 * @brief Shared XML string-list helpers for RenderPass / RenderPipeline resources.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Utility/Xml/XmlDocument.h"

namespace sw
{
	/** @brief XML 자식 목록을 문자열 벡터로 읽습니다. */
	SW_INLINE void parseStringList( XmlNode parent, const utf8* pListName, vector<string>& out )
	{
		out.clear();
		if ( parent.isValid() == false )
			return;
		XmlNode list = parent.child( pListName );
		if ( list.isValid() == false )
			return;
		for ( XmlNode item = list.child( "item" ); item.isValid(); item = item.next( "item" ) )
		{
			if ( item.text() != nullptr && item.text()[0] != '\0' )
				out.emplace_back( item.text() );
			else
			{
				const utf8* pAttr = item.attr( "name" );
				if ( pAttr != nullptr )
					out.emplace_back( pAttr );
			}
		}
	}

	/** @brief 문자열 목록을 `<listName><item>…</item></listName>`로 씁니다. */
	SW_INLINE void appendStringList( XmlNode parent, const utf8* pListName, const vector<string>& values )
	{
		XmlNode list = parent.appendChild( pListName );
		for ( const string& valueStr : values )
			list.appendChild( "item", valueStr );
	}
} // namespace sw

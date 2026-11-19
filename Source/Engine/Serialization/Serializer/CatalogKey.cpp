#include "pch.h"

#include "Engine/Serialization/Serializer/CatalogKey.h"

#include "Core/String/StringUtil.h"

#include "Engine/Serialization/Serializer/SerializeContext.h"

namespace sw
{
    void CatalogKeyBase::assignText( string_view text )
    {
        string_view id = StringUtil::trim( text );
        if ( id.size() >= 2 && id.front() == '"' && id.back() == '"' )
            id = id.substr( 1, id.size() - 2 );
        _key = id.empty() ? hashed_string{} : hashed_string( id.data(), static_cast<uint32>( id.size() ) );
    }

    void CatalogKeyBase::registerSerializeHandler( SerializeContext& context )
    {
        // 있는지는 여기서 보지 않는다 — 글 처리기는 칸 주소만 받아 어느 카탈로그 한 벌인지 모른다. 대조는 쿠커(표)와 쓰는 쪽의 형식 있는 조회가 한다.
        context.registerTextHandler(
            hashed_string( kTypeName ),
            []( const void* pPtr )
        { return static_cast<const CatalogKeyBase*>( pPtr )->toText(); },
            []( void* pPtr, string_view strView )
        {
            static_cast<CatalogKeyBase*>( pPtr )->assignText( strView );
            return true;
        } );
    }
} // namespace sw

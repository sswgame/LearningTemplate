#include "pch.h"

#include "Engine/Serialization/Serializer/DataPath.h"

#include "Core/Log/Logger.h"

#include "Engine/Content/AssetRegistry.h"
#include "Engine/Serialization/Serializer/SerializeContext.h"

namespace sw
{
    void DataPath::registerSerializeHandler( SerializeContext& context )
    {
        // 쓰기는 색인의 정체성이다(에셋이 아닌 경로면 경로 글). 빈 글은 칸을 비운다 — 필수 칸이면 그 구조체의 읽기가 오류로 알린다.
        context.registerTextHandler(
            hashed_string( "DataPath" ),
            []( const void* pPtr )
        {
            const string& path = static_cast<const DataPath*>( pPtr )->_path;
            return AssetRegistry::makeReferenceText( path, AssetRegistry::resolveGUID( path ) );
        },
            []( void* pPtr, string_view strView )
        {
            DataPath&                      value  = *static_cast<DataPath*>( pPtr );
            expected<AssetReferenceTarget> target = AssetRegistry::readReferenceText( strView );
            if ( target.has_value() == false )
            {
                SW_LOG_ERROR_ID( "DataPath.ReferenceNotResolved", "'%#' is %# - the field keeps '%#'", target.error().detail(), target.error().message(), value._path );
                return false;
            }
            value._path = std::move( target->_path );
            return true;
        } );
    }
} // namespace sw

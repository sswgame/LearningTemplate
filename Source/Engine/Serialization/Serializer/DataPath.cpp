#include "pch.h"

#include "Engine/Serialization/Serializer/DataPath.h"

#include "Core/Container/StringUtil.h"

#include "Engine/Serialization/Serializer/SerializeContext.h"

namespace sw
{
    void DataPath::registerSerializeHandler( SerializeContext& context )
    {
        // 쓰기는 경로 그대로, 읽기는 앞뒤 공백을 뺀 경로다. 빈 글은 칸을 비운다 — 필수 칸이면 그 구조체의 읽기가 오류로 알린다.
        context.registerTextHandler(
            hashed_string( "DataPath" ),
            []( const void* pPtr )
        { return static_cast<const DataPath*>( pPtr )->_path; },
            []( void* pPtr, string_view strView )
        {
            static_cast<DataPath*>( pPtr )->_path = string( StringUtil::trim( strView ) );
            return true;
        } );
    }
} // namespace sw

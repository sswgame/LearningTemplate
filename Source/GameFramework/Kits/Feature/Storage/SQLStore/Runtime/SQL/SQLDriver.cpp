#include "pch.h"

#include "GameFramework/Kits/Feature/Storage/SQLStore/Runtime/SQL/SQLDriver.h"

#include "Core/Container/StringUtil.h"

namespace sw
{
    SQLValue SQLValue::makeInt64( int64 value )
    {
        SQLValue sqlValue;
        sqlValue._integer = value;
        sqlValue._type    = SQLValueType::Int64;
        return sqlValue;
    }

    SQLValue SQLValue::makeText( string_view text )
    {
        SQLValue sqlValue;
        StringUtil::assignBytes( sqlValue._bytes, text );
        sqlValue._type = SQLValueType::Text;
        return sqlValue;
    }

    SQLValue SQLValue::makeBlob( const uint8* pData, int32 size )
    {
        SQLValue sqlValue;
        if ( pData != nullptr && size > 0 )
            sqlValue._bytes.assign( pData, pData + size );
        sqlValue._type = SQLValueType::Blob;
        return sqlValue;
    }
} // namespace sw

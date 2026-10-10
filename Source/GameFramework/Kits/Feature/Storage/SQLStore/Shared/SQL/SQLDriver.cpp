#include "pch.h"

#include "GameFramework/Kits/Feature/Storage/SQLStore/Shared/SQL/SQLDriver.h"

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
        sqlValue._bytes.assign( reinterpret_cast<const uint8*>( text.data() ), reinterpret_cast<const uint8*>( text.data() ) + text.size() );
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

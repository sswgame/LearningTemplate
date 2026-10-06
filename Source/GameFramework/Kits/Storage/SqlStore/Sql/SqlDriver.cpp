#include "pch.h"

#include "GameFramework/Kits/Storage/SqlStore/Sql/SqlDriver.h"

namespace sw
{
    SqlValue SqlValue::makeInt64( int64 value )
    {
        SqlValue sqlValue;
        sqlValue._integer = value;
        sqlValue._type    = SqlValueType::Int64;
        return sqlValue;
    }

    SqlValue SqlValue::makeText( string_view text )
    {
        SqlValue sqlValue;
        sqlValue._bytes.assign( reinterpret_cast<const uint8*>( text.data() ), reinterpret_cast<const uint8*>( text.data() ) + text.size() );
        sqlValue._type = SqlValueType::Text;
        return sqlValue;
    }

    SqlValue SqlValue::makeBlob( const uint8* pData, int32 size )
    {
        SqlValue sqlValue;
        if ( pData != nullptr && size > 0 )
            sqlValue._bytes.assign( pData, pData + size );
        sqlValue._type = SqlValueType::Blob;
        return sqlValue;
    }
} // namespace sw

#include "pch.h"

#include "GameFramework/Kits/Feature/Storage/SQLStore/Server/Driver/Postgres/PostgresCodec.h"

#include "Core/String/StringBuilder.h"

namespace sw
{
    string PostgresCodec::convertPlaceholders( string_view sql, int32& outParamCount )
    {
        string text;
        text.reserve( sql.size() + 8 );
        outParamCount    = 0;
        utf8   quoteChar = '\0';
        size_t index     = 0;
        while ( index < sql.size() )
        {
            const utf8 ch   = sql[index];
            const utf8 next = index + 1 < sql.size() ? sql[index + 1] : '\0';
            if ( quoteChar != '\0' )
            {
                if ( ch == quoteChar )
                    quoteChar = '\0';
                text.push_back( ch );
                ++index;
                continue;
            }
            if ( ch == '\'' || ch == '"' )
            {
                quoteChar = ch;
                text.push_back( ch );
                ++index;
                continue;
            }
            if ( ch == '-' && next == '-' )
            {
                const size_t lineEnd = sql.find( '\n', index );
                const size_t end     = lineEnd == string_view::npos ? sql.size() : lineEnd;
                text.append( sql.substr( index, end - index ) );
                index = end;
                continue;
            }
            if ( ch == '/' && next == '*' )
            {
                size_t end   = index + 2;
                int32  depth = 1;
                while ( end < sql.size() && depth > 0 )
                {
                    const string_view pair = sql.substr( end, 2 );
                    if ( pair == "/*" || pair == "*/" )
                    {
                        depth += pair == "/*" ? 1 : -1;
                        end += 2;
                        continue;
                    }
                    ++end;
                }
                text.append( sql.substr( index, end - index ) );
                index = end;
                continue;
            }
            ++index;
            if ( ch != '?' )
            {
                text.push_back( ch );
                continue;
            }
            ++outParamCount;
            StringBuilder<constant::buffer::kMax16> placeholder;
            placeholder.appendFormat( "$%#", outParamCount );
            text.append( placeholder.c_str() );
        }
        return text;
    }

    expected<void> PostgresCodec::encodeParameters( const SQLValue* pParam, int32 paramCount, int32 placeholderCount, PostgresParameters& outParameters )
    {
        if ( paramCount != placeholderCount ) // 예전에는 모자란 칸을 NULL 로 채우고 남는 값을 버렸다
            return unexpected( Error{ ErrorKind::InvalidArgument, "the statement and the parameter list differ in length" } );
        const size_t count = static_cast<size_t>( placeholderCount );
        outParameters._listText.assign( count, string{} ); // 크기를 먼저 정한다 — `_listValue` 가 원소를 가리킨다
        outParameters._listValue.assign( count, nullptr );
        outParameters._listLength.assign( count, 0 );
        outParameters._listFormat.assign( count, kFormatText );
        for ( size_t index = 0; index < count; ++index )
        {
            const SQLValue& value = pParam[index];
            switch ( value._type )
            {
                case SQLValueType::Null:
                {
                    break;
                }
                case SQLValueType::Int64:
                {
                    StringBuilder<constant::buffer::kMax32> number;
                    number.append( value._integer );
                    outParameters._listText[index]  = string( number.view() );
                    outParameters._listValue[index] = outParameters._listText[index].c_str();
                    break;
                }
                case SQLValueType::Text:
                {
                    if ( value.getText().find( '\0' ) != string_view::npos )
                        return unexpected( Error{ ErrorKind::InvalidArgument, "a text parameter holds a NUL byte" } );
                    outParameters._listText[index]  = string( value.getText() );
                    outParameters._listValue[index] = outParameters._listText[index].c_str();
                    break;
                }
                case SQLValueType::Blob:
                {
                    outParameters._listValue[index]  = value._bytes.empty() ? "" : reinterpret_cast<const utf8*>( value._bytes.data() ); // 빈 바이트도 NULL 이 아니다
                    outParameters._listLength[index] = static_cast<int32>( value._bytes.size() );
                    outParameters._listFormat[index] = kFormatBinary;
                    break;
                }
            }
        }
        return {};
    }

    expected<void> PostgresCodec::decodeColumn( uint32 typeOid, const uint8* pData, int32 size, SQLValue& outValue )
    {
        int32 integerSize = 0;
        if ( typeOid == kBoolOid )
            integerSize = 1;
        else if ( typeOid == kInt2Oid )
            integerSize = 2;
        else if ( typeOid == kInt4Oid )
            integerSize = 4;
        else if ( typeOid == kInt8Oid )
            integerSize = 8;
        if ( integerSize != 0 )
        {
            if ( size != integerSize )
            {
                StringBuilder<constant::buffer::kMax128> errorBuilder;
                errorBuilder.appendFormat( "%#, %#", typeOid, size );
                return unexpected( Error{ ErrorKind::InvalidArgument, "an integer column holds a byte count its type does not have (type oid, bytes)", errorBuilder.view() } );
            }
            uint64 raw = 0;
            for ( int32 byteIndex = 0; byteIndex < size; ++byteIndex )
            {
                raw = ( raw << 8 ) | pData[byteIndex];
            }
            const int32 shift = 64 - size * 8; // 부호는 크기만큼 확장한다
            outValue._type    = SQLValueType::Int64;
            outValue._integer = shift > 0 ? static_cast<int64>( raw << shift ) >> shift : static_cast<int64>( raw );
            return {};
        }
        const bool bText = typeOid == kTextOid || typeOid == kVarcharOid;
        if ( bText == false && typeOid != kByteaOid )
        {
            StringBuilder<constant::buffer::kMax128> errorBuilder;
            errorBuilder.appendFormat( "%#", typeOid );
            return unexpected( Error{ ErrorKind::InvalidArgument, "the column type (oid) has no SQLValue form", errorBuilder.view() } );
        }
        outValue._type = bText ? SQLValueType::Text : SQLValueType::Blob;
        if ( size > 0 )
            outValue._bytes.assign( pData, pData + size );
        return {};
    }

    SQLResult PostgresCodec::classifySQLState( string_view state )
    {
        if ( state == "23505" )
            return SQLResult::Constraint;
        if ( state == "40001" || state == "40P01" )
            return SQLResult::SerializationFailure;
        const bool bConnectionClass = state.size() == 5 && state[0] == '0' && state[1] == '8';
        // 57P01 관리자 종료 · 57P02 크래시 종료 · 57P03 아직 받지 못함 - 다시 열어야 하는 끊김이다(예전에는 Error 라 풀이 다시 열 기회가 다음 끊김 판정까지 밀렸다).
        const bool bServerGoingDown = state == "57P01" || state == "57P02" || state == "57P03";
        if ( bConnectionClass || bServerGoingDown )
            return SQLResult::ConnectionLost;
        if ( state == "55P03" )
            return SQLResult::Busy; // lock_not_available
        return SQLResult::Error;
    }
} // namespace sw

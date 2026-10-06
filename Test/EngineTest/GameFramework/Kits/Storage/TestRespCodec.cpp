#include "pch.h"

#include "GameFramework/Kits/Storage/Server/CacheStore/Driver/Resp/RespCodec.h"

#include "TestFramework/TestFramework.h"

#include <algorithm>
#include <cstring>

// RESP2 인코더 · 증분 파서 — 명령 바이트 고정, 한 바이트씩 와도 같은 값, 상한 · 깨진 프레임은 오류, 서버 점수 글을 정수로.

using namespace sw;

namespace
{
    vector<uint8> makeBytes( const utf8* pText, size_t size ) { return vector<uint8>( reinterpret_cast<const uint8*>( pText ), reinterpret_cast<const uint8*>( pText ) + size ); }

    vector<uint8> makeBytes( const utf8* pText ) { return makeBytes( pText, std::strlen( pText ) ); }

    /** @brief @p bytes 를 @p chunkSize 조각으로 넣으며 값을 모두 꺼냅니다. 오류면 false. */
    bool parseAll( const vector<uint8>& bytes, size_t chunkSize, vector<RespValue>& outListValue )
    {
        RespParser parser;
        for ( size_t offset = 0; offset < bytes.size(); offset += chunkSize )
        {
            parser.append( bytes.data() + offset, std::min( chunkSize, bytes.size() - offset ) );
            RespValue value;
            for ( ;; )
            {
                const RespParseResult result = parser.next( value );
                if ( result == RespParseResult::Error )
                    return false;
                if ( result == RespParseResult::NeedMore )
                    break;
                outListValue.push_back( value );
            }
        }
        return true;
    }

    bool isSameValue( const RespValue& left, const RespValue& right )
    {
        if ( left._type != right._type || left._integer != right._integer || left._bytes != right._bytes || left._listElement.size() != right._listElement.size() )
            return false;
        for ( size_t index = 0; index < left._listElement.size(); ++index )
        {
            if ( isSameValue( left._listElement[index], right._listElement[index] ) == false )
                return false;
        }
        return true;
    }

    RespParseResult parseOne( const utf8* pText )
    {
        RespParser          parser;
        const vector<uint8> bytes = makeBytes( pText );
        parser.append( bytes.data(), bytes.size() );
        RespValue value;
        return parser.next( value );
    }
} // namespace

SW_TEST_CASE( RespCodecTest, EncodesACommandAsABulkStringArray )
{
    vector<uint8> bytes;
    const uint8   arrBinary[2] = { 0x00, 0xFF };
    RespCommand{ "SET" }.addText( "k" ).addBytes( arrBinary, 2 ).addInteger( -15 ).appendTo( bytes );
    const utf8 arrExpected[] = "*4\r\n$3\r\nSET\r\n$1\r\nk\r\n$2\r\n\x00\xFF\r\n$3\r\n-15\r\n";
    SW_EXPECT_TRUE( bytes == makeBytes( arrExpected, sizeof( arrExpected ) - 1 ) );

    vector<uint8> emptyArgument;
    RespCommand{ "GET" }.addText( "" ).appendTo( emptyArgument );
    SW_EXPECT_TRUE( emptyArgument == makeBytes( "*2\r\n$3\r\nGET\r\n$0\r\n\r\n" ) );
}

SW_TEST_CASE( RespCodecTest, ParsesTheSameValuesWhenFedOneByteAtATime )
{
    const vector<uint8> stream = makeBytes( "+OK\r\n-ERR bad thing\r\n:-42\r\n$5\r\nhe\r\no\r\n$-1\r\n*-1\r\n*3\r\n*1\r\n:1\r\n$0\r\n\r\n+QUEUED\r\n*0\r\n" );
    vector<RespValue>   listWhole;
    vector<RespValue>   listByte;
    SW_ASSERT_TRUE( parseAll( stream, stream.size(), listWhole ) );
    SW_ASSERT_TRUE( parseAll( stream, 1, listByte ) );
    SW_ASSERT_EQUAL( size_t( 8 ), listWhole.size() );
    SW_ASSERT_EQUAL( listWhole.size(), listByte.size() );
    for ( size_t index = 0; index < listWhole.size(); ++index )
        SW_EXPECT_TRUE( isSameValue( listWhole[index], listByte[index] ) );

    SW_EXPECT_TRUE( listWhole[0].isText( "OK" ) );
    SW_EXPECT_TRUE( listWhole[1].isError() && listWhole[1].getText() == "ERR bad thing" );
    SW_EXPECT_TRUE( listWhole[2]._type == RespType::Integer && listWhole[2]._integer == -42 );
    SW_EXPECT_TRUE( listWhole[3]._type == RespType::BulkString && listWhole[3].getText() == "he\r\no" ); // 몸 안의 CRLF 는 글이다
    SW_EXPECT_TRUE( listWhole[4].isNull() );
    SW_EXPECT_TRUE( listWhole[5].isNull() );
    SW_ASSERT_TRUE( listWhole[6]._type == RespType::Array && listWhole[6]._listElement.size() == 3 );
    SW_EXPECT_TRUE( listWhole[6]._listElement[0]._type == RespType::Array && listWhole[6]._listElement[0]._listElement[0]._integer == 1 );
    SW_EXPECT_TRUE( listWhole[6]._listElement[1]._type == RespType::BulkString && listWhole[6]._listElement[1]._bytes.empty() );
    SW_EXPECT_TRUE( listWhole[6]._listElement[2].isText( "QUEUED" ) );
    SW_EXPECT_TRUE( listWhole[7]._type == RespType::Array && listWhole[7]._listElement.empty() );
}

SW_TEST_CASE( RespCodecTest, MalformedOrOversizedFramesAreErrors )
{
    SW_EXPECT_TRUE( parseOne( "$2000000\r\n" ) == RespParseResult::Error ); // 벌크 상한(1 MiB) 위 — 몸을 기다리지 않는다
    SW_EXPECT_TRUE( parseOne( "*2000000\r\n" ) == RespParseResult::Error );
    SW_EXPECT_TRUE( parseOne( "?what\r\n" ) == RespParseResult::Error );
    SW_EXPECT_TRUE( parseOne( "+OK\rX" ) == RespParseResult::Error );
    SW_EXPECT_TRUE( parseOne( ":12a\r\n" ) == RespParseResult::Error );
    SW_EXPECT_TRUE( parseOne( "$3\r\nabcd\r\n" ) == RespParseResult::Error ); // 몸 뒤가 CRLF 가 아니다
    SW_EXPECT_TRUE( parseOne( "$3\r\nab" ) == RespParseResult::NeedMore );
    SW_EXPECT_TRUE( parseOne( "*9\r\n*9\r\n*9\r\n*9\r\n*9\r\n*9\r\n*9\r\n*9\r\n*9\r\n*9\r\n" ) == RespParseResult::Error ); // 중첩 8 넘음

    RespParser          parser;
    const vector<uint8> broken = makeBytes( "?\r\n+OK\r\n" );
    parser.append( broken.data(), broken.size() );
    RespValue value;
    SW_EXPECT_TRUE( parser.next( value ) == RespParseResult::Error );
    SW_EXPECT_TRUE( parser.next( value ) == RespParseResult::Error ); // 한 번 깨지면 다시 맞추지 않는다(연결을 닫는다)
    parser.reset();
    const vector<uint8> fresh = makeBytes( "+OK\r\n" );
    parser.append( fresh.data(), fresh.size() );
    SW_EXPECT_TRUE( parser.next( value ) == RespParseResult::Complete );
}

SW_TEST_CASE( RespCodecTest, ReadsServerScoreTextAsAnExactInteger )
{
    RespValue value;
    value._type  = RespType::BulkString;
    int64 score  = 0;
    value._bytes = makeBytes( "20" );
    SW_EXPECT_TRUE( value.tryReadIntegerText( score ) && score == 20 );
    value._bytes = makeBytes( "-3" );
    SW_EXPECT_TRUE( value.tryReadIntegerText( score ) && score == -3 );
    value._bytes = makeBytes( "4.5e+15" );
    SW_EXPECT_TRUE( value.tryReadIntegerText( score ) && score == 4500000000000000ll );
    value._bytes = makeBytes( "9007199254740992" );
    SW_EXPECT_TRUE( value.tryReadIntegerText( score ) && score == ( 1ll << 53 ) );
    value._bytes = makeBytes( "1e+16" ); // 정확한 범위(±2^53) 밖
    SW_EXPECT_FALSE( value.tryReadIntegerText( score ) );
    value._bytes = makeBytes( "1.5" );
    SW_EXPECT_FALSE( value.tryReadIntegerText( score ) );
    value._bytes = makeBytes( "inf" );
    SW_EXPECT_FALSE( value.tryReadIntegerText( score ) );
}

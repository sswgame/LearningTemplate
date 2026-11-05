#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "GameFramework/Kits/Feature/Storage/CacheStore/Server/Driver/Resp/RespCodec.h"

using namespace sw;

namespace
{
    struct RespCodecTestUtil
    {
        static void append( RespParser& parser, string_view text ) { parser.append( reinterpret_cast<const uint8*>( text.data() ), text.size() ); }

        /** @brief 글 전체를 한 번에 넣고 값 하나를 읽습니다. */
        static RespParseResult parseOne( string_view text, RespValue& outValue )
        {
            RespParser parser;
            append( parser, text );
            return parser.next( outValue );
        }

        static RespValue makeBulk( string_view text )
        {
            RespValue value;
            value._type = RespType::BulkString;
            value._bytes.assign( text.begin(), text.end() );
            return value;
        }
    };
    using Util = RespCodecTestUtil;
} // namespace

// 명령은 늘 벌크 문자열 배열이다. 바이너리 값도 길이로 실린다.
SW_TEST_CASE( RespCodecTest, CommandIsAnArrayOfBulkStrings )
{
    RespCommand         command{ "SET" };
    const vector<uint8> value{ 0, '\r', '\n' };
    command.addText( "k" ).addBytes( value ).addInteger( -15 );
    SW_EXPECT_EQUAL( command.getArgumentCount(), 4 );

    vector<uint8> bytes{ 'x' };
    command.appendTo( bytes );
    const string expected( "x*4\r\n$3\r\nSET\r\n$1\r\nk\r\n$3\r\n\0\r\n\r\n$3\r\n-15\r\n", 39 );
    SW_EXPECT_EQUAL( string( reinterpret_cast<const utf8*>( bytes.data() ), bytes.size() ), expected );
}

// 다섯 종류 값과 널 두 모양, 중첩 배열을 읽는다.
SW_TEST_CASE( RespCodecTest, ParsesEveryValueKind )
{
    RespValue value;
    SW_REQUIRE_TRUE( Util::parseOne( "+OK\r\n", value ) == RespParseResult::Complete );
    SW_EXPECT_TRUE( value._type == RespType::SimpleString );
    SW_EXPECT_TRUE( value.isText( "OK" ) );

    SW_REQUIRE_TRUE( Util::parseOne( "-ERR bad\r\n", value ) == RespParseResult::Complete );
    SW_EXPECT_TRUE( value.isError() );
    SW_EXPECT_FALSE( value.isText( "ERR bad" ) ); // 오류는 "답이 그 글이다" 로 치지 않는다
    SW_EXPECT_EQUAL( string( value.getText() ), string( "ERR bad" ) );

    SW_REQUIRE_TRUE( Util::parseOne( ":-42\r\n", value ) == RespParseResult::Complete );
    SW_EXPECT_EQUAL( value._integer, int64{ -42 } );

    SW_REQUIRE_TRUE( Util::parseOne( "$0\r\n\r\n", value ) == RespParseResult::Complete );
    SW_EXPECT_TRUE( value._type == RespType::BulkString );
    SW_EXPECT_TRUE( value._bytes.empty() );

    SW_REQUIRE_TRUE( Util::parseOne( "$-1\r\n", value ) == RespParseResult::Complete );
    SW_EXPECT_TRUE( value.isNull() );
    SW_REQUIRE_TRUE( Util::parseOne( "*-1\r\n", value ) == RespParseResult::Complete );
    SW_EXPECT_TRUE( value.isNull() );

    SW_REQUIRE_TRUE( Util::parseOne( "*2\r\n*1\r\n:1\r\n$2\r\nhi\r\n", value ) == RespParseResult::Complete );
    SW_REQUIRE_EQUAL( value._listElement.size(), size_t{ 2 } );
    SW_EXPECT_EQUAL( value._listElement[0]._listElement[0]._integer, int64{ 1 } );
    SW_EXPECT_TRUE( value._listElement[1].isText( "hi" ) );
}

// 바이트가 몇 조각으로 와도 같은 값이고, 끝나지 않은 값은 기다린다. 값 뒤에 이어진 값도 차례로 나온다.
SW_TEST_CASE( RespCodecTest, ByteByByteGivesTheSameValues )
{
    const string_view text = "*2\r\n$5\r\nhe\r\no\r\n:7\r\n+next\r\n";
    RespParser        parser;
    RespValue         value;
    vector<RespValue> listValue;
    for ( const char byte : text )
    {
        Util::append( parser, string_view( &byte, 1 ) );
        const RespParseResult result = parser.next( value );
        SW_REQUIRE_TRUE( result != RespParseResult::Error );
        if ( result == RespParseResult::Complete )
            listValue.push_back( value );
    }
    SW_REQUIRE_EQUAL( listValue.size(), size_t{ 2 } );
    SW_EXPECT_EQUAL( string( listValue[0]._listElement[0].getText() ), string( "he\r\no" ) );
    SW_EXPECT_EQUAL( listValue[0]._listElement[1]._integer, int64{ 7 } );
    SW_EXPECT_TRUE( listValue[1].isText( "next" ) );
    SW_EXPECT_TRUE( parser.next( value ) == RespParseResult::NeedMore );

    RespParser batch;
    Util::append( batch, text );
    SW_EXPECT_TRUE( batch.next( value ) == RespParseResult::Complete );
    SW_EXPECT_TRUE( batch.next( value ) == RespParseResult::Complete );
    SW_EXPECT_TRUE( value.isText( "next" ) );
    SW_EXPECT_TRUE( batch.next( value ) == RespParseResult::NeedMore );
}

// 깨진 프레임은 오류이고, 그 뒤로도 오류다. reset 하면 새 연결처럼 읽는다.
SW_TEST_CASE( RespCodecTest, MalformedFramesFailAndStayFailed )
{
    RespValue value;
    for ( const string_view text : { string_view( "?x\r\n" ), string_view( "\r\n" ), string_view( "+a\rb\r\n" ), string_view( ":12a\r\n" ), string_view( ":+1\r\n" ), string_view( ": 1\r\n" ),
                                     string_view( "$-2\r\n" ), string_view( "$x\r\n" ), string_view( "$2\r\nabXY" ), string_view( "*-2\r\n" ), string_view( "*x\r\n" ), string_view( "*1\r\n?\r\n" ) } )
    {
        SW_EXPECT_TRUE( Util::parseOne( text, value ) == RespParseResult::Error );
    }

    RespParser parser;
    Util::append( parser, "?\r\n" );
    SW_EXPECT_TRUE( parser.next( value ) == RespParseResult::Error );
    SW_EXPECT_TRUE( parser.hasFailed() );
    SW_EXPECT_NOT_EQUAL( string( parser.getFailureText() ), string() );
    Util::append( parser, "+OK\r\n" );
    SW_EXPECT_TRUE( parser.next( value ) == RespParseResult::Error );

    parser.reset();
    Util::append( parser, "+OK\r\n" );
    SW_EXPECT_TRUE( parser.next( value ) == RespParseResult::Complete );
}

// 상한: 벌크 · 배열 · 중첩. 큰 배열 머리는 바이트가 오기 전에 메모리를 잡지 않고 기다린다.
SW_TEST_CASE( RespCodecTest, LimitsAreEnforced )
{
    RespValue value;
    SW_EXPECT_TRUE( Util::parseOne( "$" + to_string( RespParser::kMaxBulkSize + 1 ) + "\r\n", value ) == RespParseResult::Error );
    SW_EXPECT_TRUE( Util::parseOne( "$" + to_string( RespParser::kMaxBulkSize ) + "\r\n", value ) == RespParseResult::NeedMore );
    SW_EXPECT_TRUE( Util::parseOne( "*" + to_string( RespParser::kMaxArrayCount + 1 ) + "\r\n", value ) == RespParseResult::Error );
    SW_EXPECT_TRUE( Util::parseOne( "*" + to_string( RespParser::kMaxArrayCount ) + "\r\n", value ) == RespParseResult::NeedMore );

    string nested;
    for ( int32 depth = 0; depth <= RespParser::kMaxNestedDepth; ++depth )
    {
        nested += "*1\r\n";
    }
    SW_EXPECT_TRUE( Util::parseOne( nested + ":1\r\n", value ) == RespParseResult::Error );
}

// 결함: 줄 상한이 줄 끝을 아직 못 봤을 때만 적용돼, 같은 바이트가 조각으로 오면 오류이고 한 번에 오면 통과했다.
SW_TEST_CASE( RespCodecTest, LineLimitDoesNotDependOnChunking )
{
    const string longLine = "+" + string( static_cast<size_t>( RespParser::kMaxLineSize ) + 1, 'a' );
    RespValue    value;
    SW_EXPECT_TRUE( Util::parseOne( longLine + "\r\n", value ) == RespParseResult::Error );

    RespParser chunked;
    Util::append( chunked, longLine );
    SW_EXPECT_TRUE( chunked.next( value ) == RespParseResult::Error );

    const string fitting = "+" + string( static_cast<size_t>( RespParser::kMaxLineSize ) - 1, 'a' ) + "\r\n";
    SW_EXPECT_TRUE( Util::parseOne( fitting, value ) == RespParseResult::Complete );
}

// 읽은 앞부분은 버퍼에서 당겨 지운다 - 값이 이어져도 그대로 읽힌다.
SW_TEST_CASE( RespCodecTest, BufferCompactionKeepsTheRest )
{
    RespParser   parser;
    RespValue    value;
    const string big = "$5000\r\n" + string( 5000, 'x' ) + "\r\n";
    Util::append( parser, big + "+partial" );
    SW_REQUIRE_TRUE( parser.next( value ) == RespParseResult::Complete );
    SW_EXPECT_TRUE( parser.next( value ) == RespParseResult::NeedMore );
    Util::append( parser, "\r\n" );
    SW_REQUIRE_TRUE( parser.next( value ) == RespParseResult::Complete );
    SW_EXPECT_TRUE( value.isText( "partial" ) );
}

// 점수는 정수이거나, 정확히 표현되는 범위 안의 정수인 실수 글이다.
SW_TEST_CASE( RespCodecTest, IntegerTextAcceptsOnlyExactIntegers )
{
    int64 number = 0;
    SW_EXPECT_TRUE( Util::makeBulk( "20" ).tryReadIntegerText( number ) );
    SW_EXPECT_EQUAL( number, int64{ 20 } );
    SW_EXPECT_TRUE( Util::makeBulk( "-3" ).tryReadIntegerText( number ) );
    SW_EXPECT_EQUAL( number, int64{ -3 } );
    SW_EXPECT_TRUE( Util::makeBulk( "1e+3" ).tryReadIntegerText( number ) );
    SW_EXPECT_EQUAL( number, int64{ 1000 } );
    SW_EXPECT_TRUE( Util::makeBulk( "9007199254740992" ).tryReadIntegerText( number ) );

    for ( const string_view text : { string_view( "2.5" ), string_view( "1e+16" ), string_view( "9007199254740993" ), string_view( "inf" ), string_view( "nan" ), string_view( "abc" ), string_view( "" ) } )
    {
        SW_EXPECT_FALSE( Util::makeBulk( text ).tryReadIntegerText( number ) );
    }

    RespValue integer;
    integer._type    = RespType::Integer;
    integer._integer = 7;
    SW_EXPECT_TRUE( integer.tryReadIntegerText( number ) );
    SW_EXPECT_EQUAL( number, int64{ 7 } );

    RespValue null;
    SW_EXPECT_FALSE( null.tryReadIntegerText( number ) );
}

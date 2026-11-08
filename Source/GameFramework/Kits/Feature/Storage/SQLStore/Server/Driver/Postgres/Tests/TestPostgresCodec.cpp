#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "GameFramework/Kits/Feature/Storage/SQLStore/Runtime/SQL/SQLDriverRegistry.h"
#include "GameFramework/Kits/Feature/Storage/SQLStore/Server/Driver/Postgres/PostgresCodec.h"

#include <limits>

namespace
{
    /** @brief 실패의 문장과 대상을 이은 글입니다(성공이면 빈 글). 오류 글을 확인하는 테스트가 씁니다. */
    template <typename T>
    sw::string describeError( const sw::expected<T>& result )
    {
        return result.has_value() ? sw::string{} : sw::string( result.error().message() ) + " " + sw::string( result.error().detail() );
    }
} // namespace

// PostgresCodecTest — PostgreSQL 드라이버의 변환을 DB 없이 본다: 자리표시자 바꾸기, 매개변수 인코딩(NUL · 자리 수 · 빈 바이트), 이진 칸 읽기(길이는 믿지 않는다), SQLSTATE 분류(nogpu).

using namespace sw;

namespace
{
    constexpr uint32 kOidBool    = 16;
    constexpr uint32 kOidBytea   = 17;
    constexpr uint32 kOidInt8    = 20;
    constexpr uint32 kOidInt2    = 21;
    constexpr uint32 kOidInt4    = 23;
    constexpr uint32 kOidText    = 25;
    constexpr uint32 kOidVarchar = 1043;
    constexpr uint32 kOidFloat8  = 701;

    struct PostgresCodecTestUtil
    {
        static string convert( string_view sql, int32& outCount ) { return PostgresCodec::convertPlaceholders( sql, outCount ); }
    };
} // namespace

SW_TEST_CASE( PostgresCodecTest, PlaceholdersAreNumberedInOrder )
{
    int32 count = -1;
    SW_EXPECT_EQUAL( string( "SELECT a FROM t" ), PostgresCodecTestUtil::convert( "SELECT a FROM t", count ) );
    SW_EXPECT_EQUAL( 0, count );
    SW_EXPECT_EQUAL( string( "INSERT INTO t VALUES ( $1, $2, $3 )" ), PostgresCodecTestUtil::convert( "INSERT INTO t VALUES ( ?, ?, ? )", count ) );
    SW_EXPECT_EQUAL( 3, count );
    SW_EXPECT_EQUAL( string( "$1$2" ), PostgresCodecTestUtil::convert( "??", count ) );
    SW_EXPECT_EQUAL( 2, count );
    SW_EXPECT_EQUAL( string(), PostgresCodecTestUtil::convert( "", count ) );
    SW_EXPECT_EQUAL( 0, count );
}

SW_TEST_CASE( PostgresCodecTest, QuotedQuestionMarksAreData )
{
    int32 count = -1;
    SW_EXPECT_EQUAL( string( "SELECT 'a?b', \"c?\", $1" ), PostgresCodecTestUtil::convert( "SELECT 'a?b', \"c?\", ?", count ) );
    SW_EXPECT_EQUAL( 1, count );
    SW_EXPECT_EQUAL( string( "SELECT 'it''s?', $1" ), PostgresCodecTestUtil::convert( "SELECT 'it''s?', ?", count ) ); // 겹친 따옴표는 닫혔다 다시 열린다
    SW_EXPECT_EQUAL( 1, count );
    SW_EXPECT_EQUAL( string( "SELECT '?" ), PostgresCodecTestUtil::convert( "SELECT '?", count ) ); // 닫히지 않은 따옴표는 서버가 거절한다 — 넘치지 않는다
    SW_EXPECT_EQUAL( 0, count );
}

/**
 * @brief 주석 안의 `?` 는 자리가 아니다.
 * @details 결함: 주석을 몰라 `-- what?` 이 자리 하나로 세어지고, `-- it's` 의 따옴표는 뒤의 진짜 자리를 삼켰다. 값 수가 어긋나 빈 칸을 NULL 로 채운 채 돌았다.
 */
SW_TEST_CASE( PostgresCodecTest, CommentsAreSkipped )
{
    int32 count = -1;
    SW_EXPECT_EQUAL( string( "SELECT $1 -- what? it's\n, $2" ), PostgresCodecTestUtil::convert( "SELECT ? -- what? it's\n, ?", count ) );
    SW_EXPECT_EQUAL( 2, count );
    SW_EXPECT_EQUAL( string( "SELECT /* a? /* b? */ c' */ $1" ), PostgresCodecTestUtil::convert( "SELECT /* a? /* b? */ c' */ ?", count ) ); // 블록 주석은 겹친다
    SW_EXPECT_EQUAL( 1, count );
    SW_EXPECT_EQUAL( string( "SELECT 1 -- ?" ), PostgresCodecTestUtil::convert( "SELECT 1 -- ?", count ) );
    SW_EXPECT_EQUAL( 0, count );
    SW_EXPECT_EQUAL( string( "/* ?" ), PostgresCodecTestUtil::convert( "/* ?", count ) ); // 닫히지 않은 주석
    SW_EXPECT_EQUAL( 0, count );
    SW_EXPECT_EQUAL( string( "$1 - $2 / $3" ), PostgresCodecTestUtil::convert( "? - ? / ?", count ) ); // 연산자는 주석이 아니다
    SW_EXPECT_EQUAL( 3, count );
}

SW_TEST_CASE( PostgresCodecTest, ParametersKeepTheirFormats )
{
    const uint8        arrByte[]  = { 0x00, 0xFF };
    const SQLValue     arrParam[] = { SQLValue{}, SQLValue::makeInt64( std::numeric_limits<int64>::min() ), SQLValue::makeText( "key'; DROP TABLE t; --" ), SQLValue::makeBlob( arrByte, 2 ),
                                      SQLValue::makeBlob( nullptr, 0 ), SQLValue::makeText( "" ) };
    PostgresParameters parameters;
    string             error;
    SW_REQUIRE_TRUE( PostgresCodec::encodeParameters( arrParam, 6, 6, parameters ) );
    SW_EXPECT_NULL( parameters._listValue[0] ); // NULL
    SW_EXPECT_EQUAL( string( "-9223372036854775808" ), string( parameters._listValue[1] ) );
    SW_EXPECT_EQUAL( PostgresCodec::kFormatText, parameters._listFormat[1] );
    SW_EXPECT_EQUAL( string( "key'; DROP TABLE t; --" ), string( parameters._listValue[2] ) ); // 값 칸으로 간다 — SQL 문에 이어 붙이지 않는다
    SW_EXPECT_EQUAL( PostgresCodec::kFormatText, parameters._listFormat[2] );
    SW_EXPECT_TRUE( parameters._listValue[3] == reinterpret_cast<const utf8*>( arrParam[3]._bytes.data() ) );
    SW_EXPECT_EQUAL( 2, parameters._listLength[3] );
    SW_EXPECT_EQUAL( PostgresCodec::kFormatBinary, parameters._listFormat[3] );
    SW_EXPECT_NOT_NULL( parameters._listValue[4] ); // 빈 바이트는 NULL 이 아니다(NOT NULL 칸에 들어간다)
    SW_EXPECT_EQUAL( 0, parameters._listLength[4] );
    SW_EXPECT_NOT_NULL( parameters._listValue[5] ); // 빈 글도 NULL 이 아니다
}

/**
 * @brief 값 수가 자리 수와 다르면 오류다.
 * @details 결함: 모자란 칸을 NULL 로 채우고 남는 값을 버려 다른 문장이 말없이 돌았다(주석에 "덜 주면 NULL 로 채운다" 라고 적혀 있었다).
 */
SW_TEST_CASE( PostgresCodecTest, ParameterCountMustMatch )
{
    const SQLValue     arrParam[] = { SQLValue::makeInt64( 1 ), SQLValue::makeInt64( 2 ) };
    PostgresParameters parameters;
    string             error;
    error = describeError( PostgresCodec::encodeParameters( arrParam, 1, 2, parameters ) );
    SW_EXPECT_FALSE( error.empty() );
    SW_EXPECT_TRUE( error.empty() == false );
    error.clear();
    error = describeError( PostgresCodec::encodeParameters( arrParam, 2, 1, parameters ) );
    SW_EXPECT_FALSE( error.empty() );
    SW_EXPECT_TRUE( error.empty() == false );
    SW_EXPECT_TRUE( PostgresCodec::encodeParameters( nullptr, 0, 0, parameters ) );
}

/**
 * @brief 글에 NUL 이 있으면 오류다.
 * @details 결함: 텍스트 형식은 NUL 로 끝나 뒤가 말없이 잘렸다 — `victim\0x` 가 `victim` 을 읽고 지웠다. 키 규칙을 지나지 않는 값(목록 접두어 · 커서)이 닿는 길이다.
 */
SW_TEST_CASE( PostgresCodecTest, TextWithNulIsRefusedNotTruncated )
{
    const string_view  withNul( "victim\0x", 8 );
    const SQLValue     arrParam[] = { SQLValue::makeText( withNul ) };
    PostgresParameters parameters;
    string             error;
    error = describeError( PostgresCodec::encodeParameters( arrParam, 1, 1, parameters ) );
    SW_EXPECT_FALSE( error.empty() );
    SW_EXPECT_TRUE( error.empty() == false );
}

SW_TEST_CASE( PostgresCodecTest, IntegersDecodeBigEndianWithSign )
{
    string      error;
    SQLValue    value;
    const uint8 arrMinusOne2[] = { 0xFF, 0xFF };
    SW_REQUIRE_TRUE( PostgresCodec::decodeColumn( kOidInt2, arrMinusOne2, 2, value ) );
    SW_EXPECT_TRUE( value._type == SQLValueType::Int64 );
    SW_EXPECT_EQUAL( int64( -1 ), value._integer );

    const uint8 arrInt4[] = { 0x01, 0x02, 0x03, 0x04 };
    SW_REQUIRE_TRUE( PostgresCodec::decodeColumn( kOidInt4, arrInt4, 4, value ) );
    SW_EXPECT_EQUAL( int64( 0x01020304 ), value._integer );

    const uint8 arrMin8[] = { 0x80, 0, 0, 0, 0, 0, 0, 0 };
    SW_REQUIRE_TRUE( PostgresCodec::decodeColumn( kOidInt8, arrMin8, 8, value ) );
    SW_EXPECT_EQUAL( std::numeric_limits<int64>::min(), value._integer );

    const uint8 arrTrue[] = { 1 };
    SW_REQUIRE_TRUE( PostgresCodec::decodeColumn( kOidBool, arrTrue, 1, value ) );
    SW_EXPECT_EQUAL( int64( 1 ), value._integer ); // bool 이 부호 확장으로 -1 이 되지 않는다(값이 1 이다)
}

/**
 * @brief 정수 칸의 길이는 타입과 맞아야 한다.
 * @details 결함: 길이를 그대로 믿어 0 바이트면 64 비트 자리 이동(정의되지 않은 동작), 9 바이트 이상이면 앞 바이트가 말없이 사라졌다. 서버가 준 값이다.
 */
SW_TEST_CASE( PostgresCodecTest, IntegerLengthIsNotTrusted )
{
    string      error;
    SQLValue    value;
    const uint8 arrByte[16] = {};
    error                   = describeError( PostgresCodec::decodeColumn( kOidInt8, arrByte, 0, value ) );
    SW_EXPECT_FALSE( error.empty() );
    SW_EXPECT_TRUE( error.empty() == false );
    SW_EXPECT_FALSE( PostgresCodec::decodeColumn( kOidInt8, arrByte, 16, value ) );
    SW_EXPECT_FALSE( PostgresCodec::decodeColumn( kOidInt4, arrByte, 8, value ) );
    SW_EXPECT_FALSE( PostgresCodec::decodeColumn( kOidBool, arrByte, 2, value ) );
}

SW_TEST_CASE( PostgresCodecTest, TextAndBytesKeepTheirKinds )
{
    string      error;
    const uint8 arrByte[] = { 'a', 0x00, 'b' };
    SQLValue    text;
    SW_REQUIRE_TRUE( PostgresCodec::decodeColumn( kOidText, arrByte, 3, text ) );
    SW_EXPECT_TRUE( text._type == SQLValueType::Text && text._bytes.size() == 3 );
    SQLValue varchar;
    SW_REQUIRE_TRUE( PostgresCodec::decodeColumn( kOidVarchar, arrByte, 1, varchar ) );
    SW_EXPECT_TRUE( varchar._type == SQLValueType::Text && varchar.getText() == "a" );
    SQLValue bytes;
    SW_REQUIRE_TRUE( PostgresCodec::decodeColumn( kOidBytea, arrByte, 3, bytes ) );
    SW_EXPECT_TRUE( bytes._type == SQLValueType::Blob && bytes._bytes == vector<uint8>( arrByte, arrByte + 3 ) );
    SQLValue empty;
    SW_REQUIRE_TRUE( PostgresCodec::decodeColumn( kOidBytea, arrByte, 0, empty ) );
    SW_EXPECT_TRUE( empty._type == SQLValueType::Blob && empty._bytes.empty() );
}

/**
 * @brief 모르는 타입은 오류다.
 * @details 결함: 모르는 타입의 이진 표현을 바이트로 그대로 건네 float8 이 8 바이트 쓰레기로 읽혔다. SQLite 드라이버의 REAL 거절과 같은 규칙이다.
 */
SW_TEST_CASE( PostgresCodecTest, UnknownTypeIsRefused )
{
    string      error;
    SQLValue    value;
    const uint8 arrByte[8] = {};
    error                  = describeError( PostgresCodec::decodeColumn( kOidFloat8, arrByte, 8, value ) );
    SW_EXPECT_FALSE( error.empty() );
    SW_EXPECT_TRUE( error.empty() == false );
}

/**
 * @brief SQLSTATE 분류입니다.
 * @details 결함: 서버 내려감(57P01 · 57P02 · 57P03)이 Error 라 그 연결을 끊김으로 보지 않았다. 다시 여는 것이 답인 경우다.
 */
SW_TEST_CASE( PostgresCodecTest, SQLStatesClassify )
{
    SW_EXPECT_TRUE( PostgresCodec::classifySQLState( "23505" ) == SQLResult::Constraint );
    SW_EXPECT_TRUE( PostgresCodec::classifySQLState( "40001" ) == SQLResult::SerializationFailure );
    SW_EXPECT_TRUE( PostgresCodec::classifySQLState( "40P01" ) == SQLResult::SerializationFailure );
    SW_EXPECT_TRUE( PostgresCodec::classifySQLState( "08006" ) == SQLResult::ConnectionLost );
    SW_EXPECT_TRUE( PostgresCodec::classifySQLState( "08000" ) == SQLResult::ConnectionLost );
    SW_EXPECT_TRUE( PostgresCodec::classifySQLState( "57P01" ) == SQLResult::ConnectionLost );
    SW_EXPECT_TRUE( PostgresCodec::classifySQLState( "57P03" ) == SQLResult::ConnectionLost );
    SW_EXPECT_TRUE( PostgresCodec::classifySQLState( "55P03" ) == SQLResult::Busy );
    SW_EXPECT_TRUE( PostgresCodec::classifySQLState( "23502" ) == SQLResult::Error ); // NOT NULL 은 다시 해도 같다
    SW_EXPECT_TRUE( PostgresCodec::classifySQLState( "42601" ) == SQLResult::Error ); // 문법
    SW_EXPECT_TRUE( PostgresCodec::classifySQLState( "08" ) == SQLResult::Error );    // 다섯 자가 아니다
    SW_EXPECT_TRUE( PostgresCodec::classifySQLState( "" ) == SQLResult::Error );
}

/**
 * @brief 빈 접속 문자열은 어디에도 붙지 않는다.
 * @details 결함: 빈 글을 libpq 에 넘기면 환경 변수 · 기본값(localhost, OS 사용자)으로 어딘가에 붙었다 — 설정을 빠뜨린 서버가 엉뚱한 DB 를 쓴다. 네트워크를 쓰지 않는 길이다.
 */
SW_TEST_CASE( PostgresCodecTest, EmptyConnectionStringIsRefused )
{
    string      error;
    ISQLDriver* pDriver = SQLDriverRegistry::findDriver( "postgres" ).value_or( nullptr );
    SW_REQUIRE_NOT_NULL( pDriver );
    error = describeError( pDriver->openConnection( "", "" ) );
    SW_EXPECT_FALSE( error.empty() );
    SW_EXPECT_TRUE( error.empty() == false );
}

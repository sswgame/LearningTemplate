// ServiceKeyUtilTest — 저장소 키 · 레코드 도우미: 고정폭 16 진 키의 순서 · 왕복과 거절(길이 · 대문자 · 글자),
// 길이 붙은 문자열의 상한 그대로 · 상한 + 1 · 잘린 몸, 토큰 글 허용 목록(상한 · 부호 · 거절할 바이트).
#include "pch.h"

#include "Core/Serialization/BitStream.h"
#include "Core/Testing/TestFramework.h"

#include "GameFramework/Runtime/Online/Store/ServiceKeyUtil.h"

using namespace sw;

/** @brief [ServiceKeyUtilTest] 16 진 키는 16 자 소문자이고 키 사전순이 수 순서와 같으며, 읽기는 그 모양만 받는다 */
SW_TEST_CASE( ServiceKeyUtilTest, HexKeysAreFixedWidthLowercaseAndOrdered )
{
    // 틀리면: 0 을 채우지 않으면 길이 · 순서 단언이, 대문자 검사를 빼면 "ABCDEF..." 단언이(같은 값이 두 키가 된다), 길이 검사를 빼면 15 · 17 자 단언이 진다.
    SW_EXPECT_EQUAL( string( "0000000000000000" ), ServiceKeyUtil::makeHex64( 0 ) );
    SW_EXPECT_EQUAL( string( "00000000000000ff" ), ServiceKeyUtil::makeHex64( 0xff ) );
    SW_EXPECT_TRUE( ServiceKeyUtil::makeHex64( 0xf ) < ServiceKeyUtil::makeHex64( 0x10 ) );
    SW_EXPECT_TRUE( ServiceKeyUtil::makeHex64( 0x7fffffffffffffffull ) < ServiceKeyUtil::makeHex64( ~0ull ) );

    uint64 value = 0;
    SW_REQUIRE_TRUE( ServiceKeyUtil::parseHex64( ServiceKeyUtil::makeHex64( 0x0123456789abcdefull ), value ) );
    SW_EXPECT_EQUAL( uint64( 0x0123456789abcdefull ), value );
    SW_REQUIRE_TRUE( ServiceKeyUtil::parseHex64( "ffffffffffffffff", value ) );
    SW_EXPECT_EQUAL( uint64( ~0ull ), value );

    value = 7;
    SW_EXPECT_FALSE( ServiceKeyUtil::parseHex64( "0123456789ABCDEF", value ) );
    SW_EXPECT_FALSE( ServiceKeyUtil::parseHex64( "000000000000000", value ) );
    SW_EXPECT_FALSE( ServiceKeyUtil::parseHex64( "00000000000000000", value ) );
    SW_EXPECT_FALSE( ServiceKeyUtil::parseHex64( "000000000000000g", value ) );
    SW_EXPECT_FALSE( ServiceKeyUtil::parseHex64( "", value ) );
    SW_EXPECT_EQUAL( uint64( 7 ), value ); // 거절은 출력을 건드리지 않는다

    string key{ "sd/srv/" };
    ServiceKeyUtil::appendHex64( key, 0x2a );
    SW_EXPECT_EQUAL( string( "sd/srv/000000000000002a" ), key );
}

/** @brief [ServiceKeyUtilTest] 길이 붙은 문자열은 상한 그대로를 받고, 상한 + 1 과 길이보다 짧은 몸은 거절한다 */
SW_TEST_CASE( ServiceKeyUtilTest, ReadStringHonoursTheCapAndTheBody )
{
    // 틀리면: 상한을 보지 않으면 상한 + 1 단언이, 몸 길이를 보지 않으면 잘린 몸 단언이 진다.
    BitWriter writer;
    ServiceKeyUtil::writeString( writer, "abcd" );
    const vector<uint8> bytes = writer.releaseBytes();

    string    text;
    BitReader atCap( bytes.data(), static_cast<int32>( bytes.size() ) );
    SW_REQUIRE_TRUE( ServiceKeyUtil::readString( atCap, 4, text ) );
    SW_EXPECT_EQUAL( string( "abcd" ), text );

    BitReader oversized( bytes.data(), static_cast<int32>( bytes.size() ) );
    SW_EXPECT_FALSE( ServiceKeyUtil::readString( oversized, 3, text ) );

    BitReader truncated( bytes.data(), static_cast<int32>( bytes.size() ) - 1 );
    SW_EXPECT_FALSE( ServiceKeyUtil::readString( truncated, 4, text ) );
}

/** @brief [ServiceKeyUtilTest] 토큰 글은 소문자 · 숫자 · 넘긴 부호만 받고, 빈 글 · 상한 넘김 · 그 밖의 바이트(대문자 · 공백 · 널 · 비 ASCII · 넘기지 않은 부호)는 거절한다 */
SW_TEST_CASE( ServiceKeyUtilTest, TokenTextIsAnAllowlistWithACap )
{
    // 틀리면: 허용 목록이 넓어지면(대문자 · 경로 부호 · 널) 거절 단언이, 상한을 보지 않으면 상한 + 1 단언이, 부호 목록을 보지 않으면 "a.b" 단언이 진다.
    SW_EXPECT_TRUE( ServiceKeyUtil::isTokenText( "abc_09", 6, "_" ) );
    SW_EXPECT_TRUE( ServiceKeyUtil::isTokenText( "a.b", 8, "_." ) );
    SW_EXPECT_TRUE( ServiceKeyUtil::isTokenText( "a/b:c-d", 8, "_.:/-" ) );
    SW_EXPECT_TRUE( ServiceKeyUtil::isTokenText( "abc", 3, "" ) ); // 상한 그대로

    SW_EXPECT_FALSE( ServiceKeyUtil::isTokenText( "", 8, "_." ) );
    SW_EXPECT_FALSE( ServiceKeyUtil::isTokenText( "abcd", 3, "_." ) ); // 상한 + 1
    SW_EXPECT_FALSE( ServiceKeyUtil::isTokenText( "a.b", 8, "_" ) );   // 넘기지 않은 부호
    SW_EXPECT_FALSE( ServiceKeyUtil::isTokenText( "Abc", 8, "_." ) );  // 대문자 — 같은 값이 두 키가 된다
    SW_EXPECT_FALSE( ServiceKeyUtil::isTokenText( "a b", 8, "_." ) );
    SW_EXPECT_FALSE( ServiceKeyUtil::isTokenText( "a/b", 8, "_." ) ); // 경로 부호는 넘긴 곳에서만
    SW_EXPECT_FALSE( ServiceKeyUtil::isTokenText( string_view( "a\0b", 3 ), 8, "_." ) );
    SW_EXPECT_FALSE( ServiceKeyUtil::isTokenText( "a\xc3\xa9", 8, "_." ) ); // 비 ASCII 바이트
}

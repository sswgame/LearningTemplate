#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Encoding/Base64Util.h"
#include "Core/Testing/TestFramework.h"

using namespace sw;

SW_TEST_CASE( Base64UtilTest, StandardAlphabetPadsAndRoundTrips )
{
    const uint8  arrByte[] = { 'M', 'a', 'n', 'y' };
    const string encoded   = Base64Util::encode( arrByte, sizeof( arrByte ) );
    SW_EXPECT_EQUAL( string( "TWFueQ==" ), encoded );
    vector<uint8> listDecoded;
    SW_REQUIRE_TRUE( Base64Util::decode( encoded, listDecoded ) );
    SW_REQUIRE_EQUAL( sizeof( arrByte ), listDecoded.size() );
    for ( size_t index = 0; index < sizeof( arrByte ); ++index )
    {
        SW_EXPECT_EQUAL( arrByte[index], listDecoded[index] );
    }
}

SW_TEST_CASE( Base64UtilTest, URLAlphabetHasNoPaddingAndRoundTrips )
{
    const uint8 arrByte[] = { 0xFB, 0xFF, 0xBF, 0x00, 0x10 };
    SW_EXPECT_EQUAL( string( "-_-_ABA" ), Base64Util::encodeURL( arrByte, sizeof( arrByte ) ) );
    SW_EXPECT_EQUAL( string( "+/+/ABA=" ), Base64Util::encode( arrByte, sizeof( arrByte ) ) );
    vector<uint8> listDecoded;
    SW_REQUIRE_TRUE( Base64Util::decodeURL( "-_-_ABA", listDecoded ) );
    SW_EXPECT_EQUAL( sizeof( arrByte ), listDecoded.size() );
}

SW_TEST_CASE( Base64UtilTest, DecodeRejectsCharactersOutsideTheAlphabet )
{
    vector<uint8> listDecoded;
    SW_EXPECT_FALSE( Base64Util::decode( "TW!u", listDecoded ) );
}

/** @brief [Base64UtilTest] 채움(`=`)은 두 글자까지, 채운 길이는 4 의 배수다 — 한 바이트 열에 글은 하나다 */
SW_TEST_CASE( Base64UtilTest, DecodeRejectsExtraOrMisplacedPadding )
{
    vector<uint8> listDecoded;
    SW_REQUIRE_TRUE( Base64Util::decode( "QQ==", listDecoded ) );
    SW_EXPECT_EQUAL( size_t( 1 ), listDecoded.size() );
    SW_EXPECT_FALSE( Base64Util::decode( "QQ======", listDecoded ) ); // 예전에는 `QQ==` 와 같은 바이트로 풀렸다
    SW_EXPECT_FALSE( Base64Util::decode( "QQ=", listDecoded ) );
    SW_EXPECT_FALSE( Base64Util::decodeURL( "QQ===", listDecoded ) );
    SW_EXPECT_TRUE( Base64Util::decodeURL( "QQ", listDecoded ) ); // 채우지 않은 글은 그대로 받는다
}

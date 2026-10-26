// HexUtil — 바이트 · 정수의 16진 쓰기(대소문자 · 자릿수 채움)와 읽기(대소문자 무관, 홀수 길이 · 16진 밖 글자 거절), 왕복.
#include "pch.h"

#include "Core/String/HexUtil.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

SW_TEST_CASE( HexUtilTest, EncodesBytesInOrderWithChosenCase )
{
    const uint8 arrByte[] = { 0x00, 0x0F, 0xA5, 0xFF };
    SW_EXPECT_EQUAL( string( "000fa5ff" ), HexUtil::encodeBytes( arrByte, sizeof( arrByte ) ) );
    SW_EXPECT_EQUAL( string( "000FA5FF" ), HexUtil::encodeBytes( arrByte, sizeof( arrByte ), HexCase::Upper ) );
    SW_EXPECT_EQUAL( string( "" ), HexUtil::encodeBytes( arrByte, 0 ) );

    string text( "id=" );
    HexUtil::appendBytes( text, arrByte + 2, 1 );
    SW_EXPECT_EQUAL( string( "id=a5" ), text );
}

SW_TEST_CASE( HexUtilTest, AppendsFixedWidthIntegers )
{
    string text;
    HexUtil::appendUint64( text, 0x2Aull );
    SW_EXPECT_EQUAL( string( "000000000000002a" ), text );
    text.clear();
    HexUtil::appendUint64( text, 0xBEEFull, 4, HexCase::Upper );
    SW_EXPECT_EQUAL( string( "BEEF" ), text );
    text.clear();
    HexUtil::appendUint64( text, 0x12345ull, 4 ); // 자릿수보다 큰 값은 아래 자리만 남는다
    SW_EXPECT_EQUAL( string( "2345" ), text );
}

SW_TEST_CASE( HexUtilTest, DecodesAnyCaseAndRejectsMalformedText )
{
    vector<uint8> listByte;
    SW_ASSERT_TRUE( HexUtil::decodeBytes( "00fA5f", listByte ) );
    SW_ASSERT_EQUAL( static_cast<size_t>( 3 ), listByte.size() );
    SW_EXPECT_EQUAL( static_cast<uint8>( 0x00 ), listByte[0] );
    SW_EXPECT_EQUAL( static_cast<uint8>( 0xFA ), listByte[1] );
    SW_EXPECT_EQUAL( static_cast<uint8>( 0x5F ), listByte[2] );

    SW_EXPECT_FALSE( HexUtil::decodeBytes( "abc", listByte ) );
    SW_EXPECT_TRUE( listByte.empty() );
    SW_EXPECT_FALSE( HexUtil::decodeBytes( "0g", listByte ) );
    SW_EXPECT_TRUE( listByte.empty() );

    const uint8 arrByte[] = { 0x10, 0xC3, 0x7E };
    SW_ASSERT_TRUE( HexUtil::decodeBytes( HexUtil::encodeBytes( arrByte, sizeof( arrByte ) ), listByte ) );
    SW_EXPECT_EQUAL( static_cast<size_t>( 3 ), listByte.size() );
    SW_EXPECT_EQUAL( static_cast<uint8>( 0xC3 ), listByte[1] );
}

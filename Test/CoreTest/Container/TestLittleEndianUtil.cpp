// LittleEndianUtil — 바이트 위치(낮은 바이트가 앞), 부호 있는 값 왕복, 쓰기 · 덧붙이기가 같은 바이트를 내는지.
#include "pch.h"

#include "Core/Container/LittleEndianUtil.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

SW_TEST_CASE( LittleEndianUtilTest, ReadsLowByteFirst )
{
    const uint8 arrByte[8] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08 };
    SW_EXPECT_EQUAL( static_cast<uint16>( 0x0201u ), LittleEndianUtil::readUint16( arrByte ) );
    SW_EXPECT_EQUAL( 0x04030201u, LittleEndianUtil::readUint32( arrByte ) );
    SW_EXPECT_EQUAL( 0x0807060504030201ull, LittleEndianUtil::readUint64( arrByte ) );
    SW_EXPECT_EQUAL( 0x05040302u, LittleEndianUtil::readUint32( arrByte + 1 ) ); // 정렬되지 않은 주소
}

SW_TEST_CASE( LittleEndianUtilTest, WriteAndAppendProduceSameBytes )
{
    uint8 arrWritten[14] = {};
    LittleEndianUtil::writeUint16( arrWritten, 0xBEEFu );
    LittleEndianUtil::writeUint32( arrWritten + 2, 0xDEADBEEFu );
    LittleEndianUtil::writeUint64( arrWritten + 6, 0x0123456789ABCDEFull );

    vector<uint8> appendedBytes;
    LittleEndianUtil::appendUint16( appendedBytes, 0xBEEFu );
    LittleEndianUtil::appendUint32( appendedBytes, 0xDEADBEEFu );
    LittleEndianUtil::appendUint64( appendedBytes, 0x0123456789ABCDEFull );

    SW_ASSERT_EQUAL( sizeof( arrWritten ), appendedBytes.size() );
    for ( size_t index = 0; index < appendedBytes.size(); ++index )
    {
        SW_EXPECT_EQUAL( arrWritten[index], appendedBytes[index] );
    }
    SW_EXPECT_EQUAL( static_cast<uint8>( 0xEF ), arrWritten[0] );
    SW_EXPECT_EQUAL( static_cast<uint8>( 0xEF ), arrWritten[2] );
    SW_EXPECT_EQUAL( static_cast<uint8>( 0x01 ), arrWritten[13] );
}

SW_TEST_CASE( LittleEndianUtilTest, SignedValuesRoundTrip )
{
    vector<uint8> bytes;
    LittleEndianUtil::appendInt16( bytes, static_cast<int16>( -2 ) );
    SW_ASSERT_EQUAL( static_cast<size_t>( 2 ), bytes.size() );
    SW_EXPECT_EQUAL( static_cast<uint8>( 0xFE ), bytes[0] );
    SW_EXPECT_EQUAL( static_cast<int16>( -2 ), LittleEndianUtil::readInt16( bytes.data() ) );

    uint8 arrByte[8] = {};
    LittleEndianUtil::writeInt64( arrByte, -1234567890123ll );
    SW_EXPECT_EQUAL( -1234567890123ll, LittleEndianUtil::readInt64( arrByte ) );
}

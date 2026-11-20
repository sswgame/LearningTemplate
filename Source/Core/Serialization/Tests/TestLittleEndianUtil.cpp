// LittleEndianUtil — 바이트 위치(낮은 바이트가 앞), 부호 있는 값 왕복, 쓰기 · 덧붙이기가 같은 바이트를 내는지. 읽기는 `ByteReader` 로만 한다(날 읽기는 감췄다).
#include "pch.h"

#include "Core/Serialization/ByteStream.h"
#include "Core/Serialization/LittleEndianUtil.h"
#include "Core/Testing/TestFramework.h"

#include <type_traits>

using namespace sw;

SW_TEST_CASE( LittleEndianUtilTest, ReadsLowByteFirst )
{
    const uint8 arrByte[8] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08 };
    SW_EXPECT_EQUAL( static_cast<uint16>( 0x0201u ), ByteReader( arrByte, sizeof( arrByte ) ).readUint16() );
    SW_EXPECT_EQUAL( 0x04030201u, ByteReader( arrByte, sizeof( arrByte ) ).readUint32() );
    SW_EXPECT_EQUAL( 0x0807060504030201ull, ByteReader( arrByte, sizeof( arrByte ) ).readUint64() );
    SW_EXPECT_EQUAL( 0x05040302u, ByteReader( arrByte + 1, sizeof( arrByte ) - 1 ).readUint32() ); // 정렬되지 않은 주소
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

    SW_REQUIRE_EQUAL( sizeof( arrWritten ), appendedBytes.size() );
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
    SW_REQUIRE_EQUAL( static_cast<size_t>( 2 ), bytes.size() );
    SW_EXPECT_EQUAL( static_cast<uint8>( 0xFE ), bytes[0] );
    SW_EXPECT_EQUAL( static_cast<int16>( -2 ), ByteReader( bytes.data(), bytes.size() ).readInt16() );

    uint8 arrByte[8] = {};
    LittleEndianUtil::writeInt64( arrByte, -1234567890123ll );
    SW_EXPECT_EQUAL( -1234567890123ll, ByteReader( arrByte, sizeof( arrByte ) ).readInt64() );
}

namespace
{
    // 날 읽기(`LittleEndianUtil::read*`)는 비공개다 — 바깥이 부르면 컴파일 오류여야 한다(접근 검사는 치환 실패다). 읽기는 범위를 보는 `ByteReader` 만 한다.
    template <typename T, typename = void>
    struct IsRawReadCallable : std::false_type
    {
    };
    template <typename T>
    struct IsRawReadCallable<T, std::void_t<decltype( T::readUint32( std::declval<const sw::uint8*>() ) )>> : std::true_type
    {
    };
    static_assert( IsRawReadCallable<sw::LittleEndianUtil>::value == false, "LittleEndianUtil::read* must stay private - read through ByteReader" );
} // namespace

SW_TEST_CASE( LittleEndianUtilTest, CountReadStopsAtRemainingBytes )
{
    vector<uint8> bytes;
    LittleEndianUtil::appendUint32( bytes, 1000000u ); // 원소 백만 개라고 적었지만 뒤에 바이트가 없다
    ByteReader shortReader( bytes.data(), bytes.size() );
    uint32     count = 7;
    SW_EXPECT_FALSE( shortReader.readCount( 4, count ) );
    SW_EXPECT_TRUE( shortReader.hasOverflowed() );
    SW_EXPECT_EQUAL( 7u, count ); // 실패하면 개수를 건드리지 않는다

    LittleEndianUtil::appendUint32( bytes, 0xAABBCCDDu );
    bytes[0] = 1;
    bytes[1] = bytes[2] = bytes[3] = 0;
    ByteReader fullReader( bytes.data(), bytes.size() );
    SW_EXPECT_TRUE( fullReader.readCount( 4, count ) );
    SW_EXPECT_EQUAL( 1u, count );
}

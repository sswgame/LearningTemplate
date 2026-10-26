// ByteWriter · ByteReader — 리틀 엔디언 왕복, 실수 비트 보존, 남은 바이트를 넘는 읽기가 버퍼 밖을 읽지 않고 넘침으로 남는지.
#include "pch.h"

#include "Core/Serialization/ByteStream.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

SW_TEST_CASE( ByteStreamTest, WriteThenReadRoundTrips )
{
    vector<uint8> bytes;
    ByteWriter    writer{ bytes };
    writer.writeUint8( 0x7Fu );
    writer.writeUint16( 0xBEEFu );
    writer.writeUint32( 0xDEADBEEFu );
    writer.writeUint64( 0x0123456789ABCDEFull );
    writer.writeInt64( -1234567890123ll );
    writer.writeFloat32( -2.5f );
    const uint8 arrTail[3] = { 1, 2, 3 };
    writer.writeBytes( arrTail, sizeof( arrTail ) );
    SW_ASSERT_EQUAL( static_cast<size_t>( 1 + 2 + 4 + 8 + 8 + 4 + 3 ), writer.getByteCount() );
    SW_EXPECT_EQUAL( static_cast<uint8>( 0xEF ), bytes[1] ); // 낮은 바이트가 앞

    ByteReader reader{ bytes.data(), bytes.size() };
    SW_EXPECT_EQUAL( static_cast<uint8>( 0x7Fu ), reader.readUint8() );
    SW_EXPECT_EQUAL( static_cast<uint16>( 0xBEEFu ), reader.readUint16() );
    SW_EXPECT_EQUAL( 0xDEADBEEFu, reader.readUint32() );
    SW_EXPECT_EQUAL( 0x0123456789ABCDEFull, reader.readUint64() );
    SW_EXPECT_EQUAL( -1234567890123ll, reader.readInt64() );
    SW_EXPECT_EQUAL( -2.5f, reader.readFloat32() );
    uint8 arrRead[3] = {};
    SW_EXPECT_TRUE( reader.readBytes( arrRead, sizeof( arrRead ) ) );
    SW_EXPECT_EQUAL( static_cast<uint8>( 3 ), arrRead[2] );
    SW_EXPECT_EQUAL( static_cast<size_t>( 0 ), reader.getRemainingByteCount() );
    SW_EXPECT_FALSE( reader.hasOverflowed() );
}

SW_TEST_CASE( ByteStreamTest, ShortReadOverflowsWithoutMoving )
{
    const uint8 arrByte[3] = { 0x01, 0x02, 0x03 };
    ByteReader  reader{ arrByte, sizeof( arrByte ) };
    SW_EXPECT_EQUAL( 0u, reader.readUint32() ); // 3 바이트뿐이라 읽지 않는다
    SW_EXPECT_TRUE( reader.hasOverflowed() );
    SW_EXPECT_EQUAL( static_cast<size_t>( 0 ), reader.getOffset() );
    SW_EXPECT_EQUAL( static_cast<uint16>( 0x0201u ), reader.readUint16() ); // 남은 바이트 안의 읽기는 이어서 된다
    SW_EXPECT_FALSE( reader.skipBytes( 2 ) );
    SW_EXPECT_TRUE( reader.skipBytes( 1 ) );

    ByteReader emptyReader{ nullptr, 16 }; // 데이터가 없으면 길이를 믿지 않는다
    SW_EXPECT_EQUAL( static_cast<size_t>( 0 ), emptyReader.getRemainingByteCount() );
    SW_EXPECT_EQUAL( 0.0f, emptyReader.readFloat32() );
    SW_EXPECT_TRUE( emptyReader.hasOverflowed() );
}

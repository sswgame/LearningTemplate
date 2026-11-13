// ByteWriter · ByteReader — 리틀 엔디언 왕복, 실수 비트 보존, 남은 바이트를 넘는 읽기가 버퍼 밖을 읽지 않고 넘침으로 남는지.
#include "pch.h"

#include "Core/Serialization/ByteStream.h"
#include "Core/Testing/TestFramework.h"

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
    SW_REQUIRE_EQUAL( static_cast<size_t>( 1 + 2 + 4 + 8 + 8 + 4 + 3 ), writer.getByteCount() );
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

/**
 * @brief [ByteStreamTest] 개수 검사는 나눗셈이라 넘치지 않고, 차원의 곱은 넘치면 false 다
 */
SW_TEST_CASE( ByteStreamTest, CountChecksDoNotOverflow )
{
    SW_EXPECT_TRUE( ByteReader::canHoldCount( 3, 4, 12 ) );
    SW_EXPECT_FALSE( ByteReader::canHoldCount( 4, 4, 12 ) );
    SW_EXPECT_FALSE( ByteReader::canHoldCount( uint64{ 1 } << 62, 8, 16 ) ); // 곱하면 2^65 가 넘쳐 0 이 되어 통과할 값
    SW_EXPECT_TRUE( ByteReader::canHoldCount( 16, 0, 16 ) );                 // 크기 0 은 1 로 본다
    SW_EXPECT_FALSE( ByteReader::canHoldCount( 17, 0, 16 ) );

    uint64 product = 0;
    SW_EXPECT_TRUE( ByteReader::tryMultiplyCounts( { 65536, 65536, 16 }, product ) );
    SW_EXPECT_EQUAL( uint64{ 1 } << 36, product );
    SW_EXPECT_FALSE( ByteReader::tryMultiplyCounts( { uint64{ 1 } << 31, uint64{ 1 } << 31, 4 }, product ) ); // 2^64 — 그대로 곱하면 0
    SW_EXPECT_TRUE( ByteReader::tryMultiplyCounts( { 0, uint64{ 1 } << 63, uint64{ 1 } << 63 }, product ) );
    SW_EXPECT_EQUAL( uint64{ 0 }, product );
}

/**
 * @brief [ByteStreamTest] 목록 읽기는 위조된 개수를 할당 전에 거르고, 잘리거나 원소가 거절되면 빈 목록과 실패를 남긴다
 */
SW_TEST_CASE( ByteStreamTest, ListReadsRefuseForgedCountsAndTruncation )
{
    vector<uint8> bytes;
    ByteWriter    writer{ bytes };
    writer.writeUint32( 3 );
    writer.writeUint32( 10 );
    writer.writeUint32( 20 );
    writer.writeUint32( 30 );

    const auto readValue = []( ByteReader& reader, uint32& outValue )
    {
        outValue = reader.readUint32();
    };
    {
        ByteReader     reader{ bytes.data(), bytes.size() };
        vector<uint32> listValue;
        SW_REQUIRE_TRUE( reader.readCountedList( listValue, sizeof( uint32 ), [&]( uint32& outValue )
        { readValue( reader, outValue ); } ) );
        SW_REQUIRE_EQUAL( size_t{ 3 }, listValue.size() );
        SW_EXPECT_EQUAL( 30u, listValue[2] );
        SW_EXPECT_TRUE( reader.isFullyRead() );
    }
    // 모든 길이의 잘림: 어느 길이에서도 목록이 비고 실패다.
    for ( size_t length = 0; length < bytes.size(); ++length )
    {
        ByteReader     reader{ bytes.data(), length };
        vector<uint32> listValue{ 99u };
        SW_EXPECT_FALSE( reader.readCountedList( listValue, sizeof( uint32 ), [&]( uint32& outValue )
        { readValue( reader, outValue ); } ) );
        SW_EXPECT_TRUE( listValue.empty() );
        SW_EXPECT_TRUE( reader.hasOverflowed() );
    }
    // 위조된 개수: 원소를 부르기 전에 멈춘다(할당하지 않는다).
    {
        vector<uint8> forged;
        ByteWriter    craftedWriter{ forged };
        craftedWriter.writeUint32( 0xFFFFFFFFu );
        craftedWriter.writeUint32( 1 );
        ByteReader     reader{ forged.data(), forged.size() };
        vector<uint64> listValue;
        uint32         callCount = 0;
        SW_EXPECT_FALSE( reader.readCountedList( listValue, sizeof( uint64 ), [&]( uint64& )
        { ++callCount; } ) );
        SW_EXPECT_EQUAL( 0u, callCount );
    }
    // 원소가 거절하면 그 자리에서 멈춘다.
    {
        ByteReader     reader{ bytes.data(), bytes.size() };
        vector<uint32> listValue;
        uint32         callCount = 0;
        SW_EXPECT_FALSE( reader.readCountedList( listValue, sizeof( uint32 ), [&]( uint32& outValue ) -> bool
        {
            ++callCount;
            outValue = reader.readUint32();
            return outValue != 20u;
        } ) );
        SW_EXPECT_EQUAL( 2u, callCount );
        SW_EXPECT_TRUE( listValue.empty() );
    }
    // POD 표: 개수 × 크기가 넘치는 값도 거른다.
    {
        ByteReader     reader{ bytes.data(), bytes.size() };
        vector<uint32> listValue;
        SW_EXPECT_FALSE( reader.readPodList( listValue, uint64{ 1 } << 62 ) );
        SW_EXPECT_TRUE( listValue.empty() );
    }
    {
        ByteReader     reader{ bytes.data(), bytes.size() };
        vector<uint32> listValue;
        SW_REQUIRE_TRUE( reader.readPodList( listValue, 4 ) );
        SW_EXPECT_EQUAL( 3u, listValue[0] );
        SW_EXPECT_TRUE( reader.isFullyRead() );
    }
}

/**
 * @brief [ByteStreamTest] 실수 읽기는 NaN · 무한대를 0 과 실패로 답하고 위치는 넘어간다. 유한한 값은 그대로다
 */
SW_TEST_CASE( ByteStreamTest, FloatReadRefusesNonFinite )
{
    vector<uint8> bytes;
    ByteWriter    writer{ bytes };
    writer.writeUint32( 0x7FC00000u ); // NaN
    writer.writeUint32( 0x7F800000u ); // +무한대
    writer.writeUint32( 0xFF800000u ); // -무한대
    writer.writeFloat32( 1.5f );

    for ( size_t skip = 0; skip < 3; ++skip )
    {
        ByteReader reader{ bytes.data(), bytes.size() };
        SW_REQUIRE_TRUE( reader.skipBytes( skip * 4 ) );
        SW_EXPECT_EQUAL( 0.0f, reader.readFloat32() );
        SW_EXPECT_TRUE( reader.hasOverflowed() );
    }
    ByteReader reader{ bytes.data(), bytes.size() };
    SW_REQUIRE_TRUE( reader.skipBytes( 12 ) );
    SW_EXPECT_EQUAL( 1.5f, reader.readFloat32() );
    SW_EXPECT_TRUE( reader.isFullyRead() );
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

/**
 * @brief [ByteStreamTest] 가변 정수 · 길이 붙은 글 · 바이트 · POD · 덮어쓰기가 왕복하고, 잘린 글은 false 와 넘침을 남긴다(옛 Engine BinaryStream 의 몫)
 */
SW_TEST_CASE( ByteStreamTest, VarIntsStringsPodsRoundTripAndTruncationFails )
{
    struct Header
    {
        uint32  _magic;
        float32 _scale;
    };
    sw::vector<uint8> bytes;
    sw::ByteWriter    writer{ bytes };
    writer.writeUint32( 0 ); // 뒤에 덮어쓸 자리
    writer.writeVarUint( 300 );
    writer.writeVarInt( -5 );
    writer.writeString( "hello" );
    writer.writeByteVector( { 1, 2, 3 } );
    writer.writePod( Header{ 0xABCDu, 2.5f } );
    writer.writePodAt( 0, uint32{ 7 } );

    sw::ByteReader    reader{ bytes.data(), bytes.size() };
    uint64            unsignedValue = 0;
    int32             signedValue   = 0;
    sw::string        text;
    sw::vector<uint8> listByte;
    Header            header{};
    SW_EXPECT_EQUAL( 7u, reader.readUint32() );
    SW_REQUIRE_TRUE( reader.readVarUint( unsignedValue ) );
    SW_REQUIRE_TRUE( reader.readVarInt( signedValue ) );
    SW_REQUIRE_TRUE( reader.readString( text ) );
    SW_REQUIRE_TRUE( reader.readByteVector( listByte ) );
    SW_REQUIRE_TRUE( reader.readPod( header ) );
    SW_EXPECT_EQUAL( uint64{ 300 }, unsignedValue );
    SW_EXPECT_EQUAL( -5, signedValue );
    SW_EXPECT_TRUE( text == "hello" );
    SW_EXPECT_EQUAL( size_t{ 3 }, listByte.size() );
    SW_EXPECT_EQUAL( 0xABCDu, header._magic );
    SW_EXPECT_EQUAL( 2.5f, header._scale );
    SW_EXPECT_FALSE( reader.hasOverflowed() );
    SW_EXPECT_EQUAL( size_t{ 0 }, reader.getRemainingByteCount() );

    sw::vector<uint8> truncated;
    sw::ByteWriter    shortWriter{ truncated };
    shortWriter.writeString( "hello" );
    truncated.resize( truncated.size() - 2 );
    sw::ByteReader shortReader{ truncated.data(), truncated.size() };
    SW_EXPECT_FALSE( shortReader.readString( text ) );
    SW_EXPECT_TRUE( shortReader.hasOverflowed() );
}

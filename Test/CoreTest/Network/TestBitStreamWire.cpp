/**
 * @file TestBitStreamWire.cpp
 * @brief BitStream 의 와이어 바이트 고정 — 비트 폭 · 정렬 · 가변 정수 · 바이트 쓰기를 섞은 고정 입력이 늘 같은 바이트가 되고 그대로 읽힌다.
 * @details 쓰기 · 읽기 구현을 바꿀 때(64 비트 워드로 모아 쓰기 등) 옛 빌드와 새 빌드가 같은 바이트를 주고받는지 지키는 테스트다. 기대 바이트는
 *          바꾸기 전 구현(낮은 비트부터, 바이트 안에서도 낮은 비트부터)으로 계산했다. 이 배열을 고치면 와이어 형식을 바꾼 것이다.
 */
#include "pch.h"

#include "Core/Network/BitStream.h"

#include "TestFramework/TestFramework.h"

namespace
{
    struct BitStreamWireTestInternal
    {
        /** @brief 아래 `writeFixedInput` 이 만드는 바이트입니다. */
        static constexpr uint8 kArrExpectedByte[] = { 0xFB, 0xE7, 0xD5, 0xEF, 0xBE, 0xAD, 0xDE, 0xCC, 0x2A,
                                                      0x10, 0x21, 0x32, 0x03, 0x44, 0x55, 0x45, 0x23, 0x01 };
        /** @brief 바이트로 쓰는 값(정렬 안 된 자리 셋 · 정렬된 자리 둘)입니다. */
        static constexpr uint8 kArrUnalignedByte[] = { 0x11, 0x22, 0x33 };
        static constexpr uint8 kArrAlignedByte[]   = { 0x44, 0x55 };

        static void writeFixedInput( sw::BitWriter& writer )
        {
            writer.writeBits( 1, 1 );
            writer.writeBits( 0x5, 3 );
            writer.writeBits( 0x7F, 7 );
            writer.writeBits( 0x1ABC, 13 );
            writer.writeBits( 0xDEADBEEF, 32 );
            writer.writeBits( 0, 2 );
            writer.writeBits( 0x3, 2 );
            writer.writeVarUint( 300 );
            writer.writeBytes( kArrUnalignedByte, 3 );
            writer.alignToByte();
            writer.writeBytes( kArrAlignedByte, 2 );
            writer.writeBits( 0x12345, 17 );
        }
    };
} // namespace

/**
 * @brief [BitStreamWireTest] 고정 입력이 기대한 와이어 바이트가 된다
 */
SW_TEST_CASE( BitStreamWireTest, FixedInputMakesTheSameBytes )
{
    sw::BitWriter writer;
    BitStreamWireTestInternal::writeFixedInput( writer );

    const sw::vector<uint8>& bytes = writer.getBytes();
    SW_ASSERT_EQUAL( sizeof( BitStreamWireTestInternal::kArrExpectedByte ), bytes.size() );
    SW_EXPECT_EQUAL( 137, writer.getBitCount() );
    for ( size_t index = 0; index < bytes.size(); ++index )
    {
        SW_EXPECT_EQUAL( static_cast<uint32>( BitStreamWireTestInternal::kArrExpectedByte[index] ), static_cast<uint32>( bytes[index] ) );
    }
}

/**
 * @brief [BitStreamWireTest] 기대 바이트를 읽으면 쓴 값이 그대로 나온다
 */
SW_TEST_CASE( BitStreamWireTest, ExpectedBytesReadBackTheFixedInput )
{
    sw::BitReader reader( BitStreamWireTestInternal::kArrExpectedByte, static_cast<int32>( sizeof( BitStreamWireTestInternal::kArrExpectedByte ) ) );
    SW_EXPECT_EQUAL( 1u, reader.readBits( 1 ) );
    SW_EXPECT_EQUAL( 0x5u, reader.readBits( 3 ) );
    SW_EXPECT_EQUAL( 0x7Fu, reader.readBits( 7 ) );
    SW_EXPECT_EQUAL( 0x1ABCu, reader.readBits( 13 ) );
    SW_EXPECT_EQUAL( 0xDEADBEEFu, reader.readBits( 32 ) );
    SW_EXPECT_EQUAL( 0u, reader.readBits( 2 ) );
    SW_EXPECT_EQUAL( 0x3u, reader.readBits( 2 ) );
    SW_EXPECT_EQUAL( uint64{ 300 }, reader.readVarUint() );
    uint8 arrUnaligned[3]{};
    SW_ASSERT_TRUE( reader.readBytes( arrUnaligned, 3 ) );
    SW_EXPECT_EQUAL( 0x11u, static_cast<uint32>( arrUnaligned[0] ) );
    SW_EXPECT_EQUAL( 0x33u, static_cast<uint32>( arrUnaligned[2] ) );
    reader.alignToByte();
    uint8 arrAligned[2]{};
    SW_ASSERT_TRUE( reader.readBytes( arrAligned, 2 ) );
    SW_EXPECT_EQUAL( 0x55u, static_cast<uint32>( arrAligned[1] ) );
    SW_EXPECT_EQUAL( 0x12345u, reader.readBits( 17 ) );
    SW_EXPECT_FALSE( reader.hasOverflowed() );
}

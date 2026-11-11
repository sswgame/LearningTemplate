#include "pch.h"

#include "Core/Math/BitUtil.h"
#include "Core/Math/VectorMath.h"
#include "Core/Serialization/BitStream.h"
#include "Core/Testing/ScopedAssertCapture.h"
#include "Core/Testing/TestFramework.h"

#include <limits>

// 비트 스트림 — 범위 · 실수 · 가변 정수 · 덩어리 쓰기와 읽기, 넘침 검출, 잘라 쓰지 않는 실패, 버퍼 다시 쓰기, 와이어 배치 고정.

using namespace sw;

namespace
{
    /** @brief 테스트용 의사 난수(xorshift) — 시드가 같으면 같은 수열입니다. */
    uint32 nextBitStreamTestRandom( uint32& inOutState )
    {
        inOutState ^= inOutState << 13;
        inOutState ^= inOutState >> 17;
        inOutState ^= inOutState << 5;
        return inOutState;
    }
} // namespace

SW_TEST_CASE( BitStreamTest, BitStreamPacksRangesFloatsVarIntsAndDetectsOverflow )
{
    BitWriter writer;
    writer.writeBool( true );
    SW_REQUIRE_TRUE( writer.writeInt( 73, 0, 100 ) );  // 7 비트
    SW_REQUIRE_TRUE( writer.writeInt( -5, -10, 10 ) ); // 5 비트
    SW_REQUIRE_TRUE( writer.writeQuantizedFloat( 12.345f, -100.0f, 100.0f, 0.01f ) );
    writer.writeFloat( 3.25f );
    writer.writeVarUint( 300 );
    writer.writeVarInt( -70000 );
    writer.alignToByte();
    const uint8 arrRaw[3] = { 1, 2, 250 };
    writer.writeBytes( arrRaw, 3 );
    SW_EXPECT_EQUAL( 7, BitMath::computeBitsRequired( 0, 100 ) );
    SW_EXPECT_EQUAL( 5, BitMath::computeBitsRequired( -10, 10 ) );

    BitReader reader( writer.getBytes().data(), writer.getByteCount() );
    SW_EXPECT_TRUE( reader.readBool() );
    int32   readValue{ 0 };
    float32 readQuantized{ 0.0f };
    SW_EXPECT_TRUE( reader.readInt( 0, 100, readValue ) && readValue == 73 );
    SW_EXPECT_TRUE( reader.readInt( -10, 10, readValue ) && readValue == -5 );
    SW_EXPECT_TRUE( reader.readQuantizedFloat( -100.0f, 100.0f, 0.01f, readQuantized ) );
    SW_EXPECT_NEAR( 12.345f, readQuantized, 0.006f );
    float32 readFloatValue{ 0.0f };
    SW_EXPECT_TRUE( reader.readFiniteFloat( readFloatValue ) );
    SW_EXPECT_NEAR( 3.25f, readFloatValue, 1.0e-6f );
    SW_EXPECT_EQUAL( 300, static_cast<int32>( reader.readVarUint() ) );
    SW_EXPECT_EQUAL( -70000, static_cast<int32>( reader.readVarInt() ) );
    reader.alignToByte();
    uint8 arrRead[3] = {};
    SW_EXPECT_TRUE( reader.readBytes( arrRead, 3 ) );
    SW_EXPECT_EQUAL( 250, arrRead[2] );
    SW_EXPECT_FALSE( reader.hasOverflowed() );
    SW_EXPECT_EQUAL( 0, static_cast<int32>( reader.readBits( 32 ) ) );
    SW_EXPECT_TRUE( reader.hasOverflowed() );

    // 끝나지 않는 가변 정수.
    const uint8 arrBroken[2] = { 0xFF, 0xFF };
    BitReader   broken( arrBroken, 2 );
    (void)broken.readVarUint(); // 넘침을 일으키는 것이 목적이다 — 아래 단언이 본다
    SW_EXPECT_TRUE( broken.hasOverflowed() );
}

/**
 * @brief [BitStreamTest] 받은 실수는 유한한 값만 읽히고, uint32 가변 정수는 넘치면 잘리지 않고 깨진 읽기가 된다
 * @details NaN 은 비교가 모두 거짓이라 뒤의 범위 검사(`x > 1`)를 빠져나간다. 비트 그대로가 필요한 곳만 `readFloatBits` 를 쓴다.
 */
SW_TEST_CASE( BitStreamTest, BitReaderRejectsNonFiniteFloatsAndTruncatedVarUint32 )
{
    const float32 arrNonFinite[3] = { std::numeric_limits<float32>::quiet_NaN(), std::numeric_limits<float32>::infinity(), -std::numeric_limits<float32>::infinity() };
    for ( const float32 nonFinite : arrNonFinite )
    {
        BitWriter writer;
        writer.writeFloat( nonFinite );
        BitReader finite( writer.getBytes().data(), writer.getByteCount() );
        float32   value{ 7.0f };
        SW_EXPECT_FALSE( finite.readFiniteFloat( value ) );
        SW_EXPECT_EQUAL( 7.0f, value ); // 실패하면 그대로다
        SW_EXPECT_TRUE( finite.hasOverflowed() );

        BitReader bits( writer.getBytes().data(), writer.getByteCount() );
        SW_EXPECT_EQUAL( BitUtil::floatToBits( nonFinite ), BitUtil::floatToBits( bits.readFloatBits() ) ); // 비트 그대로
        SW_EXPECT_FALSE( bits.hasOverflowed() );
    }

    // 모자란 비트도 실패다.
    const uint8 arrShort[2] = { 0, 0 };
    BitReader   shortReader( arrShort, 2 );
    float32     shortValue{ 0.0f };
    SW_EXPECT_FALSE( shortReader.readFiniteFloat( shortValue ) );

    BitWriter varWriter;
    varWriter.writeVarUint( 0xFFFFFFFFull );
    varWriter.writeVarUint( 0x100000005ull ); // 잘라 읽으면 5 가 된다
    BitReader varReader( varWriter.getBytes().data(), varWriter.getByteCount() );
    SW_EXPECT_EQUAL( 0xFFFFFFFFu, varReader.readVarUint32() );
    SW_EXPECT_FALSE( varReader.hasOverflowed() );
    SW_EXPECT_EQUAL( 0u, varReader.readVarUint32() );
    SW_EXPECT_TRUE( varReader.hasOverflowed() );
}

/**
 * @brief [BitStreamTest] 한 번에 32 비트를 넘는 비트 수는 32 로 잘라 쓰거나 읽지 않고 실패로 남는다
 * @details 예전에는 `writeBits( value, 40 )` 이 32 비트를 쓰고 조용히 넘어갔다. 쓰는 쪽이 센 비트 수와 스트림이 어긋나 뒤의 모든 필드가 밀린다.
 */
SW_TEST_CASE( BitStreamTest, BitStreamRejectsBitCountsAboveOneCall )
{
    test::ScopedAssertCapture assertCapture;

    BitWriter writer;
    writer.writeBits( 0xFFu, BitMath::kMaxBitsPerCall + 8 );
    SW_EXPECT_TRUE( writer.hasFailed() );
    SW_EXPECT_EQUAL( 0, writer.getBitCount() ); // 아무 비트도 쓰지 않았다
    writer.writeBits( 0xFFu, -1 );
    SW_EXPECT_TRUE( writer.hasFailed() );
    writer.clear();
    SW_EXPECT_FALSE( writer.hasFailed() );
    writer.writeBits( 0xFFFFFFFFu, BitMath::kMaxBitsPerCall ); // 상한은 받는다
    SW_EXPECT_FALSE( writer.hasFailed() );
    SW_EXPECT_EQUAL( BitMath::kMaxBitsPerCall, writer.getBitCount() );

    BitReader reader( writer.getBytes().data(), writer.getByteCount() );
    SW_EXPECT_EQUAL( 0u, reader.readBits( BitMath::kMaxBitsPerCall + 1 ) );
    SW_EXPECT_TRUE( reader.hasOverflowed() );
    if ( test::ScopedAssertCapture::kAssertsAreActive )
        SW_EXPECT_EQUAL( 3u, assertCapture.getCount() );
}

/**
 * @brief [BitStreamTest] 범위가 있는 값은 범위 밖을 잘라 쓰거나 잘라 읽지 않고 실패를 돌려준다
 * @details 예전에는 `writeInt( 500, 0, 100 )` 이 100 을 쓰고, 범위 비트로 읽은 칸이 범위를 넘으면 최댓값으로 잘라 읽었다. 깨진 패킷이나 잘못 정한 범위가
 *          경고 없이 다른 값이 된다. 실패한 쓰기는 아무 비트도 쓰지 않는다.
 */
SW_TEST_CASE( BitStreamTest, BitStreamRangedValuesFailInsteadOfClamping )
{
    BitWriter writer;
    SW_EXPECT_FALSE( writer.writeInt( 500, 0, 100 ) );
    SW_EXPECT_FALSE( writer.writeInt( -1, 0, 100 ) );
    SW_EXPECT_FALSE( writer.writeQuantizedFloat( 10.5f, -10.0f, 10.0f, 0.01f ) );
    SW_EXPECT_FALSE( writer.writeQuantizedFloat( std::numeric_limits<float32>::quiet_NaN(), -10.0f, 10.0f, 0.01f ) );
    SW_EXPECT_FALSE( writer.writeQuantizedFloat3( float3( 0.0f, 11.0f, 0.0f ), float3( -10.0f, -10.0f, -10.0f ), float3( 10.0f, 10.0f, 10.0f ), 0.01f ) );
    SW_EXPECT_EQUAL( 0, writer.getBitCount() );

    // 반 칸 안쪽으로 넘친 값은 끝 칸으로 반올림된다(정규화한 사원수의 1.0000001).
    SW_EXPECT_TRUE( writer.writeQuantizedFloat( 1.0000001f, -1.0f, 1.0f, 1.0f / 32767.0f ) );

    // 0..100 은 7 비트라 101..127 칸을 담을 수 있다. 그 칸은 깨진 데이터다.
    BitWriter rawWriter;
    rawWriter.writeBits( 127, 7 );
    rawWriter.writeBits( 0x7FFF, 15 );
    BitReader reader( rawWriter.getBytes().data(), rawWriter.getByteCount() );
    int32     readValue{ 42 };
    SW_EXPECT_FALSE( reader.readInt( 0, 100, readValue ) );
    SW_EXPECT_EQUAL( 42, readValue );
    SW_EXPECT_TRUE( reader.hasOverflowed() );

    // 0..20 을 0.001 간격이면 칸은 0..20000 이고 15 비트다. 32767 칸은 범위 밖이다.
    BitReader quantizedReader( rawWriter.getBytes().data(), rawWriter.getByteCount() );
    SW_EXPECT_EQUAL( 127u, quantizedReader.readBits( 7 ) );
    float32 readQuantized{ 0.5f };
    SW_EXPECT_FALSE( quantizedReader.readQuantizedFloat( 0.0f, 20.0f, 0.001f, readQuantized ) );
    SW_EXPECT_EQUAL( 0.5f, readQuantized );
    SW_EXPECT_TRUE( quantizedReader.hasOverflowed() );
}

/**
 * @brief [BitStreamTest] 필요 없는 페이로드는 읽지 않고 넘긴다 — 그 뒤 필드가 제자리에서 읽히고, 모자라면 움직이지 않는다
 * @details 서버는 여러 패킷에 겹쳐 실려 오는 입력(최대 32 개) 가운데 이미 가진 틱을 버린다. 버릴 것까지 버퍼를 잡아 읽으면 입력 패킷마다
 *          할당이 수십 번이다(`ReplicationServer::handleInput`).
 */
SW_TEST_CASE( BitStreamTest, SkippedBytesLeaveTheFollowingPropertiesInPlace )
{
    BitWriter writer;
    writer.writeVarUint( 3 );
    const uint8 arrPayload[3] = { 9, 8, 7 };
    writer.writeBytes( arrPayload, 3 );
    writer.writeVarUint( 4242 );

    BitReader   reader( writer.getBytes().data(), writer.getByteCount() );
    const int32 byteCount = static_cast<int32>( reader.readVarUint() );
    SW_REQUIRE_TRUE( reader.skipBytes( byteCount ) );
    SW_EXPECT_EQUAL( 4242, static_cast<int32>( reader.readVarUint() ) );
    SW_EXPECT_FALSE( reader.hasOverflowed() );

    // 남은 것보다 많이 넘기라면 거부하고 자리를 지킨다.
    BitReader   shortReader( writer.getBytes().data(), 2 );
    const int32 position = shortReader.getBitPosition();
    SW_EXPECT_FALSE( shortReader.skipBytes( 3 ) );
    SW_EXPECT_EQUAL( position, shortReader.getBitPosition() );
    SW_EXPECT_TRUE( shortReader.hasOverflowed() );
}

/**
 * @brief [BitStreamTest] 길이 붙인 덩어리는 상한 안에서 그대로 오가고, 상한을 넘는 길이 · 비트로 바꾸면 int32 를 넘는 길이는 읽지 않는다
 * @details 서버가 길이를 255 로 잘라 읽으면 남은 바이트가 다음 입력의 길이로 읽혀 옛 틱들에 쓰레기가 들어갔다. `byteCount * 8` 이 넘쳐 작은 수가 되면
 *          넘기기가 "성공" 하고 자리가 움직이지 않았다.
 */
SW_TEST_CASE( BitStreamTest, BlobRoundTripsAndRejectsOversize )
{
    vector<uint8> smallBlob( 5, uint8{ 0x5A } );
    vector<uint8> largeBlob( 300, uint8{ 0xC3 } );
    BitWriter     writer;
    writer.writeBool( true ); // 바이트 경계가 아닌 자리에서도
    writer.writeBlob( smallBlob.data(), static_cast<int32>( smallBlob.size() ) );
    writer.writeBlob( largeBlob.data(), static_cast<int32>( largeBlob.size() ) );
    writer.writeBlob( nullptr, 0 );
    writer.writeVarUint( 4242 );
    SW_EXPECT_EQUAL( 1 + BitMath::computeBlobBits( 5 ) + BitMath::computeBlobBits( 300 ) + BitMath::computeBlobBits( 0 ) + BitMath::computeVarUintBits( 4242 ),
                     writer.getBitCount() );

    BitReader     reader( writer.getBytes().data(), writer.getByteCount() );
    vector<uint8> blob;
    SW_EXPECT_TRUE( reader.readBool() );
    SW_REQUIRE_TRUE( reader.readBlob( blob, 255 ) );
    SW_EXPECT_TRUE( blob == smallBlob );
    SW_REQUIRE_TRUE( reader.readBlob( blob, 512 ) );
    SW_EXPECT_TRUE( blob == largeBlob );
    SW_REQUIRE_TRUE( reader.skipBlob( 0 ) );
    SW_EXPECT_EQUAL( 4242, static_cast<int32>( reader.readVarUint() ) );
    SW_EXPECT_FALSE( reader.hasOverflowed() );

    // 상한을 넘는 길이는 자르지 않고 거부한다 — 넘침으로 남아 뒤를 읽지 않는다.
    BitReader oversize( writer.getBytes().data(), writer.getByteCount() );
    (void)oversize.readBool(); // 앞의 bool 칸을 건너뛴다 — 뒤는 아래 단언이 본다
    SW_REQUIRE_TRUE( oversize.readBlob( blob, 255 ) );
    SW_EXPECT_FALSE( oversize.readBlob( blob, 255 ) );
    SW_EXPECT_TRUE( oversize.hasOverflowed() );
    SW_EXPECT_TRUE( blob.empty() );

    // 비트로 바꾸면 int32 를 넘는 길이 — 남은 바이트보다 많으니 거부하고 자리를 지킨다.
    BitReader   huge( writer.getBytes().data(), writer.getByteCount() );
    const int32 position = huge.getBitPosition();
    SW_EXPECT_FALSE( huge.skipBytes( 0x20000000 ) );
    SW_EXPECT_FALSE( huge.skipBytes( 0x10000001 ) );
    SW_EXPECT_EQUAL( position, huge.getBitPosition() );

    // 가변 정수 비트 수는 실제로 쓴 비트와 같다(경계마다).
    const uint64 arrValue[] = { 0u, 1u, 127u, 128u, 16383u, 16384u, 0x1FFFFFu, 0x200000u, 0xFFFFFFFFu, 0xFFFFFFFFFFFFFFFFull };
    for ( const uint64 value : arrValue )
    {
        BitWriter varWriter;
        varWriter.writeVarUint( value );
        SW_EXPECT_EQUAL( varWriter.getBitCount(), BitMath::computeVarUintBits( value ) );
    }
}

/**
 * @brief [BitStreamTest] 받은 버퍼를 이어받아 쓴 BitWriter 는 그 버퍼를 새로 잡지 않고 돌려준다 — 같은 자리에 매 프레임 써도 할당이 없다
 * @details 롤백 넷코드는 프레임마다 · 되감아 다시 돌 때마다 상태를 링 슬롯에 저장한다. 지역 BitWriter 로 써서 복사해 넘기면 저장마다
 *          버퍼 하나가 새로 잡히고 하나가 복사된다(`FightingMatch::saveState`).
 */
SW_TEST_CASE( BitStreamTest, WriterReusesTheGivenBufferAndReleasesItWithoutCopying )
{
    vector<uint8> slot;
    slot.reserve( 256 );
    slot.assign( 10, uint8{ 0xEE } );
    const uint8* pStorage = slot.data();

    BitWriter writer{ std::move( slot ) };
    SW_EXPECT_EQUAL( 0, writer.getByteCount() ); // 이어받은 내용은 버린다
    writer.writeVarUint( 77 );
    writer.writeUint32( 0xA5A5A5A5u );
    slot = writer.releaseBytes();
    SW_EXPECT_TRUE( slot.data() == pStorage ); // 같은 저장소 — 새로 잡지 않았다
    SW_EXPECT_EQUAL( 0, writer.getByteCount() );

    BitReader reader( slot.data(), static_cast<int32>( slot.size() ) );
    SW_EXPECT_EQUAL( 77, static_cast<int32>( reader.readVarUint() ) );
    SW_EXPECT_EQUAL( 0xA5A5A5A5u, reader.readUint32() );
    SW_EXPECT_FALSE( reader.hasOverflowed() );
}

SW_TEST_CASE( BitStreamTest, BitStreamKeepsWireLayoutAndRoundTripsRandomProperties )
{
    // 선 위의 비트 배치는 바꾸지 않는다 — 바이트 단위로 빨라진 쓰기가 옛 비트 단위 쓰기와 같은 바이트를 낸다.
    BitWriter   writer;
    const uint8 arrRaw[5] = { 1, 2, 3, 0xFE, 0x80 };
    writer.writeBits( 5, 3 );
    writer.writeBool( true );
    writer.writeBits( 0xABCDE, 20 );
    SW_REQUIRE_TRUE( writer.writeInt( -7, -100, 100 ) );
    writer.writeFloat( 3.25f );
    SW_REQUIRE_TRUE( writer.writeQuantizedFloat( 1.5f, -10.0f, 10.0f, 0.01f ) );
    writer.writeVarUint( 300 );
    writer.writeVarInt( -12345 );
    writer.writeBytes( arrRaw, 5 ); // 바이트 경계가 아닌 곳
    writer.writeBits( 0xFFFFFFFFu, 32 );
    writer.alignToByte();
    writer.writeBytes( arrRaw, 3 ); // 바이트 경계
    const uint8 arrExpected[] = { 0xED, 0xCD, 0xAB, 0x5D, 0x00, 0x00, 0x50, 0x40, 0x7E, 0x64, 0x15, 0x88, 0x07, 0x0E,
                                  0x08, 0x10, 0x18, 0xF0, 0x07, 0xFC, 0xFF, 0xFF, 0xFF, 0x07, 0x01, 0x02, 0x03 };
    SW_REQUIRE_EQUAL( static_cast<int32>( sizeof( arrExpected ) ), writer.getByteCount() );
    SW_EXPECT_EQUAL( 216, writer.getBitCount() );
    for ( size_t index = 0; index < sizeof( arrExpected ); ++index )
    {
        SW_EXPECT_EQUAL( static_cast<int32>( arrExpected[index] ), static_cast<int32>( writer.getBytes()[index] ) );
    }

    // 무작위 — 폭 1..32 비트와 경계를 가리지 않는 바이트 덩어리를 섞어 쓰고 그대로 읽는다. 쓰기를 비워도 다시 쓸 수 있다.
    uint32 state = 0x2545F491u;
    for ( int32 round = 0; round < 500; ++round )
    {
        writer.clear();
        uint32      arrValue[48]{};
        int32       arrWidth[48]{};
        uint8       arrBlob[48][6]{};
        const int32 fieldCount = 1 + static_cast<int32>( nextBitStreamTestRandom( state ) % 48 );
        for ( int32 field = 0; field < fieldCount; ++field )
        {
            if ( nextBitStreamTestRandom( state ) % 4 == 0 )
            {
                arrWidth[field] = -static_cast<int32>( nextBitStreamTestRandom( state ) % 7 ); // 0..6 바이트
                for ( int32 byteIndex = 0; byteIndex < -arrWidth[field]; ++byteIndex )
                {
                    arrBlob[field][byteIndex] = static_cast<uint8>( nextBitStreamTestRandom( state ) );
                }
                writer.writeBytes( arrBlob[field], -arrWidth[field] );
                continue;
            }
            arrWidth[field] = 1 + static_cast<int32>( nextBitStreamTestRandom( state ) % 32 );
            arrValue[field] = nextBitStreamTestRandom( state ) & ( arrWidth[field] == 32 ? 0xFFFFFFFFu : ( 1u << arrWidth[field] ) - 1u );
            writer.writeBits( arrValue[field], arrWidth[field] );
        }
        BitReader reader( writer.getBytes().data(), writer.getByteCount() );
        for ( int32 field = 0; field < fieldCount; ++field )
        {
            if ( arrWidth[field] <= 0 )
            {
                uint8 arrRead[6]{};
                SW_REQUIRE_TRUE( reader.readBytes( arrRead, -arrWidth[field] ) );
                for ( int32 byteIndex = 0; byteIndex < -arrWidth[field]; ++byteIndex )
                {
                    SW_REQUIRE_EQUAL( static_cast<int32>( arrBlob[field][byteIndex] ), static_cast<int32>( arrRead[byteIndex] ) );
                }
                continue;
            }
            SW_REQUIRE_TRUE( reader.readBits( arrWidth[field] ) == arrValue[field] );
        }
        SW_REQUIRE_FALSE( reader.hasOverflowed() );
        SW_EXPECT_TRUE( reader.getBitsRemaining() < 8 );
    }
}

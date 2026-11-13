// `.animclip` · 원시 코덱 바이트는 신뢰 경계다 — 어느 길이로 잘려도 · 어느 개수를 꾸며도 · NaN 을 넣어도 거절하고 반쯤 읽은 클립을 남기지 않는다(nogpu).
#include "pch.h"

#include "Core/Memory/Memory.h"
#include "Core/Serialization/ByteStream.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Animation/AnimationClip.h"
#include "Engine/Animation/Codec/Raw/RawAnimationCodec.h"
#include "Engine/Animation/Skeletal/Pose.h"

using namespace sw;

namespace
{
    struct XTestUtil
    {
        // 머리 뒤의 칸 자리는 `makeClipBytes` 가 알려 준다.
        struct ClipOffsets
        {
            size_t _duration{ 0 };
            size_t _trackCount{ 0 };
            size_t _firstTrackNameLength{ 0 };
            size_t _codecByteSize{ 0 };
            size_t _notifyCount{ 0 };
            size_t _curveCount{ 0 };
            size_t _keyCount{ 0 };
            size_t _keyValue{ 0 };
        };

        /** @brief 트랙 둘 · 코덱 바이트 넷 · 알림 하나 · 곡선 하나(키 하나)인 클립의 바이트를 손으로 씁니다(형식은 `AnimationClip::makeBytes` 와 같다). */
        static vector<uint8> makeClipBytes( ClipOffsets* pOutOffsets = nullptr )
        {
            ClipOffsets   offsets{};
            vector<uint8> bytes;
            ByteWriter    writer{ bytes };
            const uint8   arrMagic[4] = { 'S', 'W', 'A', 'C' };
            writer.writeBytes( arrMagic, sizeof( arrMagic ) );
            writer.writeUint32( AnimationClip::kVersion );
            writer.writeUint32( 1u ); // 반복
            offsets._duration = writer.getByteCount();
            writer.writeFloat32( 2.0f );
            writer.writeString( "Clip" );
            offsets._trackCount = writer.getByteCount();
            writer.writeUint32( 2u );
            offsets._firstTrackNameLength = writer.getByteCount();
            writer.writeString( "Hip" );
            writer.writeString( "Head" );
            writer.writeUint32( static_cast<uint32>( -1 ) ); // 루트 모션 없음
            writer.writeUint32( 0u );                        // 코덱 번호
            offsets._codecByteSize  = writer.getByteCount();
            const uint8 arrCodec[4] = { 1, 2, 3, 4 };
            writer.writeUint32( sizeof( arrCodec ) );
            writer.writeBytes( arrCodec, sizeof( arrCodec ) );
            offsets._notifyCount = writer.getByteCount();
            writer.writeUint32( 1u );
            writer.writeString( "Step" );
            writer.writeFloat32( 0.5f );
            writer.writeFloat32( 0.0f );
            offsets._curveCount = writer.getByteCount();
            writer.writeUint32( 1u );
            writer.writeString( "Weight" );
            offsets._keyCount = writer.getByteCount();
            writer.writeUint32( 1u );
            writer.writeFloat32( 0.25f );
            offsets._keyValue = writer.getByteCount();
            writer.writeFloat32( 0.75f );
            if ( pOutOffsets != nullptr )
                *pOutOffsets = offsets;
            return bytes;
        }

        static void patch( vector<uint8>& inoutBytes, size_t offset, uint32 value ) { Memory::copy( inoutBytes.data() + offset, &value, sizeof( value ) ); }

        static void expectClipRefused( const vector<uint8>& bytes )
        {
            AnimationClip clip;
            SW_EXPECT_FALSE( clip.readFromBytes( bytes.data(), bytes.size(), "forged" ) );
            SW_EXPECT_EQUAL( 0u, clip.getTrackCount() );
            SW_EXPECT_TRUE( clip.getCurves().empty() );
        }

        /** @brief 샘플 수 · 샘플율 · 트랙 수 머리와 @p bodySize 바이트의 본문입니다. */
        static vector<uint8> makeRawBytes( uint32 sampleCount, float32 sampleRate, uint32 trackCount, size_t bodySize )
        {
            vector<uint8> bytes;
            ByteWriter    writer{ bytes };
            writer.writeUint32( sampleCount );
            writer.writeFloat32( sampleRate );
            writer.writeUint32( trackCount );
            bytes.resize( bytes.size() + bodySize, 0 );
            return bytes;
        }
    };
} // namespace

/** @brief [AnimationClipBytesTest] 손으로 쓴 바이트가 칸마다 읽힌다 — 아래 거절 테스트의 기준이 정말 올바른 파일이다 */
SW_TEST_CASE( AnimationClipBytesTest, WellFormedBytesRead )
{
    const vector<uint8> bytes = XTestUtil::makeClipBytes();
    AnimationClip       clip;
    SW_REQUIRE_TRUE( clip.readFromBytes( bytes.data(), bytes.size(), "good" ) );
    SW_EXPECT_NEAR( 2.0f, clip.getDuration(), 0.0f );
    SW_EXPECT_TRUE( clip.getName() == hashed_string( "Clip" ) );
    SW_REQUIRE_EQUAL( 2u, clip.getTrackCount() );
    SW_EXPECT_TRUE( clip.getTrackNames()[1] == hashed_string( "Head" ) );
    SW_EXPECT_EQUAL( -1, clip.getRootMotionTrack() );
    SW_REQUIRE_EQUAL( size_t{ 1 }, clip.getCurves().size() );
    SW_REQUIRE_EQUAL( size_t{ 1 }, clip.getCurves()[0]._listKey.size() );
    SW_EXPECT_NEAR( 0.75f, clip.getCurves()[0]._listKey[0]._value, 0.0f );
}

/** @brief [AnimationClipBytesTest] 어느 길이로 잘려도 거절하고 비운다 */
SW_TEST_CASE( AnimationClipBytesTest, EveryTruncatedLengthIsRefused )
{
    SW_EXPECT_LOG( "AnimationClip.AnimationClipNoSWAC" );
    SW_EXPECT_LOG( "AnimationClip.AnimationClipVersionNot" );
    SW_EXPECT_LOG( "AnimationClip.AnimationClipMalformed" );
    const vector<uint8> bytes = XTestUtil::makeClipBytes();
    for ( size_t length = 0; length < bytes.size(); ++length )
    {
        AnimationClip clip;
        SW_EXPECT_FALSE( clip.readFromBytes( bytes.data(), length, "truncated" ) );
        SW_EXPECT_EQUAL( 0u, clip.getTrackCount() );
    }
}

/** @brief [AnimationClipBytesTest] 꾸민 개수 · 길이는 할당 전에, NaN 은 칸에서 거절한다. 남는 바이트도 틀린 파일이다 */
SW_TEST_CASE( AnimationClipBytesTest, CraftedCountsAndNonFiniteValuesAreRefused )
{
    SW_EXPECT_LOG( "AnimationClip.AnimationClipMalformed" );
    XTestUtil::ClipOffsets offsets{};
    const vector<uint8>    bytes = XTestUtil::makeClipBytes( &offsets );

    for ( const size_t offset : { offsets._trackCount, offsets._firstTrackNameLength, offsets._codecByteSize, offsets._notifyCount, offsets._curveCount, offsets._keyCount } )
    {
        vector<uint8> crafted = bytes;
        XTestUtil::patch( crafted, offset, 0xFFFFFFFFu );
        XTestUtil::expectClipRefused( crafted );
    }
    for ( const size_t offset : { offsets._duration, offsets._keyValue } )
    {
        vector<uint8> crafted = bytes;
        XTestUtil::patch( crafted, offset, 0x7FC00000u );
        XTestUtil::expectClipRefused( crafted );
    }
    vector<uint8> longer = bytes;
    longer.push_back( 0 );
    XTestUtil::expectClipRefused( longer );
}

/** @brief [RawAnimationCodecBytesTest] 샘플 수 × 트랙 수 × 40 이 넘쳐 길이가 맞아 보이는 머리와 NaN 샘플율은 표본을 읽기 전에 거절한다 */
SW_TEST_CASE( RawAnimationCodecBytesTest, WrappedLengthAndNonFiniteRateAreRefused )
{
    const RawAnimationCodec& codec = RawAnimationCodec::getInstance();
    Pose                     pose;

    vector<uint8> good = XTestUtil::makeRawBytes( 1, 30.0f, 1, 40 );
    // 표본 하나: 이동 3 · 회전 4 · 크기 3. 회전 w 와 크기는 1 이다.
    for ( const size_t offset : { size_t{ 36 }, size_t{ 40 }, size_t{ 44 }, size_t{ 48 } } )
    {
        XTestUtil::patch( good, offset, 0x3F800000u );
    }
    SW_EXPECT_TRUE( codec.sample( good.data(), good.size(), 0.0f, pose ) );

    // 2^31 × 2^31 × 40 = 2^64 × 10 → 그대로 곱하면 0 이라 머리만 있는 바이트가 맞아 보이고 표본 자리가 버퍼 밖이 된다.
    const vector<uint8> wrapped = XTestUtil::makeRawBytes( 0x80000000u, 30.0f, 0x80000000u, 0 );
    SW_EXPECT_FALSE( codec.sample( wrapped.data(), wrapped.size(), 0.0f, pose ) );

    const vector<uint8> mismatched = XTestUtil::makeRawBytes( 2, 30.0f, 1, 40 );
    SW_EXPECT_FALSE( codec.sample( mismatched.data(), mismatched.size(), 0.0f, pose ) );

    vector<uint8> nanRate = good;
    XTestUtil::patch( nanRate, sizeof( uint32 ), 0x7FC00000u );
    SW_EXPECT_FALSE( codec.sample( nanRate.data(), nanRate.size(), 0.0f, pose ) );
}

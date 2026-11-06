#include "pch.h"

#include "Core/Memory/Memory.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Graphics/Mesh/MeshVertexAnimation.h"

#include <limits>

using namespace sw;

namespace
{
    /** @brief x 가 프레임 번호인 정점 하나짜리 표와 쿠킹본 머리 칸을 고치는 도우미입니다. */
    struct MeshVertexAnimationTestUtil
    {
        static constexpr size_t  kOffsetVersion     = 4;
        static constexpr size_t  kOffsetFrameRate   = 16;
        static constexpr size_t  kHeaderSize        = 52;
        static constexpr float32 kPositionTolerance = 1.0e-3f;

        /** @brief 프레임 4 개, 정점 1 개, 초당 2 프레임입니다. 프레임 f 의 위치는 (f, 3, -1) 입니다. */
        static MeshVertexAnimation makeRamp( uint8 bLoop )
        {
            MeshVertexAnimation animation;
            animation._frameCount      = 4;
            animation._vertexCount     = 1;
            animation._framesPerSecond = 2.0f;
            animation._duration        = 2.0f;
            animation._bLoop           = bLoop;
            vector<float3> listPosition;
            vector<float3> listNormal;
            for ( uint32 frame = 0; frame < animation._frameCount; ++frame )
            {
                listPosition.push_back( float3{ static_cast<float32>( frame ), 3.0f, -1.0f } );
                listNormal.push_back( float3::UnitZ );
            }
            const bool bQuantized = animation.quantize( listPosition, listNormal );
            SW_EXPECT_TRUE( bQuantized );
            return animation;
        }

        template <typename T>
        static void patch( vector<uint8>& inoutBytes, size_t offset, const T& value )
        {
            Memory::copy( inoutBytes.data() + offset, &value, sizeof( T ) );
        }

        /** @brief 표 없이 머리만 쓴 쿠킹본입니다. 프레임 · 정점 수는 표와 맞지 않아도 됩니다. */
        static vector<uint8> makeHeaderOnlyBytes( uint32 frameCount, uint32 vertexCount )
        {
            MeshVertexAnimation animation;
            animation._frameCount  = frameCount;
            animation._vertexCount = vertexCount;
            vector<uint8> bytes;
            animation.makeBytes( bytes );
            return bytes;
        }

        static void expectNear( const float3& expected, const float3& actual, float32 tolerance )
        {
            SW_EXPECT_NEAR( expected._x, actual._x, tolerance );
            SW_EXPECT_NEAR( expected._y, actual._y, tolerance );
            SW_EXPECT_NEAR( expected._z, actual._z, tolerance );
        }
    };
} // namespace

/**
 * @brief [MeshVertexAnimationTest] 양자화는 모든 프레임의 경계로 정규화하고, 크기가 0 인 축은 0 으로 나누지 않고 중심으로 풀린다
 */
SW_TEST_CASE( MeshVertexAnimationTest, QuantizeUsesAllFrameBoundsAndKeepsFlatAxes )
{
    using Util                          = MeshVertexAnimationTestUtil;
    const MeshVertexAnimation animation = Util::makeRamp( SW_TRUE );
    SW_REQUIRE_EQUAL( 4u, static_cast<uint32>( animation._listFrameVertex.size() ) );
    Util::expectNear( float3{ 1.5f, 3.0f, -1.0f }, animation._boundsCenter, 1.0e-6f );
    Util::expectNear( float3{ 1.5f, 0.0f, 0.0f }, animation._boundsHalfExtent, 1.0e-6f );
    for ( uint32 frame = 0; frame < 4u; ++frame )
    {
        const MeshVertexAnimationElement& element = animation._listFrameVertex[frame];
        SW_EXPECT_EQUAL( 0, static_cast<int32>( element._y ) );
        SW_EXPECT_EQUAL( 0, static_cast<int32>( element._z ) );
        Util::expectNear( float3{ static_cast<float32>( frame ), 3.0f, -1.0f }, animation.unpackPosition( element ), Util::kPositionTolerance );
    }
    // 경계의 양 끝은 snorm16 의 양 끝이다(최솟값 -32768 은 만들지 않는다).
    SW_EXPECT_EQUAL( -32767, static_cast<int32>( animation._listFrameVertex[0]._x ) );
    SW_EXPECT_EQUAL( 32767, static_cast<int32>( animation._listFrameVertex[3]._x ) );
}

/**
 * @brief [MeshVertexAnimationTest] 길이가 틀리거나 표가 빈 양자화는 오류이고, 비워서 샘플링이 빈 표를 읽지 않는다(결함: 프레임 수가 남아 빈 표를 읽었다)
 */
SW_TEST_CASE( MeshVertexAnimationTest, QuantizeFailureLeavesAnimationEmpty )
{
    SW_EXPECT_LOG( "MeshVertexAnimation.VertexAnimationQuantizeExpected" );
    MeshVertexAnimation animation;
    animation._frameCount  = 2;
    animation._vertexCount = 2;
    const vector<float3> listTooShort( 3, float3::UnitX );
    SW_EXPECT_FALSE( animation.quantize( listTooShort, listTooShort ) );
    SW_EXPECT_TRUE( animation.isEmpty() );
    SW_EXPECT_TRUE( animation._listFrameVertex.empty() );
    SW_EXPECT_TRUE( animation.samplePosition( 0.0f, 0 ) == float3::Zero );

    // 위치와 법선의 길이가 서로 달라도 틀리다.
    MeshVertexAnimation mismatched;
    mismatched._frameCount  = 1;
    mismatched._vertexCount = 2;
    const vector<float3> listPosition( 2, float3::UnitX );
    const vector<float3> listNormal( 1, float3::UnitX );
    SW_EXPECT_FALSE( mismatched.quantize( listPosition, listNormal ) );

    // 프레임이 0 이면 원소가 없어 경계를 낼 수 없다.
    MeshVertexAnimation  noFrames;
    const vector<float3> listEmpty;
    SW_EXPECT_FALSE( noFrames.quantize( listEmpty, listEmpty ) );
}

/**
 * @brief [MeshVertexAnimationTest] 법선은 옥타헤드럴 snorm8 로 왕복한다. 길이가 1 이 아닌 입력도 방향만 쓴다
 */
SW_TEST_CASE( MeshVertexAnimationTest, NormalRoundTripsThroughOctahedral )
{
    using Util                  = MeshVertexAnimationTestUtil;
    const float3 arrDirection[] = {
        float3::UnitX,
        -float3::UnitX,
        float3::UnitY,
        -float3::UnitY,
        float3::UnitZ,
        -float3::UnitZ,
        float3{1.0f,  1.0f,  1.0f},
        float3{1.0f, -1.0f, -1.0f}
    };
    const uint32        directionCount = static_cast<uint32>( std::size( arrDirection ) );
    MeshVertexAnimation animation;
    animation._frameCount  = 1;
    animation._vertexCount = directionCount;
    vector<float3> listPosition( directionCount, float3::Zero );
    vector<float3> listNormal;
    for ( const float3& direction : arrDirection )
    {
        listNormal.push_back( direction * 2.0f );
    }
    SW_REQUIRE_TRUE( animation.quantize( listPosition, listNormal ) );
    for ( uint32 index = 0; index < directionCount; ++index )
    {
        float3 expected = arrDirection[index];
        expected.normalize();
        Util::expectNear( expected, MeshVertexAnimation::unpackNormal( animation._listFrameVertex[index] ), 0.03f );
    }
}

/**
 * @brief [MeshVertexAnimationTest] 반복 클립은 마지막 프레임 뒤에 첫 프레임으로 보간해 감기고, 음수 시각은 0 이다
 */
SW_TEST_CASE( MeshVertexAnimationTest, SampleLoopWrapsToFirstFrame )
{
    using Util                          = MeshVertexAnimationTestUtil;
    const MeshVertexAnimation animation = Util::makeRamp( SW_TRUE );
    SW_EXPECT_NEAR( 0.0f, animation.samplePosition( -1.0f, 0 )._x, Util::kPositionTolerance );
    SW_EXPECT_NEAR( 0.5f, animation.samplePosition( 0.25f, 0 )._x, Util::kPositionTolerance ); // 프레임 0.5
    SW_EXPECT_NEAR( 3.0f, animation.samplePosition( 1.5f, 0 )._x, Util::kPositionTolerance );  // 프레임 3
    SW_EXPECT_NEAR( 1.5f, animation.samplePosition( 1.75f, 0 )._x, Util::kPositionTolerance ); // 프레임 3.5: 3 과 첫 프레임 0 의 가운데
    SW_EXPECT_NEAR( 0.0f, animation.samplePosition( 2.0f, 0 )._x, Util::kPositionTolerance );  // 프레임 4 = 프레임 0
    SW_EXPECT_NEAR( 1.0f, animation.samplePosition( 2.5f, 0 )._x, Util::kPositionTolerance );  // 프레임 5 = 프레임 1
    SW_EXPECT_NEAR( 3.0f, animation.samplePosition( 1.0f, 0 )._y, Util::kPositionTolerance );  // 크기 0 인 축은 보간해도 그대로다
}

/**
 * @brief [MeshVertexAnimationTest] 반복하지 않는 클립은 마지막 프레임에 멈추고, 프레임 하나짜리 표도 그 프레임을 낸다
 */
SW_TEST_CASE( MeshVertexAnimationTest, SampleClampsWithoutLoop )
{
    using Util                          = MeshVertexAnimationTestUtil;
    const MeshVertexAnimation animation = Util::makeRamp( SW_FALSE );
    SW_EXPECT_NEAR( 2.5f, animation.samplePosition( 1.25f, 0 )._x, Util::kPositionTolerance );
    SW_EXPECT_NEAR( 3.0f, animation.samplePosition( 1.75f, 0 )._x, Util::kPositionTolerance );
    SW_EXPECT_NEAR( 3.0f, animation.samplePosition( 100.0f, 0 )._x, Util::kPositionTolerance );

    MeshVertexAnimation single;
    single._frameCount      = 1;
    single._vertexCount     = 1;
    single._framesPerSecond = 10.0f;
    const vector<float3> listPosition( 1, float3{ 7.0f, 8.0f, 9.0f } );
    const vector<float3> listNormal( 1, float3::UnitY );
    SW_REQUIRE_TRUE( single.quantize( listPosition, listNormal ) );
    for ( const float32 time : { 0.0f, 0.03f, 5.0f } )
    {
        Util::expectNear( float3{ 7.0f, 8.0f, 9.0f }, single.samplePosition( time, 0 ), Util::kPositionTolerance );
    }
}

/**
 * @brief [MeshVertexAnimationTest] 빈 표나 범위 밖 정점의 샘플은 영벡터다
 */
SW_TEST_CASE( MeshVertexAnimationTest, SampleOutOfRangeIsZero )
{
    using Util                          = MeshVertexAnimationTestUtil;
    const MeshVertexAnimation animation = Util::makeRamp( SW_TRUE );
    SW_EXPECT_TRUE( animation.samplePosition( 0.5f, 1 ) == float3::Zero );
    const MeshVertexAnimation empty{};
    SW_EXPECT_TRUE( empty.isEmpty() );
    SW_EXPECT_TRUE( empty.samplePosition( 0.5f, 0 ) == float3::Zero );
}

/**
 * @brief [MeshVertexAnimationTest] 쿠킹본은 머리 52 바이트 + 원소 표이고, 플래그 · 경계 · 원소가 그대로 왕복한다
 */
SW_TEST_CASE( MeshVertexAnimationTest, BytesRoundTripEveryField )
{
    using Util                    = MeshVertexAnimationTestUtil;
    MeshVertexAnimation animation = Util::makeRamp( SW_FALSE );
    animation._bAnchorRootMotion  = SW_TRUE;
    vector<uint8> bytes;
    animation.makeBytes( bytes );
    SW_REQUIRE_EQUAL( Util::kHeaderSize + 4u * sizeof( MeshVertexAnimationElement ), bytes.size() );
    SW_EXPECT_EQUAL( static_cast<uint32>( 'S' ), static_cast<uint32>( bytes[0] ) );
    SW_EXPECT_EQUAL( static_cast<uint32>( 'A' ), static_cast<uint32>( bytes[3] ) );

    MeshVertexAnimation read;
    SW_REQUIRE_TRUE( read.readFromBytes( bytes.data(), bytes.size(), "test" ) );
    SW_EXPECT_EQUAL( animation._frameCount, read._frameCount );
    SW_EXPECT_EQUAL( animation._vertexCount, read._vertexCount );
    SW_EXPECT_NEAR( animation._framesPerSecond, read._framesPerSecond, 1.0e-6f );
    SW_EXPECT_NEAR( animation._duration, read._duration, 1.0e-6f );
    SW_EXPECT_EQUAL( static_cast<uint32>( SW_FALSE ), static_cast<uint32>( read._bLoop ) );
    SW_EXPECT_EQUAL( static_cast<uint32>( SW_TRUE ), static_cast<uint32>( read._bAnchorRootMotion ) );
    SW_EXPECT_TRUE( read._boundsCenter == animation._boundsCenter );
    SW_EXPECT_TRUE( read._boundsHalfExtent == animation._boundsHalfExtent );
    SW_REQUIRE_EQUAL( animation._listFrameVertex.size(), read._listFrameVertex.size() );
    for ( size_t index = 0; index < read._listFrameVertex.size(); ++index )
    {
        SW_EXPECT_EQUAL( static_cast<int32>( animation._listFrameVertex[index]._x ), static_cast<int32>( read._listFrameVertex[index]._x ) );
        SW_EXPECT_EQUAL( static_cast<int32>( animation._listFrameVertex[index]._normalV ), static_cast<int32>( read._listFrameVertex[index]._normalV ) );
    }
}

/**
 * @brief [MeshVertexAnimationTest] 매직이 틀리거나 머리보다 짧거나 널이면 오류이고, 전에 들고 있던 표는 비운다
 */
SW_TEST_CASE( MeshVertexAnimationTest, ReadRejectsNonVertexAnimationBytes )
{
    SW_EXPECT_LOG( "MeshVertexAnimation.VertexAnimationNotSWVA" );
    using Util                    = MeshVertexAnimationTestUtil;
    MeshVertexAnimation animation = Util::makeRamp( SW_TRUE );
    vector<uint8>       bytes;
    animation.makeBytes( bytes );

    SW_EXPECT_FALSE( animation.readFromBytes( nullptr, 0, "null" ) );
    SW_EXPECT_TRUE( animation.isEmpty() );
    SW_EXPECT_TRUE( animation._listFrameVertex.empty() );

    MeshVertexAnimation read;
    SW_EXPECT_FALSE( read.readFromBytes( bytes.data(), Util::kHeaderSize - 1, "short" ) );
    vector<uint8> wrongMagic = bytes;
    wrongMagic[0]            = 'X';
    SW_EXPECT_FALSE( read.readFromBytes( wrongMagic.data(), wrongMagic.size(), "magic" ) );
}

/**
 * @brief [MeshVertexAnimationTest] 다른 버전은 다시 쿠킹하라는 오류다(지금 버전만 읽는다)
 */
SW_TEST_CASE( MeshVertexAnimationTest, ReadRejectsOtherVersion )
{
    SW_EXPECT_LOG( "MeshVertexAnimation.VertexAnimationVersionNot" );
    using Util = MeshVertexAnimationTestUtil;
    vector<uint8> bytes;
    Util::makeRamp( SW_TRUE ).makeBytes( bytes );
    Util::patch( bytes, Util::kOffsetVersion, MeshVertexAnimation::kVersion + 1u );
    MeshVertexAnimation read;
    SW_EXPECT_FALSE( read.readFromBytes( bytes.data(), bytes.size(), "version" ) );
    SW_EXPECT_TRUE( read.isEmpty() );
}

/**
 * @brief [MeshVertexAnimationTest] 프레임 · 정점이 0 이거나 표 길이가 틀리면 형식 오류다 — 표가 잘리거나 남아도 받지 않는다
 */
SW_TEST_CASE( MeshVertexAnimationTest, ReadRejectsMismatchedTable )
{
    SW_EXPECT_LOG( "MeshVertexAnimation.VertexAnimationMalformedFrames" );
    using Util = MeshVertexAnimationTestUtil;
    MeshVertexAnimation read;

    const vector<uint8> noFrames = Util::makeHeaderOnlyBytes( 0, 4 );
    SW_EXPECT_FALSE( read.readFromBytes( noFrames.data(), noFrames.size(), "frames" ) );
    const vector<uint8> noVertices = Util::makeHeaderOnlyBytes( 4, 0 );
    SW_EXPECT_FALSE( read.readFromBytes( noVertices.data(), noVertices.size(), "vertices" ) );

    vector<uint8> bytes;
    Util::makeRamp( SW_TRUE ).makeBytes( bytes );
    SW_EXPECT_FALSE( read.readFromBytes( bytes.data(), bytes.size() - sizeof( MeshVertexAnimationElement ), "truncated" ) );
    SW_EXPECT_FALSE( read.readFromBytes( bytes.data(), bytes.size() - 1, "partial element" ) );
    vector<uint8> longer = bytes;
    longer.resize( bytes.size() + sizeof( MeshVertexAnimationElement ), 0 );
    SW_EXPECT_FALSE( read.readFromBytes( longer.data(), longer.size(), "longer" ) );
    SW_EXPECT_TRUE( read.isEmpty() );
}

/**
 * @brief [MeshVertexAnimationTest] 프레임 × 정점 × 원소 크기가 64 비트를 넘는 머리는 형식 오류다(결함: 길이 비교가 넘쳐 머리만 있는 파일이 통과해 거대한 표를 잡았다)
 */
SW_TEST_CASE( MeshVertexAnimationTest, ReadRejectsTableLengthOverflow )
{
    SW_EXPECT_LOG( "MeshVertexAnimation.VertexAnimationMalformedFrames" );
    using Util = MeshVertexAnimationTestUtil;
    // 2^30 × 2^31 × 8 = 2^64 라 곱하면 0 이 되어 표 길이 0 과 같아진다.
    const vector<uint8> bytes = Util::makeHeaderOnlyBytes( 1u << 30, 1u << 31 );
    SW_REQUIRE_EQUAL( Util::kHeaderSize, bytes.size() );
    MeshVertexAnimation read;
    SW_EXPECT_FALSE( read.readFromBytes( bytes.data(), bytes.size(), "overflow" ) );
    SW_EXPECT_TRUE( read.isEmpty() );
}

/**
 * @brief [MeshVertexAnimationTest] 프레임율은 유한한 양수여야 한다(결함: NaN 이 `<= 0` 검사를 지나 샘플링에서 미정의 정수 변환이 되었다)
 */
SW_TEST_CASE( MeshVertexAnimationTest, ReadRejectsBadFrameRate )
{
    SW_EXPECT_LOG( "MeshVertexAnimation.VertexAnimationMalformedFrames" );
    using Util = MeshVertexAnimationTestUtil;
    vector<uint8> bytes;
    Util::makeRamp( SW_TRUE ).makeBytes( bytes );
    const float32 arrBadRate[] = { 0.0f, -15.0f, std::numeric_limits<float32>::quiet_NaN(), std::numeric_limits<float32>::infinity() };
    for ( const float32 badRate : arrBadRate )
    {
        vector<uint8> patched = bytes;
        Util::patch( patched, Util::kOffsetFrameRate, badRate );
        MeshVertexAnimation read;
        SW_EXPECT_FALSE( read.readFromBytes( patched.data(), patched.size(), "rate" ) );
        SW_EXPECT_TRUE( read.isEmpty() );
    }
}

/** @brief [MeshVertexAnimationTest] 어느 길이로 잘려도, 경계에 NaN 이 있어도 읽지 않고 비운다 */
SW_TEST_CASE( MeshVertexAnimationTest, EveryTruncatedLengthAndNonFiniteBoundsAreRejected )
{
    SW_EXPECT_LOG( "MeshVertexAnimation.VertexAnimationNotSWVA" );
    SW_EXPECT_LOG( "MeshVertexAnimation.VertexAnimationMalformedFrames" );
    vector<uint8> bytes;
    MeshVertexAnimationTestUtil::makeRamp( SW_FALSE ).makeBytes( bytes );
    for ( size_t length = 0; length < bytes.size(); ++length )
    {
        MeshVertexAnimation read;
        SW_EXPECT_FALSE( read.readFromBytes( bytes.data(), length, "truncated" ) );
        SW_EXPECT_TRUE( read.isEmpty() );
    }
    // 머리의 경계 중심(28) · 반 크기(40)는 그대로 샘플 위치가 된다.
    for ( const size_t offset : { size_t{ 28 }, size_t{ 40 } } )
    {
        vector<uint8> patched = bytes;
        MeshVertexAnimationTestUtil::patch( patched, offset, std::numeric_limits<float32>::quiet_NaN() );
        MeshVertexAnimation read;
        SW_EXPECT_FALSE( read.readFromBytes( patched.data(), patched.size(), "bounds" ) );
        SW_EXPECT_TRUE( read.isEmpty() );
    }
}

/**
 * @brief [MeshVertexAnimationTest] 쿠킹본 경로는 메시 확장자를 클립 소문자 + `.vat` 로 바꾼다. `.mesh` 가 아니거나 클립 이름이 없으면 빈 문자열이다
 */
SW_TEST_CASE( MeshVertexAnimationTest, MakeCookedPathRules )
{
    SW_EXPECT_TRUE( MeshVertexAnimation::makeCookedPath( "a/knight.mesh", hashed_string( "Idle" ) ) == "a/knight.idle.vat" );
    SW_EXPECT_TRUE( MeshVertexAnimation::makeCookedPath( "A/KNIGHT.MESH", hashed_string( "Run" ) ) == "A/KNIGHT.run.vat" );
    SW_EXPECT_TRUE( MeshVertexAnimation::makeCookedPath( "a/knight.fbx", hashed_string( "Idle" ) ).empty() );
    SW_EXPECT_TRUE( MeshVertexAnimation::makeCookedPath( "", hashed_string( "Idle" ) ).empty() );
    SW_EXPECT_TRUE( MeshVertexAnimation::makeCookedPath( "a/knight.mesh", hashed_string{} ).empty() );
}

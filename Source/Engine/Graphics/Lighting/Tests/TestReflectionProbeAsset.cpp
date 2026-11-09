#include "pch.h"

#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/Path/AbsolutePath.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Graphics/Lighting/ReflectionProbeAsset.h"

using namespace sw;

namespace
{
    struct ReflectionProbeAssetTestUtil
    {
        // 바이트 자리: 매직(0) · 버전(4) · 찍은 위치(8) · 한 변(20) · 단계 수(24) · 텍셀(28).
        static constexpr size_t kVersionOffset    = 4;
        static constexpr size_t kResolutionOffset = 20;
        static constexpr size_t kLevelCountOffset = 24;
        static constexpr size_t kHeaderBytes      = 28;

        /** @brief 텍셀마다 값이 다른 가장 작은 프로브입니다(R = 번호). */
        static ReflectionProbeAsset makeAsset()
        {
            ReflectionProbeAsset asset;
            asset._capturePosition  = float3{ 1.0f, 2.0f, 3.0f };
            asset._resolution       = ReflectionProbeAsset::kMinResolution;
            asset._levelCount       = ReflectionProbeAsset::kMinLevelCount;
            const size_t texelCount = static_cast<size_t>( asset._resolution ) * asset._resolution * asset._levelCount;
            for ( size_t texelIndex = 0; texelIndex < texelCount; ++texelIndex )
            {
                asset._listTexel.push_back( float3{ static_cast<float32>( texelIndex ), 0.5f, -0.5f } );
            }
            return asset;
        }

        static vector<uint8> makeBytes()
        {
            vector<uint8> listByte;
            makeAsset().makeBytes( listByte );
            return listByte;
        }

        static void patchUint32( vector<uint8>& inoutBytes, size_t offset, uint32 value ) { Memory::copy( inoutBytes.data() + offset, &value, sizeof( value ) ); }

        /** @brief 잘못된 바이트를 읽으면 false · 이유 · 빈 내용이어야 합니다. 앞서 든 내용이 남으면 안 됩니다. */
        static void expectRejected( const vector<uint8>& listByte )
        {
            ReflectionProbeAsset asset = makeAsset();
            SW_EXPECT_FALSE( asset.readFromBytes( listByte.data(), listByte.size() ) );
            SW_EXPECT_EQUAL( 0u, asset._resolution );
            SW_EXPECT_EQUAL( 0u, asset._levelCount );
            SW_EXPECT_TRUE( asset._listTexel.empty() );
        }

        /** @brief 축 방향 여섯(면마다 제 방향을 보고 한 색)의 면입니다. 면 행렬은 단위 행렬이고, 면이 한 색이라 어느 픽셀을 읽어도 같습니다. */
        static void makeFaces( ReflectionProbeFace ( &outArrFace )[ReflectionProbeAsset::kFaceCount], const float3 ( &arrColor )[ReflectionProbeAsset::kFaceCount] )
        {
            const float3 arrForward[ReflectionProbeAsset::kFaceCount] = {
                float3{ 1.0f,  0.0f,  0.0f},
                float3{-1.0f,  0.0f,  0.0f},
                float3{ 0.0f,  1.0f,  0.0f},
                float3{ 0.0f, -1.0f,  0.0f},
                float3{ 0.0f,  0.0f,  1.0f},
                float3{ 0.0f,  0.0f, -1.0f},
            };
            for ( uint32 faceIndex = 0; faceIndex < ReflectionProbeAsset::kFaceCount; ++faceIndex )
            {
                ReflectionProbeFace& face = outArrFace[faceIndex];
                face._size                = 2;
                face._forward             = arrForward[faceIndex];
                face._listPixel.assign( 4, arrColor[faceIndex] );
            }
        }

        static bool isNear( const float3& lhs, const float3& rhs, float32 tolerance )
        {
            return MathUtil::abs( lhs._x - rhs._x ) <= tolerance && MathUtil::abs( lhs._y - rhs._y ) <= tolerance && MathUtil::abs( lhs._z - rhs._z ) <= tolerance;
        }
    };
    using Util = ReflectionProbeAssetTestUtil;
} // namespace

/** @brief [ReflectionProbeAssetTest] 쓴 바이트를 그대로 읽고, 길이는 머리말 + 텍셀마다 12 바이트이며, 파일 앞은 글자 그대로 SWRP 다 */
SW_TEST_CASE( ReflectionProbeAssetTest, BytesRoundTrip )
{
    const ReflectionProbeAsset source   = Util::makeAsset();
    const vector<uint8>        listByte = Util::makeBytes();
    SW_REQUIRE_EQUAL( Util::kHeaderBytes + source._listTexel.size() * 12u, listByte.size() );
    SW_EXPECT_EQUAL( static_cast<uint32>( 'S' ), static_cast<uint32>( listByte[0] ) );
    SW_EXPECT_EQUAL( static_cast<uint32>( 'P' ), static_cast<uint32>( listByte[3] ) );

    ReflectionProbeAsset asset;
    SW_REQUIRE_TRUE( asset.readFromBytes( listByte.data(), listByte.size() ) );
    SW_EXPECT_TRUE( asset.validate() );
    SW_EXPECT_NEAR( 2.0f, asset._capturePosition._y, 0.0f );
    SW_EXPECT_EQUAL( source._resolution, asset._resolution );
    SW_EXPECT_EQUAL( source._levelCount, asset._levelCount );
    SW_REQUIRE_EQUAL( source._listTexel.size(), asset._listTexel.size() );
    SW_EXPECT_NEAR( 31.0f, asset._listTexel[31]._x, 0.0f );
    SW_EXPECT_NEAR( -0.5f, asset._listTexel[31]._z, 0.0f );
}

/** @brief [ReflectionProbeAssetTest] 파일 형식은 신뢰 경계다 — 어느 길이로 잘려도 false 이고 내용을 비운다 */
SW_TEST_CASE( ReflectionProbeAssetTest, EveryTruncatedLengthIsRejected )
{
    const vector<uint8> listByte = Util::makeBytes();
    for ( size_t length = 0; length < listByte.size(); ++length )
    {
        Util::expectRejected( vector<uint8>( listByte.begin(), listByte.begin() + static_cast<ptrdiff_t>( length ) ) );
    }
}

/** @brief [ReflectionProbeAssetTest] 텍셀 뒤에 남는 바이트도 틀린 길이다 */
SW_TEST_CASE( ReflectionProbeAssetTest, TrailingBytesAreRejected )
{
    vector<uint8> listByte = Util::makeBytes();
    listByte.push_back( 0 );
    Util::expectRejected( listByte );
}

/** @brief [ReflectionProbeAssetTest] 바이트 포인터가 없으면 오류다. 이유를 받지 않아도(nullptr) 된다 */
SW_TEST_CASE( ReflectionProbeAssetTest, NullBytesAreRejected )
{
    ReflectionProbeAsset asset = Util::makeAsset();
    SW_EXPECT_FALSE( asset.readFromBytes( nullptr, 1024 ) );
    SW_EXPECT_TRUE( asset._listTexel.empty() );
}

/** @brief [ReflectionProbeAssetTest] 매직 · 버전이 다르면 읽지 않는다 */
SW_TEST_CASE( ReflectionProbeAssetTest, WrongMagicOrVersionIsRejected )
{
    vector<uint8> listMagic = Util::makeBytes();
    listMagic[0]            = 'X';
    Util::expectRejected( listMagic );

    vector<uint8> listVersion = Util::makeBytes();
    Util::patchUint32( listVersion, Util::kVersionOffset, ReflectionProbeAsset::kVersion + 1u );
    Util::expectRejected( listVersion );
}

/** @brief [ReflectionProbeAssetTest] 범위 밖 크기는 길이를 맞춰도 받지 않는다 — 곱이 넘치는 큰 값도 할당 전에 거른다 */
SW_TEST_CASE( ReflectionProbeAssetTest, OutOfRangeSizesAreRejected )
{
    const uint32 arrBadResolution[] = { 0u, ReflectionProbeAsset::kMinResolution - 1u, ReflectionProbeAsset::kMaxResolution + 1u, 0xFFFFFFFFu };
    for ( const uint32 resolution : arrBadResolution )
    {
        vector<uint8> listByte = Util::makeBytes();
        Util::patchUint32( listByte, Util::kResolutionOffset, resolution );
        Util::expectRejected( listByte );
    }
    const uint32 arrBadLevel[] = { 0u, ReflectionProbeAsset::kMinLevelCount - 1u, ReflectionProbeAsset::kMaxLevelCount + 1u, 0xFFFFFFFFu };
    for ( const uint32 levelCount : arrBadLevel )
    {
        vector<uint8> listByte = Util::makeBytes();
        Util::patchUint32( listByte, Util::kLevelCountOffset, levelCount );
        Util::expectRejected( listByte );
    }
}

/** @brief [ReflectionProbeAssetTest] NaN · 무한대는 위치에서도 텍셀에서도 읽지 않는다 */
SW_TEST_CASE( ReflectionProbeAssetTest, NonFiniteValuesAreRejected )
{
    vector<uint8> listPosition = Util::makeBytes();
    Util::patchUint32( listPosition, 8, 0x7FC00000u );
    Util::expectRejected( listPosition );

    vector<uint8> listTexel = Util::makeBytes();
    Util::patchUint32( listTexel, Util::kHeaderBytes + 4, 0x7F800000u );
    Util::expectRejected( listTexel );
}

/** @brief [ReflectionProbeAssetTest] 머리말은 범위 안인데 길이가 그 크기와 다르면 읽지 않는다 */
SW_TEST_CASE( ReflectionProbeAssetTest, HeaderSizeMustMatchTheLength )
{
    vector<uint8> listByte = Util::makeBytes();
    Util::patchUint32( listByte, Util::kResolutionOffset, ReflectionProbeAsset::kMinResolution + 1u );
    Util::expectRejected( listByte );
}

/** @brief [ReflectionProbeAssetTest] 유효성은 범위의 양 끝을 받고 그 밖과 텍셀 수 어긋남을 이유와 함께 거절한다 */
SW_TEST_CASE( ReflectionProbeAssetTest, IsValidBoundaries )
{
    ReflectionProbeAsset asset = Util::makeAsset();
    SW_EXPECT_TRUE( asset.validate() );

    asset._listTexel.pop_back();
    SW_EXPECT_FALSE( asset.validate() );

    ReflectionProbeAsset big;
    big._resolution = ReflectionProbeAsset::kMaxResolution;
    big._levelCount = ReflectionProbeAsset::kMaxLevelCount;
    big._listTexel.resize( static_cast<size_t>( big._resolution ) * big._resolution * big._levelCount );
    SW_EXPECT_TRUE( big.validate() );
    big._resolution = ReflectionProbeAsset::kMaxResolution + 1u;
    SW_EXPECT_FALSE( big.validate() );
    big._resolution = ReflectionProbeAsset::kMaxResolution;
    big._levelCount = ReflectionProbeAsset::kMaxLevelCount + 1u;
    SW_EXPECT_FALSE( big.validate() );

    asset.clear();
    SW_EXPECT_FALSE( asset.validate() );
}

/** @brief [ReflectionProbeAssetTest] 단계의 거칠기는 l / (n − 1) 이다. 단계 0 은 거르지 않은 빛이다 */
SW_TEST_CASE( ReflectionProbeAssetTest, LevelRoughness )
{
    ReflectionProbeAsset asset;
    asset._levelCount = 5;
    SW_EXPECT_NEAR( 0.0f, asset.computeLevelRoughness( 0 ), 0.0f );
    SW_EXPECT_NEAR( 0.5f, asset.computeLevelRoughness( 2 ), 0.0f );
    SW_EXPECT_NEAR( 1.0f, asset.computeLevelRoughness( 4 ), 0.0f );
    asset._levelCount = 1;
    SW_EXPECT_NEAR( 0.0f, asset.computeLevelRoughness( 0 ), 0.0f );
}

/** @brief [ReflectionProbeAssetTest] 텍셀 번호는 단계 → 행 → 열 순서다 */
SW_TEST_CASE( ReflectionProbeAssetTest, TexelIndexOrder )
{
    ReflectionProbeAsset asset;
    asset._resolution = 4;
    asset._levelCount = 2;
    SW_EXPECT_EQUAL( 0u, asset.computeTexelIndex( 0, 0, 0 ) );
    SW_EXPECT_EQUAL( 3u, asset.computeTexelIndex( 0, 3, 0 ) );
    SW_EXPECT_EQUAL( 4u, asset.computeTexelIndex( 0, 0, 1 ) );
    SW_EXPECT_EQUAL( 16u, asset.computeTexelIndex( 1, 0, 0 ) );
    SW_EXPECT_EQUAL( 31u, asset.computeTexelIndex( 1, 3, 3 ) );
}

/** @brief [ReflectionProbeAssetTest] 옥타헤드럴은 +Y 가 가운데이고 -Y 가 귀퉁이다. 접고 펴면 방향이 돌아온다(아래 반구 · 축 위의 접힌 자리 포함). 식은 셰이더와 한 벌이다 */
SW_TEST_CASE( ReflectionProbeAssetTest, OctahedralRoundTrip )
{
    const float2 center = ReflectionProbeAsset::encodeOctahedral( float3{ 0.0f, 1.0f, 0.0f } );
    SW_EXPECT_NEAR( 0.5f, center._x, 0.0f );
    SW_EXPECT_NEAR( 0.5f, center._y, 0.0f );
    const float2 corner = ReflectionProbeAsset::encodeOctahedral( float3{ 0.0f, -1.0f, 0.0f } );
    SW_EXPECT_NEAR( 1.0f, corner._x, 0.0f ); // 0 은 양수로 친다(`signNotZero`)
    SW_EXPECT_NEAR( 1.0f, corner._y, 0.0f );

    const float3 arrDirection[] = {
        float3{ 1.0f,  0.0f,  0.0f},
        float3{-1.0f,  0.0f,  0.0f},
        float3{ 0.0f,  1.0f,  0.0f},
        float3{ 0.0f, -1.0f,  0.0f},
        float3{ 0.0f,  0.0f,  1.0f},
        float3{ 0.0f,  0.0f, -1.0f},
        float3{ 1.0f,  1.0f,  1.0f}
            .normalize(),
        float3{-1.0f,  1.0f, -1.0f}
            .normalize(),
        float3{ 1.0f, -1.0f,  1.0f}
            .normalize(),
        float3{-1.0f, -1.0f,  1.0f}
            .normalize(),
        float3{ 1.0f, -1.0f, -1.0f}
            .normalize(),
        float3{-1.0f, -1.0f, -1.0f}
            .normalize(),
        float3{ 0.3f, -0.9f,  0.0f}
            .normalize(),
        float3{ 0.0f, -0.9f, -0.3f}
            .normalize(),
    };
    for ( const float3& direction : arrDirection )
    {
        const float2 uv = ReflectionProbeAsset::encodeOctahedral( direction );
        SW_EXPECT_TRUE( 0.0f <= uv._x && uv._x <= 1.0f && 0.0f <= uv._y && uv._y <= 1.0f );
        SW_EXPECT_TRUE( Util::isNear( direction, ReflectionProbeAsset::decodeOctahedral( uv ), 0.0001f ) );
    }
}

/** @brief [ReflectionProbeAssetTest] 샘플은 그 단계의 텍셀만 읽고, 사각형 밖으로 접힌 이웃을 읽지 않는다(가장자리는 자른다 — 셰이더와 같다) */
SW_TEST_CASE( ReflectionProbeAssetTest, SampleLevelStaysInItsLevel )
{
    ReflectionProbeAsset asset;
    asset._resolution = 4;
    asset._levelCount = 2;
    asset._listTexel.assign( 32, float3{ 1.0f, 0.0f, 0.0f } );
    for ( uint32 texelIndex = 16; texelIndex < 32; ++texelIndex )
    {
        asset._listTexel[texelIndex] = float3{ 0.0f, 2.0f, 0.0f };
    }
    const float3 arrDirection[] = {
        float3{ 0.0f,  1.0f, 0.0f},
        float3{ 0.0f, -1.0f, 0.0f},
        float3{-1.0f,  0.0f, 0.0f},
        float3{ 0.0f,  0.0f, 1.0f},
    };
    for ( const float3& direction : arrDirection )
    {
        SW_EXPECT_TRUE( Util::isNear( float3{ 1.0f, 0.0f, 0.0f }, asset.sampleLevel( 0, direction ), 0.0001f ) );
        SW_EXPECT_TRUE( Util::isNear( float3{ 0.0f, 2.0f, 0.0f }, asset.sampleLevel( 1, direction ), 0.0001f ) );
    }

    // 쌍선형: +Y 는 사각형 가운데라 둘레 네 텍셀의 평균이다.
    asset._listTexel[asset.computeTexelIndex( 0, 1, 1 )] = float3{ 5.0f, 0.0f, 0.0f };
    SW_EXPECT_NEAR( 2.0f, asset.sampleLevel( 0, float3{ 0.0f, 1.0f, 0.0f } )._x, 0.0001f );
}

/** @brief [ReflectionProbeAssetTest] 저장은 없는 폴더를 만들고 같은 바이트를 쓴다 */
SW_TEST_CASE( ReflectionProbeAssetTest, SaveToFileCreatesTheFolder )
{
    const optional<AbsolutePath> absPath = AbsolutePath::tryMake( test::makeScratchPath( "nested/folder/probe.reflprobe" ) );
    SW_REQUIRE_TRUE( absPath.has_value() );
    SW_REQUIRE_TRUE( Util::makeAsset().saveToFile( *absPath ) );
    vector<uint8> listRead;
    SW_REQUIRE_TRUE( FileUtil::readFile( absPath->str(), listRead ) );
    SW_EXPECT_TRUE( listRead == Util::makeBytes() );
}

/** @brief [ReflectionProbeAssetTest] 필터는 틀린 입력을 이유와 함께 거절하고 비운다 — 범위 밖 크기는 텍셀을 잡기 전에 거른다(잡으면 이 테스트가 메모리에서 멈춘다) */
SW_TEST_CASE( ReflectionProbeAssetTest, FilterRejectsBadInput )
{
    ReflectionProbeFace arrFace[ReflectionProbeAsset::kFaceCount];
    const float3        arrColor[ReflectionProbeAsset::kFaceCount]{};
    Util::makeFaces( arrFace, arrColor );

    const float3         position{};
    ReflectionProbeAsset data = Util::makeAsset();
    SW_EXPECT_FALSE( ReflectionProbeAsset::makeFromFaces( position, nullptr, 4, 2, 8, data ) );
    SW_EXPECT_TRUE( data._listTexel.empty() );

    SW_EXPECT_FALSE( ReflectionProbeAsset::makeFromFaces( position, arrFace, 4, 2, 0, data ) );

    const uint32 arrBadResolution[] = { ReflectionProbeAsset::kMinResolution - 1u, ReflectionProbeAsset::kMaxResolution + 1u, 0xFFFFFFFFu };
    for ( const uint32 resolution : arrBadResolution )
    {
        SW_EXPECT_FALSE( ReflectionProbeAsset::makeFromFaces( position, arrFace, resolution, 2, 8, data ) );
        SW_EXPECT_EQUAL( 0u, data._resolution );
    }
    const uint32 arrBadLevel[] = { ReflectionProbeAsset::kMinLevelCount - 1u, ReflectionProbeAsset::kMaxLevelCount + 1u, 0xFFFFFFFFu };
    for ( const uint32 levelCount : arrBadLevel )
    {
        SW_EXPECT_FALSE( ReflectionProbeAsset::makeFromFaces( position, arrFace, 4, levelCount, 8, data ) );
        SW_EXPECT_EQUAL( 0u, data._levelCount );
    }

    arrFace[3]._listPixel.pop_back();
    SW_EXPECT_FALSE( ReflectionProbeAsset::makeFromFaces( position, arrFace, 4, 2, 8, data ) );

    arrFace[3]._listPixel.clear();
    arrFace[3]._size = 0;
    SW_EXPECT_FALSE( ReflectionProbeAsset::makeFromFaces( position, arrFace, 4, 2, 8, data ) );
}

/** @brief [ReflectionProbeAssetTest] 둘레가 한 색이면 어느 단계 어느 방향이나 그 색이다 — 거르기는 빛을 더하거나 잃지 않는다 */
SW_TEST_CASE( ReflectionProbeAssetTest, FilterKeepsAUniformEnvironment )
{
    const float3        color{ 0.25f, 0.5f, 2.0f };
    ReflectionProbeFace arrFace[ReflectionProbeAsset::kFaceCount];
    const float3        arrColor[ReflectionProbeAsset::kFaceCount] = { color, color, color, color, color, color };
    Util::makeFaces( arrFace, arrColor );

    ReflectionProbeAsset data;
    const float3         position{ 4.0f, 5.0f, 6.0f };
    SW_REQUIRE_TRUE( ReflectionProbeAsset::makeFromFaces( position, arrFace, 8, 3, 16, data ) );
    SW_EXPECT_TRUE( data.validate() );
    SW_EXPECT_TRUE( Util::isNear( position, data._capturePosition, 0.0f ) );
    uint32 wrongTexelCount = 0;
    for ( const float3& texel : data._listTexel )
    {
        wrongTexelCount += Util::isNear( color, texel, 0.001f ) ? 0u : 1u;
    }
    SW_EXPECT_EQUAL( 0u, wrongTexelCount );
}

/** @brief [ReflectionProbeAssetTest] 단계 0 의 각 방향은 그쪽을 보는 면의 빛이다 */
SW_TEST_CASE( ReflectionProbeAssetTest, FilterFoldsEachDirectionFromItsFace )
{
    ReflectionProbeFace arrFace[ReflectionProbeAsset::kFaceCount];
    const float3        arrColor[ReflectionProbeAsset::kFaceCount] = {
        float3{1.0f, 0.0f, 0.0f},
        float3{2.0f, 0.0f, 0.0f},
        float3{3.0f, 0.0f, 0.0f},
        float3{4.0f, 0.0f, 0.0f},
        float3{5.0f, 0.0f, 0.0f},
        float3{6.0f, 0.0f, 0.0f},
    };
    Util::makeFaces( arrFace, arrColor );

    ReflectionProbeAsset data;
    SW_REQUIRE_TRUE( ReflectionProbeAsset::makeFromFaces( float3{}, arrFace, 16, 2, 4, data ) );
    SW_EXPECT_NEAR( 1.0f, data.sampleLevel( 0, float3{ 1.0f, 0.0f, 0.0f } )._x, 0.001f );
    SW_EXPECT_NEAR( 2.0f, data.sampleLevel( 0, float3{ -1.0f, 0.0f, 0.0f } )._x, 0.001f );
    SW_EXPECT_NEAR( 3.0f, data.sampleLevel( 0, float3{ 0.0f, 1.0f, 0.0f } )._x, 0.001f );
    SW_EXPECT_NEAR( 4.0f, data.sampleLevel( 0, float3{ 0.0f, -1.0f, 0.0f } )._x, 0.001f );
    SW_EXPECT_NEAR( 5.0f, data.sampleLevel( 0, float3{ 0.0f, 0.0f, 1.0f } )._x, 0.001f );
    SW_EXPECT_NEAR( 6.0f, data.sampleLevel( 0, float3{ 0.0f, 0.0f, -1.0f } )._x, 0.001f );
}

/** @brief [ReflectionProbeAssetTest] 거칠수록 빛이 번진다 — 위만 밝은 둘레에서 마지막 단계의 위쪽은 1 보다 어둡고 0 보다 밝다 */
SW_TEST_CASE( ReflectionProbeAssetTest, RougherLevelsBlurTheLight )
{
    ReflectionProbeFace arrFace[ReflectionProbeAsset::kFaceCount];
    const float3        arrColor[ReflectionProbeAsset::kFaceCount] = {
        float3{},
        float3{},
        float3{ 1.0f, 1.0f, 1.0f },
        float3{},
        float3{},
        float3{},
    };
    Util::makeFaces( arrFace, arrColor );

    ReflectionProbeAsset data;
    SW_REQUIRE_TRUE( ReflectionProbeAsset::makeFromFaces( float3{}, arrFace, 16, 3, 64, data ) );
    const float3  up{ 0.0f, 1.0f, 0.0f };
    const float32 sharp = data.sampleLevel( 0, up )._x;
    const float32 rough = data.sampleLevel( 2, up )._x;
    SW_EXPECT_NEAR( 1.0f, sharp, 0.001f );
    SW_EXPECT_TRUE( 0.01f < rough && rough < 0.99f );
}

/** @brief [ReflectionProbeAssetTest] 시차 보정: 찍은 위치에 선 표면은 방향이 그대로이고, 벽 가까운 표면은 상자 벽의 그 점을 찍은 위치에서 보는 방향으로 읽는다 */
SW_TEST_CASE( ReflectionProbeAssetTest, ParallaxDirectionPointsAtTheBoxHit )
{
    const float3 center{ 0.0f, 0.0f, 0.0f };
    const float3 half{ 5.0f, 5.0f, 5.0f };
    const float3 up{ 0.0f, 1.0f, 0.0f };

    SW_EXPECT_TRUE( Util::isNear( up, ReflectionProbeAsset::computeParallaxDirection( center, up, center, half, center ), 0.0001f ) );

    // (4, 0, 0) 에서 위로 쏘면 (4, 5, 0) 에 닿는다 — 가운데에서 보면 위가 아니라 비스듬하다.
    const float3 expected = float3{ 4.0f, 5.0f, 0.0f }.normalize();
    SW_EXPECT_TRUE( Util::isNear( expected, ReflectionProbeAsset::computeParallaxDirection( float3{ 4.0f, 0.0f, 0.0f }, up, center, half, center ), 0.0001f ) );
}

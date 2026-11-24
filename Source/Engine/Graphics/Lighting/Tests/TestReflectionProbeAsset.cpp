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
        /// @brief 거르기 원본의 밉 수 상한 — 한 변 8 · 16 의 단계 0 에서 사슬이 끝까지 가지 않는 값이다.
        static constexpr uint32 kMipCount = 4;

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

    /**
     * @struct FilterReferenceTestUtil
     * @brief 거르기를 기준 적분과 견주는 도구입니다. 기준은 원본 밉 0 의 모든 텍셀을 더하는 느린 길이라 표본 · 밉 고르기가 없고 잡음이 없다.
     */
    struct FilterReferenceTestUtil
    {
        static constexpr uint32 kSourceResolution = 64;
        static constexpr uint32 kResolution       = 8;
        static constexpr uint32 kLevelCount       = 4;
        static constexpr uint32 kSampleCount      = 32;
        static constexpr uint32 kMipCount         = 6; ///< 64 → 2

        /** @brief 오차 요약입니다(상대 오차 |거른 값 − 기준| / 기준). */
        struct ErrorSummary
        {
            float32 _mean{ 0.0f };
            float32 _max{ 0.0f };
        };

        /** @brief 바탕 0.2 + 위 반구(y) + 해 쪽 엽(폭 @p lobeWidth — 1 − cos 단위, 세기 @p lobeIntensity)입니다. */
        static float32 evaluateEnvironment( const float3& direction, float32 lobeWidth, float32 lobeIntensity )
        {
            const float3 toSun = float3{ 0.8f, 0.5f, 0.3f }.normalize();
            return 0.2f + MathUtil::max( direction._y, 0.0f ) + lobeIntensity * MathUtil::exp( ( direction.dot( toSun ) - 1.0f ) / lobeWidth );
        }

        /** @brief 한 변 64 의 원본을 밉 @p mipCount 개까지 만듭니다. */
        static OctahedralRadianceMap makeSource( float32 lobeWidth, float32 lobeIntensity, uint32 mipCount )
        {
            vector<float3> listTexel;
            listTexel.reserve( static_cast<size_t>( kSourceResolution ) * kSourceResolution );
            for ( uint32 y = 0; y < kSourceResolution; ++y )
            {
                for ( uint32 x = 0; x < kSourceResolution; ++x )
                {
                    const float32 value = evaluateEnvironment( OctahedralRadianceMap::computeTexelDirection( kSourceResolution, x, y ), lobeWidth, lobeIntensity );
                    listTexel.push_back( float3{ value, value, value } );
                }
            }
            OctahedralRadianceMap source;
            SW_EXPECT_TRUE( source.assignBase( kSourceResolution, std::move( listTexel ) ) );
            SW_EXPECT_TRUE( source.buildMips( mipCount ) );
            return source;
        }

        /** @brief 기준 적분입니다 — 밉 0 의 텍셀마다 입체각(방향의 L1 길이³에 비례) × GGX D(H) × N·L 로 가중 평균한다(중요도 표본이 수렴하는 값, N = V = R). */
        static float32 integrateReference( const OctahedralRadianceMap& source, const float3& normal, float32 roughness )
        {
            const float32                     clamped      = MathUtil::max( roughness, 0.045f );
            const float32                     alpha        = clamped * clamped;
            const float32                     alphaSquared = alpha * alpha;
            const OctahedralRadianceMap::Mip& base         = source._listMip[0];
            float64                           sum          = 0.0;
            float64                           weightSum    = 0.0;
            for ( uint32 y = 0; y < base._resolution; ++y )
            {
                for ( uint32 x = 0; x < base._resolution; ++x )
                {
                    const float3  direction      = OctahedralRadianceMap::computeTexelDirection( base._resolution, x, y );
                    const float32 normalDotLight = normal.dot( direction );
                    if ( normalDotLight <= 0.0f )
                        continue;
                    const float3  halfway      = ( normal + direction ).normalize();
                    const float32 cosine       = normal.dot( halfway );
                    const float32 denominator  = cosine * cosine * ( alphaSquared - 1.0f ) + 1.0f;
                    const float32 distribution = alphaSquared / ( MathUtil::kPi * denominator * denominator );
                    const float32 axisSum      = MathUtil::abs( direction._x ) + MathUtil::abs( direction._y ) + MathUtil::abs( direction._z );
                    const float64 weight       = static_cast<float64>( normalDotLight * distribution * axisSum * axisSum * axisSum );
                    sum += static_cast<float64>( base._listTexel[static_cast<size_t>( y ) * base._resolution + x]._x ) * weight;
                    weightSum += weight;
                }
            }
            return static_cast<float32>( sum / weightSum );
        }

        /** @brief @p source 를 표본 @p sampleCount 개로 거른 단계 1 부터를 기준 적분과 견줍니다. */
        static ErrorSummary measure( const OctahedralRadianceMap& source, uint32 sampleCount )
        {
            ReflectionProbeAsset filtered;
            SW_EXPECT_TRUE( filtered.allocate( float3{}, kResolution, kLevelCount ) );
            SW_EXPECT_TRUE( filtered.filterRoughnessLevels( source, sampleCount ) );
            ErrorSummary summary;
            if ( filtered._listTexel.empty() )
            {
                summary._mean = 1.0e9f;
                summary._max  = 1.0e9f;
                return summary;
            }
            float64 errorSum   = 0.0;
            uint32  errorCount = 0;
            for ( uint32 level = 1; level < kLevelCount; ++level )
            {
                for ( uint32 y = 0; y < kResolution; ++y )
                {
                    for ( uint32 x = 0; x < kResolution; ++x )
                    {
                        const float32 reference = integrateReference( source, filtered.computeTexelDirection( x, y ), filtered.computeLevelRoughness( level ) );
                        const float32 value     = filtered._listTexel[filtered.computeTexelIndex( level, x, y )]._x;
                        const float32 error     = MathUtil::abs( value - reference ) / reference;
                        errorSum += static_cast<float64>( error );
                        ++errorCount;
                        summary._max = MathUtil::max( summary._max, error );
                    }
                }
            }
            summary._mean = static_cast<float32>( errorSum / static_cast<float64>( errorCount ) );
            return summary;
        }
    };
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
    const optional<AbsolutePath> absPath = AbsolutePath::tryMake( test::makeScratchPath( "nested/folder/probe.reflprobe" ).str() );
    SW_REQUIRE_TRUE( absPath.has_value() );
    SW_REQUIRE_TRUE( Util::makeAsset().saveToFile( *absPath ) );
    vector<uint8> listRead;
    SW_REQUIRE_TRUE( FileUtil::readFile( *absPath, listRead ) );
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
    SW_EXPECT_FALSE( ReflectionProbeAsset::makeFromFaces( position, nullptr, 4, 2, 8, Util::kMipCount, data ) );
    SW_EXPECT_TRUE( data._listTexel.empty() );

    SW_EXPECT_FALSE( ReflectionProbeAsset::makeFromFaces( position, arrFace, 4, 2, 0, Util::kMipCount, data ) );
    SW_EXPECT_FALSE( ReflectionProbeAsset::makeFromFaces( position, arrFace, 4, 2, 8, 0, data ) );

    const uint32 arrBadResolution[] = { ReflectionProbeAsset::kMinResolution - 1u, ReflectionProbeAsset::kMaxResolution + 1u, 0xFFFFFFFFu };
    for ( const uint32 resolution : arrBadResolution )
    {
        SW_EXPECT_FALSE( ReflectionProbeAsset::makeFromFaces( position, arrFace, resolution, 2, 8, Util::kMipCount, data ) );
        SW_EXPECT_EQUAL( 0u, data._resolution );
    }
    const uint32 arrBadLevel[] = { ReflectionProbeAsset::kMinLevelCount - 1u, ReflectionProbeAsset::kMaxLevelCount + 1u, 0xFFFFFFFFu };
    for ( const uint32 levelCount : arrBadLevel )
    {
        SW_EXPECT_FALSE( ReflectionProbeAsset::makeFromFaces( position, arrFace, 4, levelCount, 8, Util::kMipCount, data ) );
        SW_EXPECT_EQUAL( 0u, data._levelCount );
    }

    arrFace[3]._listPixel.pop_back();
    SW_EXPECT_FALSE( ReflectionProbeAsset::makeFromFaces( position, arrFace, 4, 2, 8, Util::kMipCount, data ) );

    arrFace[3]._listPixel.clear();
    arrFace[3]._size = 0;
    SW_EXPECT_FALSE( ReflectionProbeAsset::makeFromFaces( position, arrFace, 4, 2, 8, Util::kMipCount, data ) );
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
    SW_REQUIRE_TRUE( ReflectionProbeAsset::makeFromFaces( position, arrFace, 8, 3, 16, Util::kMipCount, data ) );
    SW_EXPECT_TRUE( data.validate() );
    SW_EXPECT_TRUE( Util::isNear( position, data._capturePosition, 0.0f ) );
    uint32 wrongTexelCount = 0;
    for ( const float3& texel : data._listTexel )
    {
        wrongTexelCount += Util::isNear( color, texel, 0.001f ) ? 0u : 1u;
    }
    SW_EXPECT_EQUAL( 0u, wrongTexelCount );
}

/** @brief [ReflectionProbeAssetTest] 면 없이 단계 0 을 직접 채운 데이터도 같은 거르기를 탄다(스카이 라이트의 길) — 위가 밝은 둘레는 거친 단계에서도 위가 아래보다 밝고, 틀린 입력은 거절한다 */
SW_TEST_CASE( ReflectionProbeAssetTest, FilterRoughnessLevelsFromAFilledBaseLevel )
{
    ReflectionProbeAsset data = Util::makeAsset();
    SW_EXPECT_FALSE( data.allocate( float3{}, ReflectionProbeAsset::kMaxResolution + 1u, 3 ) );
    SW_EXPECT_TRUE( data._listTexel.empty() );
    SW_EXPECT_FALSE( data.filterRoughnessLevels( 8, Util::kMipCount ) ); // 빈 데이터

    SW_REQUIRE_TRUE( data.allocate( float3{}, 8, 3 ) );
    for ( uint32 y = 0; y < data._resolution; ++y )
    {
        for ( uint32 x = 0; x < data._resolution; ++x )
        {
            // 텍셀 방향은 길이 1 이고 그 방향을 다시 접으면 같은 텍셀의 중심이다.
            const float3 direction = data.computeTexelDirection( x, y );
            SW_EXPECT_NEAR( 1.0f, direction.getLength(), 1.0e-4f );
            const float2 uv = ReflectionProbeAsset::encodeOctahedral( direction );
            SW_EXPECT_NEAR( ( static_cast<float32>( x ) + 0.5f ) / 8.0f, uv._x, 1.0e-4f );
            SW_EXPECT_NEAR( ( static_cast<float32>( y ) + 0.5f ) / 8.0f, uv._y, 1.0e-4f );
            data._listTexel[data.computeTexelIndex( 0, x, y )] = direction._y >= 0.0f ? float3{ 1.0f, 1.0f, 1.0f } : float3{};
        }
    }
    SW_EXPECT_FALSE( data.filterRoughnessLevels( 0, Util::kMipCount ) );
    SW_REQUIRE_TRUE( data.filterRoughnessLevels( 32, Util::kMipCount ) );
    const float3 up{ 0.0f, 1.0f, 0.0f };
    const float3 down{ 0.0f, -1.0f, 0.0f };
    SW_EXPECT_TRUE( data.sampleLevel( 2, up )._x > 0.6f );
    SW_EXPECT_TRUE( data.sampleLevel( 2, down )._x < 0.4f );
    // 가장 거친 단계는 반구를 넓게 섞는다 — 지평선은 위와 아래의 사이다.
    const float32 horizon = data.sampleLevel( 2, float3{ 1.0f, 0.0f, 0.0f } )._x;
    SW_EXPECT_TRUE( horizon > data.sampleLevel( 2, down )._x );
    SW_EXPECT_TRUE( horizon < data.sampleLevel( 2, up )._x );
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
    SW_REQUIRE_TRUE( ReflectionProbeAsset::makeFromFaces( float3{}, arrFace, 16, 2, 4, Util::kMipCount, data ) );
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
    SW_REQUIRE_TRUE( ReflectionProbeAsset::makeFromFaces( float3{}, arrFace, 16, 3, 64, Util::kMipCount, data ) );
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

/** @brief [ReflectionProbeAssetTest] 원본 밉은 2×2 입체각 가중 평균이고, 한 변이 홀수가 되면 멈추며, 밉 0 이 없거나 밉 수가 0 이면 거절한다 */
SW_TEST_CASE( ReflectionProbeAssetTest, RadianceMapMipsAverageAndStopAtAnOddSize )
{
    OctahedralRadianceMap map;
    SW_EXPECT_FALSE( map.buildMips( 4 ) );
    SW_EXPECT_FALSE( map.assignBase( OctahedralRadianceMap::kMinResolution - 1u, vector<float3>( 9 ) ) );
    SW_EXPECT_FALSE( map.assignBase( 12, vector<float3>( 12 * 12 - 1 ) ) );
    SW_EXPECT_EQUAL( 0u, map.getMipCount() );

    vector<float3> listTexel;
    for ( uint32 texelIndex = 0; texelIndex < 12u * 12u; ++texelIndex )
    {
        listTexel.push_back( float3{ static_cast<float32>( texelIndex ), 1.0f, 0.0f } );
    }
    SW_REQUIRE_TRUE( map.assignBase( 12, std::move( listTexel ) ) );
    SW_EXPECT_FALSE( map.buildMips( 0 ) );
    SW_REQUIRE_TRUE( map.buildMips( 8 ) );
    // 12 → 6 → 3 에서 멈춘다(3 은 반으로 나뉘지 않는다).
    SW_REQUIRE_EQUAL( 3u, map.getMipCount() );
    SW_EXPECT_EQUAL( 6u, map._listMip[1]._resolution );
    SW_EXPECT_EQUAL( 3u, map._listMip[2]._resolution );
    // 밉 1 의 (1, 0) 은 밉 0 의 (2, 0) (3, 0) (2, 1) (3, 1) 을 입체각(방향의 L1 길이³)으로 섞은 값이다 — 무게가 달라 상자 평균 8.5 와 다르다.
    float32 weightedSum = 0.0f;
    float32 weightSum   = 0.0f;
    for ( uint32 y = 0; y < 2u; ++y )
    {
        for ( uint32 x = 2; x < 4u; ++x )
        {
            const float3  direction = OctahedralRadianceMap::computeTexelDirection( 12, x, y );
            const float32 axisSum   = MathUtil::abs( direction._x ) + MathUtil::abs( direction._y ) + MathUtil::abs( direction._z );
            weightedSum += static_cast<float32>( y * 12u + x ) * axisSum * axisSum * axisSum;
            weightSum += axisSum * axisSum * axisSum;
        }
    }
    SW_EXPECT_NEAR( weightedSum / weightSum, map._listMip[1]._listTexel[1]._x, 1.0e-4f );
    SW_EXPECT_TRUE( MathUtil::abs( map._listMip[1]._listTexel[1]._x - 8.5f ) > 0.01f );
    SW_EXPECT_NEAR( 1.0f, map._listMip[2]._listTexel[4]._y, 1.0e-5f );

    // 다시 지으면 밉 0 만 남기고 새로 만든다.
    SW_REQUIRE_TRUE( map.buildMips( 2 ) );
    SW_EXPECT_EQUAL( 2u, map.getMipCount() );
}

/** @brief [ReflectionProbeAssetTest] 소수 밉은 두 밉 사이를 섞고 범위 밖은 자른다. 빈 원본은 0 이다 */
SW_TEST_CASE( ReflectionProbeAssetTest, RadianceMapSampleBlendsBetweenMips )
{
    OctahedralRadianceMap empty;
    SW_EXPECT_NEAR( 0.0f, empty.sample( float3{ 0.0f, 1.0f, 0.0f }, 0.0f )._x, 0.0f );

    OctahedralRadianceMap map;
    SW_REQUIRE_TRUE( map.assignBase( 8, vector<float3>( 64, float3{ 1.0f, 1.0f, 1.0f } ) ) );
    SW_REQUIRE_TRUE( map.buildMips( 2 ) );
    map._listMip[1]._listTexel.assign( 16, float3{ 3.0f, 3.0f, 3.0f } );
    const float3 direction = float3{ 0.3f, 0.8f, -0.2f }.normalize();
    SW_EXPECT_NEAR( 1.0f, map.sample( direction, -1.0f )._x, 1.0e-5f );
    SW_EXPECT_NEAR( 1.0f, map.sample( direction, 0.0f )._x, 1.0e-5f );
    SW_EXPECT_NEAR( 2.0f, map.sample( direction, 0.5f )._x, 1.0e-5f );
    SW_EXPECT_NEAR( 3.0f, map.sample( direction, 7.0f )._x, 1.0e-5f );
}

/** @brief [ReflectionProbeAssetTest] 원본 파일은 밉 0 을 그대로 왕복하고(파일 앞은 SWRM), 잘린 길이 · 틀린 매직 · 범위 밖 크기는 거절하고 비운다 */
SW_TEST_CASE( ReflectionProbeAssetTest, RadianceMapBytesRoundTripAndRejectBadInput )
{
    OctahedralRadianceMap source;
    vector<float3>        listTexel;
    for ( uint32 texelIndex = 0; texelIndex < 16u; ++texelIndex )
    {
        listTexel.push_back( float3{ static_cast<float32>( texelIndex ), 0.5f, -0.5f } );
    }
    SW_REQUIRE_TRUE( source.assignBase( 4, std::move( listTexel ) ) );
    SW_REQUIRE_TRUE( source.buildMips( 3 ) );
    vector<uint8> listByte;
    source.makeBytes( listByte );
    SW_REQUIRE_EQUAL( size_t{ 12 + 16 * 12 }, listByte.size() );
    SW_EXPECT_EQUAL( static_cast<uint32>( 'S' ), static_cast<uint32>( listByte[0] ) );
    SW_EXPECT_EQUAL( static_cast<uint32>( 'M' ), static_cast<uint32>( listByte[3] ) );

    OctahedralRadianceMap read;
    SW_REQUIRE_TRUE( read.readFromBytes( listByte.data(), listByte.size() ) );
    SW_EXPECT_EQUAL( 1u, read.getMipCount() ); // 밉 사슬은 파일에 없다
    SW_EXPECT_EQUAL( 4u, read.getResolution() );
    SW_EXPECT_NEAR( 15.0f, read._listMip[0]._listTexel[15]._x, 0.0f );

    for ( size_t length = 0; length < listByte.size(); ++length )
    {
        SW_EXPECT_FALSE( read.readFromBytes( listByte.data(), length ) );
        SW_EXPECT_EQUAL( 0u, read.getMipCount() );
    }
    vector<uint8> badMagic = listByte;
    badMagic[0]            = 'X';
    SW_EXPECT_FALSE( read.readFromBytes( badMagic.data(), badMagic.size() ) );
    vector<uint8> badResolution = listByte;
    Util::patchUint32( badResolution, 8, OctahedralRadianceMap::kMaxResolution + 1u );
    SW_EXPECT_FALSE( read.readFromBytes( badResolution.data(), badResolution.size() ) );
    SW_EXPECT_EQUAL( 0u, read.getMipCount() );
}

/** @brief [ReflectionProbeAssetTest] 거르기는 원본이 거른 데이터보다 크면 단계 0 을 크기가 맞는 밉에서 읽는다 — 원본 텍셀 넷의 입체각 가중 평균이다 */
SW_TEST_CASE( ReflectionProbeAssetTest, FilterReadsTheBaseLevelFromTheMatchingMip )
{
    vector<float3> listTexel;
    for ( uint32 texelIndex = 0; texelIndex < 16u * 16u; ++texelIndex )
    {
        // 바둑판 — 텍셀 넷의 입체각 가중 평균은 0.5 가까이다(밉 0 의 텍셀은 0 · 1 뿐이라 갈린다).
        const uint32 x = texelIndex % 16u;
        const uint32 y = texelIndex / 16u;
        listTexel.push_back( ( ( x + y ) & 1u ) == 0u ? float3{ 1.0f, 1.0f, 1.0f } : float3{} );
    }
    OctahedralRadianceMap source;
    SW_REQUIRE_TRUE( source.assignBase( 16, std::move( listTexel ) ) );
    SW_REQUIRE_TRUE( source.buildMips( 3 ) );
    ReflectionProbeAsset data;
    SW_REQUIRE_TRUE( data.allocate( float3{}, 8, 2 ) );
    SW_EXPECT_FALSE( data.filterRoughnessLevels( OctahedralRadianceMap{}, 8 ) );
    SW_EXPECT_FALSE( data.filterRoughnessLevels( source, 0 ) );
    SW_REQUIRE_TRUE( data.filterRoughnessLevels( source, 8 ) );
    uint32 wrongTexelCount = 0;
    for ( uint32 y = 0; y < 8u; ++y )
    {
        for ( uint32 x = 0; x < 8u; ++x )
        {
            const float32 filtered = data._listTexel[data.computeTexelIndex( 0, x, y )]._x;
            const float32 average  = source._listMip[1]._listTexel[y * 8u + x]._x;
            wrongTexelCount += MathUtil::abs( filtered - average ) < 1.0e-3f && MathUtil::abs( average - 0.5f ) < 0.25f ? 0u : 1u;
        }
    }
    SW_EXPECT_EQUAL( 0u, wrongTexelCount );
}

/**
 * @brief [ReflectionProbeAssetTest] filtered importance sampling 을 기준 적분(원본 텍셀 전부를 더하는 느린 길)과 견준다.
 * @details 기준이 맞는지 먼저 본다 — 밉 하나 · 표본 1024 개의 보통 중요도 표본은 기준과 평균 1 % 안이다. 매끈한 하늘에서 표본 32 개의 FIS 는 평균 4 % · 최대 30 % 안이고,
 *          작고 밝은 해(1 − cos 폭 0.005, 세기 200)에서는 같은 표본 수의 보통 중요도 표본보다 평균 오차가 반 아래, 최대 오차가 4 분의 1 아래다(표본이 해를 맞히거나 놓쳐 튀는 것을 밉이 메운다).
 *          문턱은 같은 식의 numpy 원형에서 잰 값(FIS 평균 2.6 % · 최대 19 %, 해: FIS 평균 11 % · 최대 59 %, 보통 27 % · 450 %)에 여유를 둔 것이다.
 */
SW_TEST_CASE( ReflectionProbeAssetTest, FilteredImportanceSamplingMatchesTheReference )
{
    using Reference = FilterReferenceTestUtil;

    const Reference::ErrorSummary manySamples = Reference::measure( Reference::makeSource( 0.1f, 4.0f, 1 ), 1024 );
    SW_EXPECT_TRUE( manySamples._mean < 0.01f );

    const Reference::ErrorSummary smooth = Reference::measure( Reference::makeSource( 0.1f, 4.0f, Reference::kMipCount ), Reference::kSampleCount );
    SW_EXPECT_TRUE( smooth._mean < 0.04f );
    SW_EXPECT_TRUE( smooth._max < 0.3f );

    const Reference::ErrorSummary sunFiltered = Reference::measure( Reference::makeSource( 0.005f, 200.0f, Reference::kMipCount ), Reference::kSampleCount );
    const Reference::ErrorSummary sunPlain    = Reference::measure( Reference::makeSource( 0.005f, 200.0f, 1 ), Reference::kSampleCount );
    SW_EXPECT_TRUE( sunFiltered._mean < 0.15f );
    SW_EXPECT_TRUE( sunFiltered._mean * 2.0f < sunPlain._mean );
    SW_EXPECT_TRUE( sunFiltered._max * 4.0f < sunPlain._max );
}

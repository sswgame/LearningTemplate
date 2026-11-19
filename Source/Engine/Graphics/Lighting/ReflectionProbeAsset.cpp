#include "pch.h"

#include "Engine/Graphics/Lighting/ReflectionProbeAsset.h"

#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Serialization/ByteStream.h"

#include "Engine/Content/AssetIO.h"
#include "Engine/Foundation/EngineParallel.h"

namespace sw
{
    namespace
    {
        struct ReflectionProbeAssetInternal
        {
            /// @brief 머리말 바이트 수 — 매직 · 버전 · 찍은 위치 · 한 변 텍셀 수 · 단계 수.
            static constexpr size_t kHeaderBytes = sizeof( uint32 ) * 2 + sizeof( float32 ) * 3 + sizeof( uint32 ) * 2;
            /// @brief 텍셀 하나의 바이트 수입니다(float32 RGB).
            static constexpr size_t kTexelBytes = sizeof( float32 ) * 3;
            /// @brief 거칠기 0 근처의 GGX α 하한 — 셰이더 lighting.hlsli 의 kMinRoughness 와 같은 뜻(0.045² 근처에서 표본이 한 점에 모인다).
            static constexpr float32 kMinRoughness = 0.045f;
            /// @brief 원본 밉 파일의 머리말 바이트 수 — 매직 · 버전 · 한 변 텍셀 수.
            static constexpr size_t kRadianceMapHeaderBytes = sizeof( uint32 ) * 3;
            /**
             * @brief filtered importance sampling 의 밉 치우침입니다. 쌍선형 샘플은 밉 텍셀 둘 너비를 덮으므로 텍셀 너비가 표본 간격의 반인 밉(−1)이면 표본 사이가 메워진다.
             * @details GPU Gems 3 20.4 는 큐브맵 하드웨어 필터에 +1 을 쓴다. 옥타헤드럴 상자 밉에서 기준 적분과 견주면 +1 은 거친 단계를 너무 흐린다(64² 원본 · 표본 32 ·
             *          거칠기 1/3 ~ 1 의 평균 오차: +1 은 10 ~ 35 %, −1 은 1 ~ 5 % — ReflectionProbeAssetTest.FilteredImportanceSamplingMatchesTheReference).
             */
            static constexpr float32 kFilterLODBias = -1.0f;
            /// @brief 구 전체의 입체각(sr) — 옥타헤드럴 텍셀의 평균 입체각은 이것 / 텍셀 수다.
            static constexpr float32 kSphereSolidAngle = 4.0f * MathUtil::kPi;
            /// @brief N = V 일 때 반사 방향의 pdf 는 하프 벡터 분포 D 의 이만큼이다 — pdf(L) = D · N·H / (4 · V·H), N·H = V·H.
            static constexpr float32 kReflectedDensityScale = 0.25f;
            /// @brief 밉 텍셀 하나가 위 밉 텍셀 넷(2×2)의 평균이다.
            static constexpr float32 kMipBoxWeight = 0.25f;

            /** @brief 한 축에서 상자 안의 점이 방향을 따라 상자 벽을 나가기까지의 거리입니다(시차 보정). 셰이더 lighting.hlsli 와 같은 식이다. */
            [[nodiscard]] static float32 computeAxisExit( float32 position, float32 direction, float32 center, float32 halfExtent )
            {
                const float32 safe = MathUtil::abs( direction ) < 1e-5f ? 1e-5f : direction;
                return MathUtil::max( ( center + halfExtent - position ) / safe, ( center - halfExtent - position ) / safe );
            }

            /** @brief 한 변 텍셀 수와 단계 수가 범위 안인지 봅니다. 텍셀을 잡기 전에 부릅니다 — 범위 밖 값이 큰 할당을 일으키지 않게. */
            [[nodiscard]] static expected<void> validateSizeInRange( uint32 resolution, uint32 levelCount )
            {
                if ( resolution < ReflectionProbeAsset::kMinResolution || ReflectionProbeAsset::kMaxResolution < resolution )
                    return unexpected( Error{ ErrorKind::InvalidArgument, "reflection probe resolution is out of range" } );
                if ( levelCount < ReflectionProbeAsset::kMinLevelCount || ReflectionProbeAsset::kMaxLevelCount < levelCount )
                    return unexpected( Error{ ErrorKind::InvalidArgument, "reflection probe level count is out of range" } );
                return {};
            }

            /** @brief 0 을 양수로 치는 부호입니다(HLSL `sign` 은 0 에서 0 이라 셰이더도 같은 식을 쓴다). */
            static float32 signNotZero( float32 value ) { return value >= 0.0f ? 1.0f : -1.0f; }

            /** @brief 행 0 이 위인 정사각 이미지 하나를 픽셀 좌표(@p pixelX, @p pixelY — 텍셀 중심이 정수)로 쌍선형 샘플합니다. 가장자리는 자른다. */
            static float3 sampleBilinear( const vector<float3>& listPixel, size_t firstPixel, uint32 size, float32 pixelX, float32 pixelY )
            {
                const float32 lastPixel = static_cast<float32>( size - 1u );
                const float32 clampedX  = MathUtil::clamp( pixelX, 0.0f, lastPixel );
                const float32 clampedY  = MathUtil::clamp( pixelY, 0.0f, lastPixel );
                const uint32  x0        = static_cast<uint32>( MathUtil::floor( clampedX ) );
                const uint32  y0        = static_cast<uint32>( MathUtil::floor( clampedY ) );
                const uint32  x1        = MathUtil::min( x0 + 1u, size - 1u );
                const uint32  y1        = MathUtil::min( y0 + 1u, size - 1u );
                const float32 blendX    = clampedX - static_cast<float32>( x0 );
                const float32 blendY    = clampedY - static_cast<float32>( y0 );
                const float3& topLeft   = listPixel[firstPixel + static_cast<size_t>( y0 ) * size + x0];
                const float3& topRight  = listPixel[firstPixel + static_cast<size_t>( y0 ) * size + x1];
                const float3& lowLeft   = listPixel[firstPixel + static_cast<size_t>( y1 ) * size + x0];
                const float3& lowRight  = listPixel[firstPixel + static_cast<size_t>( y1 ) * size + x1];
                const float3  top       = topLeft * ( 1.0f - blendX ) + topRight * blendX;
                const float3  low       = lowLeft * ( 1.0f - blendX ) + lowRight * blendX;
                return top * ( 1.0f - blendY ) + low * blendY;
            }

            /** @brief 찍은 위치에서 방향 @p direction 으로 본 빛입니다 — 그 방향을 가장 정면으로 보는 면을 골라 그 카메라의 행렬로 픽셀에 되짚는다. */
            static float3 sampleFaces( const ReflectionProbeFace* pArrFace, const float3& capturePosition, const float3& direction )
            {
                uint32  bestFace = 0;
                float32 bestDot  = -2.0f;
                for ( uint32 faceIndex = 0; faceIndex < ReflectionProbeAsset::kFaceCount; ++faceIndex )
                {
                    const float32 facing = direction.dot( pArrFace[faceIndex]._forward );
                    if ( facing > bestDot )
                    {
                        bestDot  = facing;
                        bestFace = faceIndex;
                    }
                }
                const ReflectionProbeFace& face  = pArrFace[bestFace];
                const float3               point = capturePosition + direction;
                const float4               clip  = float4::transform( float4{ point._x, point._y, point._z, 1.0f }, face._viewProj );
                const float32              clipW = MathUtil::max( clip._w, 1e-6f );
                const float32              size  = static_cast<float32>( face._size );
                const float32              ndcX  = clip._x / clipW;
                const float32              ndcY  = clip._y / clipW;
                return sampleBilinear( face._listPixel, 0, face._size, ( ndcX * 0.5f + 0.5f ) * size - 0.5f, ( 0.5f - ndcY * 0.5f ) * size - 0.5f );
            }

            /** @brief 반 비트 뒤집기(Van der Corput) — 해머슬리 점의 둘째 좌표입니다. */
            static float32 computeRadicalInverse( uint32 bits )
            {
                bits = ( bits << 16u ) | ( bits >> 16u );
                bits = ( ( bits & 0x55555555u ) << 1u ) | ( ( bits & 0xAAAAAAAAu ) >> 1u );
                bits = ( ( bits & 0x33333333u ) << 2u ) | ( ( bits & 0xCCCCCCCCu ) >> 2u );
                bits = ( ( bits & 0x0F0F0F0Fu ) << 4u ) | ( ( bits & 0xF0F0F0F0u ) >> 4u );
                bits = ( ( bits & 0x00FF00FFu ) << 8u ) | ( ( bits & 0xFF00FF00u ) >> 8u );
                return static_cast<float32>( bits ) * 2.3283064365386963e-10f; // 2^-32
            }

            /** @brief 원본 @p source 를 GGX 로 걸러 방향 @p normal 의 텍셀 하나를 만듭니다(N = V = R). 표본마다 pdf 로 원본 밉을 고른다(filtered importance sampling). */
            static float3 prefilterTexel( const OctahedralRadianceMap& source, const float3& normal, float32 roughness, uint32 sampleCount )
            {
                const float32 clampedRoughness = MathUtil::max( roughness, kMinRoughness );
                const float32 alpha            = clampedRoughness * clampedRoughness;
                const float32 alphaSquared     = alpha * alpha;
                const float3  up               = MathUtil::abs( normal._y ) < 0.999f ? float3{ 0.0f, 1.0f, 0.0f } : float3{ 1.0f, 0.0f, 0.0f };
                const float3  tangent          = up.cross( normal ).normalize();
                const float3  bitangent        = normal.cross( tangent );
                // 옥타헤드럴 텍셀의 평균 입체각 — 밉 고르기의 기준이다(텍셀마다 입체각이 다른 것은 무시한다, 큐브맵 거르기도 면 가운데 텍셀로 셈한다).
                const float32 sourceResolution = static_cast<float32>( source.getResolution() );
                const float32 texelSolidAngle  = kSphereSolidAngle / ( sourceResolution * sourceResolution );
                float3        sum{};
                float32       weight = 0.0f;
                for ( uint32 sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex )
                {
                    const float32 firstUnit      = ( static_cast<float32>( sampleIndex ) + 0.5f ) / static_cast<float32>( sampleCount );
                    const float32 secondUnit     = computeRadicalInverse( sampleIndex );
                    const float32 phi            = MathUtil::kTwoPi * firstUnit;
                    const float32 cosTheta       = MathUtil::sqrt( ( 1.0f - secondUnit ) / ( 1.0f + ( alphaSquared - 1.0f ) * secondUnit ) );
                    const float32 sinTheta       = MathUtil::sqrt( MathUtil::max( 1.0f - cosTheta * cosTheta, 0.0f ) );
                    const float3  halfway        = tangent * ( sinTheta * MathUtil::cos( phi ) ) + bitangent * ( sinTheta * MathUtil::sin( phi ) ) + normal * cosTheta;
                    const float3  toLight        = halfway * ( 2.0f * normal.dot( halfway ) ) - normal;
                    const float32 normalDotLight = normal.dot( toLight );
                    if ( normalDotLight <= 0.0f )
                        continue;
                    // N = V 라 pdf(L) = D(H) · N·H / (4 · V·H) = D(H) / 4 이다. 표본 하나가 맡는 입체각이 원본 텍셀 몇 개를 덮는지로 밉을 고른다.
                    const float32 denominator        = cosTheta * cosTheta * ( alphaSquared - 1.0f ) + 1.0f;
                    const float32 distribution       = alphaSquared / ( MathUtil::kPi * denominator * denominator );
                    const float32 probabilityDensity = distribution * kReflectedDensityScale;
                    const float32 sampleSolidAngle   = 1.0f / ( static_cast<float32>( sampleCount ) * probabilityDensity );
                    // 텍셀 한 변(입체각의 제곱근)이 표본 간격과 같은 밉이다.
                    const float32 lod = MathUtil::max( MathUtil::log2( MathUtil::sqrt( sampleSolidAngle / texelSolidAngle ) ) + kFilterLODBias, 0.0f );
                    sum += source.sample( toLight.normalize(), lod ) * normalDotLight;
                    weight += normalDotLight;
                }
                return weight > 0.0f ? sum / weight : source.sample( normal, 0.0f );
            }

            /**
             * @brief 거르기의 병렬 본문입니다 — 항목 하나가 단계 1 부터의 텍셀 하나이고 제 칸(`runParallelInto` 가 건넨다)에만 쓴다. 원본(`_pSource`)은 읽기만 한다.
             * @details 출력 칸을 인자로 받으므로 텍셀 벡터를 본문이 잡지 않는다(벡터의 쓰기 검사를 작업자마다 열면 다른 칸이어도 경합으로 잡힌다).
             */
            struct FilterJob
            {
                const ReflectionProbeAsset*  _pData{ nullptr };
                const OctahedralRadianceMap* _pSource{ nullptr };
                uint32                       _sampleCount{ 0 };

                /** @brief 단계 1 첫 텍셀부터 센 번호 @p texelIndex 의 텍셀을 걸러 @p outTexel 에 씁니다. */
                void operator()( uint32 texelIndex, float3& outTexel ) const
                {
                    const uint32 resolution   = _pData->_resolution;
                    const uint32 levelTexels  = resolution * resolution;
                    const uint32 level        = 1u + texelIndex / levelTexels;
                    const uint32 texelInLevel = texelIndex % levelTexels;
                    const uint32 x            = texelInLevel % resolution;
                    const uint32 y            = texelInLevel / resolution;
                    outTexel                  = prefilterTexel( *_pSource, _pData->computeTexelDirection( x, y ), _pData->computeLevelRoughness( level ), _sampleCount );
                }
            };
        };
    } // namespace
} // namespace sw

namespace sw
{
    float32 ReflectionProbeAsset::computeLevelRoughness( uint32 level ) const
    {
        return _levelCount > 1u ? static_cast<float32>( level ) / static_cast<float32>( _levelCount - 1u ) : 0.0f;
    }

    expected<void> ReflectionProbeAsset::validate() const
    {
        if ( expected<void> size = ReflectionProbeAssetInternal::validateSizeInRange( _resolution, _levelCount ); size.has_value() == false )
            return size;
        if ( _listTexel.size() != static_cast<size_t>( _resolution ) * _resolution * _levelCount )
            return unexpected( Error{ ErrorKind::InvalidArgument, "reflection probe texels do not match its size" } );
        return {};
    }

    void ReflectionProbeAsset::clear()
    {
        _capturePosition = float3{};
        _resolution      = 0;
        _levelCount      = 0;
        _listTexel.clear();
    }

    expected<void> ReflectionProbeAsset::allocate( const float3& capturePosition, uint32 resolution, uint32 levelCount )
    {
        clear();
        if ( expected<void> size = ReflectionProbeAssetInternal::validateSizeInRange( resolution, levelCount ); size.has_value() == false )
            return size;
        _capturePosition = capturePosition;
        _resolution      = resolution;
        _levelCount      = levelCount;
        _listTexel.assign( static_cast<size_t>( resolution ) * resolution * levelCount, float3{} );
        return {};
    }

    float3 ReflectionProbeAsset::computeTexelDirection( uint32 x, uint32 y ) const
    {
        return OctahedralRadianceMap::computeTexelDirection( _resolution, x, y );
    }

    expected<void> ReflectionProbeAsset::filterRoughnessLevels( const OctahedralRadianceMap& source, uint32 sampleCount )
    {
        using Internal = ReflectionProbeAssetInternal;
        if ( sampleCount == 0 )
            return unexpected( Error{ ErrorKind::InvalidArgument, "reflection probe filter needs at least one sample" } );
        if ( source._listMip.empty() )
            return unexpected( Error{ ErrorKind::InvalidArgument, "reflection probe filter source is empty" } );
        if ( expected<void> valid = validate(); valid.has_value() == false )
            return valid;
        // 단계 0 — 원본이 이 데이터보다 크면 한 변이 맞는 밉을 읽는다(텍셀 하나가 원본 텍셀 여럿의 평균이 된다).
        const float32 baseLOD = MathUtil::max( MathUtil::log2( static_cast<float32>( source.getResolution() ) / static_cast<float32>( _resolution ) ), 0.0f );
        for ( uint32 y = 0; y < _resolution; ++y )
        {
            for ( uint32 x = 0; x < _resolution; ++x )
            {
                _listTexel[computeTexelIndex( 0, x, y )] = source.sample( computeTexelDirection( x, y ), baseLOD );
            }
        }
        Internal::FilterJob job{};
        job._pData       = this;
        job._pSource     = &source;
        job._sampleCount = sampleCount;
        // 출력은 단계 1 부터의 텍셀이다(단계 0 은 거르기의 입력). 행 하나가 텍셀 `_resolution` 개 × 표본 `sampleCount` 개라 디스패치 비용보다 크다 — 행이 둘이면 나눈다.
        const vector_reference<float3> listFiltered = vector_reference<float3>( _listTexel ).subspan( computeTexelIndex( 1u, 0, 0 ) );
        engine::runParallelInto<float3>( listFiltered, 2u * _resolution, job );
        return {};
    }

    expected<void> ReflectionProbeAsset::filterRoughnessLevels( uint32 sampleCount, uint32 mipCount )
    {
        if ( expected<void> valid = validate(); valid.has_value() == false )
            return valid;
        OctahedralRadianceMap source;
        const size_t          baseTexelCount = static_cast<size_t>( _resolution ) * _resolution;
        if ( expected<void> assigned = source.assignBase( _resolution, vector<float3>( _listTexel.begin(), _listTexel.begin() + baseTexelCount ) ); assigned.has_value() == false )
            return assigned;
        if ( expected<void> built = source.buildMips( mipCount ); built.has_value() == false )
            return built;
        return filterRoughnessLevels( source, sampleCount );
    }

    float3 ReflectionProbeAsset::sampleLevel( uint32 level, const float3& direction ) const
    {
        const float2  uv         = encodeOctahedral( direction );
        const float32 resolution = static_cast<float32>( _resolution );
        const size_t  firstTexel = static_cast<size_t>( computeTexelIndex( level, 0, 0 ) );
        return ReflectionProbeAssetInternal::sampleBilinear( _listTexel, firstTexel, _resolution, uv._x * resolution - 0.5f, uv._y * resolution - 0.5f );
    }

    void ReflectionProbeAsset::makeBytes( vector<uint8>& outBytes ) const
    {
        using Internal = ReflectionProbeAssetInternal;
        outBytes.clear();
        outBytes.reserve( Internal::kHeaderBytes + Internal::kTexelBytes * _listTexel.size() );
        ByteWriter writer{ outBytes };
        writer.writeUint32( kMagic );
        writer.writeUint32( kVersion );
        writer.writeFloat32( _capturePosition._x );
        writer.writeFloat32( _capturePosition._y );
        writer.writeFloat32( _capturePosition._z );
        writer.writeUint32( _resolution );
        writer.writeUint32( _levelCount );
        for ( const float3& texel : _listTexel )
        {
            writer.writeFloat32( texel._x );
            writer.writeFloat32( texel._y );
            writer.writeFloat32( texel._z );
        }
    }

    expected<void> ReflectionProbeAsset::readFromBytes( const uint8* pData, size_t size )
    {
        using Internal = ReflectionProbeAssetInternal;
        clear();
        if ( pData == nullptr || size < Internal::kHeaderBytes )
            return unexpected( Error{ ErrorKind::InvalidArgument, "reflection probe file is shorter than its header" } );
        ByteReader reader{ pData, size };
        if ( reader.readUint32() != kMagic )
            return unexpected( Error{ ErrorKind::InvalidArgument, "reflection probe file magic is not SWRP" } );
        if ( reader.readUint32() != kVersion )
            return unexpected( Error{ ErrorKind::InvalidArgument, "reflection probe file version differs - bake the probe again" } );
        float3 capturePosition{};
        capturePosition._x        = reader.readFloat32();
        capturePosition._y        = reader.readFloat32();
        capturePosition._z        = reader.readFloat32();
        const uint32 resolution   = reader.readUint32();
        const uint32 levelCount   = reader.readUint32();
        const bool   bSizeInRange = kMinResolution <= resolution && resolution <= kMaxResolution && kMinLevelCount <= levelCount && levelCount <= kMaxLevelCount;
        const uint64 texelCount   = static_cast<uint64>( resolution ) * resolution * levelCount; // 범위 안이라 넘치지 않는다
        const bool   bRead        = bSizeInRange && reader.readList( _listTexel, texelCount, Internal::kTexelBytes, [&]( float3& texel )
                 {
            texel._x = reader.readFloat32();
            texel._y = reader.readFloat32();
            texel._z = reader.readFloat32();
        } );
        if ( bRead == false || reader.isFullyRead() == false )
        {
            clear();
            return unexpected( Error{ ErrorKind::InvalidArgument, "reflection probe file length does not match its size, or it holds a non-finite value" } );
        }
        _capturePosition = capturePosition;
        _resolution      = resolution;
        _levelCount      = levelCount;
        return {};
    }

    bool ReflectionProbeAsset::saveToFile( const AbsolutePath& absPath ) const
    {
        vector<uint8> bytes;
        makeBytes( bytes );
        if ( FileUtil::ensureParentDirectoryExists( absPath.str() ) == false )
            return false;
        return FileUtil::writeFile( absPath.str(), bytes.data(), bytes.size() );
    }

    expected<void> ReflectionProbeAsset::loadFromAsset( const AssetID& assetID )
    {
        clear();
        vector<uint8> bytes;
        if ( AssetIO::readBinaryAsset( assetID, bytes ) == false )
            return unexpected( Error{ ErrorKind::NotFound, "reflection probe file could not be read", assetID.view() } );
        return readFromBytes( bytes.data(), bytes.size() );
    }

    float2 ReflectionProbeAsset::encodeOctahedral( const float3& direction )
    {
        using Internal    = ReflectionProbeAssetInternal;
        const float32 sum = MathUtil::max( MathUtil::abs( direction._x ) + MathUtil::abs( direction._y ) + MathUtil::abs( direction._z ), 1e-8f );
        const float32 x   = direction._x / sum;
        const float32 y   = direction._y / sum;
        const float32 z   = direction._z / sum;
        // 아래 반구(y < 0)는 네 귀퉁이로 접는다.
        const float32 u = ( y >= 0.0f ) ? x : ( 1.0f - MathUtil::abs( z ) ) * Internal::signNotZero( x );
        const float32 v = ( y >= 0.0f ) ? z : ( 1.0f - MathUtil::abs( x ) ) * Internal::signNotZero( z );
        return float2{ u * 0.5f + 0.5f, v * 0.5f + 0.5f };
    }

    float3 ReflectionProbeAsset::decodeOctahedral( const float2& uv )
    {
        const float32 u    = uv._x * 2.0f - 1.0f;
        const float32 v    = uv._y * 2.0f - 1.0f;
        const float32 y    = 1.0f - MathUtil::abs( u ) - MathUtil::abs( v );
        const float32 fold = MathUtil::max( -y, 0.0f );
        const float32 x    = u + ( u >= 0.0f ? -fold : fold );
        const float32 z    = v + ( v >= 0.0f ? -fold : fold );
        return float3{ x, y, z }.normalize();
    }

    float3 ReflectionProbeAsset::computeParallaxDirection( const float3& worldPosition, const float3& reflectDirection, const float3& boxCenter, const float3& boxHalfExtent, const float3& capturePosition )
    {
        using Internal            = ReflectionProbeAssetInternal;
        const float32 exitX       = Internal::computeAxisExit( worldPosition._x, reflectDirection._x, boxCenter._x, boxHalfExtent._x );
        const float32 exitY       = Internal::computeAxisExit( worldPosition._y, reflectDirection._y, boxCenter._y, boxHalfExtent._y );
        const float32 exitZ       = Internal::computeAxisExit( worldPosition._z, reflectDirection._z, boxCenter._z, boxHalfExtent._z );
        const float32 hitDistance = MathUtil::min( exitX, MathUtil::min( exitY, exitZ ) );
        return ( worldPosition + reflectDirection * hitDistance - capturePosition ).normalize();
    }
} // namespace sw

namespace sw
{
    expected<void> ReflectionProbeAsset::makeFromFaces( const float3& capturePosition, const ReflectionProbeFace* pArrFace, uint32 resolution, uint32 levelCount, uint32 sampleCount,
                                                        uint32 mipCount, ReflectionProbeAsset& outData )
    {
        using Internal = ReflectionProbeAssetInternal;
        outData.clear();
        if ( pArrFace == nullptr || sampleCount == 0 )
            return unexpected( Error{ ErrorKind::InvalidArgument, "reflection probe filter needs six faces and at least one sample" } );
        for ( uint32 faceIndex = 0; faceIndex < kFaceCount; ++faceIndex )
        {
            const ReflectionProbeFace& face      = pArrFace[faceIndex];
            const bool                 bFaceFits = face._size > 0 && face._listPixel.size() == static_cast<size_t>( face._size ) * face._size;
            if ( bFaceFits == false )
                return unexpected( Error{ ErrorKind::InvalidArgument, "a captured face is empty or its pixels do not match its size" } );
        }
        if ( expected<void> allocated = outData.allocate( capturePosition, resolution, levelCount ); allocated.has_value() == false )
            return allocated;

        // 단계 0 — 텍셀 중심의 방향으로 면을 읽는다.
        for ( uint32 y = 0; y < resolution; ++y )
        {
            for ( uint32 x = 0; x < resolution; ++x )
            {
                outData._listTexel[outData.computeTexelIndex( 0, x, y )] = Internal::sampleFaces( pArrFace, capturePosition, outData.computeTexelDirection( x, y ) );
            }
        }
        // 단계 1 부터 — 단계 0 의 밉 사슬을 그 단계의 거칠기로 거른다.
        return outData.filterRoughnessLevels( sampleCount, mipCount );
    }
} // namespace sw

namespace sw
{
    expected<void> OctahedralRadianceMap::assignBase( uint32 resolution, vector<float3>&& listTexel )
    {
        clear();
        if ( resolution < kMinResolution || kMaxResolution < resolution )
            return unexpected( Error{ ErrorKind::InvalidArgument, "radiance map resolution is out of range" } );
        if ( listTexel.size() != static_cast<size_t>( resolution ) * resolution )
            return unexpected( Error{ ErrorKind::InvalidArgument, "radiance map texels do not match its resolution" } );
        Mip base;
        base._resolution = resolution;
        base._listTexel  = std::move( listTexel );
        _listMip.push_back( std::move( base ) );
        return {};
    }

    expected<void> OctahedralRadianceMap::buildMips( uint32 mipCount )
    {
        if ( _listMip.empty() )
            return unexpected( Error{ ErrorKind::InvalidArgument, "radiance map has no base mip" } );
        if ( mipCount == 0 )
            return unexpected( Error{ ErrorKind::InvalidArgument, "radiance map needs at least one mip" } );
        _listMip.resize( 1 );
        // shortcut: 상자 평균은 텍셀마다 다른 입체각을 무시한다(옥타헤드럴은 큐브보다 고르다), 귀퉁이 번짐이 보이면 입체각 가중 평균으로 바꾼다.
        while ( getMipCount() < mipCount )
        {
            const Mip&   parent        = _listMip.back();
            const uint32 parentSize    = parent._resolution;
            const bool   bCannotShrink = parentSize < 2u || ( parentSize & 1u ) != 0u;
            if ( bCannotShrink )
                break;
            Mip child;
            child._resolution = parentSize / 2u;
            child._listTexel.resize( static_cast<size_t>( child._resolution ) * child._resolution );
            for ( uint32 y = 0; y < child._resolution; ++y )
            {
                for ( uint32 x = 0; x < child._resolution; ++x )
                {
                    const size_t top                                                   = static_cast<size_t>( y * 2u ) * parentSize + x * 2u;
                    const size_t low                                                   = top + parentSize;
                    child._listTexel[static_cast<size_t>( y ) * child._resolution + x] = ( parent._listTexel[top] + parent._listTexel[top + 1u] + parent._listTexel[low] + parent._listTexel[low + 1u] ) * ReflectionProbeAssetInternal::kMipBoxWeight;
                }
            }
            _listMip.push_back( std::move( child ) );
        }
        return {};
    }

    float3 OctahedralRadianceMap::sample( const float3& direction, float32 lod ) const
    {
        if ( _listMip.empty() )
            return float3{};
        const float32 lastMip   = static_cast<float32>( _listMip.size() - 1u );
        const float32 clamped   = MathUtil::clamp( lod, 0.0f, lastMip );
        const uint32  lowerMip  = static_cast<uint32>( MathUtil::floor( clamped ) );
        const uint32  upperMip  = MathUtil::min( lowerMip + 1u, getMipCount() - 1u );
        const float32 blend     = clamped - static_cast<float32>( lowerMip );
        const float2  uv        = ReflectionProbeAsset::encodeOctahedral( direction );
        const auto    sampleMip = [&]( uint32 mipIndex )
        {
            const Mip&    mip  = _listMip[mipIndex];
            const float32 size = static_cast<float32>( mip._resolution );
            return ReflectionProbeAssetInternal::sampleBilinear( mip._listTexel, 0, mip._resolution, uv._x * size - 0.5f, uv._y * size - 0.5f );
        };
        const float3 lower = sampleMip( lowerMip );
        return blend > 0.0f ? lower * ( 1.0f - blend ) + sampleMip( upperMip ) * blend : lower;
    }

    float3 OctahedralRadianceMap::computeTexelDirection( uint32 resolution, uint32 x, uint32 y )
    {
        const float32 texelSize = 1.0f / static_cast<float32>( resolution );
        return ReflectionProbeAsset::decodeOctahedral( float2{ ( static_cast<float32>( x ) + 0.5f ) * texelSize, ( static_cast<float32>( y ) + 0.5f ) * texelSize } );
    }

    void OctahedralRadianceMap::makeBytes( vector<uint8>& outBytes ) const
    {
        using Internal = ReflectionProbeAssetInternal;
        outBytes.clear();
        const size_t texelCount = _listMip.empty() ? 0u : _listMip[0]._listTexel.size();
        outBytes.reserve( Internal::kRadianceMapHeaderBytes + Internal::kTexelBytes * texelCount );
        ByteWriter writer{ outBytes };
        writer.writeUint32( kMagic );
        writer.writeUint32( kVersion );
        writer.writeUint32( getResolution() );
        for ( size_t texelIndex = 0; texelIndex < texelCount; ++texelIndex )
        {
            const float3& texel = _listMip[0]._listTexel[texelIndex];
            writer.writeFloat32( texel._x );
            writer.writeFloat32( texel._y );
            writer.writeFloat32( texel._z );
        }
    }

    expected<void> OctahedralRadianceMap::readFromBytes( const uint8* pData, size_t size )
    {
        using Internal = ReflectionProbeAssetInternal;
        clear();
        if ( pData == nullptr || size < Internal::kRadianceMapHeaderBytes )
            return unexpected( Error{ ErrorKind::InvalidArgument, "radiance map file is shorter than its header" } );
        ByteReader reader{ pData, size };
        if ( reader.readUint32() != kMagic )
            return unexpected( Error{ ErrorKind::InvalidArgument, "radiance map file magic is not SWRM" } );
        if ( reader.readUint32() != kVersion )
            return unexpected( Error{ ErrorKind::InvalidArgument, "radiance map file version differs - run Scripts/dev/make_sky_hdri_prefabs.py again" } );
        const uint32   resolution   = reader.readUint32();
        const bool     bSizeInRange = kMinResolution <= resolution && resolution <= kMaxResolution;
        const uint64   texelCount   = static_cast<uint64>( resolution ) * resolution; // 범위 안이라 넘치지 않는다
        vector<float3> listTexel;
        const bool     bRead = bSizeInRange && reader.readList( listTexel, texelCount, Internal::kTexelBytes, [&]( float3& texel )
            {
            texel._x = reader.readFloat32();
            texel._y = reader.readFloat32();
            texel._z = reader.readFloat32();
        } );
        if ( bRead == false || reader.isFullyRead() == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "radiance map file length does not match its resolution, or it holds a non-finite value" } );
        return assignBase( resolution, std::move( listTexel ) );
    }

    expected<void> OctahedralRadianceMap::loadFromAsset( const AssetID& assetID )
    {
        clear();
        vector<uint8> bytes;
        if ( AssetIO::readBinaryAsset( assetID, bytes ) == false )
            return unexpected( Error{ ErrorKind::NotFound, "radiance map file could not be read", assetID.view() } );
        return readFromBytes( bytes.data(), bytes.size() );
    }
} // namespace sw

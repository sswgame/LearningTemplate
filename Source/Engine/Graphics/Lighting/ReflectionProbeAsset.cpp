#include "pch.h"

#include "Engine/Graphics/Lighting/ReflectionProbeAsset.h"

#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Serialization/ByteStream.h"

#include "Engine/Content/AssetIO.h"

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

            /** @brief 한 축에서 상자 안의 점이 방향을 따라 상자 벽을 나가기까지의 거리입니다(시차 보정). 셰이더 lighting.hlsli 와 같은 식이다. */
            [[nodiscard]] static float32 computeAxisExit( float32 position, float32 direction, float32 center, float32 halfExtent )
            {
                const float32 safe = MathUtil::abs( direction ) < 1e-5f ? 1e-5f : direction;
                return MathUtil::max( ( center + halfExtent - position ) / safe, ( center - halfExtent - position ) / safe );
            }

            /** @brief 한 변 텍셀 수와 단계 수가 범위 안인지 봅니다. 텍셀을 잡기 전에 부릅니다 — 범위 밖 값이 큰 할당을 일으키지 않게. */
            [[nodiscard]] static expected<void> checkSizeInRange( uint32 resolution, uint32 levelCount )
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

            /** @brief 단계 0 을 GGX 로 걸러 방향 @p normal 의 텍셀 하나를 만듭니다(N = V = R). */
            static float3 prefilterTexel( const ReflectionProbeAsset& data, const float3& normal, float32 roughness, uint32 sampleCount )
            {
                const float32 clampedRoughness = MathUtil::max( roughness, kMinRoughness );
                const float32 alpha            = clampedRoughness * clampedRoughness;
                const float3  up               = MathUtil::abs( normal._y ) < 0.999f ? float3{ 0.0f, 1.0f, 0.0f } : float3{ 1.0f, 0.0f, 0.0f };
                const float3  tangent          = up.cross( normal ).normalize();
                const float3  bitangent        = normal.cross( tangent );
                float3        sum{};
                float32       weight = 0.0f;
                for ( uint32 sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex )
                {
                    const float32 firstUnit      = ( static_cast<float32>( sampleIndex ) + 0.5f ) / static_cast<float32>( sampleCount );
                    const float32 secondUnit     = computeRadicalInverse( sampleIndex );
                    const float32 phi            = MathUtil::kTwoPi * firstUnit;
                    const float32 cosTheta       = MathUtil::sqrt( ( 1.0f - secondUnit ) / ( 1.0f + ( alpha * alpha - 1.0f ) * secondUnit ) );
                    const float32 sinTheta       = MathUtil::sqrt( MathUtil::max( 1.0f - cosTheta * cosTheta, 0.0f ) );
                    const float3  halfway        = tangent * ( sinTheta * MathUtil::cos( phi ) ) + bitangent * ( sinTheta * MathUtil::sin( phi ) ) + normal * cosTheta;
                    const float3  toLight        = halfway * ( 2.0f * normal.dot( halfway ) ) - normal;
                    const float32 normalDotLight = normal.dot( toLight );
                    if ( normalDotLight <= 0.0f )
                        continue;
                    sum += data.sampleLevel( 0, toLight.normalize() ) * normalDotLight;
                    weight += normalDotLight;
                }
                return weight > 0.0f ? sum / weight : data.sampleLevel( 0, normal );
            }
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
        if ( expected<void> size = ReflectionProbeAssetInternal::checkSizeInRange( _resolution, _levelCount ); size.has_value() == false )
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
                                                        ReflectionProbeAsset& outData )
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
        if ( expected<void> size = Internal::checkSizeInRange( resolution, levelCount ); size.has_value() == false )
            return size;
        outData._capturePosition = capturePosition;
        outData._resolution      = resolution;
        outData._levelCount      = levelCount;
        outData._listTexel.assign( static_cast<size_t>( resolution ) * resolution * levelCount, float3{} );

        // 단계 0 — 텍셀 중심의 방향으로 면을 읽는다.
        const float32 texelSize = 1.0f / static_cast<float32>( resolution );
        for ( uint32 y = 0; y < resolution; ++y )
        {
            for ( uint32 x = 0; x < resolution; ++x )
            {
                const float3 direction                                   = ReflectionProbeAsset::decodeOctahedral( float2{ ( static_cast<float32>( x ) + 0.5f ) * texelSize, ( static_cast<float32>( y ) + 0.5f ) * texelSize } );
                outData._listTexel[outData.computeTexelIndex( 0, x, y )] = Internal::sampleFaces( pArrFace, capturePosition, direction );
            }
        }
        // 단계 1 부터 — 단계 0 을 그 단계의 거칠기로 거른다.
        for ( uint32 level = 1; level < levelCount; ++level )
        {
            const float32 roughness = outData.computeLevelRoughness( level );
            for ( uint32 y = 0; y < resolution; ++y )
            {
                for ( uint32 x = 0; x < resolution; ++x )
                {
                    const float3 normal                                          = ReflectionProbeAsset::decodeOctahedral( float2{ ( static_cast<float32>( x ) + 0.5f ) * texelSize, ( static_cast<float32>( y ) + 0.5f ) * texelSize } );
                    outData._listTexel[outData.computeTexelIndex( level, x, y )] = Internal::prefilterTexel( outData, normal, roughness, sampleCount );
                }
            }
        }
        return {};
    }
} // namespace sw

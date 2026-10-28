#include "pch.h"

#include "Engine/Graphics/Mesh/MeshVertexAnimation.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/NormalizedInt.h"
#include "Core/Math/OctahedralNormal.h"
#include "Core/Memory/Memory.h"

#include "Engine/Graphics/Mesh/MeshAssetFormat.h"
#include "Engine/Resource/ResourceIO.h"

namespace sw
{
    SW_LOG_CALLER( "MeshVertexAnimation" );

    namespace
    {
        struct MeshVertexAnimationInternal
        {
            /** @brief 쿠킹본 매직, 헤더 크기, 플래그입니다. */
            static constexpr uint8  kArrMagic[4]    = { 'S', 'W', 'V', 'A' };
            static constexpr size_t kHeaderSize     = 52;
            static constexpr uint32 kFlagLoop       = 1u;
            static constexpr uint32 kFlagAnchorRoot = 2u;

            /** @brief 값 하나를 리틀 엔디언 그대로 덧붙입니다(엔진은 리틀 엔디언만 생성한다). */
            template <typename T>
            static void appendValue( vector<uint8>& outBytes, const T& value )
            {
                const size_t offset = outBytes.size();
                outBytes.resize( offset + sizeof( T ) );
                Memory::copy( outBytes.data() + offset, &value, sizeof( T ) );
            }

            template <typename T>
            static T readValue( const uint8* pData, size_t offset )
            {
                T value{};
                Memory::copy( &value, pData + offset, sizeof( T ) );
                return value;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool MeshVertexAnimation::quantize( const vector<float3>& listPosition, const vector<float3>& listNormal )
    {
        const size_t elementCount = static_cast<size_t>( _frameCount ) * _vertexCount;
        _listFrameVertex.clear();
        if ( elementCount == 0 || listPosition.size() != elementCount || listNormal.size() != elementCount )
        {
            SW_LOG_ERROR( "Vertex animation quantize: expected %# elements, got %# positions and %# normals", elementCount, listPosition.size(), listNormal.size() );
            return false;
        }
        float3 minimum = listPosition[0];
        float3 maximum = listPosition[0];
        for ( const float3& position : listPosition )
        {
            minimum = float3{ MathUtil::min( minimum._x, position._x ), MathUtil::min( minimum._y, position._y ), MathUtil::min( minimum._z, position._z ) };
            maximum = float3{ MathUtil::max( maximum._x, position._x ), MathUtil::max( maximum._y, position._y ), MathUtil::max( maximum._z, position._z ) };
        }
        _boundsCenter     = float3{ ( minimum._x + maximum._x ) * 0.5f, ( minimum._y + maximum._y ) * 0.5f, ( minimum._z + maximum._z ) * 0.5f };
        _boundsHalfExtent = float3{ ( maximum._x - minimum._x ) * 0.5f, ( maximum._y - minimum._y ) * 0.5f, ( maximum._z - minimum._z ) * 0.5f };
        // 한 축의 크기가 0 이면 정규화 값은 0 이고 풀면 중심이다(0 으로 나누지 않는다).
        const float32 invX = _boundsHalfExtent._x > 0.0f ? 1.0f / _boundsHalfExtent._x : 0.0f;
        const float32 invY = _boundsHalfExtent._y > 0.0f ? 1.0f / _boundsHalfExtent._y : 0.0f;
        const float32 invZ = _boundsHalfExtent._z > 0.0f ? 1.0f / _boundsHalfExtent._z : 0.0f;
        _listFrameVertex.resize( elementCount );
        for ( size_t index = 0; index < elementCount; ++index )
        {
            const float3&               position = listPosition[index];
            const OctahedralNormal      octant   = OctahedralNormal::fromNormal( listNormal[index] );
            MeshVertexAnimationElement& element  = _listFrameVertex[index];
            element._x                           = snorm16::fromFloat( ( position._x - _boundsCenter._x ) * invX ).getRaw();
            element._y                           = snorm16::fromFloat( ( position._y - _boundsCenter._y ) * invY ).getRaw();
            element._z                           = snorm16::fromFloat( ( position._z - _boundsCenter._z ) * invZ ).getRaw();
            element._normalU                     = snorm8::fromFloat( octant._u.toFloat() ).getRaw();
            element._normalV                     = snorm8::fromFloat( octant._v.toFloat() ).getRaw();
        }
        return true;
    }

    float3 MeshVertexAnimation::unpackPosition( const MeshVertexAnimationElement& element ) const
    {
        return float3{ _boundsCenter._x + _boundsHalfExtent._x * snorm16::fromRaw( element._x ).toFloat(),
                       _boundsCenter._y + _boundsHalfExtent._y * snorm16::fromRaw( element._y ).toFloat(),
                       _boundsCenter._z + _boundsHalfExtent._z * snorm16::fromRaw( element._z ).toFloat() };
    }

    float3 MeshVertexAnimation::unpackNormal( const MeshVertexAnimationElement& element )
    {
        return OctahedralNormal::decodeOctant( snorm8::fromRaw( element._normalU ).toFloat(), snorm8::fromRaw( element._normalV ).toFloat() );
    }

    float3 MeshVertexAnimation::samplePosition( float32 time, uint32 vertexIndex ) const
    {
        if ( isEmpty() || vertexIndex >= _vertexCount )
            return float3{};
        // 셰이더(swLoadVertexAnimation)와 같은 식이다. 반복이면 프레임 수로 감고, 아니면 마지막 프레임에 멈춘다.
        const float32 framePosition = MathUtil::max( time, 0.0f ) * _framesPerSecond;
        const float32 frameCount    = static_cast<float32>( _frameCount );
        float32       wrapped       = framePosition;
        if ( _bLoop == SW_TRUE )
            wrapped = framePosition - MathUtil::floor( framePosition / frameCount ) * frameCount;
        else
            wrapped = MathUtil::min( framePosition, frameCount - 1.0f );
        const uint32  firstFrame  = MathUtil::min( static_cast<uint32>( wrapped ), _frameCount - 1u );
        const uint32  secondFrame = ( _bLoop == SW_TRUE ) ? ( firstFrame + 1u ) % _frameCount : MathUtil::min( firstFrame + 1u, _frameCount - 1u );
        const float32 alpha       = wrapped - static_cast<float32>( firstFrame );
        const float3  first       = unpackPosition( _listFrameVertex[static_cast<size_t>( firstFrame ) * _vertexCount + vertexIndex] );
        const float3  second      = unpackPosition( _listFrameVertex[static_cast<size_t>( secondFrame ) * _vertexCount + vertexIndex] );
        return float3{ first._x + ( second._x - first._x ) * alpha, first._y + ( second._y - first._y ) * alpha, first._z + ( second._z - first._z ) * alpha };
    }

    void MeshVertexAnimation::makeBytes( vector<uint8>& outBytes ) const
    {
        outBytes.clear();
        outBytes.reserve( MeshVertexAnimationInternal::kHeaderSize + _listFrameVertex.size() * sizeof( MeshVertexAnimationElement ) );
        outBytes.insert( outBytes.end(), std::begin( MeshVertexAnimationInternal::kArrMagic ), std::end( MeshVertexAnimationInternal::kArrMagic ) );
        const uint32 flags = ( _bLoop == SW_TRUE ? MeshVertexAnimationInternal::kFlagLoop : 0u ) |
                             ( _bAnchorRootMotion == SW_TRUE ? MeshVertexAnimationInternal::kFlagAnchorRoot : 0u );
        MeshVertexAnimationInternal::appendValue( outBytes, kVersion );
        MeshVertexAnimationInternal::appendValue( outBytes, _frameCount );
        MeshVertexAnimationInternal::appendValue( outBytes, _vertexCount );
        MeshVertexAnimationInternal::appendValue( outBytes, _framesPerSecond );
        MeshVertexAnimationInternal::appendValue( outBytes, _duration );
        MeshVertexAnimationInternal::appendValue( outBytes, flags );
        MeshVertexAnimationInternal::appendValue( outBytes, _boundsCenter._x );
        MeshVertexAnimationInternal::appendValue( outBytes, _boundsCenter._y );
        MeshVertexAnimationInternal::appendValue( outBytes, _boundsCenter._z );
        MeshVertexAnimationInternal::appendValue( outBytes, _boundsHalfExtent._x );
        MeshVertexAnimationInternal::appendValue( outBytes, _boundsHalfExtent._y );
        MeshVertexAnimationInternal::appendValue( outBytes, _boundsHalfExtent._z );
        const size_t offset = outBytes.size();
        outBytes.resize( offset + _listFrameVertex.size() * sizeof( MeshVertexAnimationElement ) );
        if ( _listFrameVertex.empty() == false )
            Memory::copy( outBytes.data() + offset, _listFrameVertex.data(), _listFrameVertex.size() * sizeof( MeshVertexAnimationElement ) );
    }

    bool MeshVertexAnimation::readFromBytes( const uint8* pData, size_t byteCount, string_view sourceLabel )
    {
        *this                              = MeshVertexAnimation{};
        const bool bNotVertexAnimationFile = pData == nullptr || byteCount < MeshVertexAnimationInternal::kHeaderSize ||
                                             Memory::compare( pData, MeshVertexAnimationInternal::kArrMagic, sizeof( MeshVertexAnimationInternal::kArrMagic ) ) != 0;
        if ( bNotVertexAnimationFile )
        {
            SW_LOG_ERROR( "Vertex animation '%#' is not a SWVA file", sourceLabel );
            return false;
        }
        const uint32 version = MeshVertexAnimationInternal::readValue<uint32>( pData, 4 );
        if ( version != kVersion )
        {
            SW_LOG_ERROR( "Vertex animation '%#' is version %#, not the current %# - cook again", sourceLabel, version, kVersion );
            return false;
        }
        const uint32  frameCount  = MeshVertexAnimationInternal::readValue<uint32>( pData, 8 );
        const uint32  vertexCount = MeshVertexAnimationInternal::readValue<uint32>( pData, 12 );
        const float32 frameRate   = MeshVertexAnimationInternal::readValue<float32>( pData, 16 );
        const float32 duration    = MeshVertexAnimationInternal::readValue<float32>( pData, 20 );
        const uint32  flags       = MeshVertexAnimationInternal::readValue<uint32>( pData, 24 );
        const float3  boundsCenter{ MeshVertexAnimationInternal::readValue<float32>( pData, 28 ), MeshVertexAnimationInternal::readValue<float32>( pData, 32 ),
                                   MeshVertexAnimationInternal::readValue<float32>( pData, 36 ) };
        const float3  boundsHalfExtent{ MeshVertexAnimationInternal::readValue<float32>( pData, 40 ), MeshVertexAnimationInternal::readValue<float32>( pData, 44 ),
                                       MeshVertexAnimationInternal::readValue<float32>( pData, 48 ) };
        const size_t  elementCount              = static_cast<size_t>( frameCount ) * vertexCount;
        const bool    bMalformedVertexAnimation = frameCount == 0 || vertexCount == 0 || frameRate <= 0.0f || byteCount != MeshVertexAnimationInternal::kHeaderSize + elementCount * sizeof( MeshVertexAnimationElement );
        if ( bMalformedVertexAnimation )
        {
            SW_LOG_ERROR( "Vertex animation '%#' is malformed (%# frames x %# vertices, %# bytes)", sourceLabel, frameCount, vertexCount, byteCount );
            return false;
        }
        _listFrameVertex.resize( elementCount );
        Memory::copy( _listFrameVertex.data(), pData + MeshVertexAnimationInternal::kHeaderSize, elementCount * sizeof( MeshVertexAnimationElement ) );
        _boundsCenter      = boundsCenter;
        _boundsHalfExtent  = boundsHalfExtent;
        _frameCount        = frameCount;
        _vertexCount       = vertexCount;
        _framesPerSecond   = frameRate;
        _duration          = duration;
        _bLoop             = ( flags & MeshVertexAnimationInternal::kFlagLoop ) != 0 ? SW_TRUE : SW_FALSE;
        _bAnchorRootMotion = ( flags & MeshVertexAnimationInternal::kFlagAnchorRoot ) != 0 ? SW_TRUE : SW_FALSE;
        return true;
    }

    bool MeshVertexAnimation::saveToFile( string_view path ) const
    {
        vector<uint8> bytes;
        makeBytes( bytes );
        if ( FileUtil::ensureParentDirectoryExists( path ) == false )
            return false;
        return FileUtil::writeFile( path, bytes.data(), bytes.size() );
    }

    bool MeshVertexAnimation::loadFromResource( const ResourceID& resourceID )
    {
        const string_view path = resourceID.view();
        vector<uint8>     bytes;
        if ( ResourceIO::readBinaryResource( resourceID, bytes ) == false )
        {
            SW_LOG_ERROR( "Vertex animation '%#' could not be read", path );
            *this = MeshVertexAnimation{};
            return false;
        }
        return readFromBytes( bytes.data(), bytes.size(), path );
    }

    string MeshVertexAnimation::makeCookedPath( string_view meshPath, const hashed_string& clipName )
    {
        if ( StringUtil::endsWith( meshPath, MeshAssetFormat::kExtension, true ) == false || clipName.empty() )
            return string{};
        string path{ meshPath.substr( 0, meshPath.size() - MeshAssetFormat::kExtension.size() ) };
        path.append( "." );
        path.append( StringUtil::toLower( clipName.c_str() ) );
        path.append( kExtension.data(), kExtension.size() );
        return path;
    }
} // namespace sw

namespace sw
{
    SW_ASSET_TYPE( MeshVertexAnimation, MeshVertexAnimation::kAssetType );
} // namespace sw

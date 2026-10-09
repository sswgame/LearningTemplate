#include "pch.h"

#include "Engine/Graphics/Mesh/MeshVertexAnimation.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"

#include "Engine/Animation/AnimClip.h"
#include "Engine/Animation/Skeletal/Pose.h"
#include "Engine/Animation/Skeletal/Skeleton.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/Mesh/MeshAssetFormat.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Resource/ResourceUtil.h"

namespace sw
{
    SW_LOG_CALLER( "MeshVertexAnimation" );

    namespace
    {
        struct MeshVertexAnimationInternal
        {
            /** @brief 부호(0 은 +)입니다. */
            static float32 signNotZero( float32 value ) { return value >= 0.0f ? 1.0f : -1.0f; }

            /** @brief 쿠킹본 매직 · 머리 크기 · 플래그입니다. */
            static constexpr uint8  kArrMagic[4]    = { 'S', 'W', 'V', 'A' };
            static constexpr size_t kHeaderSize     = 28;
            static constexpr uint32 kFlagLoop       = 1u;
            static constexpr uint32 kFlagAnchorRoot = 2u;

            /** @brief 값 하나를 리틀 엔디언 그대로 덧붙입니다(엔진은 리틀 엔디언만 짓는다). */
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
    float32 MeshVertexAnimation::packNormal( const float3& normal )
    {
        // 팔면체 사상 — 단위 구를 팔면체에 투영하고 아래 반구를 접어 [-1, 1]^2 사각형에 펼친다.
        const float32 sum = MathUtil::abs( normal._x ) + MathUtil::abs( normal._y ) + MathUtil::abs( normal._z );
        float32       u   = sum > 0.0f ? normal._x / sum : 0.0f;
        float32       v   = sum > 0.0f ? normal._y / sum : 0.0f;
        if ( normal._z < 0.0f )
        {
            const float32 foldedU = ( 1.0f - MathUtil::abs( v ) ) * MeshVertexAnimationInternal::signNotZero( u );
            const float32 foldedV = ( 1.0f - MathUtil::abs( u ) ) * MeshVertexAnimationInternal::signNotZero( v );
            u                     = foldedU;
            v                     = foldedV;
        }
        constexpr float32 kMaxStep = static_cast<float32>( shaderslot::kVertexAnimationNormalSteps - 1u );
        const uint32      stepU    = static_cast<uint32>( MathUtil::clamp( ( u * 0.5f + 0.5f ) * kMaxStep + 0.5f, 0.0f, kMaxStep ) );
        const uint32      stepV    = static_cast<uint32>( MathUtil::clamp( ( v * 0.5f + 0.5f ) * kMaxStep + 0.5f, 0.0f, kMaxStep ) );
        return static_cast<float32>( stepU * shaderslot::kVertexAnimationNormalSteps + stepV );
    }

    float3 MeshVertexAnimation::unpackNormal( float32 packed )
    {
        constexpr float32 kMaxStep = static_cast<float32>( shaderslot::kVertexAnimationNormalSteps - 1u );
        const uint32      integer  = static_cast<uint32>( packed );
        const float32     u        = static_cast<float32>( integer / shaderslot::kVertexAnimationNormalSteps ) / kMaxStep * 2.0f - 1.0f;
        const float32     v        = static_cast<float32>( integer % shaderslot::kVertexAnimationNormalSteps ) / kMaxStep * 2.0f - 1.0f;
        float3            normal{ u, v, 1.0f - MathUtil::abs( u ) - MathUtil::abs( v ) };
        if ( normal._z < 0.0f )
        {
            const float32 foldedX = ( 1.0f - MathUtil::abs( normal._y ) ) * MeshVertexAnimationInternal::signNotZero( normal._x );
            const float32 foldedY = ( 1.0f - MathUtil::abs( normal._x ) ) * MeshVertexAnimationInternal::signNotZero( normal._y );
            normal._x             = foldedX;
            normal._y             = foldedY;
        }
        return normal.normalize();
    }

    float3 MeshVertexAnimation::samplePosition( float32 time, uint32 vertexIndex ) const
    {
        if ( isEmpty() || vertexIndex >= _vertexCount )
            return float3{};
        // 셰이더(swLoadVertexAnimation)와 같은 식 — 반복이면 프레임 수로 감고, 아니면 마지막 프레임에 멈춘다.
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
        const float4& first       = _listFrameVertex[static_cast<size_t>( firstFrame ) * _vertexCount + vertexIndex];
        const float4& second      = _listFrameVertex[static_cast<size_t>( secondFrame ) * _vertexCount + vertexIndex];
        return float3{ first._x + ( second._x - first._x ) * alpha, first._y + ( second._y - first._y ) * alpha, first._z + ( second._z - first._z ) * alpha };
    }

    void MeshVertexAnimation::makeBytes( vector<uint8>& outBytes ) const
    {
        outBytes.clear();
        outBytes.reserve( MeshVertexAnimationInternal::kHeaderSize + _listFrameVertex.size() * sizeof( float4 ) );
        outBytes.insert( outBytes.end(), std::begin( MeshVertexAnimationInternal::kArrMagic ), std::end( MeshVertexAnimationInternal::kArrMagic ) );
        const uint32 flags = ( _bLoop == SW_TRUE ? MeshVertexAnimationInternal::kFlagLoop : 0u ) |
                             ( _bAnchorRootMotion == SW_TRUE ? MeshVertexAnimationInternal::kFlagAnchorRoot : 0u );
        MeshVertexAnimationInternal::appendValue( outBytes, kVersion );
        MeshVertexAnimationInternal::appendValue( outBytes, _frameCount );
        MeshVertexAnimationInternal::appendValue( outBytes, _vertexCount );
        MeshVertexAnimationInternal::appendValue( outBytes, _framesPerSecond );
        MeshVertexAnimationInternal::appendValue( outBytes, _duration );
        MeshVertexAnimationInternal::appendValue( outBytes, flags );
        const size_t offset = outBytes.size();
        outBytes.resize( offset + _listFrameVertex.size() * sizeof( float4 ) );
        if ( _listFrameVertex.empty() == false )
            Memory::copy( outBytes.data() + offset, _listFrameVertex.data(), _listFrameVertex.size() * sizeof( float4 ) );
    }

    bool MeshVertexAnimation::readFromBytes( const uint8* pData, size_t byteCount, string_view sourceLabel )
    {
        *this = MeshVertexAnimation{};
        if ( pData == nullptr || byteCount < MeshVertexAnimationInternal::kHeaderSize ||
             Memory::compare( pData, MeshVertexAnimationInternal::kArrMagic, sizeof( MeshVertexAnimationInternal::kArrMagic ) ) != 0 )
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
        const uint32  frameCount   = MeshVertexAnimationInternal::readValue<uint32>( pData, 8 );
        const uint32  vertexCount  = MeshVertexAnimationInternal::readValue<uint32>( pData, 12 );
        const float32 frameRate    = MeshVertexAnimationInternal::readValue<float32>( pData, 16 );
        const float32 duration     = MeshVertexAnimationInternal::readValue<float32>( pData, 20 );
        const uint32  flags        = MeshVertexAnimationInternal::readValue<uint32>( pData, 24 );
        const size_t  elementCount = static_cast<size_t>( frameCount ) * vertexCount;
        if ( frameCount == 0 || vertexCount == 0 || frameRate <= 0.0f || byteCount != MeshVertexAnimationInternal::kHeaderSize + elementCount * sizeof( float4 ) )
        {
            SW_LOG_ERROR( "Vertex animation '%#' is malformed (%# frames x %# vertices, %# bytes)", sourceLabel, frameCount, vertexCount, byteCount );
            return false;
        }
        _listFrameVertex.resize( elementCount );
        Memory::copy( _listFrameVertex.data(), pData + MeshVertexAnimationInternal::kHeaderSize, elementCount * sizeof( float4 ) );
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

    bool MeshVertexAnimation::loadFromResource( string_view path )
    {
        vector<uint8> bytes;
        if ( ResourceUtil::readBinaryResource( path, bytes ) == false )
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

    void MeshVertexAnimationBaker::skinVertices( const Mesh& skinMesh, const vector<float4x4>& listPalette, vector<float3>& outListPosition,
                                                 vector<float3>& outListNormal )
    {
        const vector<RHIVertex>&      listVertex = skinMesh.getVertices();
        const vector<MeshSkinVertex>& listSkin   = skinMesh.getSkinVertices();
        outListPosition.resize( listVertex.size() );
        outListNormal.resize( listVertex.size() );
        const uint32 boneCount = static_cast<uint32>( listPalette.size() );
        for ( size_t vertexIndex = 0; vertexIndex < listVertex.size(); ++vertexIndex )
        {
            const RHIVertex& vertex = listVertex[vertexIndex];
            const float3     position{ vertex._arrPosition[0], vertex._arrPosition[1], vertex._arrPosition[2] };
            const float3     normal{ vertex._arrNormal[0], vertex._arrNormal[1], vertex._arrNormal[2] };
            if ( vertexIndex >= listSkin.size() || boneCount == 0 )
            {
                outListPosition[vertexIndex] = position;
                outListNormal[vertexIndex]   = normal;
                continue;
            }
            // meshskin.hlsl 과 같은 식 — 본 넷을 가중치로 섞고(본 번호는 본 수 안으로), 노멀은 이동 없이 옮겨 정규화한다.
            float3 skinnedPosition{};
            float3 skinnedNormal{};
            for ( uint32 influence = 0; influence < 4; ++influence )
            {
                const uint32    bone   = MathUtil::min( static_cast<uint32>( listSkin[vertexIndex]._arrJoint[influence] ), boneCount - 1u );
                const float32   weight = listSkin[vertexIndex]._arrWeight[influence];
                const float4x4& matrix = listPalette[bone];
                skinnedPosition        = skinnedPosition + float3::transform( position, matrix ) * weight;
                skinnedNormal          = skinnedNormal + float3::transformVector( normal, matrix ) * weight;
            }
            const float32 normalLength   = skinnedNormal.getLength();
            outListPosition[vertexIndex] = skinnedPosition;
            outListNormal[vertexIndex]   = normalLength > 1e-6f ? skinnedNormal * ( 1.0f / normalLength ) : normal;
        }
    }

    bool MeshVertexAnimationBaker::bake( const Mesh& skinMesh, const Skeleton& skeleton, const AnimClip& clip, float32 framesPerSecond, bool bAnchorRootMotion,
                                         MeshVertexAnimation& outAnimation )
    {
        outAnimation = MeshVertexAnimation{};
        if ( skinMesh.hasSkin() == false || skinMesh.getSkinBoneCount() != skeleton.getBoneCount() || framesPerSecond <= 0.0f )
        {
            SW_LOG_WARNING( "Vertex animation bake skipped: the mesh has no skin or its bone count (%#) differs from the skeleton (%#)", skinMesh.getSkinBoneCount(),
                            skeleton.getBoneCount() );
            return false;
        }
        const float32 duration = clip.getDuration();
        const bool    bLoop    = clip.isLoopingByDefault();
        // 반복 클립의 끝 프레임은 첫 프레임과 같아 싣지 않는다(셰이더가 감아 보간한다). 반복하지 않으면 끝 시각을 마지막 프레임으로 싣는다.
        const uint32 frameCount  = bLoop ? MathUtil::max( static_cast<uint32>( MathUtil::round( duration * framesPerSecond ) ), 1u )
                                         : static_cast<uint32>( MathUtil::floor( duration * framesPerSecond ) ) + 1u;
        const uint32 vertexCount = skinMesh.getVertexCount();
        // 반복 클립은 표 한 바퀴가 클립 길이와 꼭 같아야 CPU 시각과 어긋나지 않는다 — 프레임율을 프레임 수 / 길이로 맞춘다.
        const float32 effectiveRate = ( bLoop && duration > 0.0f ) ? static_cast<float32>( frameCount ) / duration : framesPerSecond;

        vector<int32> listTrackToBone;
        clip.makeTrackToBoneMap( skeleton, listTrackToBone );
        Pose             pose;
        Pose             scratchTrackPose;
        vector<float4x4> listModel;
        vector<float4x4> listPalette;
        vector<float3>   listPosition;
        vector<float3>   listNormal;
        outAnimation._listFrameVertex.resize( static_cast<size_t>( frameCount ) * vertexCount );
        for ( uint32 frameIndex = 0; frameIndex < frameCount; ++frameIndex )
        {
            const float32 time = MathUtil::min( static_cast<float32>( frameIndex ) / effectiveRate, duration );
            pose.setToReference( skeleton );
            if ( clip.samplePose( time, listTrackToBone, pose, scratchTrackPose, bAnchorRootMotion ) == false )
                return false;
            pose.computeModelSpace( skeleton.getParentIndices(), listModel );
            Pose::computeSkinPalette( skeleton, listModel, listPalette );
            skinVertices( skinMesh, listPalette, listPosition, listNormal );
            float4* pFrame = &outAnimation._listFrameVertex[static_cast<size_t>( frameIndex ) * vertexCount];
            for ( uint32 vertexIndex = 0; vertexIndex < vertexCount; ++vertexIndex )
            {
                const float3& position = listPosition[vertexIndex];
                pFrame[vertexIndex]    = float4{ position._x, position._y, position._z, MeshVertexAnimation::packNormal( listNormal[vertexIndex] ) };
            }
        }
        outAnimation._frameCount        = frameCount;
        outAnimation._vertexCount       = vertexCount;
        outAnimation._framesPerSecond   = effectiveRate;
        outAnimation._duration          = duration;
        outAnimation._bLoop             = bLoop ? SW_TRUE : SW_FALSE;
        outAnimation._bAnchorRootMotion = bAnchorRootMotion ? SW_TRUE : SW_FALSE;
        return true;
    }
} // namespace sw

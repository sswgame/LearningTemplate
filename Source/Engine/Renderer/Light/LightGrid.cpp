#include "pch.h"

#include "Engine/Renderer/Light/LightGrid.h"

#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"

#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"

namespace sw
{
    namespace
    {
        struct LightGridInternal
        {
            /// @brief 원근 모서리의 클립 w 가 이보다 작으면 경계 구가 카메라 면을 넘는다 — XY 범위를 화면 전체로 둔다.
            static constexpr float32 kMinCornerClipW = 1e-4f;
            /// @brief XY 범위의 NDC 여유와 깊이 조각의 여유 — CPU 와 GPU 의 부동소수 차이로 경계의 픽셀이 이웃 클러스터에 떨어져도 라이트가 빠지지 않게 한다.
            static constexpr float32 kNdcPad   = 1e-3f;
            static constexpr float32 kSlicePad = 1e-2f;

            /** @brief 행벡터 규약으로 점을 클립 공간으로 옮깁니다(셰이더 `mul( float4( p, 1 ), m )`). */
            static float4 computeClip( const float3& position, const float4x4& matrix ) { return float4::transform( float4{ position._x, position._y, position._z, 1.0f }, matrix ); }

            /** @brief NDC 한 축의 값을 칸 번호로 바꿉니다(셰이더와 같은 식, 양 끝은 자른다). */
            static uint32 computeTile( float32 ndc, uint32 dimension )
            {
                const float32 position = MathUtil::saturate( ndc * 0.5f + 0.5f ) * static_cast<float32>( dimension );
                return MathUtil::min( static_cast<uint32>( position ), dimension - 1u );
            }

            /** @brief 실수 비트를 단어로 담습니다. */
            static uint32 makeFloatWord( float32 value )
            {
                uint32 word = 0;
                Memory::copy( &word, &value, sizeof( word ) );
                return word;
            }
        };

        static_assert( shaderslot::lightgrid::kWordsPerElement * sizeof( uint32 ) == sizeof( GPULight ), "라이트 그리드 원소 하나는 라이트 버퍼 원소 하나(64 바이트)다" );
    } // namespace
} // namespace sw

namespace sw
{
    uint32 LightGrid::getElementCount() const
    {
        return static_cast<uint32>( _listWord.size() ) / shaderslot::lightgrid::kWordsPerElement;
    }

    float32 LightGrid::computeSlicePosition( float32 depth ) const
    {
        if ( _bPerspective == SW_TRUE )
            return MathUtil::log2( MathUtil::max( depth, _nearDepth ) / _nearDepth ) * _sliceScale;
        return MathUtil::saturate( depth ) * _sliceScale;
    }

    bool LightGrid::computeLightRange( const float4x4& viewProj, const GPULight& light, ClusterRange& outRange ) const
    {
        using Internal       = LightGridInternal;
        const float3  center = float3{ light._positionRadius._x, light._positionRadius._y, light._positionRadius._z };
        const float32 radius = light._positionRadius._w;
        if ( radius <= 0.0f )
            return false;

        // 깊이 범위는 경계 구 중심의 깊이 ± 반경 × 깊이 기울기다(깊이는 월드 위치의 일차 함수라 정확하다).
        const float4  centerClip    = Internal::computeClip( center, viewProj );
        const float3  depthGradient = ( _bPerspective == SW_TRUE ) ? float3{ viewProj._14, viewProj._24, viewProj._34 } : float3{ viewProj._13, viewProj._23, viewProj._33 };
        const float32 centerDepth   = ( _bPerspective == SW_TRUE ) ? centerClip._w : centerClip._z;
        const float32 depthReach    = radius * depthGradient.getLength();
        const float32 minDepth      = centerDepth - depthReach;
        const float32 maxDepth      = centerDepth + depthReach;
        const bool    bBehind       = ( _bPerspective == SW_TRUE ) ? maxDepth < _nearDepth : maxDepth < 0.0f;
        const bool    bBeyond       = ( _bPerspective == SW_FALSE ) && minDepth > 1.0f;
        if ( bBehind || bBeyond )
            return false;

        // XY 범위는 월드 AABB 모서리 여덟 개의 NDC 범위다. 원근에서 모서리 하나라도 카메라 면을 넘으면 화면 전체다.
        bool    bFullScreen = false;
        float32 minX        = 1.0f;
        float32 maxX        = -1.0f;
        float32 minY        = 1.0f;
        float32 maxY        = -1.0f;
        for ( uint32 corner = 0; corner < 8u; ++corner )
        {
            const float3 offset{ ( corner & 1u ) != 0 ? radius : -radius, ( corner & 2u ) != 0 ? radius : -radius, ( corner & 4u ) != 0 ? radius : -radius };
            const float4 clip = Internal::computeClip( center + offset, viewProj );
            if ( _bPerspective == SW_TRUE && clip._w < Internal::kMinCornerClipW )
            {
                bFullScreen = true;
                break;
            }
            const float32 ndcX = clip._x / clip._w;
            const float32 ndcY = clip._y / clip._w;
            minX               = MathUtil::min( minX, ndcX );
            maxX               = MathUtil::max( maxX, ndcX );
            minY               = MathUtil::min( minY, ndcY );
            maxY               = MathUtil::max( maxY, ndcY );
        }
        if ( bFullScreen )
        {
            minX = -1.0f;
            maxX = 1.0f;
            minY = -1.0f;
            maxY = 1.0f;
        }
        const bool bOffScreen = maxX < -1.0f || 1.0f < minX || maxY < -1.0f || 1.0f < minY;
        if ( bOffScreen )
            return false;

        outRange._minX         = Internal::computeTile( minX - Internal::kNdcPad, kDimX );
        outRange._maxX         = Internal::computeTile( maxX + Internal::kNdcPad, kDimX );
        outRange._minY         = Internal::computeTile( minY - Internal::kNdcPad, kDimY );
        outRange._maxY         = Internal::computeTile( maxY + Internal::kNdcPad, kDimY );
        const float32 minSlice = MathUtil::max( computeSlicePosition( minDepth ) - Internal::kSlicePad, 0.0f );
        const float32 maxSlice = MathUtil::max( computeSlicePosition( maxDepth ) + Internal::kSlicePad, 0.0f );
        outRange._minZ         = MathUtil::min( static_cast<uint32>( minSlice ), kDimZ - 1u );
        outRange._maxZ         = MathUtil::min( static_cast<uint32>( maxSlice ), kDimZ - 1u );
        return true;
    }

    bool LightGrid::populate( const float4x4& viewProj, const GPULight* pLight, uint32 lightCount )
    {
        using Internal = LightGridInternal;
        namespace grid = shaderslot::lightgrid;
        _listWord.clear();
        _listRangeScratch.clear();
        _listGlobalScratch.clear();
        _viewProj     = viewProj;
        _bPerspective = ( MathUtil::abs( viewProj._14 ) + MathUtil::abs( viewProj._24 ) + MathUtil::abs( viewProj._34 ) ) > MathUtil::kEpsilon ? SW_TRUE : SW_FALSE;

        // 원근의 가까운 면 · 먼 면 클립 w. NDC (0, 0, z, 1) 를 역행렬로 되돌린 동차 좌표의 w 가 1 / 클립 w 다.
        _nearDepth  = 1.0f;
        _sliceScale = static_cast<float32>( kDimZ );
        if ( _bPerspective == SW_TRUE )
        {
            const float4x4 inverse   = viewProj.invert();
            const float4   nearPoint = float4::transform( float4{ 0.0f, 0.0f, 0.0f, 1.0f }, inverse );
            const float4   farPoint  = float4::transform( float4{ 0.0f, 0.0f, 1.0f, 1.0f }, inverse );
            const float32  nearDepth = 1.0f / nearPoint._w;
            const float32  farDepth  = 1.0f / farPoint._w;
            const bool     bUsable   = MathUtil::isFinite( nearDepth ) && MathUtil::isFinite( farDepth ) && 0.0f < nearDepth && nearDepth < farDepth;
            if ( bUsable == false )
                return false;
            _nearDepth  = nearDepth;
            _sliceScale = static_cast<float32>( kDimZ ) / MathUtil::log2( farDepth / nearDepth );
        }

        // 1) 라이트마다 전역 구간이거나 클러스터 범위다. 2D 라이트와 2D 그림자 세그먼트는 넣지 않는다(3D 조명 식이 건너뛴다).
        for ( uint32 lightIndex = 0; lightIndex < lightCount; ++lightIndex )
        {
            const GPULight& light     = pLight[lightIndex];
            const uint32    lightType = static_cast<uint32>( light._directionType._w + 0.5f );
            if ( lightType == shaderslot::kLightTypeDirectional )
            {
                _listGlobalScratch.push_back( lightIndex );
                continue;
            }
            const bool   bLocal = lightType == shaderslot::kLightTypePoint || lightType == shaderslot::kLightTypeSpot;
            ClusterRange range{};
            if ( bLocal == false || computeLightRange( viewProj, light, range ) == false )
                continue;
            range._lightIndex = lightIndex;
            _listRangeScratch.push_back( range );
        }

        // 2) 클러스터마다 개수를 세고 시작을 정한 뒤 채운다(목록을 빈틈없이 압축한다).
        _listClusterScratch.assign( kClusterCount, 0u );
        for ( const ClusterRange& range : _listRangeScratch )
        {
            for ( uint32 slice = range._minZ; slice <= range._maxZ; ++slice )
            {
                for ( uint32 tileY = range._minY; tileY <= range._maxY; ++tileY )
                {
                    for ( uint32 tileX = range._minX; tileX <= range._maxX; ++tileX )
                    {
                        ++_listClusterScratch[( slice * kDimY + tileY ) * kDimX + tileX];
                    }
                }
            }
        }
        const uint32 globalCount = static_cast<uint32>( _listGlobalScratch.size() );
        const uint32 clusterBase = grid::kHeaderWordCount;
        const uint32 indexBase   = clusterBase + kClusterCount * grid::kWordsPerCluster;
        uint32       entryCount  = globalCount;
        _listWord.assign( indexBase, 0u );
        for ( uint32 cluster = 0; cluster < kClusterCount; ++cluster )
        {
            const uint32 count                                             = _listClusterScratch[cluster];
            _listWord[clusterBase + cluster * grid::kWordsPerCluster]      = entryCount;
            _listWord[clusterBase + cluster * grid::kWordsPerCluster + 1u] = count;
            _listClusterScratch[cluster]                                   = entryCount; // 이제부터는 채우기 커서다
            entryCount += count;
        }
        const uint32 paddedCount = ( ( indexBase + entryCount + grid::kWordsPerElement - 1u ) / grid::kWordsPerElement ) * grid::kWordsPerElement;
        _listWord.resize( paddedCount, 0u );
        for ( uint32 globalIndex = 0; globalIndex < globalCount; ++globalIndex )
        {
            _listWord[indexBase + globalIndex] = _listGlobalScratch[globalIndex];
        }
        for ( const ClusterRange& range : _listRangeScratch )
        {
            for ( uint32 slice = range._minZ; slice <= range._maxZ; ++slice )
            {
                for ( uint32 tileY = range._minY; tileY <= range._maxY; ++tileY )
                {
                    for ( uint32 tileX = range._minX; tileX <= range._maxX; ++tileX )
                    {
                        uint32& cursor                = _listClusterScratch[( slice * kDimY + tileY ) * kDimX + tileX];
                        _listWord[indexBase + cursor] = range._lightIndex;
                        ++cursor;
                    }
                }
            }
        }

        _listWord[grid::kWordDimX]        = kDimX;
        _listWord[grid::kWordDimY]        = kDimY;
        _listWord[grid::kWordDimZ]        = kDimZ;
        _listWord[grid::kWordPerspective] = _bPerspective == SW_TRUE ? 1u : 0u;
        _listWord[grid::kWordNearDepth]   = Internal::makeFloatWord( _nearDepth );
        _listWord[grid::kWordSliceScale]  = Internal::makeFloatWord( _sliceScale );
        _listWord[grid::kWordGlobalCount] = globalCount;
        _listWord[grid::kWordClusterBase] = clusterBase;
        _listWord[grid::kWordIndexBase]   = indexBase;
        return true;
    }

    uint32 LightGrid::computeClusterIndex( const float3& worldPosition ) const
    {
        using Internal = LightGridInternal;
        if ( _listWord.empty() )
            return 0;
        const float4  clip          = Internal::computeClip( worldPosition, _viewProj );
        const float32 ndcX          = clip._x / clip._w;
        const float32 ndcY          = clip._y / clip._w;
        const float32 ndcZ          = clip._z / clip._w;
        const uint32  tileX         = Internal::computeTile( ndcX, kDimX );
        const uint32  tileY         = Internal::computeTile( ndcY, kDimY );
        const float32 slicePosition = computeSlicePosition( _bPerspective == SW_TRUE ? clip._w : ndcZ );
        const uint32  slice         = MathUtil::min( static_cast<uint32>( MathUtil::max( slicePosition, 0.0f ) ), kDimZ - 1u );
        return ( slice * kDimY + tileY ) * kDimX + tileX;
    }

    uint32 LightGrid::getGlobalLightCount() const
    {
        return _listWord.empty() ? 0u : _listWord[shaderslot::lightgrid::kWordGlobalCount];
    }

    uint32 LightGrid::getClusterLightCount( uint32 clusterIndex ) const
    {
        namespace grid = shaderslot::lightgrid;
        if ( _listWord.empty() || clusterIndex >= kClusterCount )
            return 0u;
        return _listWord[_listWord[grid::kWordClusterBase] + clusterIndex * grid::kWordsPerCluster + 1u];
    }

    void LightGrid::collectLights( const float3& worldPosition, vector<uint32>& outListLightIndex ) const
    {
        namespace grid = shaderslot::lightgrid;
        outListLightIndex.clear();
        if ( _listWord.empty() )
            return;
        const uint32 indexBase   = _listWord[grid::kWordIndexBase];
        const uint32 globalCount = _listWord[grid::kWordGlobalCount];
        for ( uint32 entry = 0; entry < globalCount; ++entry )
        {
            outListLightIndex.push_back( _listWord[indexBase + entry] );
        }
        const uint32 clusterWord = _listWord[grid::kWordClusterBase] + computeClusterIndex( worldPosition ) * grid::kWordsPerCluster;
        const uint32 first       = _listWord[clusterWord];
        const uint32 count       = _listWord[clusterWord + 1u];
        for ( uint32 entry = 0; entry < count; ++entry )
        {
            outListLightIndex.push_back( _listWord[indexBase + first + entry] );
        }
    }
} // namespace sw

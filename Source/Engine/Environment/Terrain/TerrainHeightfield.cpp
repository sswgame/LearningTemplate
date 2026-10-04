#include "pch.h"

#include "Engine/Environment/Terrain/TerrainHeightfield.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Environment/Terrain/HeightfieldData.h"

namespace sw
{
    SW_LOG_CALLER( "TerrainHeightfield" );

    TerrainHeightfield::TerrainHeightfield()
        : _listHeight{}
        , _listHoleCell{}
        , _splatBytes{}
        , _origin{}
        , _size{}
        , _cellSize{}
        , _resolution{ 0 }
        , _splatWidth{ 0 }
        , _splatHeight{ 0 }
    {
    }

    bool TerrainHeightfield::initialize( const HeightfieldData& data, const float3& origin, const float2& size, float32 heightMin, float32 heightMax )
    {
        shutdown();
        if ( data.isValid() == false || size._x <= 0.0f || size._y <= 0.0f )
        {
            SW_LOG_WARNING( "Terrain heightfield is not usable (resolution %#, size %#x%#)", data._resolution, size._x, size._y );
            return false;
        }
        _resolution = data._resolution;
        _origin     = origin;
        _size       = size;
        _cellSize   = float2{ size._x / static_cast<float32>( _resolution - 1 ), size._y / static_cast<float32>( _resolution - 1 ) };
        _listHeight.resize( data._listHeight.size() );
        const float32 heightRange = heightMax - heightMin;
        for ( size_t sampleIndex = 0; sampleIndex < data._listHeight.size(); ++sampleIndex )
            _listHeight[sampleIndex] = origin._y + heightMin + static_cast<float32>( data._listHeight[sampleIndex] ) * ( 1.0f / 65535.0f ) * heightRange;
        _listHoleCell = data._listHoleCell;
        return true;
    }

    void TerrainHeightfield::shutdown()
    {
        _listHeight.clear();
        _listHoleCell.clear();
        _resolution = 0;
    }

    void TerrainHeightfield::setSplat( uint32 width, uint32 height, const vector<uint8>& rgbaBytes )
    {
        const bool bMatches = width > 0 && height > 0 && static_cast<size_t>( width ) * height * 4 <= rgbaBytes.size();
        _splatWidth         = bMatches ? width : 0u;
        _splatHeight        = bMatches ? height : 0u;
        _splatBytes.assign( rgbaBytes.begin(), bMatches ? rgbaBytes.begin() + static_cast<ptrdiff_t>( static_cast<size_t>( width ) * height * 4 ) : rgbaBytes.begin() );
    }

    float32 TerrainHeightfield::getSampleHeight( int32 sampleX, int32 sampleZ ) const
    {
        const int32 lastIndex = static_cast<int32>( _resolution ) - 1;
        const int32 clampedX  = MathUtil::clamp( sampleX, 0, lastIndex );
        const int32 clampedZ  = MathUtil::clamp( sampleZ, 0, lastIndex );
        return _listHeight[static_cast<size_t>( clampedZ ) * _resolution + static_cast<size_t>( clampedX )];
    }

    float3 TerrainHeightfield::getSamplePosition( uint32 sampleX, uint32 sampleZ ) const
    {
        return float3{ _origin._x + static_cast<float32>( sampleX ) * _cellSize._x, getSampleHeight( static_cast<int32>( sampleX ), static_cast<int32>( sampleZ ) ),
                       _origin._z + static_cast<float32>( sampleZ ) * _cellSize._y };
    }

    float3 TerrainHeightfield::computeSampleNormal( uint32 sampleX, uint32 sampleZ ) const
    {
        // 가장자리는 자른 샘플을 쓰므로 그 쪽 차분은 한 칸짜리가 된다 — 간격도 그만큼 줄인다.
        const int32   x      = static_cast<int32>( sampleX );
        const int32   z      = static_cast<int32>( sampleZ );
        const int32   last   = static_cast<int32>( _resolution ) - 1;
        const int32   left   = MathUtil::max( x - 1, 0 );
        const int32   right  = MathUtil::min( x + 1, last );
        const int32   down   = MathUtil::max( z - 1, 0 );
        const int32   up     = MathUtil::min( z + 1, last );
        const float32 slopeX = ( getSampleHeight( right, z ) - getSampleHeight( left, z ) ) / ( static_cast<float32>( right - left ) * _cellSize._x );
        const float32 slopeZ = ( getSampleHeight( x, up ) - getSampleHeight( x, down ) ) / ( static_cast<float32>( up - down ) * _cellSize._y );
        return float3{ -slopeX, 1.0f, -slopeZ }.normalize();
    }

    bool TerrainHeightfield::isHoleCell( uint32 cellX, uint32 cellZ ) const
    {
        if ( _listHoleCell.empty() || cellX + 1 >= _resolution || cellZ + 1 >= _resolution )
            return false;
        return _listHoleCell[static_cast<size_t>( cellZ ) * ( _resolution - 1 ) + cellX] != 0;
    }

    bool TerrainHeightfield::toSampleSpace( float32 worldX, float32 worldZ, float32& outSampleX, float32& outSampleZ ) const
    {
        if ( isValid() == false )
            return false;
        outSampleX         = ( worldX - _origin._x ) / _cellSize._x;
        outSampleZ         = ( worldZ - _origin._z ) / _cellSize._y;
        const float32 last = static_cast<float32>( _resolution - 1 );
        return 0.0f <= outSampleX && outSampleX <= last && 0.0f <= outSampleZ && outSampleZ <= last;
    }

    bool TerrainHeightfield::findHeightAt( float32 worldX, float32 worldZ, float32& outHeight ) const
    {
        float32 sampleX{ 0.0f };
        float32 sampleZ{ 0.0f };
        if ( toSampleSpace( worldX, worldZ, sampleX, sampleZ ) == false )
            return false;
        const uint32 cellX = MathUtil::min( static_cast<uint32>( sampleX ), _resolution - 2 );
        const uint32 cellZ = MathUtil::min( static_cast<uint32>( sampleZ ), _resolution - 2 );
        if ( isHoleCell( cellX, cellZ ) )
            return false;
        const float32 localX = sampleX - static_cast<float32>( cellX );
        const float32 localZ = sampleZ - static_cast<float32>( cellZ );
        const int32   x      = static_cast<int32>( cellX );
        const int32   z      = static_cast<int32>( cellZ );
        const float32 h00    = getSampleHeight( x, z );
        const float32 h10    = getSampleHeight( x + 1, z );
        const float32 h01    = getSampleHeight( x, z + 1 );
        const float32 h11    = getSampleHeight( x + 1, z + 1 );
        // 메시와 같은 대각선 (0,0)–(1,1). z 쪽 삼각형은 (00, 01, 11), x 쪽은 (00, 11, 10) 이다(TerrainMeshBuilder 와 같은 감김).
        if ( localZ >= localX )
            outHeight = h00 + localX * ( h11 - h01 ) + localZ * ( h01 - h00 );
        else
            outHeight = h00 + localX * ( h10 - h00 ) + localZ * ( h11 - h10 );
        return true;
    }

    bool TerrainHeightfield::findNormalAt( float32 worldX, float32 worldZ, float3& outNormal ) const
    {
        float32 sampleX{ 0.0f };
        float32 sampleZ{ 0.0f };
        if ( toSampleSpace( worldX, worldZ, sampleX, sampleZ ) == false )
            return false;
        const uint32  cellX  = MathUtil::min( static_cast<uint32>( sampleX ), _resolution - 2 );
        const uint32  cellZ  = MathUtil::min( static_cast<uint32>( sampleZ ), _resolution - 2 );
        const float32 localX = sampleX - static_cast<float32>( cellX );
        const float32 localZ = sampleZ - static_cast<float32>( cellZ );
        const float3  n00    = computeSampleNormal( cellX, cellZ );
        const float3  n10    = computeSampleNormal( cellX + 1, cellZ );
        const float3  n01    = computeSampleNormal( cellX, cellZ + 1 );
        const float3  n11    = computeSampleNormal( cellX + 1, cellZ + 1 );
        const float3  bottom = n00 * ( 1.0f - localX ) + n10 * localX;
        const float3  top    = n01 * ( 1.0f - localX ) + n11 * localX;
        outNormal            = ( bottom * ( 1.0f - localZ ) + top * localZ ).normalize();
        return true;
    }

    bool TerrainHeightfield::isHoleAt( float32 worldX, float32 worldZ ) const
    {
        float32 sampleX{ 0.0f };
        float32 sampleZ{ 0.0f };
        if ( toSampleSpace( worldX, worldZ, sampleX, sampleZ ) == false )
            return false;
        return isHoleCell( MathUtil::min( static_cast<uint32>( sampleX ), _resolution - 2 ), MathUtil::min( static_cast<uint32>( sampleZ ), _resolution - 2 ) );
    }

    float4 TerrainHeightfield::normalizeWeights( const float4& weight )
    {
        const float32 total = weight._x + weight._y + weight._z + weight._w;
        if ( total <= 1.0e-5f )
            return float4{ 1.0f, 0.0f, 0.0f, 0.0f };
        const float32 inverse = 1.0f / total;
        return float4{ weight._x * inverse, weight._y * inverse, weight._z * inverse, weight._w * inverse };
    }

    float4 TerrainHeightfield::computeLayerWeightsAtUv( float32 terrainU, float32 terrainV ) const
    {
        if ( _splatWidth == 0 || _splatHeight == 0 )
            return float4{ 1.0f, 0.0f, 0.0f, 0.0f };
        const float32 texelX = MathUtil::saturate( terrainU ) * static_cast<float32>( _splatWidth - 1 );
        const float32 texelY = MathUtil::saturate( terrainV ) * static_cast<float32>( _splatHeight - 1 );
        const uint32  x0     = MathUtil::min( static_cast<uint32>( texelX ), _splatWidth - 1 );
        const uint32  y0     = MathUtil::min( static_cast<uint32>( texelY ), _splatHeight - 1 );
        const uint32  x1     = MathUtil::min( x0 + 1, _splatWidth - 1 );
        const uint32  y1     = MathUtil::min( y0 + 1, _splatHeight - 1 );
        const float32 fracX  = texelX - static_cast<float32>( x0 );
        const float32 fracY  = texelY - static_cast<float32>( y0 );
        float32       arrWeight[4]{};
        for ( uint32 channel = 0; channel < 4; ++channel )
        {
            const float32 c00   = _splatBytes[( static_cast<size_t>( y0 ) * _splatWidth + x0 ) * 4 + channel];
            const float32 c10   = _splatBytes[( static_cast<size_t>( y0 ) * _splatWidth + x1 ) * 4 + channel];
            const float32 c01   = _splatBytes[( static_cast<size_t>( y1 ) * _splatWidth + x0 ) * 4 + channel];
            const float32 c11   = _splatBytes[( static_cast<size_t>( y1 ) * _splatWidth + x1 ) * 4 + channel];
            const float32 lower = c00 + ( c10 - c00 ) * fracX;
            const float32 upper = c01 + ( c11 - c01 ) * fracX;
            arrWeight[channel]  = ( lower + ( upper - lower ) * fracY ) * ( 1.0f / 255.0f );
        }
        return normalizeWeights( float4{ arrWeight[0], arrWeight[1], arrWeight[2], arrWeight[3] } );
    }

    float4 TerrainHeightfield::computeLayerWeightsAt( float32 worldX, float32 worldZ ) const
    {
        if ( isValid() == false )
            return float4{ 1.0f, 0.0f, 0.0f, 0.0f };
        return computeLayerWeightsAtUv( ( worldX - _origin._x ) / _size._x, ( worldZ - _origin._z ) / _size._y );
    }
} // namespace sw

namespace sw
{
    TerrainPlacementSurface::TerrainPlacementSurface( const TerrainHeightfield& heightfield )
        : _heightfield{ heightfield }
    {
    }

    bool TerrainPlacementSurface::sampleSurface( const float2& planePosition, PlacementSurfaceSample& outSample ) const
    {
        outSample = PlacementSurfaceSample{};
        float32 height{ 0.0f };
        if ( _heightfield.findHeightAt( planePosition._x, planePosition._y, height ) == false )
        {
            // 지형 밖은 표면이 아니다. 구멍은 표면이지만 놓을 수 없는 자리다.
            outSample._bValid = SW_FALSE;
            return _heightfield.isHoleAt( planePosition._x, planePosition._y );
        }
        float3 normal{ 0.0f, 1.0f, 0.0f };
        (void)_heightfield.findNormalAt( planePosition._x, planePosition._y, normal ); // 높이가 있으면 노멀도 있다
        outSample._height      = height;
        outSample._normal      = normal;
        outSample._layerWeight = _heightfield.computeLayerWeightsAt( planePosition._x, planePosition._y );
        return true;
    }
} // namespace sw

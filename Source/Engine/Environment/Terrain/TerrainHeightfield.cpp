#include "pch.h"

#include "Engine/Environment/Terrain/TerrainHeightfield.h"

#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"

#include "Engine/Environment/Terrain/HeightfieldData.h"

namespace sw
{
    SW_LOG_CALLER( "TerrainHeightfield" );

    namespace
    {
        /** @brief 스플랫 텍셀 하나(레이어 여덟 바이트)를 가중치(0..255 그대로, 합으로 나누기 전)로 읽습니다. */
        SurfaceLayerWeights loadTexel( const uint8* pTexel )
        {
            SurfaceLayerWeights result{};
            for ( uint32 layer = 0; layer < SurfaceLayerWeights::kLayerCount; ++layer )
            {
                result.set( layer, static_cast<float32>( pTexel[layer] ) );
            }
            return result;
        }
    } // namespace

    void TerrainSampleRect::include( uint32 sampleX, uint32 sampleZ )
    {
        if ( isEmpty() )
        {
            _minX = _maxX = sampleX;
            _minZ = _maxZ = sampleZ;
            return;
        }
        _minX = MathUtil::min( _minX, sampleX );
        _minZ = MathUtil::min( _minZ, sampleZ );
        _maxX = MathUtil::max( _maxX, sampleX );
        _maxZ = MathUtil::max( _maxZ, sampleZ );
    }

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
        const bool bUnusableHeightfield = data.isValid() == false || size._x <= 0.0f || size._y <= 0.0f;
        if ( bUnusableHeightfield )
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
        {
            _listHeight[sampleIndex] = origin._y + heightMin + static_cast<float32>( data._listHeight[sampleIndex] ) * ( 1.0f / 65535.0f ) * heightRange;
        }
        _listHoleCell = data._listHoleCell;
        return true;
    }

    void TerrainHeightfield::shutdown()
    {
        _listHeight.clear();
        _listHoleCell.clear();
        _resolution = 0;
    }

    void TerrainHeightfield::setSplat( uint32 width, uint32 height, const vector<uint8>& layerBytes )
    {
        constexpr size_t kTexelBytes = SurfaceLayerWeights::kLayerCount;
        const size_t     usedBytes   = static_cast<size_t>( width ) * height * kTexelBytes;
        const bool       bMatches    = width > 0 && height > 0 && usedBytes <= layerBytes.size();
        _splatWidth                  = bMatches ? width : 0u;
        _splatHeight                 = bMatches ? height : 0u;
        _splatBytes.assign( layerBytes.begin(), bMatches ? layerBytes.begin() + static_cast<ptrdiff_t>( usedBytes ) : layerBytes.begin() );
    }

    bool TerrainHeightfield::writeHeightSamples( const vector<float32>& listHeight, const TerrainSampleRect& rect )
    {
        const bool bRectInside  = rect.isEmpty() == false && rect._maxX < _resolution && rect._maxZ < _resolution;
        const bool bSizeMatches = isValid() && listHeight.size() == _listHeight.size();
        if ( bSizeMatches == false || bRectInside == false )
            return false;
        for ( uint32 sampleZ = rect._minZ; sampleZ <= rect._maxZ; ++sampleZ )
        {
            const size_t rowStart = static_cast<size_t>( sampleZ ) * _resolution;
            std::copy( listHeight.begin() + static_cast<ptrdiff_t>( rowStart + rect._minX ), listHeight.begin() + static_cast<ptrdiff_t>( rowStart + rect._maxX + 1 ),
                       _listHeight.begin() + static_cast<ptrdiff_t>( rowStart + rect._minX ) );
        }
        return true;
    }

    bool TerrainHeightfield::readSplatWeights( vector<SurfaceLayerWeights>& outListWeight ) const
    {
        outListWeight.clear();
        if ( _splatWidth == 0 || _splatHeight == 0 )
            return false;
        const size_t texelCount = static_cast<size_t>( _splatWidth ) * _splatHeight;
        outListWeight.resize( texelCount );
        for ( size_t texel = 0; texel < texelCount; ++texel )
        {
            outListWeight[texel] = loadTexel( &_splatBytes[texel * SurfaceLayerWeights::kLayerCount] ).computeNormalized();
        }
        return true;
    }

    bool TerrainHeightfield::makeSplatPlane( uint32 plane, const TerrainSampleRect& rect, vector<uint8>& outRGBABytes ) const
    {
        outRGBABytes.clear();
        const bool bCanRead    = _splatWidth > 0 && _splatHeight > 0 && plane < SurfaceLayerWeights::kPlaneCount;
        const bool bRectInside = rect.isEmpty() == false && rect._maxX < _splatWidth && rect._maxZ < _splatHeight;
        if ( bCanRead == false || bRectInside == false )
            return false;
        const uint32 width  = rect._maxX - rect._minX + 1;
        const uint32 height = rect._maxZ - rect._minZ + 1;
        outRGBABytes.resize( static_cast<size_t>( width ) * height * 4 );
        for ( uint32 row = 0; row < height; ++row )
        {
            for ( uint32 column = 0; column < width; ++column )
            {
                const size_t source = ( static_cast<size_t>( rect._minZ + row ) * _splatWidth + rect._minX + column ) * SurfaceLayerWeights::kLayerCount + static_cast<size_t>( plane ) * 4;
                const size_t target = ( static_cast<size_t>( row ) * width + column ) * 4;
                Memory::copy( outRGBABytes.data() + target, _splatBytes.data() + source, 4 );
            }
        }
        return true;
    }

    bool TerrainHeightfield::makeFullSplatPlane( uint32 plane, vector<uint8>& outRGBABytes ) const
    {
        if ( _splatWidth == 0 || _splatHeight == 0 )
        {
            outRGBABytes.clear();
            return false;
        }
        TerrainSampleRect all{};
        all.include( 0, 0 );
        all.include( _splatWidth - 1, _splatHeight - 1 );
        return makeSplatPlane( plane, all, outRGBABytes );
    }

    bool TerrainHeightfield::readHoleCells( vector<uint8>& outListHole ) const
    {
        outListHole.clear();
        if ( isValid() == false )
            return false;
        const size_t cellCount = static_cast<size_t>( _resolution - 1 ) * ( _resolution - 1 );
        if ( _listHoleCell.empty() )
            outListHole.assign( cellCount, 0u );
        else
            outListHole = _listHoleCell;
        return true;
    }

    bool TerrainHeightfield::writeHoleCells( const vector<uint8>& listHole, const TerrainSampleRect& cellRect )
    {
        const uint32 cells        = isValid() ? _resolution - 1 : 0u;
        const bool   bSizeMatches = cells > 0 && listHole.size() == static_cast<size_t>( cells ) * cells;
        const bool   bRectInside  = cellRect.isEmpty() == false && cellRect._maxX < cells && cellRect._maxZ < cells;
        if ( bSizeMatches == false || bRectInside == false )
            return false;
        if ( _listHoleCell.empty() )
            _listHoleCell.assign( listHole.size(), 0u );
        for ( uint32 cellZ = cellRect._minZ; cellZ <= cellRect._maxZ; ++cellZ )
        {
            const size_t rowStart = static_cast<size_t>( cellZ ) * cells;
            for ( uint32 cellX = cellRect._minX; cellX <= cellRect._maxX; ++cellX )
            {
                _listHoleCell[rowStart + cellX] = listHole[rowStart + cellX] != 0 ? 1u : 0u;
            }
        }
        return true;
    }

    bool TerrainHeightfield::replaceSamples( uint32 resolution, const vector<float32>& listHeight, const vector<uint8>& listHole )
    {
        const size_t cellCount   = resolution >= 2 ? static_cast<size_t>( resolution - 1 ) * ( resolution - 1 ) : 0u;
        const bool   bHeightsFit = resolution >= 2 && listHeight.size() == static_cast<size_t>( resolution ) * resolution;
        const bool   bHolesFit   = listHole.empty() || listHole.size() == cellCount;
        const bool   bUsable     = bHeightsFit && bHolesFit && isValid();
        if ( bUsable == false )
            return false;
        _resolution   = resolution;
        _cellSize     = float2{ _size._x / static_cast<float32>( resolution - 1 ), _size._y / static_cast<float32>( resolution - 1 ) };
        _listHeight   = listHeight;
        _listHoleCell = listHole;
        return true;
    }

    bool TerrainHeightfield::resampleHeights( const vector<float32>& listSource, uint32 sourceResolution, uint32 targetResolution, vector<float32>& outListHeight )
    {
        outListHeight.clear();
        const bool bUsable = sourceResolution >= 2 && targetResolution >= 2 && listSource.size() == static_cast<size_t>( sourceResolution ) * sourceResolution;
        if ( bUsable == false )
            return false;
        outListHeight.resize( static_cast<size_t>( targetResolution ) * targetResolution );
        const float32 scale = static_cast<float32>( sourceResolution - 1 ) / static_cast<float32>( targetResolution - 1 );
        for ( uint32 targetZ = 0; targetZ < targetResolution; ++targetZ )
        {
            for ( uint32 targetX = 0; targetX < targetResolution; ++targetX )
            {
                const float32 sampleX = static_cast<float32>( targetX ) * scale;
                const float32 sampleZ = static_cast<float32>( targetZ ) * scale;
                const uint32  cellX   = MathUtil::min( static_cast<uint32>( sampleX ), sourceResolution - 2 );
                const uint32  cellZ   = MathUtil::min( static_cast<uint32>( sampleZ ), sourceResolution - 2 );
                const float32 localX  = sampleX - static_cast<float32>( cellX );
                const float32 localZ  = sampleZ - static_cast<float32>( cellZ );
                const size_t  row0    = static_cast<size_t>( cellZ ) * sourceResolution + cellX;
                const size_t  row1    = row0 + sourceResolution;
                const float32 h00     = listSource[row0];
                const float32 h10     = listSource[row0 + 1];
                const float32 h01     = listSource[row1];
                const float32 h11     = listSource[row1 + 1];
                // `findHeightAt` 과 같은 대각선 삼각형이다 — 다시 표본화한 땅이 그려진 땅 위에 놓인다.
                const float32 height                                                       = localZ >= localX ? h00 + localX * ( h11 - h01 ) + localZ * ( h01 - h00 ) : h00 + localX * ( h10 - h00 ) + localZ * ( h11 - h10 );
                outListHeight[static_cast<size_t>( targetZ ) * targetResolution + targetX] = height;
            }
        }
        return true;
    }

    bool TerrainHeightfield::resampleHoles( const vector<uint8>& listSource, uint32 sourceResolution, uint32 targetResolution, vector<uint8>& outListHole )
    {
        outListHole.clear();
        const uint32 sourceCells = sourceResolution >= 2 ? sourceResolution - 1 : 0u;
        const bool   bUsable     = sourceCells > 0 && targetResolution >= 2 && ( listSource.empty() || listSource.size() == static_cast<size_t>( sourceCells ) * sourceCells );
        if ( bUsable == false )
            return false;
        if ( listSource.empty() )
            return true;
        const uint32 targetCells = targetResolution - 1;
        outListHole.resize( static_cast<size_t>( targetCells ) * targetCells );
        const float32 scale = static_cast<float32>( sourceCells ) / static_cast<float32>( targetCells );
        for ( uint32 cellZ = 0; cellZ < targetCells; ++cellZ )
        {
            const uint32 sourceZ = MathUtil::min( static_cast<uint32>( ( static_cast<float32>( cellZ ) + 0.5f ) * scale ), sourceCells - 1 );
            for ( uint32 cellX = 0; cellX < targetCells; ++cellX )
            {
                const uint32 sourceX                                            = MathUtil::min( static_cast<uint32>( ( static_cast<float32>( cellX ) + 0.5f ) * scale ), sourceCells - 1 );
                outListHole[static_cast<size_t>( cellZ ) * targetCells + cellX] = listSource[static_cast<size_t>( sourceZ ) * sourceCells + sourceX];
            }
        }
        return true;
    }

    bool TerrainHeightfield::writeSplatWeights( const vector<SurfaceLayerWeights>& listWeight, const TerrainSampleRect& rect )
    {
        const bool bHasSplat    = _splatWidth > 0 && _splatHeight > 0;
        const bool bSizeMatches = listWeight.size() == static_cast<size_t>( _splatWidth ) * _splatHeight;
        const bool bRectInside  = rect.isEmpty() == false && rect._maxX < _splatWidth && rect._maxZ < _splatHeight;
        const bool bValidWrite  = bHasSplat && bSizeMatches && bRectInside;
        if ( bValidWrite == false )
            return false;
        for ( uint32 texelZ = rect._minZ; texelZ <= rect._maxZ; ++texelZ )
        {
            for ( uint32 texelX = rect._minX; texelX <= rect._maxX; ++texelX )
            {
                const size_t texel = static_cast<size_t>( texelZ ) * _splatWidth + texelX;
                for ( uint32 layer = 0; layer < SurfaceLayerWeights::kLayerCount; ++layer )
                {
                    _splatBytes[texel * SurfaceLayerWeights::kLayerCount + layer] =
                        static_cast<uint8>( MathUtil::clamp( MathUtil::round( listWeight[texel].get( layer ) * 255.0f ), 0.0f, 255.0f ) );
                }
            }
        }
        return true;
    }

    void TerrainHeightfield::makeHeightfieldData( float32 heightMin, float32 heightMax, HeightfieldData& outData ) const
    {
        outData._resolution = _resolution;
        outData._listHeight.resize( _listHeight.size() );
        outData._listHoleCell     = _listHoleCell;
        const float32 heightRange = heightMax - heightMin;
        for ( size_t sampleIndex = 0; sampleIndex < _listHeight.size(); ++sampleIndex )
        {
            const float32 normalized         = heightRange > 0.0f ? ( _listHeight[sampleIndex] - _origin._y - heightMin ) / heightRange : 0.0f;
            outData._listHeight[sampleIndex] = static_cast<uint16>( MathUtil::clamp( MathUtil::round( normalized * 65535.0f ), 0.0f, 65535.0f ) );
        }
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
        // 가장자리는 제한한 샘플을 쓰므로 그쪽 차분은 한 셀짜리가 된다. 간격도 그만큼 줄인다.
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
        const bool bNoHoleAtCell = _listHoleCell.empty() || cellX + 1 >= _resolution || cellZ + 1 >= _resolution;
        if ( bNoHoleAtCell )
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

    SurfaceLayerWeights TerrainHeightfield::computeLayerWeightsAtUv( float32 terrainU, float32 terrainV ) const
    {
        if ( _splatWidth == 0 || _splatHeight == 0 )
            return SurfaceLayerWeights{};
        const float32       texelX      = MathUtil::saturate( terrainU ) * static_cast<float32>( _splatWidth - 1 );
        const float32       texelY      = MathUtil::saturate( terrainV ) * static_cast<float32>( _splatHeight - 1 );
        const uint32        x0          = MathUtil::min( static_cast<uint32>( texelX ), _splatWidth - 1 );
        const uint32        y0          = MathUtil::min( static_cast<uint32>( texelY ), _splatHeight - 1 );
        const uint32        x1          = MathUtil::min( x0 + 1, _splatWidth - 1 );
        const uint32        y1          = MathUtil::min( y0 + 1, _splatHeight - 1 );
        const float32       fracX       = texelX - static_cast<float32>( x0 );
        const float32       fracY       = texelY - static_cast<float32>( y0 );
        constexpr size_t    kTexelBytes = SurfaceLayerWeights::kLayerCount;
        SurfaceLayerWeights result{};
        for ( uint32 layer = 0; layer < SurfaceLayerWeights::kLayerCount; ++layer )
        {
            const float32 c00   = _splatBytes[( static_cast<size_t>( y0 ) * _splatWidth + x0 ) * kTexelBytes + layer];
            const float32 c10   = _splatBytes[( static_cast<size_t>( y0 ) * _splatWidth + x1 ) * kTexelBytes + layer];
            const float32 c01   = _splatBytes[( static_cast<size_t>( y1 ) * _splatWidth + x0 ) * kTexelBytes + layer];
            const float32 c11   = _splatBytes[( static_cast<size_t>( y1 ) * _splatWidth + x1 ) * kTexelBytes + layer];
            const float32 lower = c00 + ( c10 - c00 ) * fracX;
            const float32 upper = c01 + ( c11 - c01 ) * fracX;
            result.set( layer, ( lower + ( upper - lower ) * fracY ) * ( 1.0f / 255.0f ) );
        }
        return result.computeNormalized();
    }

    SurfaceLayerWeights TerrainHeightfield::computeLayerWeightsAt( float32 worldX, float32 worldZ ) const
    {
        if ( isValid() == false )
            return SurfaceLayerWeights{};
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
            // 지형 밖은 표면이 아니다. 구멍은 표면이지만 놓을 수 없는 위치다.
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

#include "pch.h"

#include "Editor/Viewport/TerrainBrush.h"

#include "Core/Math/MathUtil.h"

#include <cmath>

namespace sw::editor
{
    TerrainBrushTarget TerrainBrush::getTarget( TerrainBrushMode mode )
    {
        switch ( mode )
        {
            case TerrainBrushMode::PaintLayer:
                return TerrainBrushTarget::LayerWeight;
            case TerrainBrushMode::CutHole:
            case TerrainBrushMode::FillHole:
                return TerrainBrushTarget::Hole;
            case TerrainBrushMode::Raise:
            case TerrainBrushMode::Lower:
            case TerrainBrushMode::Flatten:
            case TerrainBrushMode::Smooth:
            case TerrainBrushMode::Count:
                break;
        }
        return TerrainBrushTarget::Height;
    }

    TerrainBrushGrid TerrainBrush::makeHoleGrid( const TerrainBrushGrid& sampleGrid )
    {
        TerrainBrushGrid cellGrid{};
        cellGrid._origin     = float2{ sampleGrid._origin._x + sampleGrid._cellSize._x * 0.5f, sampleGrid._origin._y + sampleGrid._cellSize._y * 0.5f };
        cellGrid._cellSize   = sampleGrid._cellSize;
        cellGrid._resolution = sampleGrid._resolution >= 2 ? sampleGrid._resolution - 1 : 0u;
        return cellGrid;
    }

    bool TerrainBrush::applyHole( vector<uint8>& inoutListHole, uint32 cellResolution, const vector<TerrainBrushSample>& listSample, const TerrainBrushSettings& settings )
    {
        const bool bHoleMode = getTarget( settings._mode ) == TerrainBrushTarget::Hole && settings._mode != TerrainBrushMode::Count;
        if ( bHoleMode == false || inoutListHole.size() != static_cast<size_t>( cellResolution ) * cellResolution )
            return false;
        const uint8 holeFlag = settings._mode == TerrainBrushMode::CutHole ? 1u : 0u;
        bool        bChanged = false;
        for ( const TerrainBrushSample& sample : listSample )
        {
            const bool bSkipped = sample._sampleX >= cellResolution || sample._sampleZ >= cellResolution || sample._weight < 0.5f;
            if ( bSkipped )
                continue;
            uint8& cell = inoutListHole[static_cast<size_t>( sample._sampleZ ) * cellResolution + sample._sampleX];
            bChanged    = bChanged || cell != holeFlag;
            cell        = holeFlag;
        }
        return bChanged;
    }

    float32 TerrainBrush::computeFalloffWeight( float32 distance, const TerrainBrushSettings& settings )
    {
        if ( settings._radius <= 0.0f || distance >= settings._radius )
            return 0.0f;
        const float32 inner = settings._radius * ( 1.0f - MathUtil::saturate( settings._falloff ) );
        if ( distance <= inner )
            return 1.0f;
        const float32 t = ( distance - inner ) / ( settings._radius - inner );
        return 1.0f - t * t * ( 3.0f - 2.0f * t );
    }

    void TerrainBrush::collectSamples( const float2& center, const TerrainBrushGrid& grid, const TerrainBrushSettings& settings,
                                       vector<TerrainBrushSample>& outListSample )
    {
        outListSample.clear();
        const bool bInvalidBrushInputs = grid._resolution < 2 || grid._cellSize._x <= 0.0f || grid._cellSize._y <= 0.0f || settings._radius <= 0.0f;
        if ( bInvalidBrushInputs )
            return;
        const float32 maxIndex = static_cast<float32>( grid._resolution - 1 );
        const float32 minX     = MathUtil::clamp( std::floor( ( center._x - settings._radius - grid._origin._x ) / grid._cellSize._x ), 0.0f, maxIndex );
        const float32 maxX     = MathUtil::clamp( std::ceil( ( center._x + settings._radius - grid._origin._x ) / grid._cellSize._x ), 0.0f, maxIndex );
        const float32 minZ     = MathUtil::clamp( std::floor( ( center._y - settings._radius - grid._origin._y ) / grid._cellSize._y ), 0.0f, maxIndex );
        const float32 maxZ     = MathUtil::clamp( std::ceil( ( center._y + settings._radius - grid._origin._y ) / grid._cellSize._y ), 0.0f, maxIndex );
        for ( uint32 sampleZ = static_cast<uint32>( minZ ); sampleZ <= static_cast<uint32>( maxZ ); ++sampleZ )
        {
            for ( uint32 sampleX = static_cast<uint32>( minX ); sampleX <= static_cast<uint32>( maxX ); ++sampleX )
            {
                const float32 dx     = grid._origin._x + static_cast<float32>( sampleX ) * grid._cellSize._x - center._x;
                const float32 dz     = grid._origin._y + static_cast<float32>( sampleZ ) * grid._cellSize._y - center._y;
                const float32 weight = computeFalloffWeight( std::sqrt( dx * dx + dz * dz ), settings );
                if ( weight > 0.0f )
                    outListSample.push_back( TerrainBrushSample{ sampleX, sampleZ, weight } );
            }
        }
    }

    bool TerrainBrush::applyHeight( vector<float32>& inOutListHeight, uint32 resolution, const vector<TerrainBrushSample>& listSample,
                                    const TerrainBrushSettings& settings, float32 flattenHeight, float32 deltaTime )
    {
        const bool bNotHeightMode = getTarget( settings._mode ) != TerrainBrushTarget::Height || settings._mode == TerrainBrushMode::Count;
        if ( bNotHeightMode || inOutListHeight.size() != static_cast<size_t>( resolution ) * resolution )
            return false;
        // 스무드의 목표(이웃 평균)는 쓰기 전에 모두 구한다. 다른 모드는 비워 둔다.
        vector<float32> listSmoothTarget;
        if ( settings._mode == TerrainBrushMode::Smooth )
        {
            listSmoothTarget.reserve( listSample.size() );
            for ( const TerrainBrushSample& sample : listSample )
            {
                float32 sum{ 0.0f };
                uint32  count{ 0 };
                for ( int32 dz = -1; dz <= 1; ++dz )
                {
                    for ( int32 dx = -1; dx <= 1; ++dx )
                    {
                        const int64 x        = static_cast<int64>( sample._sampleX ) + dx;
                        const int64 z        = static_cast<int64>( sample._sampleZ ) + dz;
                        const bool  bOutside = x < 0 || z < 0 || x >= resolution || z >= resolution;
                        if ( bOutside )
                            continue;
                        sum += inOutListHeight[static_cast<size_t>( z ) * resolution + static_cast<size_t>( x )];
                        ++count;
                    }
                }
                listSmoothTarget.push_back( sum / static_cast<float32>( MathUtil::max( count, 1u ) ) ); // 격자 밖 샘플은 고르지 않으므로 자신은 늘 센다
            }
        }
        bool bChanged = false;
        for ( size_t sampleIndex = 0; sampleIndex < listSample.size(); ++sampleIndex )
        {
            const TerrainBrushSample& sample = listSample[sampleIndex];
            if ( sample._sampleX >= resolution || sample._sampleZ >= resolution )
                continue;
            float32&      height = inOutListHeight[static_cast<size_t>( sample._sampleZ ) * resolution + sample._sampleX];
            const float32 amount = settings._strength * sample._weight * deltaTime;
            const float32 before = height;
            switch ( settings._mode )
            {
                case TerrainBrushMode::Raise:
                {
                    height += amount;
                    break;
                }
                case TerrainBrushMode::Lower:
                {
                    height -= amount;
                    break;
                }
                case TerrainBrushMode::Flatten:
                {
                    height += ( flattenHeight - height ) * MathUtil::saturate( amount );
                    break;
                }
                case TerrainBrushMode::Smooth:
                {
                    height += ( listSmoothTarget[sampleIndex] - height ) * MathUtil::saturate( amount );
                    break;
                }
                case TerrainBrushMode::PaintLayer:
                case TerrainBrushMode::CutHole:
                case TerrainBrushMode::FillHole:
                case TerrainBrushMode::Count:
                {
                    break;
                }
            }
            bChanged = bChanged || height != before;
        }
        return bChanged;
    }

    bool TerrainBrush::applyLayerWeight( vector<SurfaceLayerWeights>& inOutListWeight, uint32 resolution, const vector<TerrainBrushSample>& listSample,
                                         const TerrainBrushSettings& settings, float32 deltaTime )
    {
        const bool bCannotPaintLayer = settings._mode != TerrainBrushMode::PaintLayer || settings._layerIndex >= SurfaceLayerWeights::kLayerCount ||
                                       inOutListWeight.size() != static_cast<size_t>( resolution ) * resolution;
        if ( bCannotPaintLayer )
            return false;
        bool bChanged = false;
        for ( const TerrainBrushSample& sample : listSample )
        {
            if ( sample._sampleX >= resolution || sample._sampleZ >= resolution )
                continue;
            SurfaceLayerWeights& weight = inOutListWeight[static_cast<size_t>( sample._sampleZ ) * resolution + sample._sampleX];
            const float32        amount = MathUtil::saturate( settings._strength * sample._weight * deltaTime );
            const float32        target = weight.get( settings._layerIndex );
            if ( amount <= 0.0f || target >= 1.0f )
                continue;
            // 목표 레이어를 1 쪽으로 amount 만큼 당기고, 나머지 레이어는 같은 비율로 줄여 합 1 을 지킨다.
            const float32 others = 1.0f - target;
            const float32 scale  = others > 0.0f ? ( 1.0f - ( target + ( 1.0f - target ) * amount ) ) / others : 0.0f;
            for ( uint32 layer = 0; layer < SurfaceLayerWeights::kLayerCount; ++layer )
            {
                weight.set( layer, layer == settings._layerIndex ? target + ( 1.0f - target ) * amount : weight.get( layer ) * scale );
            }
            bChanged = true;
        }
        return bChanged;
    }
} // namespace sw::editor

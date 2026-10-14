#include "pch.h"

#include "Editor/Viewport/TerrainBrush.h"

#include "Core/Math/MathUtil.h"

#include <cmath>

namespace sw::editor
{
    namespace
    {
        struct TerrainBrushInternal
        {
            /** @brief 레이어 @p index 성분의 주소입니다(0..3 밖이면 마지막 성분). */
            static float32& getComponent( float4& value, uint32 index )
            {
                switch ( index )
                {
                    case 0:
                        return value._x;
                    case 1:
                        return value._y;
                    case 2:
                        return value._z;
                    default:
                        return value._w;
                }
            }
        };
    } // namespace

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
        if ( grid._resolution < 2 || grid._cellSize._x <= 0.0f || grid._cellSize._y <= 0.0f || settings._radius <= 0.0f )
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
        if ( settings._mode == TerrainBrushMode::PaintLayer || inOutListHeight.size() != static_cast<size_t>( resolution ) * resolution )
            return false;
        bool bChanged = false;
        for ( const TerrainBrushSample& sample : listSample )
        {
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
                case TerrainBrushMode::PaintLayer:
                {
                    break;
                }
            }
            bChanged = bChanged || height != before;
        }
        return bChanged;
    }

    bool TerrainBrush::applyLayerWeight( vector<float4>& inOutListWeight, uint32 resolution, const vector<TerrainBrushSample>& listSample,
                                         const TerrainBrushSettings& settings, float32 deltaTime )
    {
        if ( settings._mode != TerrainBrushMode::PaintLayer || settings._layerIndex > 3 ||
             inOutListWeight.size() != static_cast<size_t>( resolution ) * resolution )
            return false;
        bool bChanged = false;
        for ( const TerrainBrushSample& sample : listSample )
        {
            if ( sample._sampleX >= resolution || sample._sampleZ >= resolution )
                continue;
            float4&       weight = inOutListWeight[static_cast<size_t>( sample._sampleZ ) * resolution + sample._sampleX];
            const float32 amount = MathUtil::saturate( settings._strength * sample._weight * deltaTime );
            float32&      target = TerrainBrushInternal::getComponent( weight, settings._layerIndex );
            if ( amount <= 0.0f || target >= 1.0f )
                continue;
            // 목표 레이어를 1 쪽으로 amount 만큼 당기고, 나머지 레이어는 같은 비율로 줄여 합 1 을 지킨다.
            const float32 others = 1.0f - target;
            const float32 scale  = others > 0.0f ? ( 1.0f - ( target + ( 1.0f - target ) * amount ) ) / others : 0.0f;
            for ( uint32 layer = 0; layer < 4; ++layer )
            {
                float32& component = TerrainBrushInternal::getComponent( weight, layer );
                component          = layer == settings._layerIndex ? target + ( 1.0f - target ) * amount : component * scale;
            }
            bChanged = true;
        }
        return bChanged;
    }
} // namespace sw::editor

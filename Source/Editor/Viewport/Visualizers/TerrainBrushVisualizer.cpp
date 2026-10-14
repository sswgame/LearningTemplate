/**
 * @file TerrainBrushVisualizer.cpp
 * @brief 지형 칠하기 도구 모드("Terrain")의 미리보기입니다. 마우스 레이가 지형에 닿는 점에 브러시 원(바깥 반경 · 감쇠 안쪽 반경)을 지형 높이를 따라 그립니다.
 * @details 지금은 미리보기까지다. `TerrainHeightfield` 는 만든 뒤 읽기만 하고(병렬 틱 · 배치가 동시에 읽는다) `TerrainComponent` 에 높이 · 스플랫을
 *          바꾸는 공개 API 와 저장 경로가 없어서, 드래그 적용과 되돌리기 항목은 그 API 가 생긴 뒤 `TerrainBrush::applyHeight` ·
 *          `applyLayerWeight` 로 잇는다(백로그 D3-1 ⑧).
 */
#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Math/MatrixMath.h"

#include "Editor/Common/GUI/EditorIconGlyphs.h"
#include "Editor/Viewport/EditorProjectedLineRenderer.h"
#include "Editor/Viewport/EditorViewportProjection.h"
#include "Editor/Viewport/EditorViewportRayPick.h"
#include "Editor/Viewport/EditorViewportVisualizer.h"
#include "Editor/Viewport/TerrainBrush.h"

#include "Engine/Environment/Terrain/TerrainComponent.h"
#include "Engine/Environment/Terrain/TerrainHeightfield.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include <cmath>

namespace sw::editor
{
    namespace
    {
        struct TerrainBrushVisualizerInternal
        {
            static constexpr float32 kMarchStep     = 0.5f; ///< 레이 전진 간격(m)
            static constexpr float32 kMarchDistance = 2000.0f;
            static constexpr uint32  kRefineSteps   = 10;
            static constexpr uint32  kCircleSegment = 48;
            static constexpr float32 kLift          = 0.05f; ///< 선이 지형에 묻히지 않게 띄우는 높이

            /** @brief 레이 위 점 @p t 가 지형 아래(또는 위)인지 봅니다. 지형 밖이면 false 입니다. */
            static bool findSignedHeight( const GameObjectManager& manager, const EditorPickRay& ray, float32 t, float32& outAbove )
            {
                const float3      point    = ray._origin + ray._direction * t;
                TerrainComponent* pTerrain = TerrainComponent::findTerrainAt( manager, point._x, point._z );
                float32           height   = 0.0f;
                if ( pTerrain == nullptr || pTerrain->getHeightfield().findHeightAt( point._x, point._z, height ) == false )
                    return false;
                outAbove = point._y - height;
                return true;
            }

            /** @brief 레이를 전진시켜 처음 지형 아래로 들어가는 점을 찾고 이분법으로 다듬습니다. */
            static bool traceTerrain( const GameObjectManager& manager, const EditorPickRay& ray, float3& outHit )
            {
                float32 previousT     = 0.0f;
                float32 previousAbove = 0.0f;
                bool    bPrevious     = findSignedHeight( manager, ray, 0.0f, previousAbove );
                for ( float32 t = kMarchStep; t <= kMarchDistance; t += kMarchStep )
                {
                    float32 above = 0.0f;
                    if ( findSignedHeight( manager, ray, t, above ) == false )
                    {
                        bPrevious = false;
                        continue;
                    }
                    if ( above <= 0.0f && bPrevious && previousAbove > 0.0f )
                    {
                        float32 low  = previousT;
                        float32 high = t;
                        for ( uint32 step = 0; step < kRefineSteps; ++step )
                        {
                            const float32 middle      = ( low + high ) * 0.5f;
                            float32       middleAbove = 0.0f;
                            if ( findSignedHeight( manager, ray, middle, middleAbove ) && middleAbove > 0.0f )
                                low = middle;
                            else
                                high = middle;
                        }
                        outHit = ray._origin + ray._direction * high;
                        return true;
                    }
                    previousT     = t;
                    previousAbove = above;
                    bPrevious     = true;
                }
                return false;
            }

            /** @brief 지형 높이를 따라 반경 @p radius 의 원을 긋습니다. */
            static void drawCircle( EditorProjectedLineRenderer& renderer, const GameObjectManager& manager, const float3& center, float32 radius,
                                    const float4& color )
            {
                float3 previous{};
                for ( uint32 segment = 0; segment <= kCircleSegment; ++segment )
                {
                    const float32 angle = MathUtil::kTwoPi * static_cast<float32>( segment ) / static_cast<float32>( kCircleSegment );
                    float3        point{ center._x + std::cos( angle ) * radius, center._y, center._z + std::sin( angle ) * radius };
                    if ( const TerrainComponent* pTerrain = TerrainComponent::findTerrainAt( manager, point._x, point._z ) )
                    {
                        float32 height = 0.0f;
                        if ( pTerrain->getHeightfield().findHeightAt( point._x, point._z, height ) )
                            point._y = height;
                    }
                    point._y += kLift;
                    if ( segment > 0 )
                        renderer.drawLine( previous, point, color );
                    previous = point;
                }
            }

            static void draw( const EditorViewportVisualizerArgs& args )
            {
                if ( args._pObjectManager == nullptr || args._pViewProj == nullptr || args._bFlat2D )
                    return;
                EditorPickRay ray;
                if ( EditorViewportProjectionUtil::makeMouseRay( args._pViewProj->invert(), args._canvasPos, args._canvasSize, ray ) == false )
                    return;
                const GameObjectManager& manager = *args._pObjectManager;
                float3                   hit{};
                if ( traceTerrain( manager, ray, hit ) == false )
                    return;
                static const TerrainBrushSettings s_settings{}; // 브러시 설정 UI 는 쓰기 API 와 함께 붙인다
                EditorProjectedLineRenderer       renderer{ args._pDrawList, *args._pViewProj, args._canvasPos, args._canvasSize };
                drawCircle( renderer, manager, hit, s_settings._radius, float4{ 1.0f, 0.85f, 0.2f, 1.0f } );
                const float32 inner = s_settings._radius * ( 1.0f - s_settings._falloff );
                if ( inner > 0.0f )
                    drawCircle( renderer, manager, hit, inner, float4{ 1.0f, 0.85f, 0.2f, 0.5f } );
            }
        };
    } // namespace

    SW_EDITOR_VISUALIZER( TerrainBrush, "terrainbrush", 140, editoricon::kHeightfield, "Terrain",
                          "Terrain paint tool: shows the brush circle where the mouse ray hits the terrain", false, &TerrainBrushVisualizerInternal::draw );
} // namespace sw::editor

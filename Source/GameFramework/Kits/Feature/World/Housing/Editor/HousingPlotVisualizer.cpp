/**
 * @file HousingPlotVisualizer.cpp
 * @brief 뷰포트 시각화 housing.plot(툴바 "Plots")을 구현합니다. 씬의 하우징 부지마다 경계 · 칸 · id 와 크기를 씬 뷰에 겹쳐 그립니다.
 * @details 플레이 전에도 그린다(부지를 놓을 자리를 고를 때 보인다 — 언리얼 Landscape 구역 선 · 유니티 Gizmos.DrawWireCube 자리). 칸 선은 옅은 토큰, 경계는 진한 토큰이고
 *          같은 씬의 다른 부지와 겹침은 맵 검사(`HousingPlotComponent::validatePlot`)가 알린다 — 여기서는 판정하지 않고 그리기만 한다.
 */
#include "pch.h"

#include "Editor/Common/Config/EditorIconGlyphTable.h"
#include "Editor/Common/GUI/Theme/EditorColorUtil.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Viewport/EditorViewportProjection.h"
#include "Editor/Viewport/EditorViewportVisualizer.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Localization/LocText.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"

#include "GameFramework/Kits/Feature/World/Housing/Component/HousingPlotComponent.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct HousingPlotVisualizerInternal
        {
            /** @brief 칸 선을 그리는 부지 한 변의 칸 수 상한입니다 — 넘으면 경계만 그린다(큰 부지에서 선이 화면을 덮지 않게). */
            static constexpr int32 kMaxGridCells = 64;

            static void drawSegment( const EditorViewportVisualizerArgs& args, const float3& from, const float3& to, ImU32 color, float32 thickness )
            {
                ImVec2 screenFrom;
                ImVec2 screenTo;
                if ( EditorViewportProjectionUtil::projectSegment( *args._pViewProj, from, to, args._canvasPos, args._canvasSize, screenFrom, screenTo ) )
                    args._pDrawList->AddLine( screenFrom, screenTo, color, thickness );
            }

            /** @brief 부지 하나 — 바닥 높이의 칸 선, 경계, 원점 위의 `id  X x Z` 라벨입니다. */
            static void drawPlot( const EditorViewportVisualizerArgs& args, const HousingPlotComponent& plot, ImU32 colorBounds, ImU32 colorCell, ImU32 colorLabel )
            {
                float3  origin   = {};
                float32 cellSize = 0.0f;
                if ( plot.tryGetPlotGrid( origin, cellSize ) == false )
                    return; // 루트 · 카탈로그가 없는 부지는 맵 검사가 알린다
                const HousingPlotSettings& settings = plot.getSettings();
                const float32              sizeX    = static_cast<float32>( settings._sizeX ) * cellSize;
                const float32              sizeZ    = static_cast<float32>( settings._sizeZ ) * cellSize;
                if ( settings._sizeX <= kMaxGridCells && settings._sizeZ <= kMaxGridCells )
                {
                    for ( int32 x = 1; x < settings._sizeX; ++x )
                    {
                        const float32 offset = static_cast<float32>( x ) * cellSize;
                        drawSegment( args, origin + float3{ offset, 0.0f, 0.0f }, origin + float3{ offset, 0.0f, sizeZ }, colorCell, 1.0f );
                    }
                    for ( int32 z = 1; z < settings._sizeZ; ++z )
                    {
                        const float32 offset = static_cast<float32>( z ) * cellSize;
                        drawSegment( args, origin + float3{ 0.0f, 0.0f, offset }, origin + float3{ sizeX, 0.0f, offset }, colorCell, 1.0f );
                    }
                }
                const float3 corner1 = origin + float3{ sizeX, 0.0f, 0.0f };
                const float3 corner2 = origin + float3{ sizeX, 0.0f, sizeZ };
                const float3 corner3 = origin + float3{ 0.0f, 0.0f, sizeZ };
                drawSegment( args, origin, corner1, colorBounds, 2.0f );
                drawSegment( args, corner1, corner2, colorBounds, 2.0f );
                drawSegment( args, corner2, corner3, colorBounds, 2.0f );
                drawSegment( args, corner3, origin, colorBounds, 2.0f );
                ImVec2 labelPos;
                if ( EditorViewportProjectionUtil::projectPoint( *args._pViewProj, origin, args._canvasPos, args._canvasSize, labelPos ) )
                {
                    StringBuilder<constant::buffer::kMax256> labelBuilder;
                    labelBuilder.appendFormat( "%#  %# x %#", settings._plotID.c_str(), settings._sizeX, settings._sizeZ );
                    const string label{ labelBuilder.view() };
                    args._pDrawList->AddText( labelPos, colorLabel, label.c_str() );
                }
            }

            /** @brief 활성 씬의 부지를 모두 그립니다. 색은 테마 토큰 `Viewport.HousingPlot` · `Viewport.HousingCell` · `Viewport.Label` 입니다. */
            static void drawPlots( const EditorViewportVisualizerArgs& args )
            {
                if ( args._pObjectManager == nullptr )
                    return;
                const ImU32 colorBounds = EditorColorUtil::toImU32( SW_THEME_COLOR( "Viewport", "HousingPlot" ) );
                const ImU32 colorCell   = EditorColorUtil::toImU32( SW_THEME_COLOR( "Viewport", "HousingCell" ) );
                const ImU32 colorLabel  = EditorColorUtil::toImU32( SW_THEME_COLOR( "Viewport", "Label" ) );
                args._pObjectManager->forEachComponentOfType<HousingPlotComponent>( [&args, colorBounds, colorCell, colorLabel]( const HousingPlotComponent* pPlot )
                {
                    drawPlot( args, *pPlot, colorBounds, colorCell, colorLabel );
                } );
            }

            /** @brief 감시 값 `Editor.HousingPlotFrames` 입니다. 활성 씬에서 틀(원점 · 칸 크기)을 읽어 그릴 수 있는 부지 수 — 플레이 전 에디터에서 카탈로그를 얻는 길을 본다. */
            [[nodiscard]] static bool readPlotFrames( const GameObjectManager* pManager, float64& outValue )
            {
                if ( pManager == nullptr )
                    return false;
                uint32 count = 0;
                pManager->forEachComponentOfType<HousingPlotComponent>( [&count]( const HousingPlotComponent* pPlot )
                {
                    float3  origin   = {};
                    float32 cellSize = 0.0f;
                    count += pPlot->tryGetPlotGrid( origin, cellSize ) ? 1 : 0;
                } );
                outValue = static_cast<float64>( count );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_WATCH( editorHousingPlotFrames, "Editor.HousingPlotFrames", "Housing plots in the active scene whose bounds the viewport can draw", &HousingPlotVisualizerInternal::readPlotFrames );

    SW_EDITOR_VISUALIZER( HousingPlot, "housing.plot", 910, SW_EDITOR_ICON_NAME( "Grid" ), SW_LOCKEYREF( "EdPluginVisualizer", "HousingPlotLabel" ), SW_LOCKEYREF( "EdPluginVisualizer", "HousingPlotTip" ), true,
                          &HousingPlotVisualizerInternal::drawPlots );
} // namespace sw::editor

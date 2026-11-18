/**
 * @file EditorViewportVisualizer.cpp
 * @brief 등록된 시각화 가운데 켜진 것을 그립니다(시각화 자체는 `Viewport/Visualizers/` 의 각 파일)
 */
#include "pch.h"

#include "Editor/Viewport/EditorViewportVisualizer.h"

#include "Editor/Common/GUI/Theme/EditorColorUtil.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Viewport/EditorViewportProjection.h"
#include "Editor/Viewport/Model/EditorVisualizerDrawList.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct EditorViewportVisualizerInternal
        {
            /** @brief 글자 그림자 색입니다(밝은 배경에서도 읽히게). */
            static constexpr Color kTextShadow{ Color::fromBytes( 0, 0, 0, 200 ) };

            /** @brief 색 역할을 이번 테마의 색으로 풉니다(값에는 테마가 없다). */
            static ImU32 resolveColor( EditorVisualizerColorRole role, const float4& color )
            {
                switch ( role )
                {
                    case EditorVisualizerColorRole::ColliderWire:
                        return EditorColorUtil::toImU32( SW_THEME_COLOR( "Viewport", "ColliderWire" ) );
                    case EditorVisualizerColorRole::CameraWire:
                        return EditorColorUtil::toImU32( SW_THEME_COLOR( "Viewport", "CameraWire" ) );
                    case EditorVisualizerColorRole::SelectionOutline:
                        return EditorColorUtil::toImU32( SW_THEME_COLOR( "Viewport", "SelectionOutline" ) );
                    case EditorVisualizerColorRole::Hover:
                        return EditorColorUtil::toImU32( EditorThemeUtil::getViewportHoverColor() );
                    case EditorVisualizerColorRole::Explicit:
                        break;
                }
                return EditorColorUtil::toImU32( Color::fromFloat4( color ) );
            }

            /** @brief 그린 선 · 글자 하나를 그 통계 칸에 셉니다. */
            static void count( EditorVisualizerStats& stats, EditorVisualizerStatSlot slot, bool bText )
            {
                switch ( slot )
                {
                    case EditorVisualizerStatSlot::PhysicsShape:
                        ++stats._shape._physicsSegmentCount;
                        break;
                    case EditorVisualizerStatSlot::NavMesh:
                        ++stats._shape._navMeshSegmentCount;
                        break;
                    case EditorVisualizerStatSlot::DebugDraw:
                        ++( bText ? stats._debugDraw._textCount : stats._debugDraw._segmentCount );
                        break;
                    case EditorVisualizerStatSlot::None:
                        break;
                }
            }

            /** @brief 모은 선분은 근평면에서 잘라, 글자는 점으로 투영해 그립니다. */
            static void drawShapes( const EditorViewportVisualizerArgs& args, const EditorVisualizerDrawList& drawList )
            {
                EditorVisualizerStats& stats   = *args._pStats;
                stats._shape                   = EditorShapeVisualizerStats{};
                stats._debugDraw._segmentCount = 0;
                stats._debugDraw._textCount    = 0;
                if ( drawList._bDebugDrawGathered )
                    stats._debugDraw._frame = ImGui::GetFrameCount();

                for ( const EditorVisualizerLine& line : drawList._listLine )
                {
                    ImVec2 screenFrom;
                    ImVec2 screenTo;
                    if ( EditorViewportProjectionUtil::projectSegment( *args._pViewProj, line._from, line._to, args._canvasPos, args._canvasSize, screenFrom, screenTo ) == false )
                        continue;
                    args._pDrawList->AddLine( screenFrom, screenTo, resolveColor( line._colorRole, line._color ), line._thickness );
                    count( stats, line._statSlot, false );
                }
                for ( const EditorVisualizerText& text : drawList._listText )
                {
                    ImVec2 screen;
                    if ( EditorViewportProjectionUtil::projectPoint( *args._pViewProj, text._position, args._canvasPos, args._canvasSize, screen ) == false )
                        continue;
                    // 글자 그림자를 먼저 깔아 밝은 배경에서도 읽히게 한다.
                    args._pDrawList->AddText( ImVec2( screen.x + 1.0f, screen.y + 1.0f ), EditorColorUtil::toImU32( kTextShadow ), text._text.c_str() );
                    args._pDrawList->AddText( screen, EditorColorUtil::toImU32( Color::fromFloat4( text._color ) ), text._text.c_str() );
                    count( stats, text._statSlot, true );
                }
            }
        };
    } // namespace

    uint32 EditorViewportVisualizer::getCount()
    {
        return EditorExtensionRegistry<EditorVisualizerRegistration>::getCount();
    }

    const EditorVisualizerRegistration& EditorViewportVisualizer::getAt( uint32 index )
    {
        return EditorExtensionRegistry<EditorVisualizerRegistration>::getAt( index );
    }

    void EditorViewportVisualizer::drawAll( const EditorViewportVisualizerArgs& args, const EditorVisualizerToggles& toggles, const EditorVisualizerDrawList& drawList )
    {
        const bool bMissingDrawInputs = args._pDrawList == nullptr || args._pViewProj == nullptr;
        if ( bMissingDrawInputs )
            return;
        EditorViewportVisualizerInternal::drawShapes( args, drawList );

        for ( uint32 index = 0; index < getCount(); ++index )
        {
            const EditorVisualizerRegistration& registration = getAt( index );
            if ( toggles.isOn( registration ) == false )
                continue;
            if ( registration._pDraw != nullptr )
                registration._pDraw( args );
        }
    }

    // 이 종류의 등록 목록 — EditorModule 이미지에 하나(확장 DLL 의 등록자도 내보낸 이 함수로 오른다).
    SW_DEFINE_EDITOR_EXTENSION_KIND( EditorVisualizerRegistration )
} // namespace sw::editor

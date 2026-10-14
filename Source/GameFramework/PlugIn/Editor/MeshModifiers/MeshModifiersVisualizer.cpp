/**
 * @file MeshModifiersVisualizer.cpp
 * @brief 뷰포트 시각화 meshmodifiers.preview(툴바 "Modifiers") — 변형 결과 선(CPU 미리보기), 고른 모디파이어의 기즈모(중심 · 주축), 격자 모디파이어의 격자선 · 제어점을 그립니다.
 * @details 미리보기는 대상 메시의 정점 버퍼를 바꾸지 않고 선으로만 겹쳐 그린다(실시간 GPU 미리보기는 2 단계 — 컴퓨트 셰이더).
 */
#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Editor/Common/GUI/EditorIconGlyphs.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Viewport/EditorViewportProjection.h"
#include "Editor/Viewport/EditorViewportVisualizer.h"

#include "GameFramework/PlugIn/Editor/MeshModifiers/MeshModifierSession.h"
#include "GameFramework/PlugIn/Editor/MeshModifiers/Modifiers/LatticeModifier.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct MeshModifiersVisualizerInternal
        {
            static constexpr ImU32 kPreviewColor      = IM_COL32( 120, 220, 255, 110 );
            static constexpr ImU32 kAxisColor         = IM_COL32( 255, 200, 60, 255 );
            static constexpr ImU32 kLatticeLineColor  = IM_COL32( 255, 140, 40, 200 );
            static constexpr ImU32 kLatticePointColor = IM_COL32( 255, 240, 120, 255 );

            static void drawSegment( const EditorViewportVisualizerArgs& args, const float3& from, const float3& to, ImU32 color, float32 thickness )
            {
                ImVec2 screenFrom;
                ImVec2 screenTo;
                if ( EditorViewportProjectionUtil::projectSegment( *args._pViewProj, from, to, args._canvasPos, args._canvasSize, screenFrom, screenTo ) )
                    args._pDrawList->AddLine( screenFrom, screenTo, color, thickness );
            }

            static void drawLattice( const EditorViewportVisualizerArgs& args, const LatticeModifier& lattice, const MeshModifierBounds& fallbackBox,
                                     const float4x4& modifierToWorld )
            {
                const MeshModifierBounds box  = lattice.getEffectiveBox( fallbackBox );
                const float3             size = box.getSize();
                vector<float3>           listWorld;
                listWorld.reserve( static_cast<size_t>( lattice.getControlPointCount() ) );
                for ( int32 index = 0; index < lattice.getControlPointCount(); ++index )
                {
                    const float3& normalized = lattice.getControlPoint( index );
                    const float3  local{ box._min._x + normalized._x * size._x, box._min._y + normalized._y * size._y, box._min._z + normalized._z * size._z };
                    listWorld.push_back( float3::transform( local, modifierToWorld ) );
                }
                const int32 countX = lattice.getResolutionX();
                const int32 countY = lattice.getResolutionY();
                const int32 countZ = lattice.getResolutionZ();
                for ( int32 indexZ = 0; indexZ < countZ; ++indexZ )
                {
                    for ( int32 indexY = 0; indexY < countY; ++indexY )
                    {
                        for ( int32 indexX = 0; indexX < countX; ++indexX )
                        {
                            const float3& point = listWorld[static_cast<size_t>( lattice.getControlPointIndex( indexX, indexY, indexZ ) )];
                            if ( indexX + 1 < countX )
                                drawSegment( args, point, listWorld[static_cast<size_t>( lattice.getControlPointIndex( indexX + 1, indexY, indexZ ) )], kLatticeLineColor, 1.0f );
                            if ( indexY + 1 < countY )
                                drawSegment( args, point, listWorld[static_cast<size_t>( lattice.getControlPointIndex( indexX, indexY + 1, indexZ ) )], kLatticeLineColor, 1.0f );
                            if ( indexZ + 1 < countZ )
                                drawSegment( args, point, listWorld[static_cast<size_t>( lattice.getControlPointIndex( indexX, indexY, indexZ + 1 ) )], kLatticeLineColor, 1.0f );
                            ImVec2 screen;
                            if ( EditorViewportProjectionUtil::projectPoint( *args._pViewProj, point, args._canvasPos, args._canvasSize, screen ) )
                                args._pDrawList->AddCircleFilled( screen, 3.5f, kLatticePointColor );
                        }
                    }
                }
            }

            static void draw( const EditorViewportVisualizerArgs& args )
            {
                MeshModifierSession* pSession = getService<MeshModifierSession>();
                if ( pSession == nullptr || pSession->hasTarget() == false || pSession->findTarget() == nullptr )
                    return;
                MeshModifierSession& session = *pSession;
                session.refreshPreview();

                static vector<float3> s_listSegmentPoint; // 프레임마다 다시 채우는 재사용 버퍼
                session.makePreviewSegments( s_listSegmentPoint );
                for ( size_t index = 0; index + 1 < s_listSegmentPoint.size(); index += 2 )
                {
                    drawSegment( args, s_listSegmentPoint[index], s_listSegmentPoint[index + 1], kPreviewColor, 1.0f );
                }

                MeshModifier* pModifier = session.getStack().findModifier( session.getSelectedIndex() );
                if ( pModifier == nullptr )
                    return;
                MeshModifierEvaluation evaluation{};
                evaluation._meshToWorld          = session.computeMeshToWorld();
                evaluation._pInputs              = &session;
                const float4x4 modifierToWorld   = pModifier->computeLocalToModifier( evaluation ).invert() * evaluation._meshToWorld;
                const float3   centerWorld       = float3::transform( float3{ 0.0f, 0.0f, 0.0f }, modifierToWorld );
                const float32  axisLength        = MathUtil::max( session.getSourceBounds().getSize().getLength() * 0.6f, 0.1f );
                float3         axisDirection{ 0.0f, 0.0f, 0.0f };
                MeshModifierUtil::setComponent( axisDirection, pModifier->getGizmo()._axisIndex, axisLength );
                const float3 axisWorld = float3::transform( axisDirection, modifierToWorld );
                drawSegment( args, centerWorld - ( axisWorld - centerWorld ), axisWorld, kAxisColor, 2.0f );
                ImVec2 centerScreen;
                if ( EditorViewportProjectionUtil::projectPoint( *args._pViewProj, centerWorld, args._canvasPos, args._canvasSize, centerScreen ) )
                    args._pDrawList->AddCircle( centerScreen, 6.0f, kAxisColor, 0, 2.0f );

                // 격자 상자가 비면 입력 정점의 모디파이어 공간 상자를 쓴다 — 원본 로컬 상자 모서리를 모디파이어 공간으로 옮겨 근사한다
                LatticeModifier* pLattice = LatticeModifier::findFrom( *pModifier );
                if ( pLattice == nullptr )
                    return;
                const float4x4      localToModifier = pModifier->computeLocalToModifier( evaluation );
                const MeshModifierBounds& source    = session.getSourceBounds();
                vector<float3>      listCorner;
                for ( uint32 corner = 0; corner < 8; ++corner )
                {
                    const float3 local{ ( corner & 1u ) != 0 ? source._max._x : source._min._x, ( corner & 2u ) != 0 ? source._max._y : source._min._y,
                                        ( corner & 4u ) != 0 ? source._max._z : source._min._z };
                    listCorner.push_back( float3::transform( local, localToModifier ) );
                }
                drawLattice( args, *pLattice, MeshModifierBounds::makeFromPoints( listCorner ), modifierToWorld );
            }
        };
    } // namespace

    SW_EDITOR_VISUALIZER( MeshModifiers, "meshmodifiers.preview", 910, editoricon::kCube, "Modifiers", "메시 모디파이어 스택의 변형 미리보기 선, 기즈모, 격자 제어점", true,
                          &MeshModifiersVisualizerInternal::draw );
} // namespace sw::editor

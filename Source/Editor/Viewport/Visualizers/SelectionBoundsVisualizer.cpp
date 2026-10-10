/**
 * @file SelectionBoundsVisualizer.cpp
 * @brief 고른 오브젝트를 화면에서 알아보게 그 경계 상자를 강조색 선으로 그립니다(Godot 의 선택 상자).
 */
#include "pch.h"

#include "Core/Math/MatrixMath.h"

#include "Editor/Common/GUI/EditorIconGlyphs.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Viewport/EditorViewportProjection.h"
#include "Editor/Viewport/EditorViewportVisualizer.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Physics/Collision/AABB.h"
#include "Engine/Reflection/ReflectionCast.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct SelectionBoundsVisualizerInternal
        {
            /** @brief 지난 프레임에 그린 선택 상자 수입니다(탐침 `Editor.SelectionOutlineBoxes`). */
            static uint32& getDrawnBoxCount()
            {
                static uint32 s_count{ 0 };
                return s_count;
            }

            /** @brief 오브젝트의 씬 컴포넌트 경계를 합친 상자입니다. 경계가 있는 컴포넌트가 없으면 false 입니다. */
            static bool computeObjectBox( const GameObject& object, AABB& outBox )
            {
                bool bAny = false;
                for ( const Component* pComponent : object.getComponents() )
                {
                    const SceneComponent* pScene = pComponent != nullptr && isA<SceneComponent>( pComponent ) ? static_cast<const SceneComponent*>( pComponent ) : nullptr;
                    AABB                  box{};
                    if ( pScene == nullptr || pScene->getWorldBox( box ) == false || box.isValid() == false )
                        continue;
                    if ( bAny == false )
                    {
                        outBox = box;
                        bAny   = true;
                        continue;
                    }
                    outBox._min = float3::min( outBox._min, box._min );
                    outBox._max = float3::max( outBox._max, box._max );
                }
                return bAny;
            }

            static void draw( const EditorViewportVisualizerArgs& args )
            {
                uint32& drawnCount      = getDrawnBoxCount();
                drawnCount              = 0;
                EditorContext* pContext = EditorContext::get();
                if ( pContext == nullptr )
                    return;
                vector<GameObject*> listSelected;
                pContext->getEditorSelection().getSelectedObjects( listSelected );
                // 주황(유니티 선택 외곽선 색) — 테마 강조색은 파랑 계열이라 격자 · 카메라 프러스텀과 섞인다.
                const ImU32 color = ImGui::ColorConvertFloat4ToU32( ImVec4{ 1.0f, 0.62f, 0.15f, 0.95f } );
                for ( const GameObject* pObject : listSelected )
                {
                    AABB box{};
                    if ( pObject == nullptr || pObject->isHiddenInEditor() || computeObjectBox( *pObject, box ) == false )
                        continue;
                    const float3 arrCorner[8] = {
                        {box._min._x, box._min._y, box._min._z},
                        {box._max._x, box._min._y, box._min._z},
                        {box._max._x, box._max._y, box._min._z},
                        {box._min._x, box._max._y, box._min._z},
                        {box._min._x, box._min._y, box._max._z},
                        {box._max._x, box._min._y, box._max._z},
                        {box._max._x, box._max._y, box._max._z},
                        {box._min._x, box._max._y, box._max._z},
                    };
                    static constexpr uint32 kArrEdge[12][2] = {
                        {0, 1},
                        {1, 2},
                        {2, 3},
                        {3, 0},
                        {4, 5},
                        {5, 6},
                        {6, 7},
                        {7, 4},
                        {0, 4},
                        {1, 5},
                        {2, 6},
                        {3, 7}
                    };
                    for ( const auto& edge : kArrEdge )
                    {
                        ImVec2 screenA{};
                        ImVec2 screenB{};
                        if ( EditorViewportProjectionUtil::projectSegment( *args._pViewProj, arrCorner[edge[0]], arrCorner[edge[1]], args._canvasPos, args._canvasSize,
                                                                           screenA, screenB ) )
                            args._pDrawList->AddLine( screenA, screenB, color, 2.0f );
                    }
                    ++drawnCount;
                }
            }
        };
    } // namespace

    uint32 EditorSelectionBounds::getDrawnBoxCount()
    {
        return SelectionBoundsVisualizerInternal::getDrawnBoxCount();
    }

    SW_EDITOR_VISUALIZER( SelectionBounds, "selection_bounds", 40, editoricon::kCube, "Sel", "고른 오브젝트의 경계 상자를 강조색 선으로 표시합니다", true,
                          &SelectionBoundsVisualizerInternal::draw );
} // namespace sw::editor

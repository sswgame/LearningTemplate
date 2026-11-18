/**
 * @file SelectionBoundsVisualizer.cpp
 * @brief 고른 오브젝트를 화면에서 알아보게 그 경계 상자를 선 도형 값으로 모읍니다(Godot 의 선택 상자). 그리기는 `EditorViewportVisualizer::drawAll`.
 */
#include "pch.h"

#include "Editor/Viewport/Model/Visualizers/SelectionBoundsVisualizer.h"

#include "Core/Math/AABB.h"
#include "Core/Math/MatrixMath.h"

#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorStateStore.h"
#include "Editor/Viewport/EditorViewportVisualizer.h"
#include "Editor/Viewport/Model/EditorVisualizerDrawList.h"

#include "Engine/Localization/LocText.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/ReflectionCast.h"

namespace sw::editor
{
    namespace
    {
        struct SelectionBoundsVisualizerInternal
        {
            /** @brief 선택 상자의 그리기 기록입니다. 에디터 범위의 상태 저장소가 소유한다. */
            struct DrawState
            {
                uint32 _drawnBoxCount{ 0 };   ///< 지난 프레임에 그린 선택 상자 수(감시 값 `Editor.SelectionOutlineBoxes`)
                uint64 _hoveredObjectID{ 0 }; ///< 호버 중인 오브젝트(0 이면 없음)
            };

            static DrawState& getDrawState() { return getEditorState<DrawState>(); }

            /** @brief 오브젝트의 씬 컴포넌트 경계를 합친 상자입니다. 경계가 있는 컴포넌트가 없으면 false 입니다. */
            static bool computeObjectBox( const GameObject& object, AABB& outBox )
            {
                bool bAny = false;
                for ( const Component* pComponent : object.getComponents() )
                {
                    const SceneComponent* pScene = pComponent != nullptr && isA<SceneComponent>( pComponent ) ? static_cast<const SceneComponent*>( pComponent ) : nullptr;
                    AABB                  box{};
                    const bool            bNoWorldBox = pScene == nullptr || pScene->getWorldBox( box ) == false || box.isValid() == false;
                    if ( bNoWorldBox )
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

            static void gather( const EditorViewportVisualizerArgs& /*args*/, EditorVisualizerDrawList& outDrawList )
            {
                DrawState& drawState     = getDrawState();
                drawState._drawnBoxCount = 0;
                vector<GameObject*> listSelected;
                editor::getMainSceneSelection().getSelectedObjects( listSelected );
                // 주황(유니티 선택 외곽선 색) — 테마 강조색은 파랑 계열이라 격자 · 카메라 프러스텀과 섞인다.
                EditorVisualizerLine selected{};
                selected._thickness = 2.0f;
                selected._colorRole = EditorVisualizerColorRole::SelectionOutline;
                for ( const GameObject* pObject : listSelected )
                {
                    AABB       box{};
                    const bool bNoObjectBox = pObject == nullptr || pObject->isRenderHidden() || computeObjectBox( *pObject, box ) == false;
                    if ( bNoObjectBox )
                        continue;
                    outDrawList.addBox( box, selected );
                    ++drawState._drawnBoxCount;
                }
                // 호버는 선택보다 약하게 — 테마의 호버 색 · 얇은 선. 이미 고른 오브젝트는 건너뛴다(선택 상자가 이미 있다).
                const uint64 hoveredID = drawState._hoveredObjectID;
                if ( hoveredID == 0 )
                    return;
                for ( const GameObject* pObject : listSelected )
                {
                    if ( pObject != nullptr && pObject->getObjectID() == hoveredID )
                        return;
                }
                const GameObjectManager* pManager = editor::getActiveObjectManager();
                const GameObject*        pHovered = pManager != nullptr ? pManager->findGameObjectByID( hoveredID ) : nullptr;
                AABB                     box{};
                if ( pHovered == nullptr || computeObjectBox( *pHovered, box ) == false )
                    return;
                EditorVisualizerLine hovered{};
                hovered._thickness = 1.0f;
                hovered._colorRole = EditorVisualizerColorRole::Hover;
                outDrawList.addBox( box, hovered );
            }
        };
    } // namespace

    void SelectionBoundsVisualizer::setHoveredObjectID( uint64 objectID )
    {
        SelectionBoundsVisualizerInternal::getDrawState()._hoveredObjectID = objectID;
    }

    uint64 SelectionBoundsVisualizer::getHoveredObjectID()
    {
        return SelectionBoundsVisualizerInternal::getDrawState()._hoveredObjectID;
    }

    uint32 SelectionBoundsVisualizer::getDrawnBoxCount()
    {
        return SelectionBoundsVisualizerInternal::getDrawState()._drawnBoxCount;
    }

    SW_EDITOR_VISUALIZER_GATHER( SelectionBounds, "selection_bounds", 40, SW_EDITOR_ICON_NAME( "Cube" ), SW_LOCKEYREF( "EdVisualizer", "SelectionBoundsLabel" ),
                                 SW_LOCKEYREF( "EdVisualizer", "SelectionBoundsTip" ), true, &SelectionBoundsVisualizerInternal::gather );
} // namespace sw::editor

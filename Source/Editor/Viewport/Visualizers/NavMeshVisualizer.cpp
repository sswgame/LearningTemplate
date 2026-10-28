/**
 * @file NavMeshVisualizer.cpp
 * @brief 베이크한 내비메시의 폴리곤 테두리를 뷰포트에 그립니다(`SceneNavigation::drawDebug` 의 `NavDebugDrawFlag::kMesh`). 베이크는 `navigation.bake` 커맨드입니다.
 */
#include "pch.h"

#include "Editor/Common/GUI/Theme/EditorIconGlyphs.h"
#include "Editor/Viewport/EditorProjectedLineRenderer.h"
#include "Editor/Viewport/EditorViewportVisualizer.h"

#include "Engine/Navigation/Component/SceneNavigation.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

namespace sw::editor
{
    namespace
    {
        /**
         * @brief 마련한 내비메시마다 폴리곤 테두리를 긋습니다.
         * @details 베이크하지 않은 씬은 그릴 메시가 없어 아무 선도 나오지 않는다 — 이 시각화는 베이크를 일으키지 않는다(`ensureNavMesh` 를 부르지 않는다).
         */
        void drawNavMesh( const EditorViewportVisualizerArgs& args )
        {
            EditorShapeVisualizerStats& stats = args._pStats->_shape;
            stats._navMeshSegmentCount        = 0;
            if ( args._pObjectManager == nullptr || args._pViewProj == nullptr )
                return;
            const SceneNavigation&      navigation = static_cast<const GameObjectManager*>( args._pObjectManager )->getSubsystem<SceneNavigation>();
            EditorProjectedLineRenderer renderer{ args._pDrawList, *args._pViewProj, args._canvasPos, args._canvasSize };
            navigation.drawDebug( renderer, NavDebugDrawFlag::kMesh );
            stats._navMeshSegmentCount = renderer.getDrawnSegmentCount();
        }
    } // namespace

    SW_EDITOR_VISUALIZER( NavMesh, "navmesh", 120, editoricon::kNavigation, "Nav", "베이크한 내비메시의 폴리곤 테두리를 표시합니다(Build > Bake Navigation 으로 베이크)", false,
                          &drawNavMesh );
} // namespace sw::editor

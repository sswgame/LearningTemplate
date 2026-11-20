/**
 * @file NavMeshVisualizer.cpp
 * @brief 베이크한 내비메시의 폴리곤 테두리를 뷰포트 도형 값으로 모읍니다(`SceneNavigation::drawDebug` 의 `NavDebugDrawFlag::kMesh`). 베이크는 `navigation.bake` 커맨드입니다.
 */
#include "pch.h"

#include "Editor/Viewport/EditorViewportVisualizer.h"
#include "Editor/Viewport/Model/EditorVisualizerDrawList.h"

#include "Engine/Localization/LocText.h"
#include "Engine/Navigation/Scene/SceneNavigation.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

namespace sw::editor
{
    namespace
    {
        /**
         * @brief 마련한 내비메시마다 폴리곤 테두리를 모읍니다. 그린 수는 그리는 쪽이 `Editor.NavMeshSegments` 로 센다.
         * @details 베이크하지 않은 씬은 그릴 메시가 없어 아무 선도 나오지 않는다 — 이 시각화는 베이크를 일으키지 않는다(`ensureNavMesh` 를 부르지 않는다).
         */
        void gatherNavMesh( const EditorViewportVisualizerArgs& args, EditorVisualizerDrawList& outDrawList )
        {
            if ( args._pObjectManager == nullptr )
                return;
            const SceneNavigation&   navigation = static_cast<const GameObjectManager*>( args._pObjectManager )->getSubsystem<SceneNavigation>();
            EditorVisualizerLineSink sink{ outDrawList, EditorVisualizerStatSlot::NavMesh };
            navigation.drawDebug( sink, NavDebugDrawFlag::kMesh );
        }
    } // namespace

    SW_EDITOR_VISUALIZER_GATHER( NavMesh, "navmesh", 120, SW_EDITOR_ICON_NAME( "Navigation" ), SW_LOCKEYREF( "EdVisualizer", "NavMeshLabel" ), SW_LOCKEYREF( "EdVisualizer", "NavMeshTip" ),
                                 false, &gatherNavMesh );
} // namespace sw::editor

/**
 * @file EditorNavigationBakeCommand.cpp
 * @brief 에디터 커맨드 "Bake Navigation"(`navigation.bake`) — Build 메뉴와 커맨드 팔레트에서 활성 씬의 내비메시를 베이크합니다.
 */
#include "pch.h"

#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/Commands/Scene/EditorNavigationCommands.h"
#include "Editor/Common/GUI/EditorIconGlyphs.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorNotificationManager.h"
#include "Editor/Common/Workspace/EditorPlaySession.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorStateStore.h"

#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Probe/AutomationProbe.h"

namespace sw::editor
{
    namespace
    {
        struct EditorNavigationBakeCommandInternal
        {
            /** @brief 이 에디터 세션에서 `navigation.bake` 가 끝난 횟수(탐침 `Editor.NavigationBakeCount`)의 상태입니다. 에디터 범위의 상태 저장소가 소유한다. */
            struct BakeRunState
            {
                uint32 _count{ 0 };
            };

            static void runBake()
            {
                GameObjectManager* pManager = editor::getActiveObjectManager();
                if ( pManager == nullptr )
                    return;
                const uint32   bakedCount    = EditorNavigationCommands::bakeAll( *pManager );
                EditorContext& editorContext = editor::getSubsystem<EditorContext>();
                ++getEditorState<BakeRunState>()._count;
                if ( bakedCount == 0 )
                    editorContext.getSubsystem<EditorNotificationManager>().push( "Bake Navigation", "Nothing to bake - add a NavMeshSurface and geometry", NotificationType::Warning );
                else
                    editorContext.getSubsystem<EditorNotificationManager>().push( "Bake Navigation", "Baked " + to_string( bakedCount ) + " agent type(s) - show them with the Nav toggle", NotificationType::Success );
            }

            /** @brief 편집 상태에서만 켠다 — 플레이 중 다시 베이크하면 군중이 내비메시를 잃는다. */
            static bool isEnabled()
            {
                return EditorPlaySession::areSceneEditsAllowed() && editor::getActiveObjectManager() != nullptr;
            }

            [[nodiscard]] static bool readBakeRunCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                outValue = static_cast<float64>( getEditorState<BakeRunState>()._count );
                return true;
            }

            [[nodiscard]] static bool readNavMeshCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                GameObjectManager* pManager = editor::getActiveObjectManager();
                outValue                    = pManager != nullptr ? static_cast<float64>( EditorNavigationCommands::countBakedMeshes( *pManager ) ) : 0.0;
                return true;
            }
        };
    } // namespace

    SW_EDITOR_COMMAND( NavigationBake, "navigation.bake", 3300, "Bake Navigation", editoricon::kNavigation, "Navigation",
                       "활성 씬의 내비메시를 에이전트 종류마다 다시 베이크합니다(Nav 시각화로 폴리곤 테두리를 봅니다)", "Bake the navmesh of the active scene", nullptr,
                       &EditorNavigationBakeCommandInternal::runBake, &EditorNavigationBakeCommandInternal::isEnabled, Scene, "MainMenu/Build", None, 0 );

    SW_AUTOMATION_PROBE( editorNavigationBakeCount, "Editor.NavigationBakeCount", "Times the navigation.bake command ran in this editor session",
                         &EditorNavigationBakeCommandInternal::readBakeRunCount );
    SW_AUTOMATION_PROBE( editorNavMeshCount, "Editor.NavMeshCount", "Agent types that have a baked navmesh in the active scene",
                         &EditorNavigationBakeCommandInternal::readNavMeshCount );
} // namespace sw::editor

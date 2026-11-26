/**
 * @file EditorNavigationBakeCommand.cpp
 * @brief 에디터 커맨드 "Bake Navigation"(`navigation.bake`) — Build 메뉴와 커맨드 팔레트에서 활성 씬의 내비메시를 베이크합니다.
 */
#include "pch.h"

#include "Core/String/StringBuilder.h"

#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/Config/EditorIconGlyphTable.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorSubsystems.h"
#include "Editor/Common/Workspace/Layout/EditorStateStore.h"
#include "Editor/Common/Workspace/Notification/EditorNotificationManager.h"
#include "Editor/Common/Workspace/Play/EditorPlaySession.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Navigation/INavMeshTileStore.h"
#include "Engine/Navigation/NavMeshSettings.h"
#include "Engine/Navigation/Scene/SceneNavigation.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

namespace sw::editor
{
    namespace
    {
        struct EditorNavigationBakeCommandInternal
        {
            /** @brief 지금 베이크해 둔 에이전트 종류 수입니다(감시 값 `Editor.NavMeshCount`). */
            static uint32 countBakedMeshes( GameObjectManager& manager )
            {
                const SceneNavigation& navigation = manager.getSubsystem<SceneNavigation>();
                vector<hashed_string>  listAgentType;
                navigation.collectBakedAgentTypes( listAgentType );
                uint32 count{ 0 };
                for ( const hashed_string& agentType : listAgentType )
                {
                    const INavMeshTileStore* pNavMesh = navigation.findNavMesh( agentType );
                    if ( pNavMesh != nullptr && pNavMesh->getPolygonCount() > 0 ) // 기하가 없는 씬도 ±1 상자로 초기화돼 빈 메시가 서지만 그것은 베이크가 아니다
                        ++count;
                }
                return count;
            }

            /**
             * @brief 마련한 내비메시를 버리고 다시 베이크합니다(`SceneNavigation::rebakeAllNavMeshes` — 언리얼 Build Paths · 유니티 Navigation > Bake).
             * @details 플레이 중에는 부르지 않는다 — 군중이 내비메시를 잃는다. 커맨드는 편집 상태에서만 켜진다. 베이크 자체는 엔진 함수라 헤드리스 단위 테스트가 본다
             *          (`SceneNavigationBakeTest`).
             * @return 폴리곤이 선 에이전트 종류 수입니다. 씬에 기하가 없으면 0 입니다.
             */
            static uint32 bakeAll( GameObjectManager& manager )
            {
                SceneNavigation& navigation = manager.getSubsystem<SceneNavigation>();
                (void)navigation.rebakeAllNavMeshes();
                navigation.flushTileBakes();
                const uint32 bakedCount = countBakedMeshes( manager );
                SW_LOG_INFO_ID( "EditorNavigationCommands.NavigationBakedAgentType", "Navigation baked: %# of %# agent type(s) have a navmesh", bakedCount,
                                static_cast<uint32>( navigation.getSettings()._listAgentType.size() ) );
                return bakedCount;
            }

            /** @brief 이 에디터 세션에서 `navigation.bake` 가 끝난 횟수(감시 값 `Editor.NavigationBakeCount`)의 상태입니다. 에디터 범위의 상태 저장소가 소유한다. */
            struct BakeRunState
            {
                uint32 _count{ 0 };
            };

            static void runBake()
            {
                GameObjectManager* pManager = editor::getActiveObjectManager();
                if ( pManager == nullptr )
                    return;
                const uint32   bakedCount    = bakeAll( *pManager );
                EditorContext& editorContext = editor::getSubsystem<EditorContext>();
                ++getEditorState<BakeRunState>()._count;
                if ( bakedCount == 0 )
                    editorContext.getSubsystem<EditorNotificationManager>().push( "Bake Navigation", "Nothing to bake - add a NavMeshSurface and geometry", NotificationKind::Warning );
                else
                {
                    StringBuilder<constant::buffer::kMax256> textBuilder;
                    textBuilder.appendFormat( "Baked %# agent type(s) - show them with the Nav toggle", bakedCount );
                    editorContext.getSubsystem<EditorNotificationManager>().push( "Bake Navigation", string( textBuilder.view() ), NotificationKind::Success );
                }
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
                outValue                    = pManager != nullptr ? static_cast<float64>( countBakedMeshes( *pManager ) ) : 0.0;
                return true;
            }
        };
    } // namespace

    SW_EDITOR_COMMAND( NavigationBake, "navigation.bake", &EditorNavigationBakeCommandInternal::runBake, &EditorNavigationBakeCommandInternal::isEnabled, SceneUnrecorded ); // 굽기 작업을 시작한다 — 씬 기록이 아니다

    SW_AUTOMATION_WATCH( editorNavigationBakeCount, "Editor.NavigationBakeCount", "Times the navigation.bake command ran in this editor session",
                         &EditorNavigationBakeCommandInternal::readBakeRunCount );
    SW_AUTOMATION_WATCH( editorNavMeshCount, "Editor.NavMeshCount", "Agent types that have a baked navmesh in the active scene",
                         &EditorNavigationBakeCommandInternal::readNavMeshCount );
} // namespace sw::editor

/**
 * @file ActionPlatformerGameModeComponentDevTools.cpp
 * @brief `ActionPlatformerGameModeComponent` 의 개발 명령 · 자동화 감시 값입니다. `Dev/` 라 배포 구성은 짓지 않는다.
 */
#include "pch.h"

#include "Core/Foundation/Constants.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Serialization/Format/FormatDesc.h"
#include "Engine/Serialization/Serializer/StateIO.h"

#include "GameFramework/Kits/Genre/Action/ActionPlatformer/ActionStageRun.h"
#include "GameFramework/Kits/Genre/Action/ActionPlatformer/Component/ActionPlatformerGameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Subsystem/GameSubsystems.h"

namespace sw
{
    namespace
    {
        struct ActionPlatformerGameModeComponentDevInternal
        {
            /** @brief 활성 씬의 플랫포머 디렉터입니다. 없으면 nullptr 입니다(개발 명령이 그 까닭으로 답한다). */
            [[nodiscard]] static ActionPlatformerGameModeComponent* findGameMode( SceneIterationKey key )
            {
                const SceneManager* const          pScenes   = game::findSubsystem<SceneManager>();
                const Scene* const                 pScene    = pScenes != nullptr ? pScenes->getActiveScene() : nullptr;
                GameObjectManager* const           pManager  = pScene != nullptr ? pScene->getObjectManager() : nullptr;
                ActionPlatformerGameModeComponent* pGameMode = nullptr;
                // 감시 값(ActionPlatformer.*)과 같은 길 — 타입 버킷 순회다. 컴포넌트 등록부(getAll)는 이 씬에서 디렉터를 돌려주지 않았다(통합 확인).
                if ( pManager != nullptr )
                {
                    pManager->forEachComponentOfType<ActionPlatformerGameModeComponent>( key, [&pGameMode]( ActionPlatformerGameModeComponent* pFound )
                    {
                        if ( pGameMode == nullptr )
                            pGameMode = pFound;
                    } );
                }
                return pGameMode;
            }

            static bool runKill( SceneIterationKey key, const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() == false )
                    return false;
                ActionPlatformerGameModeComponent* const pGameMode = findGameMode( key );
                if ( pGameMode == nullptr )
                    outReply = "actionplatformer.kill: no action platformer game mode in the active scene";
                else if ( pGameMode->killPlayer() )
                    outReply = "actionplatformer.kill: the player died where it stood";
                else
                    outReply = "actionplatformer.kill: the run is not playing or the player is already dead";
                return true;
            }
        };
    } // namespace

    SW_DEV_SCENE_COMMAND( ActionPlatformerKill, "actionplatformer.kill", "actionplatformer.kill", "Kill the action platformer player where it stands (one life, then the stage's respawn rules)",
                          &ActionPlatformerGameModeComponentDevInternal::runKill );
    SW_AUTOMATION_GETTER_WATCH( actionPlatformerState, "ActionPlatformer.State", "Action platformer run state (0 not started, 1 playing, 2 cleared, 3 game over)",
                                ActionPlatformerGameModeComponent, &ActionPlatformerGameModeComponent::getRunState );
    SW_AUTOMATION_GETTER_WATCH( actionPlatformerCheckpoint, "ActionPlatformer.Checkpoint", "Last checkpoint index passed (-1 = the stage start)", ActionPlatformerGameModeComponent,
                                &ActionPlatformerGameModeComponent::getCheckpointIndex );
    SW_AUTOMATION_GETTER_WATCH( actionPlatformerSecrets, "ActionPlatformer.Secrets", "Secrets picked up (committed and carried)", ActionPlatformerGameModeComponent,
                                &ActionPlatformerGameModeComponent::getSecretCount );
    SW_AUTOMATION_GETTER_WATCH( actionPlatformerLives, "ActionPlatformer.Lives", "Lives left", ActionPlatformerGameModeComponent, &ActionPlatformerGameModeComponent::getLives );
    SW_AUTOMATION_GETTER_WATCH( actionPlatformerScore, "ActionPlatformer.Score", "Run score 0..100 (time, hits, secrets)", ActionPlatformerGameModeComponent,
                                &ActionPlatformerGameModeComponent::getScore );
    SW_AUTOMATION_GETTER_WATCH( actionPlatformerPlayerX, "ActionPlatformer.PlayerX", "Player body world X", ActionPlatformerGameModeComponent, &ActionPlatformerGameModeComponent::getPlayerX );
    SW_AUTOMATION_GETTER_WATCH( actionPlatformerPlayerY, "ActionPlatformer.PlayerY", "Player body world Y", ActionPlatformerGameModeComponent, &ActionPlatformerGameModeComponent::getPlayerY );
    SW_AUTOMATION_GETTER_WATCH( actionPlatformerRespawnPhase, "ActionPlatformer.RespawnPhase", "Respawn flow phase (0 alive, 1 waiting, 2 selection, 3 search)", ActionPlatformerGameModeComponent,
                                &ActionPlatformerGameModeComponent::getRespawnPhase );
    SW_AUTOMATION_GETTER_WATCH( actionPlatformerRespawns, "ActionPlatformer.Respawns", "Times the player stood up again", ActionPlatformerGameModeComponent, &ActionPlatformerGameModeComponent::getRespawnCount );
    SW_AUTOMATION_GETTER_WATCH( actionPlatformerLastRespawnPolicy, "ActionPlatformer.LastRespawnPolicy",
                                "Policy of the last respawn (0 last checkpoint, 1 in place, 2 nearest spawn point, 3 team base, 4 player choice, -1 none)", ActionPlatformerGameModeComponent,
                                &ActionPlatformerGameModeComponent::getLastRespawnPolicy );
    SW_AUTOMATION_GETTER_WATCH( actionPlatformerLastRespawnShift, "ActionPlatformer.LastRespawnShift", "Distance between where the player died and where it stood up last time (m)",
                                ActionPlatformerGameModeComponent, &ActionPlatformerGameModeComponent::getLastRespawnShift );
    SW_AUTOMATION_GETTER_WATCH( actionPlatformerInPlaceUses, "ActionPlatformer.InPlaceUses", "Respawns used by the in-place policy", ActionPlatformerGameModeComponent,
                                &ActionPlatformerGameModeComponent::getInPlaceUses );
} // namespace sw

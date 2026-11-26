/**
 * @file ShooterDevCommands.cpp
 * @brief 슈터 플레이어(`ShooterPlayerComponent`)의 치트(`cheat.ammo` · `cheat.shooter.*`)와 감시 값(`Shooter.*`), 게임 모드의 효과 풀 감시 값입니다. `Dev/` 라 배포 구성은 짓지 않는다.
 */
#include "pch.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/OrientationUtil.h"
#include "Core/Math/Ray.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Automation/Watch/AutomationWatchComponent.h"
#include "Engine/Character/Hit/CharacterHit.h"
#include "Engine/Character/Hit/MeleeHitWindow.h"
#include "Engine/Character/PoseModifier/PoseModifierComponent.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Object/Prefab/PrefabCache.h"
#include "Engine/Physics/PhysicsSettings.h"
#include "Engine/Physics/Scene/ScenePhysics.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Telemetry/TelemetryEvent.h"
#include "Engine/Telemetry/TelemetrySubsystem.h"
#include "Engine/Visual/Component/MeshComponent.h"

#include "GameFramework/Runtime/Character/Camera/CameraDirectorComponent.h"
#include "GameFramework/Runtime/Character/Combat/Killcam/KillcamComponent.h"
#include "GameFramework/Runtime/Character/Combat/Projectile/ProjectileDef.h"
#include "GameFramework/Runtime/Character/Control/Controller/AIControllerComponent.h"
#include "GameFramework/Runtime/Character/Control/Controller/PlayerControllerComponent.h"
#include "GameFramework/Runtime/Character/Control/FirstPersonCameraComponent.h"
#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"
#include "GameFramework/Runtime/Foundation/Condition/GameConditions.h"
#include "GameFramework/Runtime/Foundation/Presentation/GameSound.h"
#include "GameFramework/Runtime/Foundation/Presentation/IHUDViewModelBinder.h"
#include "GameFramework/Runtime/Foundation/Save/StatefulComponentRegistrar.h"
#include "GameFramework/Runtime/Foundation/Service/GameSubsystems.h"
#include "GameFramework/Runtime/Gameplay/Appearance/AppearanceDatabase.h"
#include "GameFramework/Runtime/Gameplay/Appearance/CharacterAppearanceComponent.h"
#include "GameFramework/Runtime/Gameplay/Inventory/ItemCatalog.h"

#include "Games/Playground/Shooter3D/ShooterAvatarComponent.h"
#include "Games/Playground/Shooter3D/ShooterBodyMovementComponent.h"
#include "Games/Playground/Shooter3D/ShooterEnemyComponent.h"
#include "Games/Playground/Shooter3D/ShooterGameModeComponent.h"
#include "Games/Playground/Shooter3D/ShooterHUDViewModel.h"
#include "Games/Playground/Shooter3D/ShooterNetPawnComponent.h"
#include "Games/Playground/Shooter3D/ShooterPlayerComponent.h"

namespace sw
{
    namespace
    {
        struct ShooterDevCommandsInternal
        {
            /** @brief 첫 슈터 플레이어의 지금 무기에 장전한 탄의 한 알 피해입니다(무기 · 탄 · 수정자 조합의 결과). */
            [[nodiscard]] static bool readShotDamage( const ShooterPlayerComponent& player, float64& outValue )
            {
                outValue = static_cast<float64>( player.getCurrentWeapon().getShot()._damage );
                return true;
            }

            /** @brief 첫 슈터 플레이어의 폰을 가진 조종자를 반환합니다. 0 은 플레이어, 1 은 AI(자동 플레이), -1 은 없음입니다(그 밖의 조종자도 -1). */
            [[nodiscard]] static bool readControllerKind( const ShooterPlayerComponent& player, float64& outValue )
            {
                GameObject*              pOwner   = player.getOwner();
                const GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
                const PawnComponent*     pPawn    = pOwner != nullptr ? pOwner->getComponent<PawnComponent>() : nullptr;
                if ( pPawn == nullptr || pManager == nullptr )
                    return false;
                // 조종자 목록은 레지스트리에서 얻는다. 폰을 가진 것 하나를 찾는다.
                outValue = -1.0;
                for ( ControllerComponent* pController : pManager->getComponentRegistry().getAll<ControllerComponent>() )
                {
                    if ( pController == nullptr || pController->getPawn() != pPawn->getHandle() )
                        continue;
                    if ( castTo<PlayerControllerComponent>( pController ) != nullptr )
                        outValue = 0.0;
                    else if ( castTo<AIControllerComponent>( pController ) != nullptr )
                        outValue = 1.0;
                }
                return true;
            }

            /** @brief 활성 씬의 슈터 플레이어를 모읍니다. 씬이 없거나 하나도 없으면 그 까닭(문장이 명령 답이다)입니다. */
            [[nodiscard]] static expected<vector<ShooterPlayerComponent*>> collectPlayers()
            {
                SceneManager* const pSceneManager = game::findSubsystem<SceneManager>();
                GameObjectManager*  pManager      = pSceneManager != nullptr ? pSceneManager->findActiveObjectManager() : nullptr;
                if ( pManager == nullptr )
                    return unexpected( Error{ ErrorKind::NotFound, "cheat: no active scene" } );
                vector<ShooterPlayerComponent*> listPlayer;
                for ( ShooterPlayerComponent* pPlayer : pManager->getComponentRegistry().getAll<ShooterPlayerComponent>() )
                {
                    listPlayer.push_back( pPlayer );
                }
                // 서버에서 아무도 조종하지 않는 씬의 몸은 판을 두는 사람이 아니다.
                VectorUtil::removeIf( listPlayer, [pManager]( const ShooterPlayerComponent* pPlayer )
                {
                    const ShooterGameModeComponent* const pGameMode = GameModeComponent::resolve<ShooterGameModeComponent>( *pManager, pPlayer->getGameMode() );
                    return pGameMode != nullptr && pGameMode->getPlayerViews().empty() == false &&
                           VectorUtil::findBy<&ShooterPlayerView::_object>( pGameMode->getPlayerViews(), pPlayer->getOwner()->getHandle() ) == nullptr;
                } );
                if ( listPlayer.empty() )
                    return unexpected( Error{ ErrorKind::NotFound, "cheat: no ShooterPlayerComponent in the active scene" } );
                return listPlayer;
            }

            /** @brief `cheat.ammo` — 탄 보상과 같이 무기마다 탄창 두 개를 예비 탄에 더합니다. */
            static bool runAmmo( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() == false )
                    return false;
                expected<vector<ShooterPlayerComponent*>> collected = collectPlayers();
                if ( collected.has_value() == false )
                {
                    outReply = string( collected.error().message() );
                    return true;
                }
                const vector<ShooterPlayerComponent*> listPlayer = std::move( *collected );
                for ( ShooterPlayerComponent* pPlayer : listPlayer )
                {
                    pPlayer->addWaveAmmo();
                }
                StringBuilder<constant::buffer::kMax256> replyBuilder;
                replyBuilder.appendFormat( "cheat.ammo refilled %# player(s)", listPlayer.size() );
                outReply = string( replyBuilder.view() );
                return true;
            }

            /** @brief `cheat.shooter.heal` — 살아 있는 슈터 플레이어의 체력을 가득 채웁니다(쓰러진 플레이어는 `HealthStateMachine` 가 회복을 받지 않는다). */
            static bool runHeal( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() == false )
                    return false;
                expected<vector<ShooterPlayerComponent*>> collected = collectPlayers();
                if ( collected.has_value() == false )
                {
                    outReply = string( collected.error().message() );
                    return true;
                }
                const vector<ShooterPlayerComponent*> listPlayer  = std::move( *collected );
                int32                                 healedCount = 0;
                for ( ShooterPlayerComponent* pPlayer : listPlayer )
                {
                    if ( pPlayer->isAlive() == false )
                        continue;
                    pPlayer->restoreHealth( pPlayer->getMaxHealth() ); // 최대 체력만큼 채우면 가득 찬다(heal 이 최대에서 자른다).
                    ++healedCount;
                }
                StringBuilder<constant::buffer::kMax256> replyBuilder;
                replyBuilder.appendFormat( "cheat.shooter.heal healed %# of %# player(s)", healedCount, listPlayer.size() );
                outReply = string( replyBuilder.view() );
                return true;
            }

            /**
             * @brief `cheat.shooter.god` — 슈터 플레이어의 피해 면역을 뒤집습니다. 첫 플레이어의 지금 값을 기준으로 모두 같은 값이 됩니다.
             * @details `cheat.god`(어빌리티 시스템 태그)은 `HealthStateMachine` 를 쓰는 슈터 플레이어에 닿지 않아 따로 둔다.
             */
            static bool runGod( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() == false )
                    return false;
                expected<vector<ShooterPlayerComponent*>> collected = collectPlayers();
                if ( collected.has_value() == false )
                {
                    outReply = string( collected.error().message() );
                    return true;
                }
                const vector<ShooterPlayerComponent*> listPlayer = std::move( *collected );
                const bool                            bImmune    = listPlayer.front()->isDamageImmune() == false;
                for ( ShooterPlayerComponent* pPlayer : listPlayer )
                {
                    pPlayer->setDamageImmune( bImmune );
                }
                StringBuilder<constant::buffer::kMax256> replyBuilder;
                replyBuilder.appendFormat( "%#%# player(s)", string( bImmune ? "cheat.shooter.god on for " : "cheat.shooter.god off for " ), listPlayer.size() );
                outReply = string( replyBuilder.view() );
                return true;
            }

            /** @brief `cheat.shooter.down` — 살아 있는 슈터 플레이어를 바로 쓰러뜨립니다(면역은 끈다). 쓰러짐 뒤 판 다시 열기 · 매치 끝내기를 적을 기다리지 않고 보는 시나리오의 손입니다. */
            static bool runDown( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() == false )
                    return false;
                expected<vector<ShooterPlayerComponent*>> collected = collectPlayers();
                if ( collected.has_value() == false )
                {
                    outReply = string( collected.error().message() );
                    return true;
                }
                const vector<ShooterPlayerComponent*> listPlayer = std::move( *collected );
                int32                                 downCount  = 0;
                for ( ShooterPlayerComponent* pPlayer : listPlayer )
                {
                    pPlayer->setDamageImmune( false );
                    pPlayer->takeDamage( pPlayer->getMaxHealth() );
                    downCount += pPlayer->isAlive() ? 0 : 1;
                }
                StringBuilder<constant::buffer::kMax256> replyBuilder;
                replyBuilder.appendFormat( "cheat.shooter.down downed %# of %# player(s)", downCount, listPlayer.size() );
                outReply = string( replyBuilder.view() );
                return true;
            }

            /** @brief `cheat.shooter.face` — 가장 가까운 남의 몸 가운데를 바라봅니다(접속한 클라이언트). 두 사람이 서로 쏘는 시나리오의 손입니다. */
            static bool runFace( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() == false )
                    return false;
                expected<vector<ShooterPlayerComponent*>> collected = collectPlayers();
                if ( collected.has_value() == false )
                {
                    outReply = string( collected.error().message() );
                    return true;
                }
                for ( ShooterPlayerComponent* pPlayer : *collected )
                {
                    GameObject*                      pOwner   = pPlayer->getOwner();
                    const ShooterNetPawnComponent*   pNetPawn = pOwner->getComponent<ShooterNetPawnComponent>();
                    FirstPersonCameraComponent*      pCamera  = pOwner->getComponent<FirstPersonCameraComponent>();
                    const optional<AbsolutePosition> target   = pNetPawn != nullptr ? pNetPawn->findNearestRemotePawn( pPlayer->getFeetPosition() ) : nullopt;
                    if ( pCamera == nullptr || target.has_value() == false )
                        continue;
                    const float3 eye    = pOwner->getManager()->getWorldOrigin().toAbsolute( pPlayer->getEyePosition() );
                    const float3 toBody = target->_value + float3{ 0.0f, kFaceHeight, 0.0f } - eye;
                    pCamera->setAngles( MathUtil::atan2( toBody._x, toBody._z ), MathUtil::atan2( toBody._y, float3{ toBody._x, 0.0f, toBody._z }.getLength() ) );
                    outReply = "cheat.shooter.face turned to the nearest player";
                    return true;
                }
                outReply = "cheat.shooter.face: no other player in sight";
                return true;
            }

            static constexpr float32 kFaceHeight = 0.9f; ///< 겨누는 높이(몸 가운데, m) — 반동이 위로 튀어도 몸 안에 남는다

            /** @brief `cheat.shooter.projectile [id]` — 슈터 플레이어의 지금 무기에 탄을 장전합니다. id 를 빼면 그 무기의 기본 탄입니다(무기 · 탄 조합 시나리오). */
            static bool runProjectile( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() > 1 )
                    return false;
                expected<vector<ShooterPlayerComponent*>> collected = collectPlayers();
                if ( collected.has_value() == false )
                {
                    outReply = string( collected.error().message() );
                    return true;
                }
                const vector<ShooterPlayerComponent*> listPlayer   = std::move( *collected );
                const hashed_string                   projectileID = listArgument.empty() ? hashed_string{} : hashed_string( listArgument.front() );
                int32                                 loadedCount  = 0;
                for ( ShooterPlayerComponent* pPlayer : listPlayer )
                {
                    loadedCount += pPlayer->loadProjectile( projectileID ) ? 1 : 0; // 거절한 까닭은 장전이 남겼다
                }
                StringBuilder<constant::buffer::kMax256> replyBuilder;
                replyBuilder.appendFormat( "cheat.shooter.projectile loaded '%#' for %# of %# player(s)", projectileID.c_str(), loadedCount, listPlayer.size() );
                outReply = string( replyBuilder.view() );
                return true;
            }
        };

        struct ShooterEffectWatchInternal
        {
            /** @brief 감시 값 `Shooter.EffectCreatedCount` 입니다. 판의 효과 풀이 지금까지 새로 만든 인스턴스 수입니다. */
            [[nodiscard]] static bool readEffectCreatedCount( const GameObjectManager* pManager, float64& outValue )
            {
                // 게임 모드는 씬에 하나다 — 둘이면 오류로 값이 없다(등록 순서로 고르지 않는다).
                const ShooterGameModeComponent* pGameMode = pManager != nullptr ? pManager->getComponentRegistry().findUnique<ShooterGameModeComponent>() : nullptr;
                if ( pGameMode == nullptr )
                    return false;
                outValue = static_cast<float64>( pGameMode->getEffectCreatedCount() );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_GETTER_WATCH( shooterWeaponIndex, "Shooter.WeaponIndex", "Weapon slot of the first shooter player (0 rifle, 1 shotgun, 2 pistol)", ShooterPlayerComponent, &ShooterPlayerComponent::getWeaponIndex );
    SW_AUTOMATION_GETTER_WATCH( shooterPlayerAlive, "Shooter.PlayerAlive", "1 while the first shooter player is alive", ShooterPlayerComponent, &ShooterPlayerComponent::isAlive );
    SW_AUTOMATION_GETTER_WATCH( shooterShotCount, "Shooter.ShotCount", "Shots the first shooter player fired", ShooterPlayerComponent, &ShooterPlayerComponent::getShotCount );
    SW_AUTOMATION_GETTER_WATCH( shooterFireReady, "Shooter.FireReady", "1 while the first shooter player's weapon can fire its next shot (cooldown and reload over, ammo in the magazine)", ShooterPlayerComponent,
                                &ShooterPlayerComponent::isFireReady );
    SW_AUTOMATION_GETTER_WATCH( shooterOnGround, "Shooter.OnGround", "1 while the first shooter player stands on the ground", ShooterPlayerComponent, &ShooterPlayerComponent::isOnGround );
    SW_AUTOMATION_COMPONENT_WATCH( shooterShotDamage, "Shooter.ShotDamage", "Damage of one pellet of the projectile loaded in the first shooter player's weapon", ShooterPlayerComponent, &ShooterDevCommandsInternal::readShotDamage );
    SW_AUTOMATION_GETTER_WATCH( shooterTracerWidth, "Shooter.TracerWidth", "Width of the last tracer the first shooter player drew (the projectile's trail)", ShooterPlayerComponent, &ShooterPlayerComponent::getLastTracerWidth );
    SW_AUTOMATION_GETTER_WATCH( shooterTracerStartAhead, "Shooter.TracerStartAhead",
                                "How far ahead of the eye along the aim the last tracer started (m; in first person the muzzle of the view gun, in front of the gun)", ShooterPlayerComponent,
                                &ShooterPlayerComponent::getLastTracerStartAhead );
    // 등록 줄은 기능 파일로 옮길 때(165e16647c) 빠졌다 — 함수만 남아 명령이 없었다.
    SW_DEV_COMMAND( CheatAmmo, "cheat.ammo", "cheat.ammo", "Add two magazines of reserve ammo to every weapon of every shooter player", &ShooterDevCommandsInternal::runAmmo );
    SW_DEV_COMMAND( CheatShooterHeal, "cheat.shooter.heal", "cheat.shooter.heal", "Fill the health of every living shooter player", &ShooterDevCommandsInternal::runHeal );
    SW_DEV_COMMAND( CheatShooterGod, "cheat.shooter.god", "cheat.shooter.god", "Toggle damage immunity of every shooter player", &ShooterDevCommandsInternal::runGod );
    SW_DEV_COMMAND( CheatShooterDown, "cheat.shooter.down", "cheat.shooter.down", "Down every living shooter player at once (clears damage immunity)", &ShooterDevCommandsInternal::runDown );
    SW_DEV_COMMAND( CheatShooterFace, "cheat.shooter.face", "cheat.shooter.face", "Turn the connected player toward the nearest other player", &ShooterDevCommandsInternal::runFace );
    SW_DEV_COMMAND( ShooterProjectile, "cheat.shooter.projectile", "cheat.shooter.projectile [projectile id]",
                    "Load a projectile into the current weapon of every shooter player (no id: the weapon's own projectile)", &ShooterDevCommandsInternal::runProjectile );
    SW_AUTOMATION_GETTER_WATCH( shooterEnemyHitCount, "Shooter.EnemyHitCount", "Shots of the first shooter player that hit an enemy", ShooterPlayerComponent, &ShooterPlayerComponent::getHitCount );
    SW_AUTOMATION_GETTER_WATCH( shooterServerHealth, "Shooter.ServerHealth", "Health the match server last reported to the first shooter player (-1 before the first report)", ShooterPlayerComponent, &ShooterPlayerComponent::getServerHealth );
    SW_AUTOMATION_GETTER_WATCH( shooterServerKillCount, "Shooter.ServerKillCount", "Kills the match server last reported to the first shooter player (0 without a server)", ShooterPlayerComponent, &ShooterPlayerComponent::getServerKillCount );
    SW_AUTOMATION_COMPONENT_WATCH( shooterPlayerControllerKind, "Shooter.PlayerControllerKind", "Who possesses the first shooter player: 0 the player, 1 an AI (auto play), -1 none", ShooterPlayerComponent, &ShooterDevCommandsInternal::readControllerKind );
    SW_AUTOMATION_WATCH( shooterEffectCreatedCount, "Shooter.EffectCreatedCount", "Instances the effect pools of the shooter game mode created so far (reuse does not count)",
                         &ShooterEffectWatchInternal::readEffectCreatedCount );
} // namespace sw

namespace sw
{
    namespace
    {
        // 바닥 기준의 자리 — 두 클라이언트 시나리오가 `<Record>` 로 보고에 남기고 `Scripts net-pair match` 가 견준다. 바닥 위가 아니면 값이 없다.
        struct ShooterNetPawnWatches
        {
            [[nodiscard]] static bool readFloorX( const ShooterNetPawnComponent& component, float64& outValue )
            {
                outValue = static_cast<float64>( component.getFloorLocalPosition()._x );
                return component.getFloorNumber() != 0;
            }
            [[nodiscard]] static bool readFloorZ( const ShooterNetPawnComponent& component, float64& outValue )
            {
                outValue = static_cast<float64>( component.getFloorLocalPosition()._z );
                return component.getFloorNumber() != 0;
            }
            [[nodiscard]] static bool readRemoteFloorX( const ShooterNetPawnComponent& component, float64& outValue )
            {
                outValue = static_cast<float64>( component.getRemoteFloorLocalPosition()._x );
                return component.getRemoteFloorNumber() != 0;
            }
            [[nodiscard]] static bool readRemoteFloorZ( const ShooterNetPawnComponent& component, float64& outValue )
            {
                outValue = static_cast<float64>( component.getRemoteFloorLocalPosition()._z );
                return component.getRemoteFloorNumber() != 0;
            }
        };
    } // namespace

    SW_AUTOMATION_COMPONENT_WATCH( shooterFloorX, "Shooter.FloorX", "Own body's X on the moving floor it stands on (floor space; no value off the floor)", ShooterNetPawnComponent, &ShooterNetPawnWatches::readFloorX );
    SW_AUTOMATION_COMPONENT_WATCH( shooterFloorZ, "Shooter.FloorZ", "Own body's Z on the moving floor it stands on (floor space; no value off the floor)", ShooterNetPawnComponent, &ShooterNetPawnWatches::readFloorZ );
    SW_AUTOMATION_COMPONENT_WATCH( shooterRemoteFloorX, "Shooter.RemoteFloorX", "Another player's shown X on the moving floor (floor space; no value off the floor)", ShooterNetPawnComponent, &ShooterNetPawnWatches::readRemoteFloorX );
    SW_AUTOMATION_COMPONENT_WATCH( shooterRemoteFloorZ, "Shooter.RemoteFloorZ", "Another player's shown Z on the moving floor (floor space; no value off the floor)", ShooterNetPawnComponent, &ShooterNetPawnWatches::readRemoteFloorZ );
    SW_AUTOMATION_GETTER_WATCH( shooterFloorNumber, "Shooter.FloorNumber", "Number of the moving floor the own body stands on (0 none)", ShooterNetPawnComponent, &ShooterNetPawnComponent::getFloorNumber );
    SW_AUTOMATION_GETTER_WATCH( shooterRemoteFloorNumber, "Shooter.RemoteFloorNumber", "Number of the moving floor another player is shown standing on (0 none)", ShooterNetPawnComponent, &ShooterNetPawnComponent::getRemoteFloorNumber );
    SW_AUTOMATION_GETTER_WATCH( shooterRemotePawnCount, "Shooter.RemotePawnCount", "Other players' bodies this client sees", ShooterNetPawnComponent, &ShooterNetPawnComponent::getRemotePawnCount );
} // namespace sw

namespace sw
{
    namespace
    {
        struct ShooterBodyMovementWatchInternal
        {
            /** @brief 감시 값 `Shooter.BodySpeed` 입니다 — 대상 오브젝트의 슈터 몸 속도(수평 걸음 + 수직, m/s). 몸이 없으면 값이 없습니다. */
            [[nodiscard]] static bool readBodySpeed( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const ShooterBodyMovementComponent* pBody = query.findTargetComponent<ShooterBodyMovementComponent>( "shooter body movement", outValue );
                if ( pBody == nullptr )
                    return false;
                const float3  walk = pBody->getMoveVelocity();
                const float32 fall = pBody->getState()._verticalSpeed;
                outValue.setNumber( static_cast<float64>( float3{ walk._x, fall, walk._z }.getLength() ) );
                return true;
            }
        };
    } // namespace

    // 슈터 몸은 물리 바디 없이 운동학으로 걷는다 — 엔진 `Object.Speed`(강체 · 캐릭터 컨트롤러)가 보지 못한다(shooter3d actions · cctvmonitor).
    SW_AUTOMATION_QUERY_WATCH( shooterBodySpeed, "Shooter.BodySpeed", "Speed of the shooter body on target (walk plus vertical, m/s)", &ShooterBodyMovementWatchInternal::readBodySpeed, true, false );
} // namespace sw

namespace sw
{
    namespace
    {
        struct ShooterAvatarWatchInternal
        {
            /** @brief 첫 몸이 보폭 워핑 노드에 마지막으로 넣은 배율입니다. 리그가 아직 묶이지 않았으면 프로브가 없는 것으로 보고합니다. */
            [[nodiscard]] static bool readStrideScale( const ShooterAvatarComponent& avatar, float64& outValue )
            {
                if ( avatar.getStrideAppliedCount() == 0 )
                    return false;
                outValue = static_cast<float64>( avatar.getLastStrideScale() );
                return true;
            }

            /**
             * @brief 첫 몸의 리그에서 지금 켜진(가중치 > 0) 2 본 IK 노드의 `reach_error` 중 가장 큰 값입니다(미터). 손이 총의 Grip · SupportHand 에 닿지 않으면 0 보다 크다.
             * @details 1인칭 · 3인칭 · 두 손 견착이 켜는 손 IK 를 노드 이름 없이 한 번에 본다 — 슬롯이 바뀌면 켜진 노드가 바뀐다. 리그가 묶이지 않았으면 프로브가 없는 것으로 보고한다.
             */
            [[nodiscard]] static bool readHandReachError( const ShooterAvatarComponent& avatar, float64& outValue )
            {
                const GameObject*            pOwner = avatar.getOwner();
                const PoseModifierComponent* pRig   = pOwner != nullptr ? pOwner->getComponent<PoseModifierComponent>() : nullptr;
                if ( pRig == nullptr || pRig->isRigReady() == false )
                    return false;
                const RigInstance& instance = pRig->getRigInstance();
                float32            largest  = 0.0f;
                for ( uint32 nodeIndex = 0; nodeIndex < instance.getNodeCount(); ++nodeIndex )
                {
                    float32 reachError = 0.0f;
                    if ( instance.computeNodeWeight( nodeIndex ) > 0.0f && instance.getNode( nodeIndex ).getOutputValue( hashed_string( "reach_error" ), reachError ) )
                        largest = MathUtil::max( largest, reachError );
                }
                outValue = static_cast<float64>( largest );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_COMPONENT_WATCH( shooterStrideScale, "Shooter.StrideScale", "Stride scale the first shooter body last gave its StrideWarping node (1 = clip as is); absent until the rig is bound", ShooterAvatarComponent, &ShooterAvatarWatchInternal::readStrideScale );
    SW_AUTOMATION_COMPONENT_WATCH( shooterHandReachError, "Shooter.HandReachError",
                                   "Largest reach error of the first shooter body's active two-bone IK hand nodes (m; 0 = every hand reaches its gun grip or support), absent until the rig is bound",
                                   ShooterAvatarComponent, &ShooterAvatarWatchInternal::readHandReachError );
    SW_AUTOMATION_GETTER_WATCH( shooterGunJerkMax, "Shooter.GunJerkMax", "Largest frame-to-frame second difference of the first shooter body's weapon position in body space during the current walk (m; 0 while standing)",
                                ShooterAvatarComponent, &ShooterAvatarComponent::getGunJerkMax );
    SW_AUTOMATION_GETTER_WATCH( shooterStrideAppliedCount, "Shooter.StrideAppliedCount", "Frames the first shooter body gave its StrideWarping node a value (0 while the rig is not bound to the mesh)", ShooterAvatarComponent, &ShooterAvatarComponent::getStrideAppliedCount );
} // namespace sw

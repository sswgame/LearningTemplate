#include "pch.h"

#include "Games/Shooter3D/ShooterPlayerComponent.h"

#include "Core/Math/MathUtil.h"
#include "Core/Math/OrientationUtil.h"
#include "Core/Math/Ray.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Character/Hit/CharacterHit.h"
#include "Engine/Character/Hit/MeleeHitWindow.h"
#include "Engine/Character/PoseModifier/PoseModifierComponent.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Object/Prefab/PrefabCache.h"
#include "Engine/Physics/PhysicsSettings.h"
#include "Engine/Physics/Scene/ScenePhysics.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Telemetry/TelemetryEvent.h"
#include "Engine/Telemetry/TelemetryService.h"
#include "Engine/Visual/Component/MeshComponent.h"

#include "GameFramework/Runtime/Character/Camera/CameraDirectorComponent.h"
#include "GameFramework/Runtime/Character/Combat/Killcam/KillcamComponent.h"
#include "GameFramework/Runtime/Character/Combat/Projectile/ProjectileDef.h"
#include "GameFramework/Runtime/Character/Control/Controller/AIControllerComponent.h"
#include "GameFramework/Runtime/Character/Control/Controller/PlayerControllerComponent.h"
#include "GameFramework/Runtime/Character/Control/FirstPersonCameraComponent.h"
#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"
#include "GameFramework/Runtime/Foundation/Condition/GameConditions.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Foundation/Framework/Presentation/GameSound.h"
#include "GameFramework/Runtime/Foundation/Framework/Presentation/IHUDViewModelBinder.h"
#include "GameFramework/Runtime/Gameplay/Appearance/AppearanceDatabase.h"
#include "GameFramework/Runtime/Gameplay/Appearance/CharacterAppearanceComponent.h"
#include "GameFramework/Runtime/Gameplay/Inventory/ItemCatalog.h"

#include "Games/Shooter3D/ShooterAvatarComponent.h"
#include "Games/Shooter3D/ShooterBodyMovementComponent.h"
#include "Games/Shooter3D/ShooterEnemyComponent.h"
#include "Games/Shooter3D/ShooterGameModeComponent.h"
#include "Games/Shooter3D/ShooterHUDViewModel.h"
#include "Games/Shooter3D/ShooterNetPawnComponent.h"

namespace sw
{
    namespace
    {
        struct ShooterPlayerComponentInternal
        {
            static constexpr const utf8* kArrWeaponAction[ShooterPlayerComponent::kWeaponCount] = { "Weapon1", "Weapon2", "Weapon3" };
            // 폰 스키마의 버튼과 아날로그 이름이다(플레이어는 같은 이름의 입력 맵 액션을 쓰고, 자동 플레이 AI 는 그 이름을 누른다).
            static constexpr const utf8* kFireButton         = "Fire";
            static constexpr const utf8* kMeleeButton        = "Melee";
            static constexpr uint32      kMeleeBladeSamples  = 4;
            static constexpr const utf8* kReloadButton       = "Reload";
            static constexpr const utf8* kSwitchWeaponButton = "SwitchWeapon";
            static constexpr const utf8* kViewWeaponName     = "ViewWeapon";
            static constexpr const utf8* kMuzzleSocket       = "Muzzle";
            static constexpr const utf8* kBodyMuzzleSocket   = "MainHand.Muzzle";
            static constexpr const utf8* kEyesSocket         = "Eyes";

            // 사운드 이벤트 이름(shooter3d.audioevents.xml). 플레이어 자신의 소리라 2D 로 낸다.
            static constexpr const utf8* kSoundLand     = "Land";
            static constexpr const utf8* kSoundHitEnemy = "HitEnemy";
            static constexpr const utf8* kSoundHitCover = "HitCover";

            // 맞음 표(projectiles.xml 의 `<Impact surface>`)의 표면 이름이다. 기동할 때 게임이 장전할 수 있는 모든 탄에 두 표면의 줄이 있는지 본다.
            static constexpr const utf8* kSurfaceBody  = "Body";
            static constexpr const utf8* kSurfaceCover = "Cover";

            /** @brief 같은 오브젝트에서 컴포넌트 이름이 @p pName 인 T 를 반환합니다. 없으면 nullptr 입니다. */
            template <typename T>
            static T* findNamed( const GameObject& owner, const utf8* pName )
            {
                const hashed_string name( pName );
                T*                  pFound = nullptr;
                owner.forEachComponentOfType<T>( [&name, &pFound]( T* pComponent )
                {
                    if ( pFound == nullptr && pComponent->getComponentName() == name )
                        pFound = pComponent;
                } );
                return pFound;
            }

            /** @brief 아이템의 외형에서 소켓 에셋을 가진 첫 부품의 소켓 에셋을 반환합니다(무기의 총구). 없으면 빈 이름입니다. */
            static hashed_string findItemSocketSet( const AppearanceDatabase& database, const hashed_string& itemID )
            {
                const ItemCatalog*   pItems  = database.getItemCatalog();
                const ItemDef*       pItem   = pItems != nullptr ? pItems->find( itemID ) : nullptr;
                const ItemVisualDef* pVisual = pItem != nullptr ? database.getVisuals().findVisual( pItem->_visualID ) : nullptr;
                if ( pVisual == nullptr )
                    return hashed_string{};
                for ( const AppearancePartDef& part : pVisual->_listPart )
                {
                    if ( part._socketSet.empty() == false )
                        return part._socketSet;
                }
                return hashed_string{};
            }

            /** @brief 물리 설정 표에서 레이어 이름의 질의 비트입니다. 표가 없거나 이름이 없으면 false 입니다. */
            [[nodiscard]] static bool findLayerMask( const GameObjectManager& manager, const hashed_string& layer, uint32& outMask )
            {
                const PhysicsSettings* pSettings  = manager.getSubsystem<ScenePhysics>().findSettings();
                uint8                  layerIndex = 0;
                if ( pSettings == nullptr || pSettings->findLayerIndex( layer, layerIndex ) == false )
                    return false;
                outMask = 1u << layerIndex;
                return true;
            }

            /** @brief 첫 슈터 플레이어의 지금 무기에 장전한 탄의 한 알 피해입니다(무기 · 탄 · 수정자 조합의 결과). */
            [[nodiscard]] static bool readShotDamage( const ShooterPlayerComponent& player, float64& outValue )
            {
                outValue = static_cast<float64>( player.getCurrentWeapon().getShot()._damage );
                return true;
            }

            /** @brief 첫 슈터 플레이어의 폰을 가진 조종자를 반환합니다. 0 은 플레이어, 1 은 AI(자동 플레이), -1 은 없음입니다(그 밖의 조종자도 -1). */
            [[nodiscard]] static bool readControllerKind( const ShooterPlayerComponent& player, float64& outValue )
            {
                GameObject*          pOwner   = player.getOwner();
                GameObjectManager*   pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
                const PawnComponent* pPawn    = pOwner != nullptr ? pOwner->getComponent<PawnComponent>() : nullptr;
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
        };

        struct ShooterDevCommandsInternal
        {
            /** @brief 활성 씬의 슈터 플레이어를 모읍니다. 씬이 없거나 하나도 없으면 그 까닭(문장이 명령 답이다)입니다. */
            [[nodiscard]] static expected<vector<ShooterPlayerComponent*>> collectPlayers()
            {
                SceneManager* const pSceneManager = game::findSubsystem<SceneManager>();
                GameObjectManager*  pManager      = pSceneManager != nullptr ? pSceneManager->findActiveObjectManager() : nullptr;
                if ( pManager == nullptr )
                    return unexpected( Error{ ErrorKind::NotFound, "cheat: no active scene" } );
                vector<ShooterPlayerComponent*> listPlayer;
                pManager->forEachComponentOfType<ShooterPlayerComponent>( [&listPlayer]( ShooterPlayerComponent* pPlayer )
                { listPlayer.push_back( pPlayer ); } );
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
    } // namespace
} // namespace sw

namespace sw
{
    ShooterPlayerComponent::ShooterPlayerComponent()
        : _spawnPosition{ 0.0f, 0.0f, -16.0f }
        , _bodyPrefab{}
        , _listWeaponID{}
        , _eyeHeight{ 1.6f }
        , _maxHealth{ 100.0f }
        , _regenDelay{ 4.0f }
        , _regenPerSecond{ 6.0f }
        , _downTime{ 2.5f }
        , _hitboxLayer{ "Ragdoll" }
        , _shotImpulse{ 30.0f }
        , _meleeDamage{ 25.0f }
        , _meleeImpulse{ 40.0f }
        , _meleeReach{ 2.2f }
        , _meleeRadius{ 0.15f }
        , _meleeArc{ 1.4f }
        , _meleeDuration{ 0.25f }
        , _hitEffectLifetime{ 0.12f }
        , _tracerLifetime{ 0.07f }
        , _muzzleFlashColor{ 4.0f, 2.8f, 0.8f, 1.0f }
        , _arrWeapon{}
        , _healthMachine{}
        , _weaponSockets{}
        , _listPendingShot{}
        , _listMeleePrevious{}
        , _listMeleeCurrent{}
        , _listMeleeHitTarget{}
        , _listPendingEffect{}
        , _listPendingTracer{}
        , _soundQueue{}
        , _body{}
        , _hitMarkerTimer{ 0.0f }
        , _timeSinceShot{ 10.0f }
        , _downTimer{ 0.0f }
        , _bodyEyeHeight{ 0.0f }
        , _lookYaw{ 0.0f }
        , _lastTracerWidth{ 0.0f }
        , _lastTracerStartAhead{ 0.0f }
        , _serverHealth{ 0.0f }
        , _hudViewModel{}
        , _meleeElapsed{ 0.0f }
        , _weaponIndex{ 0 }
        , _shotCount{ 0 }
        , _hitCount{ 0 }
        , _hitReactionCount{ 0 }
        , _bWeaponModelDirty{ SW_FALSE }
        , _bViewWeaponSynced{ SW_FALSE }
        , _bFlushScheduled{ SW_FALSE }
        , _bRoundJustRestarted{ SW_FALSE }
        , _bFirstPerson{ SW_TRUE }
        , _bViewModeDirty{ SW_TRUE }
        , _bBodyRequested{ SW_FALSE }
        , _bServerHealth{ SW_FALSE }
        , _bMeleeActive{ SW_FALSE }
        , _reserved{ 0 }
    {
        setCanEverTick( true );
    }

    ShooterPlayerComponent::~ShooterPlayerComponent() = default;

    void ShooterPlayerComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        if ( equipWeapons() == false )
        {
            setCanEverTick( false ); // 무기 데이터가 틀리면 플레이어가 서지 않는다 — 이유는 equipWeapons 가 남겼다
            return;
        }
        HealthSettings settings;
        settings._maxHealth        = _maxHealth;
        settings._healthRegenDelay = _regenDelay;
        settings._healthRegenRate  = _regenPerSecond;
        if ( _healthMachine.initialize( settings ) == false )
        {
            setCanEverTick( false ); // 체력 설정이 범위 밖이면 플레이어가 서지 않는다 — 이유는 HealthStateMachine 가 남겼다
            return;
        }
        _weaponIndex                            = 0;
        _downTimer                              = 0.0f;
        GameObject*                   pOwner    = getOwner();
        FirstPersonCameraComponent*   pCamera   = pOwner != nullptr ? pOwner->getComponent<FirstPersonCameraComponent>() : nullptr;
        ShooterBodyMovementComponent* pMovement = findMovement();
        if ( pMovement != nullptr )
            pMovement->teleport( _spawnPosition );
        else
            SW_LOG_ERROR_ID( "ShooterPlayer.PlayerObjectNoShooterBodyMovementComponent", "[Shooter] the player object has no ShooterBodyMovementComponent - the player cannot move" );
        if ( pCamera != nullptr )
        {
            pCamera->setAngles( 0.0f, 0.0f );
            pCamera->setEyePosition( getEyePosition() );
        }
        _bWeaponModelDirty = SW_TRUE;
        _bViewModeDirty    = SW_TRUE;
        _bBodyRequested    = SW_TRUE;
        scheduleFlush();
        SW_LOG_INFO_ID( "ShooterPlayer.ArenaReadyWASDMove", "[Shooter] arena is ready - WASD move, mouse look, LMB fire, R reload, 1/2/3 or Q/E weapons, Space jump, Shift sprint, C camera, Esc mouse" );
    }

    void ShooterPlayerComponent::onEndPlay()
    {
        GameObject*          pHUDOwner = getOwner();
        IHUDViewModelBinder* pBinder   = game::findSubsystem<IHUDViewModelBinder>();
        if ( pHUDOwner != nullptr && pBinder != nullptr && _hudViewModel != nullptr )
            (void)pBinder->bindHUDViewModel( *pHUDOwner, nullptr ); // 뷰모델은 이 컴포넌트와 함께 사라진다 — HUD 에서 먼저 뗀다
        despawnViews();
        Component::onEndPlay();
    }

    void ShooterPlayerComponent::despawnViews()
    {
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        GameObject*        pBody    = pManager != nullptr ? pManager->resolveGameObject( _body ) : nullptr;
        if ( pBody != nullptr )
        {
            // 부품(투구, 무기)은 몸의 자식으로 붙어 있지만, 외형이 스폰한 것이라 외형이 정리한다.
            CharacterAppearanceComponent* pAppearance = pBody->getComponent<CharacterAppearanceComponent>();
            if ( pAppearance != nullptr )
                pAppearance->despawnParts();
            pManager->destroyGameObject( pBody );
        }
        _body           = GameObjectHandle{};
        _bodyEyeHeight  = 0.0f;
        _bBodyRequested = SW_TRUE;
    }

    void ShooterPlayerComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        _bRoundJustRestarted                                  = SW_FALSE;
        GameObject*                     pOwner                = getOwner();
        FirstPersonCameraComponent*     pCamera               = pOwner != nullptr ? pOwner->getComponent<FirstPersonCameraComponent>() : nullptr;
        const ShooterGameModeComponent* pGameMode             = findGameMode<ShooterGameModeComponent>();
        const bool                      bMissingCameraContext = pCamera == nullptr || pGameMode == nullptr || context.getDeltaTime() <= 0.0f;
        if ( bMissingCameraContext )
            return;
        const float32 step = MathUtil::min( context.getDeltaTime(), 0.1f );

        // 시점 카메라의 프리셋(1인칭, 3인칭, 궤도, CCTV)이 바뀌면 틱이 끝난 뒤 가시성을 맞춘다.
        const bool bFirstPerson = queryFirstPerson();
        if ( bFirstPerson != ( _bFirstPerson == SW_TRUE ) )
        {
            _bFirstPerson   = bFirstPerson ? SW_TRUE : SW_FALSE;
            _bViewModeDirty = SW_TRUE;
        }
        // 눈높이는 몸의 Eyes 소켓(바인드 포즈)이 정한다. 몸이 조립되기 전에는 속성 값을 쓴다.
        if ( _bodyEyeHeight <= 0.0f )
        {
            const GameObject*                   pBody       = findBodyObject();
            const CharacterAppearanceComponent* pAppearance = pBody != nullptr ? pBody->getComponent<CharacterAppearanceComponent>() : nullptr;
            float4x4                            eyes;
            if ( pAppearance != nullptr && pAppearance->findBindSocketTransform( hashed_string( ShooterPlayerComponentInternal::kEyesSocket ), eyes ) )
                _bodyEyeHeight = MathUtil::max( 0.5f, eyes.getTranslation()._y );
        }

        for ( WeaponState& weapon : _arrWeapon )
        {
            weapon.update( step );
        }
        _healthMachine.update( step );
        _timeSinceShot += step;
        _hitMarkerTimer -= step;
        if ( _healthMachine.isAlive() == false )
        {
            // 쓰러짐 클립이 도는 동안 멈춰 있다가 판을 처음부터 다시 시작한다.
            _downTimer += step;
            pCamera->setEyePosition( getEyePosition() );
            scheduleFlush(); // HUD 갱신을 예약한다. 쓰러짐 클립이 끝나면 판도 다시 시작한다.
            return;
        }

        // 조종자(사람 또는 자동 플레이 AI)가 틱 앞에 폰에 넣은 의도만 읽는다. 조종자가 없으면 의도는 0 이다(서 있는다).
        using Internal = ShooterPlayerComponentInternal;
        // 멈춘 몸(입력 초점이 없는 네트 월드)은 의도를 쓰지 않는다 — 이동처럼 무기도 같다.
        ShooterBodyMovementComponent* pMovement = findMovement();
        const PawnComponent*          pPawn     = pMovement == nullptr || pMovement->isSuspended() == false ? pOwner->getComponent<PawnComponent>() : nullptr;
        if ( pPawn != nullptr )
            applyWeaponIntent( *pPawn );
        if ( pMovement != nullptr && pMovement->stepMovement( pGameMode->getBoxes(), step ) )
            _soundQueue.queueEvent( Internal::kSoundLand );
        const bool bTrigger     = pPawn != nullptr && pPawn->isButtonDown( pPawn->findButton( hashed_string( Internal::kFireButton ) ) );
        const bool bJustPressed = pPawn != nullptr && pPawn->wasButtonTriggered( pPawn->findButton( hashed_string( Internal::kFireButton ) ) );
        if ( bTrigger || bJustPressed )
        {
            // 서브틱 입력: 막 누른 틱은 클릭한 순간의 시선으로 쏜다(프레임 끝의 시선이 아니라). 누르고 있는 틱은 지금 시선이다.
            const ControlIntent& intent       = pPawn->getIntent();
            float3               aimDirection = pCamera->getLook().getForward();
            if ( bJustPressed && intent._bTriggerLook )
                aimDirection = OrientationUtil::computeLookDirection( intent._triggerYaw, intent._triggerPitch );
            fireWeapon( *pGameMode, *pCamera, aimDirection, bJustPressed );
        }
        if ( _bMeleeActive == SW_TRUE )
        {
            _meleeElapsed += step;
        }
        else if ( pPawn != nullptr && pPawn->wasButtonTriggered( pPawn->findButton( hashed_string( Internal::kMeleeButton ) ) ) )
        {
            _bMeleeActive = SW_TRUE;
            _meleeElapsed = 0.0f;
            _listMeleePrevious.clear();
            _listMeleeHitTarget.clear();
        }

        _lookYaw = pCamera->getLook().getYaw();
        pCamera->setEyePosition( getEyePosition() );
        // HUD 는 틱이 끝난 뒤 넣는다(위젯은 게임 스레드만 고친다). 탄약, 체력, 맞음 표시가 거의 매 프레임 바뀐다.
        scheduleFlush();
    }

    void ShooterPlayerComponent::takeDamage( float32 amount )
    {
        if ( _bRoundJustRestarted == SW_TRUE || _healthMachine.isAlive() == false )
            return;
        const HealthDamageResult  result    = _healthMachine.applyDamage( amount );
        GameObject*               pOwner    = getOwner();
        GameObjectManager*        pManager  = pOwner != nullptr ? pOwner->getManager() : nullptr;
        const GameObject*         pObject   = pManager != nullptr ? pManager->resolveGameObject( getGameMode() ) : nullptr;
        ShooterGameModeComponent* pGameMode = pObject != nullptr ? pObject->getComponent<ShooterGameModeComponent>() : nullptr;
        if ( result._bIgnored == SW_TRUE )
            return;
        ++_hitReactionCount;
        if ( pGameMode != nullptr )
            pGameMode->reportPlayerDamage( amount );
        if ( result._bDied == SW_FALSE )
            return;
        _downTimer = 0.0f;
        if ( pGameMode != nullptr )
        {
            SW_LOG_INFO_ID( "ShooterPlayer.YouOverrunWaveAfter", "[Shooter] you were overrun on wave %# after %# kills - starting over", pGameMode->getWave(), pGameMode->getKillCount() );
            TelemetryService* pTelemetry = game::findSubsystem<TelemetryService>();
            if ( pTelemetry != nullptr )
            {
                TelemetryEvent roundEnded( "progression.roundEnded" );
                roundEnded.setInt( "wave", pGameMode->getWave() ).setInt( "kills", pGameMode->getKillCount() );
                roundEnded.setFloat( "seconds", static_cast<float64>( pGameMode->getPacingDirector().getTime() ) );
                roundEnded.setFloat( "accuracy", _shotCount > 0u ? static_cast<float64>( _hitCount ) / static_cast<float64>( _shotCount ) : 0.0 );
                (void)pTelemetry->record( roundEnded );
            }
        }
    }

    void ShooterPlayerComponent::applyServerReport( uint32 confirmedHitCount, float32 health )
    {
        _serverHealth  = health;
        _bServerHealth = SW_TRUE;
        if ( confirmedHitCount <= _hitCount )
            return;
        _hitCount       = confirmedHitCount;
        _hitMarkerTimer = 0.15f;
        _soundQueue.queueEvent( ShooterPlayerComponentInternal::kSoundHitEnemy );
    }

    void ShooterPlayerComponent::recordMeleeHit()
    {
        ++_hitCount;
        _hitMarkerTimer = 0.15f;
        _soundQueue.queueEvent( ShooterPlayerComponentInternal::kSoundHitEnemy );
    }

    void ShooterPlayerComponent::addWaveAmmo()
    {
        for ( WeaponState& weapon : _arrWeapon )
        {
            weapon.addReserveAmmo( weapon.getDef()._magazineSize * 2 );
        }
    }

    void ShooterPlayerComponent::restoreHealth( float32 amount )
    {
        (void)_healthMachine.heal( MathUtil::max( 0.0f, amount ) );
    }

    float32 ShooterPlayerComponent::computeAmmoShortage() const
    {
        const WeaponState& weapon   = _arrWeapon[_weaponIndex];
        const float32      wanted   = static_cast<float32>( MathUtil::max( 1, weapon.getDef()._magazineSize * 4 ) );
        const float32      carrying = static_cast<float32>( weapon.getMagazineAmmo() + weapon.getReserveAmmo() );
        return MathUtil::clamp( 1.0f - carrying / wanted, 0.0f, 1.0f );
    }

    float3 ShooterPlayerComponent::getEyePosition() const
    {
        return getDisplayFeetPosition() + float3{ 0.0f, _bodyEyeHeight > 0.0f ? _bodyEyeHeight : _eyeHeight, 0.0f };
    }

    float3 ShooterPlayerComponent::getFeetPosition() const
    {
        const ShooterBodyMovementComponent* pMovement = findMovement();
        return pMovement != nullptr ? pMovement->getFeetPosition() : _spawnPosition;
    }

    float3 ShooterPlayerComponent::getDisplayFeetPosition() const
    {
        const ShooterBodyMovementComponent* pMovement = findMovement();
        return pMovement != nullptr ? pMovement->getDisplayFeetPosition() : _spawnPosition;
    }

    float3 ShooterPlayerComponent::getMoveVelocity() const
    {
        const ShooterBodyMovementComponent* pMovement = findMovement();
        return pMovement != nullptr ? pMovement->getMoveVelocity() : float3{ 0.0f, 0.0f, 0.0f };
    }

    bool ShooterPlayerComponent::isOnGround() const
    {
        const ShooterBodyMovementComponent* pMovement = findMovement();
        return pMovement == nullptr || pMovement->isOnGround();
    }

    ShooterBodyMovementComponent* ShooterPlayerComponent::findMovement() const
    {
        const GameObject* pOwner = getOwner();
        return pOwner != nullptr ? pOwner->getComponent<ShooterBodyMovementComponent>() : nullptr;
    }

    hashed_string ShooterPlayerComponent::getWeaponItem( int32 weaponIndex ) const
    {
        const bool bInRange = 0 <= weaponIndex && weaponIndex < kWeaponCount;
        return bInRange ? _arrWeapon[weaponIndex].getDef()._itemID : hashed_string{};
    }

    // ------------------------------------------------------------------------------
    // 입력, 이동, 사격(DuringPhysics, 자기 오브젝트)
    // ------------------------------------------------------------------------------
    bool ShooterPlayerComponent::equipWeapons()
    {
        const WeaponCatalog*     pCatalog     = game::findSubsystem<const WeaponCatalog>();
        const ProjectileCatalog* pProjectiles = game::findSubsystem<const ProjectileCatalog>();
        if ( pCatalog == nullptr || pProjectiles == nullptr )
        {
            SW_LOG_ERROR_ID( "ShooterPlayer.NoWeaponCatalogGameService", "[Shooter] no WeaponCatalog or ProjectileCatalog game service - the player cannot be armed" );
            return false;
        }
        const size_t slotCount = static_cast<size_t>( kWeaponCount );
        if ( _listWeaponID.size() != slotCount )
        {
            SW_LOG_ERROR_ID( "ShooterPlayer.PlayerNeedsWeaponIds", "[Shooter] the player needs %# weapon ids (has %#) - the player cannot be armed", kWeaponCount,
                             static_cast<uint32>( _listWeaponID.size() ) );
            return false;
        }
        bool bArmed = true;
        for ( int32 weaponIndex = 0; weaponIndex < kWeaponCount; ++weaponIndex )
        {
            const string&    weaponID = _listWeaponID[static_cast<size_t>( weaponIndex )];
            const WeaponDef* pDef     = pCatalog->find( hashed_string( weaponID ) );
            if ( pDef == nullptr )
            {
                SW_LOG_ERROR_ID( "ShooterPlayer.WeaponSlotNamesWhich", "[Shooter] weapon slot %# names '%#', which is not in the weapon catalog", weaponIndex + 1, weaponID.c_str() );
                bArmed = false;
                continue;
            }
            if ( pDef->_itemID.empty() )
            {
                SW_LOG_ERROR_ID( "ShooterPlayer.WeaponNoItemWeapon", "[Shooter] weapon '%#' has no item in the weapon catalog - its name and look come from the item", weaponID.c_str() );
                bArmed = false;
                continue;
            }
            // 기본 탄과 받음은 게임이 기동할 때 봤다(`WeaponCatalog::checkProjectiles`). 여기서 틀리면 그 슬롯이 서지 않는다.
            const ProjectileDef* pProjectile = pProjectiles->find( pDef->_projectileID );
            const bool           bEquipped   = pProjectile != nullptr &&
                                   _arrWeapon[weaponIndex].equip( *pDef, *pProjectile, pDef->_maxReserveAmmo / 2, 0x51ed270bu + static_cast<uint32>( weaponIndex ) );
            bArmed = bArmed && bEquipped;
        }
        return bArmed;
    }

    bool ShooterPlayerComponent::loadProjectile( const hashed_string& projectileID )
    {
        const ProjectileCatalog* pProjectiles = game::findSubsystem<const ProjectileCatalog>();
        WeaponState&             weapon       = _arrWeapon[_weaponIndex];
        const hashed_string&     wanted       = projectileID.empty() ? weapon.getDef()._projectileID : projectileID;
        const ProjectileDef*     pProjectile  = pProjectiles != nullptr ? pProjectiles->find( wanted ) : nullptr;
        if ( pProjectile == nullptr )
        {
            SW_LOG_ERROR_ID( "ShooterPlayer.ProjectileUnknown", "[Shooter] projectile '%#' is not in the projectile catalog", wanted.c_str() );
            return false;
        }
        return weapon.loadProjectile( *pProjectile, {} ); // 부착물은 아직 없다 — 장전만 바꾼다
    }

    void ShooterPlayerComponent::applyWeaponIntent( const PawnComponent& pawn )
    {
        using Internal   = ShooterPlayerComponentInternal;
        int32 nextWeapon = -1;
        for ( int32 weaponIndex = 0; weaponIndex < kWeaponCount; ++weaponIndex )
        {
            if ( pawn.wasButtonTriggered( pawn.findButton( hashed_string( Internal::kArrWeaponAction[weaponIndex] ) ) ) )
                nextWeapon = weaponIndex;
        }
        // SwitchWeapon 은 축(Q는 -1, E는 +1)이라 버튼으로는 "발동"만 온다. 방향은 같은 이름의 아날로그 부호가 정한다.
        const hashed_string   switchName( Internal::kSwitchWeaponButton );
        const optional<int32> analogIndex            = pawn.findAnalog( switchName );
        const bool            bWeaponSwitchRequested = pawn.wasButtonTriggered( pawn.findButton( switchName ) ) && analogIndex.has_value() && 0 <= *analogIndex &&
                                          *analogIndex < ControlIntent::kAnalogCount;
        if ( bWeaponSwitchRequested )
        {
            const float32 direction = pawn.getIntent()._arrAnalog[*analogIndex];
            if ( direction > 0.0f )
                nextWeapon = ( _weaponIndex + 1 ) % kWeaponCount;
            else if ( direction < 0.0f )
                nextWeapon = ( _weaponIndex + kWeaponCount - 1 ) % kWeaponCount;
        }
        if ( nextWeapon >= 0 )
            switchWeapon( nextWeapon );
        if ( pawn.wasButtonTriggered( pawn.findButton( hashed_string( Internal::kReloadButton ) ) ) && _arrWeapon[_weaponIndex].startReload() )
            SW_LOG_INFO_ID( "ShooterPlayer.Reloading", "[Shooter] reloading %#", _arrWeapon[_weaponIndex].getDef()._name.c_str() );
    }

    void ShooterPlayerComponent::fireWeapon( const ShooterGameModeComponent& gameMode, FirstPersonCameraComponent& camera, const float3& aimDirection, bool bJustPressed )
    {
        WeaponState& weapon = _arrWeapon[_weaponIndex];
        Ray          aim;
        aim._origin    = getEyePosition();
        aim._direction = aimDirection;
        WeaponShot             shot;
        const WeaponFireResult result = weapon.pullTrigger( aim, bJustPressed, shot );
        if ( result == WeaponFireResult::EmptyMagazine )
            SW_LOG_INFO_ID( "ShooterPlayer.EmptyReloadingReserve", "[Shooter] %# empty - reloading (%# in reserve)", weapon.getDef()._name.c_str(), weapon.getReserveAmmo() );
        else if ( result == WeaponFireResult::OutOfAmmo && bJustPressed )
            SW_LOG_INFO_ID( "ShooterPlayer.OutAmmoSwitchWeapons", "[Shooter] %# is out of ammo - switch weapons", weapon.getDef()._name.c_str() );
        if ( result != WeaponFireResult::Fired )
            return;

        ++_shotCount;
        _timeSinceShot = 0.0f;
        float3     muzzle;
        const bool bHasMuzzle = findMuzzlePosition( muzzle );
        // 총구 섬광은 총구에 잠깐 빛나는 구다. 총구가 없으면(오류를 남겼다) 섬광 · 탄도선 없이 판정만 한다.
        if ( bHasMuzzle )
        {
            EffectRequest flash;
            flash._position = muzzle;
            flash._size     = _bFirstPerson == SW_TRUE ? 0.05f : 0.12f;
            flash._color    = _muzzleFlashColor;
            flash._lifetime = 0.05f;
            _listPendingEffect.push_back( flash );
        }
        // 판정은 눈에서 한다. 상자 · 바닥까지는 여기서 재고, 적(물리 히트박스)은 틱이 끝난 뒤 그 거리 안에서 묻는다(resolveShots).
        const ResolvedShot& loaded = weapon.getShot();
        for ( const Ray& ray : shot._listRay )
        {
            PendingShot pending;
            pending._ray           = ray;
            pending._muzzle        = muzzle;
            pending._projectileID  = loaded._projectileID;
            pending._tracerColor   = loaded._tracerColor;
            pending._tracerWidth   = loaded._tracerWidth;
            pending._coverDistance = traceCover( gameMode, ray );
            pending._range         = loaded._range;
            pending._damage        = loaded._damage;
            pending._pierceCount   = loaded._pierceCount;
            pending._volley        = _shotCount;
            pending._bHasMuzzle    = bHasMuzzle;
            _listPendingShot.push_back( pending );
        }
        // 반동은 위로 튀고 옆으로 살짝 흔든다.
        const float32 kick = weapon.getDef()._recoilPitch * MathUtil::kDegreeToRadian;
        camera.addRecoil( kick, kick * 0.25f * ( ( _shotCount % 2u ) == 0u ? 1.0f : -1.0f ) );
    }

    float32 ShooterPlayerComponent::traceCover( const ShooterGameModeComponent& gameMode, const Ray& ray ) const
    {
        float32 nearest = _arrWeapon[_weaponIndex].getShot()._range;
        for ( const ShooterArenaBox& box : gameMode.getBoxes() )
        {
            float32 distance = 0.0f;
            if ( Ray::intersectAABB( ray, box._min, box._max, nearest, distance ) && distance < nearest )
                nearest = distance;
        }
        // 바닥
        if ( ray._direction._y < -1.0e-4f )
            nearest = MathUtil::min( nearest, -ray._origin._y / ray._direction._y );
        return nearest;
    }

    void ShooterPlayerComponent::resolveShots( GameObjectManager& manager )
    {
        using Internal = ShooterPlayerComponentInternal;
        if ( _listPendingShot.empty() )
            return;
        uint32     hitboxMask = 0;
        const bool bHasMask   = Internal::findLayerMask( manager, _hitboxLayer, hitboxMask );
        if ( bHasMask == false )
            SW_LOG_ERROR_ID( "ShooterPlayer.HitboxLayerNotPhysics", "Hitbox layer '%#' is not in the physics settings - shots cannot hit enemies", _hitboxLayer.c_str() );
        GameObject*              pOwner       = getOwner();
        const uint64             ownerID      = pOwner != nullptr ? pOwner->getObjectID() : 0;
        const ProjectileCatalog* pProjectiles = game::findSubsystem<const ProjectileCatalog>();
        // 네트: 서버는 쏜 클라이언트가 보던 시각으로 되감아 정하고(랙 보정), 클라이언트는 탄도선 · 탄착만 예측한다 — 맞음을 전달하지 않는다.
        ShooterNetPawnComponent* pNetPawn   = pOwner != nullptr ? pOwner->getComponent<ShooterNetPawnComponent>() : nullptr;
        const bool               bRewound   = pNetPawn != nullptr && pNetPawn->isServer();
        const bool               bPredicted = pNetPawn != nullptr && pNetPawn->isClient();
        KillcamComponent*        pKillcam   = KillcamComponent::findInScene( manager ); // 씬에 킬캠이 없으면 기록하지 않는다
        size_t                   shotHead   = 0;
        while ( shotHead < _listPendingShot.size() )
        {
            // 한 방아쇠(산탄은 광선 여럿)에 소리 · 맞음 수는 하나만 낸다. 적을 맞혔으면 쇳소리, 상자나 벽을 맞혔으면 나무 소리다.
            const uint32 volley    = _listPendingShot[shotHead]._volley;
            bool         bAnyHit   = false;
            bool         bHitCover = false;
            for ( ; shotHead < _listPendingShot.size() && _listPendingShot[shotHead]._volley == volley; ++shotHead )
            {
                const PendingShot&   shot        = _listPendingShot[shotHead];
                const ProjectileDef* pProjectile = pProjectiles != nullptr ? pProjectiles->find( shot._projectileID ) : nullptr;
                float32              distance    = shot._coverDistance;
                bool                 bHitBody    = false;
                // 관통: 맞은 적 바로 뒤에서 남은 선분을 다시 묻는다(언리얼 SweepMulti 처럼 한 발의 명중을 차례로 모은다). 관통 수가 다하면 그 몸에서 멈추고,
                // 더 맞을 적이 없으면 상자 · 바닥 · 사거리까지 간다. 명중은 킬캠 기록(판정 결과)으로도 남는다 — 연출은 다시 판정하지 않는다.
                KillcamShot record;
                record.begin( shot._ray._origin, shot._ray._direction, static_cast<float64>( _shotCount ) );
                Ray     segment     = shot._ray;
                float32 travelled   = 0.0f;
                uint64  ignoreID    = ownerID;
                int32   piercesLeft = shot._pierceCount;
                bool    bPierced    = false;
                for ( ;; )
                {
                    CharacterRayHit rayHit;
                    const float32   maxDistance = shot._coverDistance - travelled;
                    const bool      bRayHit     = bHasMask && maxDistance > 0.0f &&
                                         ( bRewound ? pNetPawn->raycastRewound( manager, segment, maxDistance, hitboxMask, ignoreID, rayHit )
                                                    : CharacterRayHit::raycast3D( manager, segment._origin, segment._direction, maxDistance, hitboxMask, ignoreID, rayHit ) );
                    ShooterEnemyComponent* pEnemy = bRayHit && rayHit._pObject != nullptr ? rayHit._pObject->getComponent<ShooterEnemyComponent>() : nullptr;
                    if ( pEnemy == nullptr )
                    {
                        if ( bPierced )
                        {
                            // 꿰뚫은 뒤 더 맞을 적이 없다 — 탄은 막는 것까지 간다.
                            distance = shot._coverDistance;
                            bHitBody = false;
                        }
                        break;
                    }
                    const float32 hitDistance = travelled + rayHit._distance;
                    distance                  = hitDistance;
                    bHitBody                  = true;
                    KillcamHit killcamHit;
                    killcamHit._position = rayHit._point;
                    killcamHit._target   = rayHit._pObject->getHandle();
                    killcamHit._distance = hitDistance;
                    if ( bPredicted == false )
                    {
                        // 쓰러진 적(래그돌)도 맞고 밀린다 — 맞음 수 · 맞음 표시는 산 적만 센다.
                        const bool    bAlive = pEnemy->isAlive();
                        const HitInfo hit    = CharacterRayHit::makeHitInfo( rayHit, pOwner, shot._ray._direction, shot._damage, _shotImpulse );
                        CharacterRayHit::deliverHit( *rayHit._pObject, hit );
                        bAnyHit             = bAnyHit || bAlive;
                        killcamHit._zone    = hit._zone;
                        killcamHit._bKilled = bAlive && pEnemy->isDead() ? SW_TRUE : SW_FALSE;
                        // 탄의 맞음 동작은 맞은 적의 태그 문맥으로 부른다(상태 이상 · 표시 같은 탄 고유 반응).
                        const GameObject* pEnemyObject = pEnemy->getOwner();
                        if ( pProjectile != nullptr && pEnemyObject != nullptr && pProjectile->_listHitAction.empty() == false )
                        {
                            ActionContext context;
                            context._world.set<SubjectTagsSlot>( &pEnemyObject->getTags() );
                            context._pOwner = this;
                            ProjectileDef::runHitActions( pProjectile->_listHitAction, context );
                        }
                    }
                    (void)record.addHit( killcamHit ); // 상한(`KillcamShot::kMaxHits`)을 넘는 명중은 연출에 싣지 않는다
                    if ( piercesLeft <= 0 )
                        break;
                    --piercesLeft;
                    bPierced                      = true;
                    constexpr float32 kPierceStep = 0.01f; // 맞은 면 바로 뒤에서 다시 묻는다(같은 몸은 ignoreID 로 건너뛴다)
                    segment._origin               = rayHit._point + shot._ray._direction * kPierceStep;
                    travelled                     = hitDistance + kPierceStep;
                    ignoreID                      = rayHit._pObject->getObjectID();
                }
                record.finish( shot._ray._origin + shot._ray._direction * distance );
                if ( pKillcam != nullptr && record.getHits().empty() == false )
                    pKillcam->recordShot( record );
                const float3 end = shot._ray._origin + shot._ray._direction * distance;
                // 탄도선은 총구에서 맞은 위치까지 이은 선이다. 색 · 두께는 탄의 궤적이다. 1인칭이면 시점 총의 총구라 총 앞에서 시작해 화면에 보인다.
                if ( shot._bHasMuzzle )
                {
                    _listPendingTracer.push_back( TracerRequest{ shot._tracerColor, shot._muzzle, end, shot._tracerWidth } );
                    _lastTracerWidth      = shot._tracerWidth;
                    _lastTracerStartAhead = ( shot._muzzle - shot._ray._origin ).dot( shot._ray._direction );
                }
                // 탄착 효과는 탄과 표면의 조합이다(맞음 표). 표면 이름은 이 게임이 정한다: 적의 몸은 Body, 상자 · 바닥은 Cover.
                const ProjectileImpactDef* pImpact =
                    pProjectile != nullptr ? pProjectiles->findImpact( hashed_string( bHitBody ? Internal::kSurfaceBody : Internal::kSurfaceCover ), pProjectile->_tags ) : nullptr;
                if ( distance < shot._range && pImpact != nullptr )
                {
                    EffectRequest effect;
                    effect._position = end;
                    effect._size     = pImpact->_size;
                    effect._color    = pImpact->_color;
                    effect._lifetime = _hitEffectLifetime;
                    _listPendingEffect.push_back( effect );
                }
                if ( distance < shot._range )
                    bHitCover = bHitCover || ( bHitBody == false && end._y > 0.05f );
            }
            if ( bAnyHit )
            {
                ++_hitCount;
                _hitMarkerTimer = 0.15f;
                _soundQueue.queueEvent( Internal::kSoundHitEnemy );
            }
            else if ( bHitCover )
            {
                _soundQueue.queueEvent( Internal::kSoundHitCover );
            }
        }
        _listPendingShot.clear();
    }

    void ShooterPlayerComponent::resolveMelee( GameObjectManager& manager )
    {
        using Internal                            = ShooterPlayerComponentInternal;
        GameObject*                       pOwner  = getOwner();
        const FirstPersonCameraComponent* pCamera = pOwner != nullptr ? pOwner->getComponent<FirstPersonCameraComponent>() : nullptr;
        if ( _bMeleeActive == SW_FALSE || pCamera == nullptr )
            return;
        uint32 hitboxMask = 0;
        if ( Internal::findLayerMask( manager, _hitboxLayer, hitboxMask ) == false )
        {
            SW_LOG_ERROR_ID( "ShooterPlayer.MeleeHitboxLayerNotPhysics", "Hitbox layer '%#' is not in the physics settings - melee cannot hit enemies", _hitboxLayer.c_str() );
            _bMeleeActive = SW_FALSE;
            return;
        }
        // 칼날은 눈에서 시선 앞으로 뻗은 선이고, 창 동안 요로 호를 그린다. 시선은 서버 · 클라이언트가 같은 의도에서 온다.
        const float32 fraction = MathUtil::min( _meleeElapsed / _meleeDuration, 1.0f );
        const float32 yaw      = pCamera->getLook().getYaw() + ( fraction - 0.5f ) * _meleeArc;
        const float3  forward  = OrientationUtil::computeLookDirection( yaw, pCamera->getLook().getPitch() );
        const float3  eye      = getEyePosition();
        MeleeHit::makeBlade( eye + forward * ( _meleeReach * 0.25f ), eye + forward * _meleeReach, Internal::kMeleeBladeSamples, _listMeleeCurrent );

        MeleeHitParams params;
        params._kind                        = hashed_string( Internal::kMeleeButton );
        params._radius                      = _meleeRadius;
        params._damage                      = _meleeDamage;
        params._impulse                     = _meleeImpulse;
        params._layerMask                   = hitboxMask;
        ShooterNetPawnComponent* pNetPawn   = pOwner->getComponent<ShooterNetPawnComponent>();
        const bool               bPredicted = pNetPawn != nullptr && pNetPawn->isClient();
        vector<MeleeHit>         listHit;
        const uint32             hitCount = MeleeHit::sweep( *pOwner, _listMeleePrevious, _listMeleeCurrent, params, bPredicted ? MeleeHitAuthority::Predicted : MeleeHitAuthority::Authority, _listMeleeHitTarget, listHit );
        for ( const MeleeHit& hit : listHit )
        {
            // 예측이다 — 맞은 것을 서버에 주장하고 표시는 서버가 인정한 뒤에 낸다(`applyServerReport`).
            const GameObject*            pTarget = manager.resolveGameObject( hit._target );
            const ShooterEnemyComponent* pEnemy  = pTarget != nullptr ? pTarget->getComponent<ShooterEnemyComponent>() : nullptr;
            if ( bPredicted && pEnemy != nullptr )
                pNetPawn->queueMeleeClaim( pEnemy->getNetEntityID() );
        }
        if ( bPredicted == false && hitCount > 0 )
            recordMeleeHit();
        _listMeleePrevious.swap( _listMeleeCurrent );
        if ( fraction >= 1.0f )
            _bMeleeActive = SW_FALSE;
    }

    bool ShooterPlayerComponent::findMuzzlePosition( float3& outMuzzle )
    {
        using Internal = ShooterPlayerComponentInternal;
        float4x4 muzzle;
        if ( _bFirstPerson == SW_FALSE )
        {
            // 몸이 든 무기는 외형의 소켓 이름 공간(슬롯 이름과 무기 소켓)으로 찾는다.
            const GameObject*                   pBody       = findBodyObject();
            const CharacterAppearanceComponent* pAppearance = pBody != nullptr ? pBody->getComponent<CharacterAppearanceComponent>() : nullptr;
            if ( pAppearance == nullptr || pAppearance->findSocketWorldTransform( hashed_string( Internal::kBodyMuzzleSocket ), muzzle ) == false )
            {
                SW_LOG_ERROR_ID( "ShooterPlayer.BodyMuzzleMissing", "[Shooter] the body has no '%#' socket for weapon '%#' - no tracer or muzzle flash", Internal::kBodyMuzzleSocket,
                                 getWeaponItem( _weaponIndex ).c_str() );
                return false;
            }
            outMuzzle = muzzle.getTranslation();
            return true;
        }
        // 1인칭에서는 손에 든 총(카메라 자식)과 그 무기의 총구 소켓을 쓴다. 무기 외형(itemvisuals.xml)의 소켓 에셋이 Muzzle 을 가져야 한다.
        const GameObject*         pOwner      = getOwner();
        const MeshComponent*      pViewWeapon = pOwner != nullptr ? Internal::findNamed<MeshComponent>( *pOwner, Internal::kViewWeaponName ) : nullptr;
        const AppearanceDatabase* pDatabase   = game::findSubsystem<AppearanceDatabase>();
        const hashed_string       socketPath  = pDatabase != nullptr ? Internal::findItemSocketSet( *pDatabase, getWeaponItem( _weaponIndex ) ) : hashed_string{};
        const SocketSet*          pSockets    = socketPath.empty() ? nullptr : _weaponSockets.findSocketSet( socketPath );
        const SocketDef*          pMuzzle     = pSockets != nullptr ? pSockets->findSocket( hashed_string( Internal::kMuzzleSocket ) ) : nullptr;
        if ( pViewWeapon == nullptr || pMuzzle == nullptr )
        {
            SW_LOG_ERROR_ID( "ShooterPlayer.ViewMuzzleMissing", "[Shooter] first-person weapon '%#' has no '%#' socket (view weapon %#, socket set '%#') - no tracer or muzzle flash",
                             getWeaponItem( _weaponIndex ).c_str(), Internal::kMuzzleSocket, pViewWeapon != nullptr ? "found" : "missing", socketPath.c_str() );
            return false;
        }
        outMuzzle = ( pMuzzle->makeLocalTransform() * pViewWeapon->getWorldMatrix() ).getTranslation();
        return true;
    }

    void ShooterPlayerComponent::switchWeapon( int32 weaponIndex )
    {
        const bool bInvalidWeaponSwitch = weaponIndex == _weaponIndex || weaponIndex < 0 || weaponIndex >= kWeaponCount;
        if ( bInvalidWeaponSwitch )
            return;
        _weaponIndex       = weaponIndex;
        _bWeaponModelDirty = SW_TRUE;
        SW_LOG_INFO_ID( "Shooter.WeaponEquipped", "[Shooter] %# - %#/%#", _arrWeapon[weaponIndex].getDef()._name.c_str(), _arrWeapon[weaponIndex].getMagazineAmmo(),
                        _arrWeapon[weaponIndex].getReserveAmmo() );
    }

    bool ShooterPlayerComponent::queryFirstPerson() const
    {
        const GameObject*  pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        if ( pManager == nullptr )
            return true;
        // 이 오브젝트를 따라가는 게임 카메라 디렉터(시점 카메라)의 프리셋 모드를 구한다.
        const GameObjectHandle self         = pOwner->getHandle();
        bool                   bFirstPerson = true;
        // 틱마다 묻는다. 씬 전체를 훑지 않고 등록된 디렉터만 본다.
        for ( const CameraDirectorComponent* pCameraDirector : pManager->getComponentRegistry().getAll<CameraDirectorComponent>() )
        {
            if ( pCameraDirector->isPendingDestroy() || pCameraDirector->getTarget() != self )
                continue;
            const CameraPresetDef* pPreset = pCameraDirector->getCatalog().find( pCameraDirector->getActivePresetID() );
            if ( pPreset != nullptr )
                bFirstPerson = pPreset->_view._mode == CameraPresetMode::FirstPerson;
        }
        return bFirstPerson;
    }

    void ShooterPlayerComponent::updateHUD()
    {
        GameObject* pOwner = getOwner();
        if ( pOwner == nullptr )
            return;
        // 값만 넣는다. 위젯은 HUD 문서의 바인딩이 연결한다(같은 값이면 알리지 않는다). 조준선은 1인칭에서만 보이고(다른 시점에서는 몸이 보인다),
        // 맞음 표시는 맞힌 직후 1인칭에서만 보인다. 무기 이름은 무기 테이블의 이름(원문)이 현지화 키이며, 글 위젯이 문화권에 맞게 해석한다.
        if ( _hudViewModel == nullptr )
            _hudViewModel = make_unique<ShooterHUDViewModel>();
        ShooterHUDViewModel* pViewModel = _hudViewModel.get();
        // HUD 는 클라이언트 전용이다 — 창구가 없으면(전용 서버) 값만 들고 보일 곳이 없다. 같은 뷰모델이면 거는 일은 아무것도 하지 않는다.
        if ( IHUDViewModelBinder* pBinder = game::findSubsystem<IHUDViewModelBinder>() )
            (void)pBinder->bindHUDViewModel( *pOwner, pViewModel );
        const bool bCrosshair = _bFirstPerson == SW_TRUE && _healthMachine.isAlive();
        pViewModel->setCrosshairShown( bCrosshair );
        pViewModel->setHitMarkerShown( bCrosshair && _hitMarkerTimer > 0.0f );
        if ( _bServerHealth == SW_TRUE )
            pViewModel->setHealth( _serverHealth, _maxHealth > 0.0f ? _serverHealth / _maxHealth : 0.0f );
        else
            pViewModel->setHealth( _healthMachine.getHealth(), _healthMachine.getHealthRatio() );
        const WeaponState& weapon = getCurrentWeapon();
        pViewModel->setAmmo( weapon.getMagazineAmmo(), weapon.getReserveAmmo() );
        // 무기의 이름은 무기의 장비 아이템(무기 표의 item)의 이름 하나가 원본이고, 아이템 표의 원문이 현지화 키다. 두 줄(WeaponName · ItemName)이 같은 값을 읽는다.
        // 아이템 표가 없거나 아이템이 표에 없으면 이름 줄을 비운다.
        const AppearanceDatabase* pDatabase = game::findSubsystem<AppearanceDatabase>();
        const ItemCatalog*        pItems    = pDatabase != nullptr ? pDatabase->getItemCatalog() : nullptr;
        const ItemDef*            pItem     = pItems != nullptr ? pItems->find( getWeaponItem( _weaponIndex ) ) : nullptr;
        const string_view         itemName  = pItem != nullptr ? string_view( pItem->_name ) : string_view{};
        pViewModel->setWeaponName( itemName );
        pViewModel->setItemName( itemName );
    }

    void ShooterPlayerComponent::resetRound()
    {
        _healthMachine.respawn();
        _downTimer                              = 0.0f;
        _bRoundJustRestarted                    = SW_TRUE;
        ShooterBodyMovementComponent* pMovement = findMovement();
        if ( pMovement != nullptr )
            pMovement->teleport( _spawnPosition );
        for ( WeaponState& weapon : _arrWeapon )
        {
            weapon.addReserveAmmo( weapon.getDef()._maxReserveAmmo );
        }
        GameObject*                 pOwner  = getOwner();
        FirstPersonCameraComponent* pCamera = pOwner != nullptr ? pOwner->getComponent<FirstPersonCameraComponent>() : nullptr;
        if ( pCamera != nullptr )
        {
            pCamera->setAngles( 0.0f, 0.0f );
            pCamera->setEyePosition( getEyePosition() );
        }
    }

    // ------------------------------------------------------------------------------
    // 틱이 끝난 뒤(게임 스레드). 다른 오브젝트에 쓰는 일.
    // ------------------------------------------------------------------------------
    bool ShooterPlayerComponent::hasPending() const
    {
        return _listPendingShot.empty() == false || _listPendingEffect.empty() == false || _listPendingTracer.empty() == false || _soundQueue.isEmpty() == false ||
               _bWeaponModelDirty == SW_TRUE || _bViewWeaponSynced == SW_FALSE || _bViewModeDirty == SW_TRUE || _bBodyRequested == SW_TRUE;
    }

    void ShooterPlayerComponent::scheduleFlush()
    {
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        if ( pManager == nullptr || _bFlushScheduled == SW_TRUE )
            return;
        _bFlushScheduled = SW_TRUE;
        pManager->executeOrDeferPostTickOnComponent( *this, []( ShooterPlayerComponent& player )
        { player.flushPending(); } );
    }

    void ShooterPlayerComponent::flushPending()
    {
        _bFlushScheduled            = SW_FALSE;
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        if ( pManager == nullptr )
            return;
        if ( _bBodyRequested == SW_TRUE )
            spawnBody();
        // 쏜 광선을 적의 히트박스에 묻고 맞음을 보낸다. 적은 움찔하거나 쓰러지고, 디렉터가 다음 틱에 센다. 탄도선 · 탄착 · 소리도 여기서 쌓인다.
        resolveShots( *pManager );
        resolveMelee( *pManager );
        const GameObject*         pGameModeObject = pManager->resolveGameObject( getGameMode() );
        ShooterGameModeComponent* pGameMode       = pGameModeObject != nullptr ? pGameModeObject->getComponent<ShooterGameModeComponent>() : nullptr;
        if ( pGameMode != nullptr )
        {
            for ( const EffectRequest& effect : _listPendingEffect )
            {
                pGameMode->spawnEffect( effect._position, effect._size, effect._color, effect._lifetime );
            }
            for ( const TracerRequest& tracer : _listPendingTracer )
            {
                pGameMode->spawnTracer( tracer._from, tracer._to, tracer._width, tracer._color, _tracerLifetime );
            }
        }
        _listPendingEffect.clear();
        _listPendingTracer.clear();
        _soundQueue.playAll();
        if ( _bWeaponModelDirty == SW_TRUE )
        {
            _bWeaponModelDirty = SW_FALSE;
            _bViewWeaponSynced = SW_FALSE; // 몸이 새 무기를 조립한 뒤(다음 틱 뒤) 1인칭 총이 그 부품을 따른다
            // 몸의 무기는 외형의 MainHand 슬롯이다(같은 소켓 이름 MainHand.Muzzle 이 새 무기의 총구를 가리킨다).
            GameObject*                   pBody       = findBodyObject();
            CharacterAppearanceComponent* pAppearance = pBody != nullptr ? pBody->getComponent<CharacterAppearanceComponent>() : nullptr;
            if ( pAppearance != nullptr )
                pAppearance->setSlotItem( hashed_string( "MainHand" ), getWeaponItem( _weaponIndex ) );
        }
        if ( _bViewWeaponSynced == SW_FALSE )
            syncViewWeapon();
        if ( _bViewModeDirty == SW_TRUE )
            applyViewMode();
        updateHUD();
        // 쓰러짐 클립이 끝났다. 디렉터가 적을 정리하고 감독을 처음부터 다시 돌리며, 플레이어는 처음 위치에서 다시 시작한다.
        if ( _healthMachine.isAlive() == false && _downTimer >= _downTime )
        {
            if ( pGameMode != nullptr )
                pGameMode->restartRound();
            resetRound();
        }
    }

    void ShooterPlayerComponent::spawnBody()
    {
        _bBodyRequested                     = SW_FALSE;
        GameObject*        pOwner           = getOwner();
        GameObjectManager* pManager         = pOwner != nullptr ? pOwner->getManager() : nullptr;
        AssetManager*      pAssets          = game::findSubsystem<AssetManager>();
        const bool         bCannotSpawnBody = pManager == nullptr || pAssets == nullptr || _bodyPrefab.empty() || pManager->resolveGameObject( _body ) != nullptr;
        if ( bCannotSpawnBody )
            return;
        GameObject*             pBody   = pAssets->getAssetCache<PrefabCache>().spawn( pManager, _bodyPrefab, "PlayerBody" );
        ShooterAvatarComponent* pAvatar = pBody != nullptr ? pBody->getComponent<ShooterAvatarComponent>() : nullptr;
        if ( pAvatar == nullptr )
        {
            SW_LOG_ERROR_ID( "ShooterPlayer.BodyPrefabNoShooterAvatarComponent", "[Shooter] body prefab '%#' has no ShooterAvatarComponent - the player has no visible body", _bodyPrefab.c_str() );
            if ( pBody != nullptr )
                pManager->destroyGameObject( pBody );
            return;
        }
        pAvatar->setPlayer( pOwner );
        CharacterAppearanceComponent* pAppearance = pBody->getComponent<CharacterAppearanceComponent>();
        if ( pAppearance != nullptr )
            pAppearance->setSlotItem( hashed_string( "MainHand" ), getWeaponItem( _weaponIndex ) );
        pAvatar->setFirstPerson( _bFirstPerson == SW_TRUE );
        _body = pBody->getHandle();
    }

    void ShooterPlayerComponent::syncViewWeapon()
    {
        // 1인칭 총(카메라 자식)은 몸의 MainHand 부품과 같은 메시 · 머티리얼이다 — 무기 외형(itemvisuals.xml)이 하나의 원본이다(씬에 총 메시 목록을 따로 두지 않는다).
        using Internal                               = ShooterPlayerComponentInternal;
        GameObject* const                   pOwner   = getOwner();
        GameObject* const                   pBody    = findBodyObject();
        const CharacterAppearanceComponent* pLook    = pBody != nullptr ? pBody->getComponent<CharacterAppearanceComponent>() : nullptr;
        GameObjectManager* const            pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        if ( pOwner == nullptr || pLook == nullptr || pManager == nullptr )
            return;
        const GameObject* const pPart     = pManager->resolveGameObject( pLook->findSlotPartObject( hashed_string( "MainHand" ) ) );
        const MeshComponent*    pPartMesh = pPart != nullptr ? pPart->getComponent<MeshComponent>() : nullptr;
        MeshComponent* const    pView     = Internal::findNamed<MeshComponent>( *pOwner, Internal::kViewWeaponName );
        if ( pPartMesh == nullptr || pView == nullptr || pPartMesh->getMeshID().empty() )
            return; // 아직 조립 전이다 — 다음 틱에 다시 본다
        pView->setMeshID( pPartMesh->getMeshID() );
        pView->setMaterialPath( pPartMesh->getMaterialPath().view() );
        // 시점 총은 카메라를 따라 피치로 움직이고 몸은 요만 돈다 — 손을 몸의 숨은 무기에 두면 아래를 볼 때 보이는 총과 떨어진다. 몸의 리그가 보이는 총의 소켓을 잡게 한다(언리얼 True First Person 의 손 IK).
        const AppearanceDatabase* pDatabase = game::findSubsystem<AppearanceDatabase>();
        const hashed_string       socketSet = pDatabase != nullptr ? Internal::findItemSocketSet( *pDatabase, getWeaponItem( _weaponIndex ) ) : hashed_string{};
        PoseModifierComponent*    pRig      = pBody->getComponent<PoseModifierComponent>();
        if ( pRig != nullptr && socketSet.empty() == false )
            pRig->bindRigidComponentUnit( hashed_string( Internal::kViewWeaponName ), pView, _weaponSockets.findSharedSocketSet( socketSet ) );
        _bViewWeaponSynced = SW_TRUE;
    }

    void ShooterPlayerComponent::applyViewMode()
    {
        using Internal         = ShooterPlayerComponentInternal;
        _bViewModeDirty        = SW_FALSE;
        GameObject*    pOwner  = getOwner();
        MeshComponent* pWeapon = pOwner != nullptr ? Internal::findNamed<MeshComponent>( *pOwner, Internal::kViewWeaponName ) : nullptr;
        if ( pWeapon != nullptr )
            pWeapon->setVisible( _bFirstPerson == SW_TRUE );
        // 1인칭에서는 머리만 숨긴다(눈이 머리 안에 있다). 몸 · 다리는 보이고, 몸 무기 대신 시점 무기가 보인다(`ShooterAvatarComponent::setFirstPerson`).
        GameObject*             pBody   = findBodyObject();
        ShooterAvatarComponent* pAvatar = pBody != nullptr ? pBody->getComponent<ShooterAvatarComponent>() : nullptr;
        if ( pAvatar != nullptr )
            pAvatar->setFirstPerson( _bFirstPerson == SW_TRUE );
    }

    GameObject* ShooterPlayerComponent::findBodyObject() const
    {
        const GameObject*        pOwner   = getOwner();
        const GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        return pManager != nullptr ? pManager->resolveGameObject( _body ) : nullptr;
    }
} // namespace sw

namespace sw
{
    SW_AUTOMATION_GETTER_WATCH( shooterWeaponIndex, "Shooter3D.WeaponIndex", "Weapon slot of the first shooter player (0 rifle, 1 shotgun, 2 pistol)", ShooterPlayerComponent, &ShooterPlayerComponent::getWeaponIndex );
    SW_AUTOMATION_GETTER_WATCH( shooterPlayerAlive, "Shooter3D.PlayerAlive", "1 while the first shooter player is alive", ShooterPlayerComponent, &ShooterPlayerComponent::isAlive );
    SW_AUTOMATION_GETTER_WATCH( shooterShotCount, "Shooter3D.ShotCount", "Shots the first shooter player fired", ShooterPlayerComponent, &ShooterPlayerComponent::getShotCount );
    SW_AUTOMATION_COMPONENT_WATCH( shooterShotDamage, "Shooter3D.ShotDamage", "Damage of one pellet of the projectile loaded in the first shooter player's weapon", ShooterPlayerComponent, &ShooterPlayerComponentInternal::readShotDamage );
    SW_AUTOMATION_GETTER_WATCH( shooterTracerWidth, "Shooter3D.TracerWidth", "Width of the last tracer the first shooter player drew (the projectile's trail)", ShooterPlayerComponent, &ShooterPlayerComponent::getLastTracerWidth );
    SW_AUTOMATION_GETTER_WATCH( shooterTracerStartAhead, "Shooter3D.TracerStartAhead",
                                "How far ahead of the eye along the aim the last tracer started (m; in first person the muzzle of the view gun, in front of the gun)", ShooterPlayerComponent,
                                &ShooterPlayerComponent::getLastTracerStartAhead );
    // 등록 줄은 기능 파일로 옮길 때(165e16647c) 빠졌다 — 함수만 남아 명령이 없었다.
    SW_DEV_COMMAND( CheatAmmo, "cheat.ammo", "cheat.ammo", "Add two magazines of reserve ammo to every weapon of every shooter player", &ShooterDevCommandsInternal::runAmmo );
    SW_DEV_COMMAND( CheatShooterHeal, "cheat.shooter.heal", "cheat.shooter.heal", "Fill the health of every living shooter player", &ShooterDevCommandsInternal::runHeal );
    SW_DEV_COMMAND( CheatShooterGod, "cheat.shooter.god", "cheat.shooter.god", "Toggle damage immunity of every shooter player", &ShooterDevCommandsInternal::runGod );
    SW_DEV_COMMAND( ShooterProjectile, "cheat.shooter.projectile", "cheat.shooter.projectile [projectile id]",
                    "Load a projectile into the current weapon of every shooter player (no id: the weapon's own projectile)", &ShooterDevCommandsInternal::runProjectile );
    SW_AUTOMATION_GETTER_WATCH( shooterEnemyHitCount, "Shooter3D.EnemyHitCount", "Shots of the first shooter player that hit an enemy", ShooterPlayerComponent, &ShooterPlayerComponent::getHitCount );
    SW_AUTOMATION_COMPONENT_WATCH( shooterPlayerControllerKind, "Shooter3D.PlayerControllerKind", "Who possesses the first shooter player: 0 the player, 1 an AI (auto play), -1 none", ShooterPlayerComponent, &ShooterPlayerComponentInternal::readControllerKind );
} // namespace sw

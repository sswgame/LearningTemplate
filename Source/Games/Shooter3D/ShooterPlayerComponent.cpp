#include "pch.h"

#include "Games/Shooter3D/ShooterPlayerComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Automation/AutomationProbe.h"
#include "Engine/Character/Hit/CharacterHit.h"
#include "Engine/Character/PoseModifier/PoseModifierComponent.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/Component/Physics/ScenePhysics.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Physics/PhysicsSettings.h"
#include "Engine/Resource/AssetManager.h"
#include "Engine/Telemetry/TelemetryEvent.h"
#include "Engine/Telemetry/TelemetryService.h"

#include "GameFramework/Base/Actor/Camera/CameraDirectorComponent.h"
#include "GameFramework/Base/Actor/Control/Controller/AIControllerComponent.h"
#include "GameFramework/Base/Actor/Control/Controller/PlayerControllerComponent.h"
#include "GameFramework/Base/Actor/Control/FirstPersonCameraComponent.h"
#include "GameFramework/Base/Actor/Control/Pawn/PawnComponent.h"
#include "GameFramework/Base/Foundation/Framework/GameService.h"
#include "GameFramework/Base/Foundation/Framework/Presentation/GameSound.h"
#include "GameFramework/Base/Foundation/Utility/Math/OrientationUtil.h"
#include "GameFramework/Base/Gameplay/Appearance/AppearanceDatabase.h"
#include "GameFramework/Base/Gameplay/Appearance/CharacterAppearanceComponent.h"
#include "GameFramework/Base/Gameplay/Inventory/ItemCatalog.h"
#include "GameFramework/Base/UI/HUD/HUDControllerComponent.h"

#include "Games/Shooter3D/ShooterAvatarComponent.h"
#include "Games/Shooter3D/ShooterBodyMovementComponent.h"
#include "Games/Shooter3D/ShooterDirectorComponent.h"
#include "Games/Shooter3D/ShooterEnemyComponent.h"
#include "Games/Shooter3D/ShooterHUDViewModel.h"

namespace sw
{
    SW_LOG_CALLER( "ShooterPlayer" );

    namespace
    {
        struct ShooterPlayerComponentInternal
        {
            static constexpr const utf8* kArrWeaponAction[ShooterPlayerComponent::kWeaponCount] = { "Weapon1", "Weapon2", "Weapon3" };
            // 폰 스키마의 버튼과 아날로그 이름이다(플레이어는 같은 이름의 입력 맵 액션을 쓰고, 자동 플레이 AI 는 그 이름을 누른다).
            static constexpr const utf8* kFireButton         = "Fire";
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

            /** @brief 시나리오 프로브가 보는 첫 슈터 플레이어입니다(드물게 불리므로 씬을 훑습니다). */
            static const ShooterPlayerComponent* findFirstPlayer( const GameObjectManager* pManager )
            {
                const ShooterPlayerComponent* pFound = nullptr;
                if ( pManager == nullptr )
                    return pFound;
                pManager->forEachComponentOfType<ShooterPlayerComponent>( [&pFound]( ShooterPlayerComponent* pPlayer )
                {
                    if ( pFound == nullptr )
                        pFound = pPlayer;
                } );
                return pFound;
            }

            /** @brief 첫 슈터 플레이어의 무기 번호입니다(0 소총, 1 산탄총, 2 권총). */
            [[nodiscard]] static bool readWeaponIndex( const GameObjectManager* pManager, float64& outValue )
            {
                const ShooterPlayerComponent* pPlayer = findFirstPlayer( pManager );
                if ( pPlayer == nullptr )
                    return false;
                outValue = static_cast<float64>( pPlayer->getWeaponIndex() );
                return true;
            }

            /** @brief 첫 슈터 플레이어가 쏜 발 수입니다(판을 다시 시작해도 이어 센다). */
            [[nodiscard]] static bool readShotCount( const GameObjectManager* pManager, float64& outValue )
            {
                const ShooterPlayerComponent* pPlayer = findFirstPlayer( pManager );
                if ( pPlayer == nullptr )
                    return false;
                outValue = static_cast<float64>( pPlayer->getShotCount() );
                return true;
            }

            /** @brief 첫 슈터 플레이어의 발 가운데 적을 맞힌 발 수입니다. */
            [[nodiscard]] static bool readEnemyHitCount( const GameObjectManager* pManager, float64& outValue )
            {
                const ShooterPlayerComponent* pPlayer = findFirstPlayer( pManager );
                if ( pPlayer == nullptr )
                    return false;
                outValue = static_cast<float64>( pPlayer->getHitCount() );
                return true;
            }

            /** @brief 첫 슈터 플레이어의 폰을 가진 조종자를 반환합니다. 0 은 플레이어, 1 은 AI(자동 플레이), -1 은 없음입니다(그 밖의 조종자도 -1). */
            [[nodiscard]] static bool readControllerKind( const GameObjectManager* pManager, float64& outValue )
            {
                const ShooterPlayerComponent* pPlayer = findFirstPlayer( pManager );
                const GameObject*             pOwner  = pPlayer != nullptr ? pPlayer->getOwner() : nullptr;
                const PawnComponent*          pPawn   = pOwner != nullptr ? pOwner->getComponent<PawnComponent>() : nullptr;
                if ( pPawn == nullptr )
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

            /** @brief 첫 슈터 플레이어가 살아 있으면 1 을 반환합니다. 쓰러지면 판을 다시 시작해 무기가 0 으로 돌아가므로 무기 단언의 전제입니다. */
            [[nodiscard]] static bool readAlive( const GameObjectManager* pManager, float64& outValue )
            {
                const ShooterPlayerComponent* pPlayer = findFirstPlayer( pManager );
                if ( pPlayer == nullptr )
                    return false;
                outValue = pPlayer->isAlive() ? 1.0 : 0.0;
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    ShooterPlayerComponent::ShooterPlayerComponent()
        : _director{}
        , _spawnPosition{ 0.0f, 0.0f, -16.0f }
        , _bodyPrefab{}
        , _listWeaponID{}
        , _listWeaponModel{}
        , _listWeaponItem{}
        , _eyeHeight{ 1.6f }
        , _maxHealth{ 100.0f }
        , _regenDelay{ 4.0f }
        , _regenPerSecond{ 6.0f }
        , _downTime{ 2.5f }
        , _hitboxLayer{ "Ragdoll" }
        , _shotImpulse{ 30.0f }
        , _hitEffectLifetime{ 0.12f }
        , _tracerLifetime{ 0.07f }
        , _tracerWidth{ 0.025f }
        , _enemyHitColor{ 1.0f, 0.45f, 0.2f, 1.0f }
        , _coverHitColor{ 0.9f, 0.85f, 0.6f, 1.0f }
        , _tracerColor{ 3.0f, 2.2f, 0.6f, 1.0f }
        , _muzzleFlashColor{ 4.0f, 2.8f, 0.8f, 1.0f }
        , _arrWeapon{}
        , _vitality{}
        , _weaponSockets{}
        , _listPendingShot{}
        , _listPendingEffect{}
        , _listPendingTracer{}
        , _soundQueue{}
        , _body{}
        , _hitMarkerTimer{ 0.0f }
        , _timeSinceShot{ 10.0f }
        , _downTimer{ 0.0f }
        , _bodyEyeHeight{ 0.0f }
        , _lookYaw{ 0.0f }
        , _weaponIndex{ 0 }
        , _shotCount{ 0 }
        , _hitCount{ 0 }
        , _hitReactionCount{ 0 }
        , _bWeaponModelDirty{ SW_FALSE }
        , _bFlushScheduled{ SW_FALSE }
        , _bRoundJustRestarted{ SW_FALSE }
        , _bFirstPerson{ SW_TRUE }
        , _bViewModeDirty{ SW_TRUE }
        , _bBodyRequested{ SW_FALSE }
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
        VitalitySettings settings;
        settings._maxHealth        = _maxHealth;
        settings._healthRegenDelay = _regenDelay;
        settings._healthRegenRate  = _regenPerSecond;
        _vitality.initialize( settings );
        _weaponIndex                            = 0;
        _downTimer                              = 0.0f;
        GameObject*                   pOwner    = getOwner();
        FirstPersonCameraComponent*   pCamera   = pOwner != nullptr ? pOwner->getComponent<FirstPersonCameraComponent>() : nullptr;
        ShooterBodyMovementComponent* pMovement = findMovement();
        if ( pMovement != nullptr )
            pMovement->teleport( _spawnPosition );
        else
            SW_LOG_ERROR( "[Shooter] the player object has no ShooterBodyMovementComponent - the player cannot move" );
        if ( pCamera != nullptr )
        {
            pCamera->setAngles( 0.0f, 0.0f );
            pCamera->setEyePosition( getEyePosition() );
        }
        _bWeaponModelDirty = SW_TRUE;
        _bViewModeDirty    = SW_TRUE;
        _bBodyRequested    = SW_TRUE;
        scheduleFlush();
        SW_LOG_INFO( "[Shooter] arena is ready - WASD move, mouse look, LMB fire, R reload, 1/2/3 or Q/E weapons, Space jump, Shift sprint, C camera, Esc mouse" );
    }

    void ShooterPlayerComponent::onEndPlay()
    {
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
            pManager->destroyObject( pBody );
        }
        _body           = GameObjectHandle{};
        _bodyEyeHeight  = 0.0f;
        _bBodyRequested = SW_TRUE;
    }

    void ShooterPlayerComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );
        _bRoundJustRestarted                                  = SW_FALSE;
        GameObject*                     pOwner                = getOwner();
        GameObjectManager*              pManager              = pOwner != nullptr ? pOwner->getManager() : nullptr;
        FirstPersonCameraComponent*     pCamera               = pOwner != nullptr ? pOwner->getComponent<FirstPersonCameraComponent>() : nullptr;
        const ShooterDirectorComponent* pDirector             = pManager != nullptr ? GameDirectorComponent::resolve<ShooterDirectorComponent>( *pManager, _director ) : nullptr;
        const bool                      bMissingCameraContext = pCamera == nullptr || pDirector == nullptr || deltaTime <= 0.0f;
        if ( bMissingCameraContext )
            return;
        const float32 step = MathUtil::min( deltaTime, 0.1f );

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
        _vitality.update( step );
        _timeSinceShot += step;
        _hitMarkerTimer -= step;
        if ( _vitality.isAlive() == false )
        {
            // 쓰러짐 클립이 도는 동안 멈춰 있다가 판을 처음부터 다시 시작한다.
            _downTimer += step;
            pCamera->setEyePosition( getEyePosition() );
            scheduleFlush(); // HUD 갱신을 예약한다. 쓰러짐 클립이 끝나면 판도 다시 시작한다.
            return;
        }

        // 조종자(사람 또는 자동 플레이 AI)가 틱 앞에 폰에 넣은 의도만 읽는다. 조종자가 없으면 의도는 0 이다(서 있는다).
        using Internal             = ShooterPlayerComponentInternal;
        const PawnComponent* pPawn = pOwner->getComponent<PawnComponent>();
        if ( pPawn != nullptr )
            applyWeaponIntent( *pPawn );
        ShooterBodyMovementComponent* pMovement = findMovement();
        if ( pMovement != nullptr && pMovement->stepMovement( pDirector->getBoxes(), step ) )
            _soundQueue.queueEvent( Internal::kSoundLand );
        const bool bTrigger     = pPawn != nullptr && pPawn->isButtonDown( pPawn->findButton( hashed_string( Internal::kFireButton ) ) );
        const bool bJustPressed = pPawn != nullptr && pPawn->wasButtonTriggered( pPawn->findButton( hashed_string( Internal::kFireButton ) ) );
        if ( bTrigger || bJustPressed )
            fireWeapon( *pDirector, *pCamera, bJustPressed );

        _lookYaw = pCamera->getLook().getYaw();
        pCamera->setEyePosition( getEyePosition() );
        // HUD 는 틱이 끝난 뒤 넣는다(위젯은 게임 스레드만 고친다). 탄약, 체력, 맞음 표시가 거의 매 프레임 바뀐다.
        scheduleFlush();
    }

    void ShooterPlayerComponent::takeDamage( float32 amount )
    {
        if ( _bRoundJustRestarted == SW_TRUE || _vitality.isAlive() == false )
            return;
        const VitalityDamageResult result    = _vitality.applyDamage( amount );
        GameObject*                pOwner    = getOwner();
        GameObjectManager*         pManager  = pOwner != nullptr ? pOwner->getManager() : nullptr;
        const GameObject*          pObject   = pManager != nullptr ? pManager->resolveGameObject( _director ) : nullptr;
        ShooterDirectorComponent*  pDirector = pObject != nullptr ? pObject->getComponent<ShooterDirectorComponent>() : nullptr;
        if ( result._bIgnored == SW_TRUE )
            return;
        ++_hitReactionCount;
        if ( pDirector != nullptr )
            pDirector->reportPlayerDamage( amount );
        if ( result._bDied == SW_FALSE )
            return;
        _downTimer = 0.0f;
        if ( pDirector != nullptr )
        {
            SW_LOG_INFO( "[Shooter] you were overrun on wave %# after %# kills - starting over", pDirector->getWave(), pDirector->getKillCount() );
            TelemetryService* pTelemetry = game::findSubsystem<TelemetryService>();
            if ( pTelemetry != nullptr )
            {
                TelemetryEvent roundEnded( "progression.roundEnded" );
                roundEnded.setInt( "wave", pDirector->getWave() ).setInt( "kills", pDirector->getKillCount() );
                roundEnded.setFloat( "seconds", static_cast<float64>( pDirector->getPacingDirector().getTime() ) );
                roundEnded.setFloat( "accuracy", _shotCount > 0u ? static_cast<float64>( _hitCount ) / static_cast<float64>( _shotCount ) : 0.0 );
                (void)pTelemetry->record( roundEnded );
            }
        }
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
        (void)_vitality.heal( MathUtil::max( 0.0f, amount ) );
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
        const bool bInRange = 0 <= weaponIndex && weaponIndex < static_cast<int32>( _listWeaponItem.size() );
        return bInRange ? hashed_string( _listWeaponItem[static_cast<size_t>( weaponIndex )] ) : hashed_string{};
    }

    // ------------------------------------------------------------------------------
    // 입력, 이동, 사격(DuringPhysics, 자기 오브젝트)
    // ------------------------------------------------------------------------------
    bool ShooterPlayerComponent::equipWeapons()
    {
        const WeaponCatalog* pCatalog = game::findSubsystem<WeaponCatalog>();
        if ( pCatalog == nullptr )
        {
            SW_LOG_ERROR( "[Shooter] no WeaponCatalog game service - the player cannot be armed" );
            return false;
        }
        const size_t slotCount   = static_cast<size_t>( kWeaponCount );
        const bool   bSlotsMatch = _listWeaponID.size() == slotCount && _listWeaponModel.size() == slotCount && _listWeaponItem.size() == slotCount;
        if ( bSlotsMatch == false )
        {
            SW_LOG_ERROR( "[Shooter] the player needs %# weapon ids, models and items (has %#, %#, %#) - the player cannot be armed", kWeaponCount,
                          static_cast<uint32>( _listWeaponID.size() ), static_cast<uint32>( _listWeaponModel.size() ), static_cast<uint32>( _listWeaponItem.size() ) );
            return false;
        }
        bool bArmed = true;
        for ( int32 weaponIndex = 0; weaponIndex < kWeaponCount; ++weaponIndex )
        {
            const string&    weaponID = _listWeaponID[static_cast<size_t>( weaponIndex )];
            const WeaponDef* pDef     = pCatalog->find( hashed_string( weaponID ) );
            if ( pDef == nullptr )
            {
                SW_LOG_ERROR( "[Shooter] weapon slot %# names '%#', which is not in the weapon catalog", weaponIndex + 1, weaponID.c_str() );
                bArmed = false;
                continue;
            }
            _arrWeapon[weaponIndex].equip( *pDef, pDef->_maxReserveAmmo / 2, 0x51ed270bu + static_cast<uint32>( weaponIndex ) );
        }
        return bArmed;
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
        const hashed_string switchName( Internal::kSwitchWeaponButton );
        const int32         analogIndex            = pawn.findAnalog( switchName );
        const bool          bWeaponSwitchRequested = pawn.wasButtonTriggered( pawn.findButton( switchName ) ) && 0 <= analogIndex && analogIndex < ControlIntent::kAnalogCount;
        if ( bWeaponSwitchRequested )
        {
            const float32 direction = pawn.getIntent()._arrAnalog[analogIndex];
            if ( direction > 0.0f )
                nextWeapon = ( _weaponIndex + 1 ) % kWeaponCount;
            else if ( direction < 0.0f )
                nextWeapon = ( _weaponIndex + kWeaponCount - 1 ) % kWeaponCount;
        }
        if ( nextWeapon >= 0 )
            switchWeapon( nextWeapon );
        if ( pawn.wasButtonTriggered( pawn.findButton( hashed_string( Internal::kReloadButton ) ) ) && _arrWeapon[_weaponIndex].startReload() )
            SW_LOG_INFO( "[Shooter] reloading %#", _arrWeapon[_weaponIndex].getDef()._name.c_str() );
    }

    void ShooterPlayerComponent::fireWeapon( const ShooterDirectorComponent& director, FirstPersonCameraComponent& camera, bool bJustPressed )
    {
        WeaponState& weapon = _arrWeapon[_weaponIndex];
        GameRay      aim;
        aim._origin    = getEyePosition();
        aim._direction = camera.getLook().getForward();
        WeaponShot             shot;
        const WeaponFireResult result = weapon.pullTrigger( aim, bJustPressed, shot );
        if ( result == WeaponFireResult::EmptyMagazine )
            SW_LOG_INFO( "[Shooter] %# empty - reloading (%# in reserve)", weapon.getDef()._name.c_str(), weapon.getReserveAmmo() );
        else if ( result == WeaponFireResult::OutOfAmmo && bJustPressed )
            SW_LOG_INFO( "[Shooter] %# is out of ammo - switch weapons", weapon.getDef()._name.c_str() );
        if ( result != WeaponFireResult::Fired )
            return;

        ++_shotCount;
        _timeSinceShot      = 0.0f;
        const float3 muzzle = findMuzzlePosition();
        // 총구 섬광은 총구에 잠깐 빛나는 구다.
        EffectRequest flash;
        flash._position = muzzle;
        flash._size     = _bFirstPerson == SW_TRUE ? 0.05f : 0.12f;
        flash._color    = _muzzleFlashColor;
        flash._lifetime = 0.05f;
        _listPendingEffect.push_back( flash );
        // 판정은 눈에서 한다. 상자 · 바닥까지는 여기서 재고, 적(물리 히트박스)은 틱이 끝난 뒤 그 거리 안에서 묻는다(resolveShots).
        for ( const GameRay& ray : shot._listRay )
        {
            PendingShot pending;
            pending._ray           = ray;
            pending._muzzle        = muzzle;
            pending._coverDistance = traceCover( director, ray );
            pending._range         = weapon.getDef()._range;
            pending._damage        = weapon.getDef()._damage;
            pending._volley        = _shotCount;
            _listPendingShot.push_back( pending );
        }
        // 반동은 위로 튀고 옆으로 살짝 흔든다.
        const float32 kick = weapon.getDef()._recoilPitch * MathUtil::kDegreeToRadian;
        camera.addRecoil( kick, kick * 0.25f * ( ( _shotCount % 2u ) == 0u ? 1.0f : -1.0f ) );
    }

    float32 ShooterPlayerComponent::traceCover( const ShooterDirectorComponent& director, const GameRay& ray ) const
    {
        float32 nearest = _arrWeapon[_weaponIndex].getDef()._range;
        for ( const ShooterArenaBox& box : director.getBoxes() )
        {
            float32 distance = 0.0f;
            if ( RayMath::intersectAABB( ray, box._min, box._max, nearest, distance ) && distance < nearest )
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
            SW_LOG_ERROR( "Hitbox layer '%#' is not in the physics settings - shots cannot hit enemies", _hitboxLayer.c_str() );
        GameObject*  pOwner   = getOwner();
        const uint64 ownerID  = pOwner != nullptr ? pOwner->getObjectID() : 0;
        size_t       shotHead = 0;
        while ( shotHead < _listPendingShot.size() )
        {
            // 한 방아쇠(산탄은 광선 여럿)에 소리 · 맞음 수는 하나만 낸다. 적을 맞혔으면 쇳소리, 상자나 벽을 맞혔으면 나무 소리다.
            const uint32 volley    = _listPendingShot[shotHead]._volley;
            bool         bAnyHit   = false;
            bool         bHitCover = false;
            for ( ; shotHead < _listPendingShot.size() && _listPendingShot[shotHead]._volley == volley; ++shotHead )
            {
                const PendingShot&     shot     = _listPendingShot[shotHead];
                float32                distance = shot._coverDistance;
                bool                   bHitBody = false;
                CharacterRayHit        rayHit;
                const bool             bRayHit = bHasMask && CharacterHitUtil::raycast3D( manager, shot._ray._origin, shot._ray._direction, shot._coverDistance, hitboxMask, ownerID, rayHit );
                ShooterEnemyComponent* pEnemy  = bRayHit && rayHit._pObject != nullptr ? rayHit._pObject->getComponent<ShooterEnemyComponent>() : nullptr;
                if ( pEnemy != nullptr )
                {
                    // 쓰러진 적(래그돌)도 맞고 밀린다 — 맞음 수 · 맞음 표시는 산 적만 센다.
                    const bool    bAlive = pEnemy->isAlive();
                    const HitInfo hit    = CharacterHitUtil::makeHitInfo( rayHit, pOwner, shot._ray._direction, shot._damage, _shotImpulse );
                    CharacterHitUtil::deliverHit( *rayHit._pObject, hit );
                    distance = rayHit._distance;
                    bHitBody = true;
                    bAnyHit  = bAnyHit || bAlive;
                }
                const float3 end = shot._ray._origin + shot._ray._direction * distance;
                // 탄도선은 총구에서 맞은 위치까지 이은 선이다.
                _listPendingTracer.push_back( TracerRequest{ shot._muzzle, end } );
                if ( distance < shot._range )
                {
                    EffectRequest effect;
                    effect._position = end;
                    effect._size     = bHitBody ? 0.18f : 0.1f;
                    effect._color    = bHitBody ? _enemyHitColor : _coverHitColor;
                    effect._lifetime = _hitEffectLifetime;
                    _listPendingEffect.push_back( effect );
                    bHitCover = bHitCover || ( bHitBody == false && end._y > 0.05f );
                }
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

    float3 ShooterPlayerComponent::findMuzzlePosition()
    {
        using Internal             = ShooterPlayerComponentInternal;
        const float3      fallback = getEyePosition() + float3{ 0.0f, -0.15f, 0.0f };
        const GameObject* pOwner   = getOwner();
        float3            result   = fallback;
        float4x4          muzzle;
        if ( _bFirstPerson == SW_FALSE )
        {
            // 몸이 든 무기는 외형의 소켓 이름 공간(슬롯 이름과 무기 소켓)으로 찾는다.
            const GameObject*                   pBody       = findBodyObject();
            const CharacterAppearanceComponent* pAppearance = pBody != nullptr ? pBody->getComponent<CharacterAppearanceComponent>() : nullptr;
            if ( pAppearance != nullptr && pAppearance->findSocketWorldTransform( hashed_string( Internal::kBodyMuzzleSocket ), muzzle ) )
            {
                result = muzzle.getTranslation();
                return result;
            }
            return result;
        }
        // 1인칭에서는 손에 든 총(카메라 자식)과 그 무기의 총구 소켓을 쓴다.
        const MeshComponent*      pViewWeapon = pOwner != nullptr ? Internal::findNamed<MeshComponent>( *pOwner, Internal::kViewWeaponName ) : nullptr;
        const AppearanceDatabase* pDatabase   = game::findSubsystem<AppearanceDatabase>();
        const hashed_string       socketPath  = pDatabase != nullptr ? Internal::findItemSocketSet( *pDatabase, getWeaponItem( _weaponIndex ) ) : hashed_string{};
        const SocketSet*          pSockets    = socketPath.empty() ? nullptr : _weaponSockets.findSocketSet( socketPath );
        const SocketDef*          pMuzzle     = pSockets != nullptr ? pSockets->findSocket( hashed_string( Internal::kMuzzleSocket ) ) : nullptr;
        if ( pViewWeapon != nullptr && pMuzzle != nullptr )
            result = ( pMuzzle->makeLocalTransform() * pViewWeapon->getWorldMatrix() ).getTranslation();
        return result;
    }

    void ShooterPlayerComponent::switchWeapon( int32 weaponIndex )
    {
        const bool bInvalidWeaponSwitch = weaponIndex == _weaponIndex || weaponIndex < 0 || weaponIndex >= kWeaponCount;
        if ( bInvalidWeaponSwitch )
            return;
        _weaponIndex       = weaponIndex;
        _bWeaponModelDirty = SW_TRUE;
        SW_LOG_INFO( "[Shooter] %# - %#/%#", _arrWeapon[weaponIndex].getDef()._name.c_str(), _arrWeapon[weaponIndex].getMagazineAmmo(),
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
        GameObject*             pOwner = getOwner();
        HUDControllerComponent* pHUD   = pOwner != nullptr ? pOwner->getComponent<HUDControllerComponent>() : nullptr;
        if ( pHUD == nullptr )
            return;
        // 값만 넣는다. 위젯은 HUD 문서의 바인딩이 연결한다(같은 값이면 알리지 않는다). 조준선은 1인칭에서만 보이고(다른 시점에서는 몸이 보인다),
        // 맞음 표시는 맞힌 직후 1인칭에서만 보인다. 무기 이름은 무기 테이블의 이름(원문)이 현지화 키이며, 글 위젯이 문화권에 맞게 해석한다.
        ShooterHUDViewModel* pViewModel = pHUD->getOrCreateViewModel<ShooterHUDViewModel>();
        if ( pViewModel == nullptr )
            return; // 다른 뷰모델이 걸려 있다 — HUD 컨트롤러가 오류를 남겼다
        const bool bCrosshair = _bFirstPerson == SW_TRUE && _vitality.isAlive();
        pViewModel->setCrosshairShown( bCrosshair );
        pViewModel->setHitMarkerShown( bCrosshair && _hitMarkerTimer > 0.0f );
        pViewModel->setHealth( _vitality.getHealth(), _vitality.getHealthRatio() );
        const WeaponState& weapon = getCurrentWeapon();
        pViewModel->setAmmo( weapon.getMagazineAmmo(), weapon.getReserveAmmo() );
        pViewModel->setWeaponName( weapon.getDef()._name );
        // 손에 든 장비 아이템(무기의 외형 아이템)의 이름도 아이템 표의 원문이 키다. 아이템 표가 없거나 무기에 아이템이 없으면 이름 줄을 비운다.
        const AppearanceDatabase* pDatabase = game::findSubsystem<AppearanceDatabase>();
        const ItemCatalog*        pItems    = pDatabase != nullptr ? pDatabase->getItemCatalog() : nullptr;
        const ItemDef*            pItem     = pItems != nullptr ? pItems->find( getWeaponItem( _weaponIndex ) ) : nullptr;
        pViewModel->setItemName( pItem != nullptr ? string_view( pItem->_name ) : string_view{} );
    }

    void ShooterPlayerComponent::resetRound()
    {
        _vitality.respawn();
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
               _bWeaponModelDirty == SW_TRUE || _bViewModeDirty == SW_TRUE || _bBodyRequested == SW_TRUE;
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
        using Internal              = ShooterPlayerComponentInternal;
        _bFlushScheduled            = SW_FALSE;
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        if ( pManager == nullptr )
            return;
        if ( _bBodyRequested == SW_TRUE )
            spawnBody();
        // 쏜 광선을 적의 히트박스에 묻고 맞음을 보낸다. 적은 움찔하거나 쓰러지고, 디렉터가 다음 틱에 센다. 탄도선 · 탄착 · 소리도 여기서 쌓인다.
        resolveShots( *pManager );
        const GameObject*         pDirectorObject = pManager->resolveGameObject( _director );
        ShooterDirectorComponent* pDirector       = pDirectorObject != nullptr ? pDirectorObject->getComponent<ShooterDirectorComponent>() : nullptr;
        if ( pDirector != nullptr )
        {
            for ( const EffectRequest& effect : _listPendingEffect )
            {
                pDirector->spawnEffect( effect._position, effect._size, effect._color, effect._lifetime );
            }
            for ( const TracerRequest& tracer : _listPendingTracer )
            {
                pDirector->spawnTracer( tracer._from, tracer._to, _tracerWidth, _tracerColor, _tracerLifetime );
            }
        }
        _listPendingEffect.clear();
        _listPendingTracer.clear();
        _soundQueue.playAll();
        if ( _bWeaponModelDirty == SW_TRUE )
        {
            _bWeaponModelDirty       = SW_FALSE;
            MeshComponent* pModel    = Internal::findNamed<MeshComponent>( *pOwner, Internal::kViewWeaponName );
            const bool     bHasModel = 0 <= _weaponIndex && _weaponIndex < static_cast<int32>( _listWeaponModel.size() );
            if ( pModel != nullptr && bHasModel )
                pModel->setMeshID( _listWeaponModel[static_cast<size_t>( _weaponIndex )] );
            // 몸의 무기는 외형의 MainHand 슬롯이다(같은 소켓 이름 MainHand.Muzzle 이 새 무기의 총구를 가리킨다).
            GameObject*                   pBody       = findBodyObject();
            CharacterAppearanceComponent* pAppearance = pBody != nullptr ? pBody->getComponent<CharacterAppearanceComponent>() : nullptr;
            if ( pAppearance != nullptr )
                pAppearance->setSlotItem( hashed_string( "MainHand" ), getWeaponItem( _weaponIndex ) );
        }
        if ( _bViewModeDirty == SW_TRUE )
            applyViewMode();
        updateHUD();
        // 쓰러짐 클립이 끝났다. 디렉터가 적을 정리하고 감독을 처음부터 다시 돌리며, 플레이어는 처음 위치에서 다시 시작한다.
        if ( _vitality.isAlive() == false && _downTimer >= _downTime )
        {
            if ( pDirector != nullptr )
                pDirector->restartRound();
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
            SW_LOG_ERROR( "[Shooter] body prefab '%#' has no ShooterAvatarComponent - the player has no visible body", _bodyPrefab.c_str() );
            if ( pBody != nullptr )
                pManager->destroyObject( pBody );
            return;
        }
        pAvatar->setPlayer( pOwner->getHandle() );
        CharacterAppearanceComponent* pAppearance = pBody->getComponent<CharacterAppearanceComponent>();
        if ( pAppearance != nullptr )
            pAppearance->setSlotItem( hashed_string( "MainHand" ), getWeaponItem( _weaponIndex ) );
        pAvatar->setFirstPerson( _bFirstPerson == SW_TRUE );
        _body = pBody->getHandle();
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
    SW_AUTOMATION_PROBE( shooterWeaponIndex, "Shooter3D.WeaponIndex", "Weapon slot of the first shooter player (0 rifle, 1 shotgun, 2 pistol)",
                         &ShooterPlayerComponentInternal::readWeaponIndex );
    SW_AUTOMATION_PROBE( shooterPlayerAlive, "Shooter3D.PlayerAlive", "1 while the first shooter player is alive", &ShooterPlayerComponentInternal::readAlive );
    SW_AUTOMATION_PROBE( shooterShotCount, "Shooter3D.ShotCount", "Shots the first shooter player fired", &ShooterPlayerComponentInternal::readShotCount );
    SW_AUTOMATION_PROBE( shooterEnemyHitCount, "Shooter3D.EnemyHitCount", "Shots of the first shooter player that hit an enemy", &ShooterPlayerComponentInternal::readEnemyHitCount );
    SW_AUTOMATION_PROBE( shooterPlayerControllerKind, "Shooter3D.PlayerControllerKind", "Who possesses the first shooter player: 0 the player, 1 an AI (auto play), -1 none",
                         &ShooterPlayerComponentInternal::readControllerKind );
} // namespace sw

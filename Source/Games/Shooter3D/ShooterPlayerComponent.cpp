#include "pch.h"

#include "Games/Shooter3D/ShooterPlayerComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Automation/AutomationProbe.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/InputMap.h"
#include "Engine/Object/Component/2D/SpriteComponent.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/GameObject/ComponentRegistry.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Resource/AssetManager.h"
#include "Engine/Telemetry/TelemetryEvent.h"
#include "Engine/Telemetry/TelemetryService.h"

#include "GameFramework/Base/Appearance/AppearanceDatabase.h"
#include "GameFramework/Base/Appearance/CharacterAppearanceComponent.h"
#include "GameFramework/Base/Camera/CameraDirectorComponent.h"
#include "GameFramework/Base/Control/FirstPersonCameraComponent.h"
#include "GameFramework/Base/Control/PawnComponent.h"
#include "GameFramework/Base/Framework/GameService.h"
#include "GameFramework/Base/Framework/GameSound.h"
#include "GameFramework/Base/Inventory/ItemCatalog.h"
#include "GameFramework/Base/Utility/OrientationUtil.h"

#include "Games/Shooter3D/ShooterAvatarComponent.h"
#include "Games/Shooter3D/ShooterDirectorComponent.h"
#include "Games/Shooter3D/ShooterEnemyComponent.h"

namespace sw
{
    SW_LOG_CALLER( "ShooterPlayer" );

    namespace
    {
        struct ShooterPlayerComponentInternal
        {
            static constexpr const utf8* kArrWeaponId[ShooterPlayerComponent::kWeaponCount]     = { "rifle", "shotgun", "pistol" };
            static constexpr const utf8* kArrWeaponModel[ShooterPlayerComponent::kWeaponCount]  = { "game/shooter3d/models/blaster_d.mesh",
                                                                                                    "game/shooter3d/models/blaster_h.mesh",
                                                                                                    "game/shooter3d/models/blaster_a.mesh" };
            static constexpr const utf8* kArrWeaponAction[ShooterPlayerComponent::kWeaponCount] = { "Weapon1", "Weapon2", "Weapon3" };
            static constexpr const utf8* kViewWeaponName                                        = "ViewWeapon";
            static constexpr const utf8* kCrosshairName                                         = "Crosshair";
            static constexpr const utf8* kHitMarkerName                                         = "HitMarker";
            static constexpr const utf8* kMuzzleSocket                                          = "Muzzle";
            static constexpr const utf8* kBodyMuzzleSocket                                      = "MainHand.Muzzle";
            static constexpr const utf8* kEyesSocket                                            = "Eyes";

            static constexpr float4 kEnemyHitColor{ 1.0f, 0.45f, 0.2f, 1.0f };
            static constexpr float4 kCoverHitColor{ 0.9f, 0.85f, 0.6f, 1.0f };
            static constexpr float4 kTracerColor{ 3.0f, 2.2f, 0.6f, 1.0f };
            static constexpr float4 kMuzzleFlashColor{ 4.0f, 2.8f, 0.8f, 1.0f };
            // 사운드 이벤트 이름(shooter3d.audioevents.xml) — 플레이어 자신의 소리라 2D 로 낸다.
            static constexpr const utf8* kSoundLand     = "Land";
            static constexpr const utf8* kSoundHitEnemy = "HitEnemy";
            static constexpr const utf8* kSoundHitCover = "HitCover";

            static float3 flatten( const float3& value ) { return float3{ value._x, 0.0f, value._z }; }

            /** @brief 같은 오브젝트에서 컴포넌트 이름이 @p pName 인 T 입니다. 없으면 nullptr 입니다. */
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

            /** @brief 아이템의 외형에서 소켓 에셋을 가진 첫 부품의 소켓 에셋입니다(무기의 총구). 없으면 빈 이름입니다. */
            static hashed_string findItemSocketSet( const AppearanceDatabase& database, const hashed_string& itemId )
            {
                const ItemCatalog*   pItems  = database.getItemCatalog();
                const ItemDef*       pItem   = pItems != nullptr ? pItems->findItem( itemId ) : nullptr;
                const ItemVisualDef* pVisual = pItem != nullptr ? database.getVisuals().findVisual( pItem->_visualId ) : nullptr;
                if ( pVisual == nullptr )
                    return hashed_string{};
                for ( const AppearancePartDef& part : pVisual->_listPart )
                {
                    if ( part._socketSet.empty() == false )
                        return part._socketSet;
                }
                return hashed_string{};
            }
        };

        /** @brief 시나리오 탐침이 보는 첫 슈터 플레이어입니다(드물게 불리므로 씬을 훑는다). */
        struct ShooterPlayerProbeInternal
        {
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

            /** @brief 첫 슈터 플레이어의 무기 번호(0 소총 · 1 산탄총 · 2 권총)입니다. */
            [[nodiscard]] static bool readWeaponIndex( const GameObjectManager* pManager, float64& outValue )
            {
                const ShooterPlayerComponent* pPlayer = findFirstPlayer( pManager );
                if ( pPlayer == nullptr )
                    return false;
                outValue = static_cast<float64>( pPlayer->getWeaponIndex() );
                return true;
            }

            /** @brief 첫 슈터 플레이어가 살아 있으면 1 — 쓰러지면 판을 다시 시작해 무기가 0 으로 돌아가므로 무기 단언의 전제입니다. */
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
        , _bodyPrefab{ "game/shooter3d/prefabs/player_body.prefab.xml" }
        , _listWeaponItem{ "blaster_rifle", "blaster_shotgun", "blaster_pistol" }
        , _walkSpeed{ 5.5f }
        , _sprintSpeed{ 8.5f }
        , _jumpSpeed{ 6.0f }
        , _gravity{ 18.0f }
        , _radius{ 0.35f }
        , _eyeHeight{ 1.6f }
        , _maxHealth{ 100.0f }
        , _regenDelay{ 4.0f }
        , _regenPerSecond{ 6.0f }
        , _downTime{ 2.5f }
        , _crosshairDistance{ 0.6f }
        , _hitEffectLifetime{ 0.12f }
        , _tracerLifetime{ 0.07f }
        , _tracerWidth{ 0.025f }
        , _autoEngageDistance{ 7.0f }
        , _autoTurnRate{ 3.0f }
        , _arrWeapon{}
        , _vitality{}
        , _weaponSockets{}
        , _listPendingHit{}
        , _listPendingEffect{}
        , _listPendingTracer{}
        , _soundQueue{}
        , _body{}
        , _position{ 0.0f, 0.0f, -16.0f }
        , _moveVelocity{ 0.0f, 0.0f, 0.0f }
        , _verticalSpeed{ 0.0f }
        , _hitMarkerTimer{ 0.0f }
        , _timeSinceShot{ 10.0f }
        , _downTimer{ 0.0f }
        , _bodyEyeHeight{ 0.0f }
        , _lookYaw{ 0.0f }
        , _weaponIndex{ 0 }
        , _shotCount{ 0 }
        , _hitCount{ 0 }
        , _hitReactionCount{ 0 }
        , _bOnGround{ SW_TRUE }
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
        equipWeapons();
        VitalitySettings settings;
        settings._maxHealth        = _maxHealth;
        settings._healthRegenDelay = _regenDelay;
        settings._healthRegenRate  = _regenPerSecond;
        _vitality.initialize( settings );
        _position                                 = _spawnPosition;
        _verticalSpeed                            = 0.0f;
        _weaponIndex                              = 0;
        _downTimer                                = 0.0f;
        GameObject*                     pOwner    = getOwner();
        GameObjectManager*              pManager  = pOwner != nullptr ? pOwner->getManager() : nullptr;
        FirstPersonCameraComponent*     pCamera   = pOwner != nullptr ? pOwner->getComponent<FirstPersonCameraComponent>() : nullptr;
        const ShooterDirectorComponent* pDirector = pManager != nullptr ? GameDirectorComponent::resolve<ShooterDirectorComponent>( *pManager, _director ) : nullptr;
        // 자동 플레이는 플레이어 조종자가 폰을 쥐지 않는다(마우스를 잠그지 않는다) — 시점은 조준 AI 가 정한다(`setAngles` → 조종 회전 요청).
        PawnComponent* pPawn = pOwner != nullptr ? pOwner->getComponent<PawnComponent>() : nullptr;
        if ( pPawn != nullptr && pDirector != nullptr && pDirector->isAutoPlayOn() )
            pPawn->setAutoPossess( PawnAutoPossess::None );
        if ( pCamera != nullptr )
        {
            pCamera->setAngles( 0.0f, 0.0f );
            pCamera->setEyePosition( getEyePosition() );
            placeOverlay();
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
            // 부품(투구 · 무기)은 몸의 자식으로 붙어 있지만, 외형이 세운 것이라 외형이 걷는다.
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
        _bRoundJustRestarted                      = SW_FALSE;
        GameObject*                     pOwner    = getOwner();
        GameObjectManager*              pManager  = pOwner != nullptr ? pOwner->getManager() : nullptr;
        FirstPersonCameraComponent*     pCamera   = pOwner != nullptr ? pOwner->getComponent<FirstPersonCameraComponent>() : nullptr;
        const ShooterDirectorComponent* pDirector = pManager != nullptr ? GameDirectorComponent::resolve<ShooterDirectorComponent>( *pManager, _director ) : nullptr;
        if ( pCamera == nullptr || pDirector == nullptr || deltaTime <= 0.0f )
            return;
        const float32 step = MathUtil::min( deltaTime, 0.1f );

        // 시점 카메라의 프리셋(1인칭 · 3인칭 · 궤도 · CCTV) — 바뀌면 틱 뒤에 보임을 맞춘다.
        const bool bFirstPerson = queryFirstPerson();
        if ( bFirstPerson != ( _bFirstPerson == SW_TRUE ) )
        {
            _bFirstPerson   = bFirstPerson ? SW_TRUE : SW_FALSE;
            _bViewModeDirty = SW_TRUE;
        }
        // 눈높이 — 몸의 Eyes 소켓(바인드 포즈)이 정한다. 몸이 조립되기 전에는 속성 값.
        if ( _bodyEyeHeight <= 0.0f )
        {
            const GameObject*                   pBody       = findBodyObject();
            const CharacterAppearanceComponent* pAppearance = pBody != nullptr ? pBody->getComponent<CharacterAppearanceComponent>() : nullptr;
            float4x4                            eyes;
            if ( pAppearance != nullptr && pAppearance->findBindSocketTransform( hashed_string( ShooterPlayerComponentInternal::kEyesSocket ), eyes ) )
                _bodyEyeHeight = MathUtil::max( 0.5f, eyes.getTranslation()._y );
        }

        for ( WeaponState& weapon : _arrWeapon )
            weapon.update( step );
        _vitality.update( step );
        _timeSinceShot += step;
        _hitMarkerTimer -= step;
        if ( _vitality.isAlive() == false )
        {
            // 쓰러짐 클립이 도는 동안 멈춰 있다가 판을 처음부터.
            _downTimer += step;
            _moveVelocity = float3{ 0.0f, 0.0f, 0.0f };
            if ( _downTimer >= _downTime )
                scheduleFlush();
            pCamera->setEyePosition( getEyePosition() );
            placeOverlay();
            return;
        }

        PlayerIntent        intent;
        const InputManager* pInput = game::getService<InputManager>();
        if ( pDirector->isAutoPlayOn() || pInput == nullptr )
            tickAutoAim( step, *pDirector, *pCamera, intent );
        else
            readIntent( pInput->getInputMap(), *pCamera, intent );
        if ( intent._switchWeapon >= 0 )
            switchWeapon( intent._switchWeapon );
        if ( intent._bReload == SW_TRUE && _arrWeapon[_weaponIndex].startReload() )
            SW_LOG_INFO( "[Shooter] reloading %#", _arrWeapon[_weaponIndex].getDef()._name.c_str() );

        const float3 before = _position;
        movePlayer( *pDirector, intent._move, intent._bJump == SW_TRUE, intent._bSprint == SW_TRUE, step );
        _moveVelocity = ShooterPlayerComponentInternal::flatten( _position - before ) * ( 1.0f / step );
        if ( intent._bTrigger == SW_TRUE || intent._bJustPressed == SW_TRUE )
            fireWeapon( *pDirector, *pCamera, intent._bJustPressed == SW_TRUE );

        _lookYaw = pCamera->getLook().getYaw();
        pCamera->setEyePosition( getEyePosition() );
        placeOverlay();
        if ( hasPending() )
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
            TelemetryService* pTelemetry = game::getService<TelemetryService>();
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
            weapon.addReserveAmmo( weapon.getDef()._magazineSize * 2 );
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
        return _position + float3{ 0.0f, _bodyEyeHeight > 0.0f ? _bodyEyeHeight : _eyeHeight, 0.0f };
    }

    hashed_string ShooterPlayerComponent::getWeaponItem( int32 weaponIndex ) const
    {
        const bool bInRange = 0 <= weaponIndex && weaponIndex < static_cast<int32>( _listWeaponItem.size() );
        return bInRange ? hashed_string( _listWeaponItem[static_cast<size_t>( weaponIndex )] ) : hashed_string{};
    }

    // ------------------------------------------------------------------------------
    // 입력 · 이동 · 사격(DuringPhysics — 자기 오브젝트)
    // ------------------------------------------------------------------------------
    void ShooterPlayerComponent::equipWeapons()
    {
        const WeaponCatalog* pCatalog = game::getService<WeaponCatalog>();
        for ( int32 weaponIndex = 0; weaponIndex < kWeaponCount; ++weaponIndex )
        {
            const WeaponDef* pDef = pCatalog != nullptr ? pCatalog->findWeapon( hashed_string( ShooterPlayerComponentInternal::kArrWeaponId[weaponIndex] ) ) : nullptr;
            if ( pDef == nullptr && pCatalog != nullptr && pCatalog->getWeapons().empty() == false )
                pDef = &pCatalog->getWeapons().front();
            if ( pDef != nullptr )
                _arrWeapon[weaponIndex].equip( *pDef, pDef->_maxReserveAmmo / 2, 0x51ed270bu + static_cast<uint32>( weaponIndex ) );
        }
    }

    void ShooterPlayerComponent::readIntent( const InputMap& inputMap, const FirstPersonCameraComponent& camera, PlayerIntent& outIntent ) const
    {
        using Internal = ShooterPlayerComponentInternal;
        // 시점은 같은 오브젝트의 1인칭 카메라가 앞 그룹에서 폰의 조종 회전으로 두었다(Look 액션 · 잠금 · Esc 는 플레이어 조종자).
        const FirstPersonLook& look = camera.getLook();
        const float2           move = inputMap.getVector2D( hashed_string( "Move" ) );
        outIntent._move             = look.getFlatForward() * move._y + look.getFlatRight() * move._x;
        outIntent._bJump            = inputMap.wasActionTriggered( hashed_string( "Jump" ) ) ? SW_TRUE : SW_FALSE;
        outIntent._bSprint          = inputMap.isActionDown( hashed_string( "Sprint" ) ) ? SW_TRUE : SW_FALSE;
        outIntent._bTrigger         = inputMap.isActionDown( hashed_string( "Fire" ) ) ? SW_TRUE : SW_FALSE;
        outIntent._bJustPressed     = inputMap.wasActionTriggered( hashed_string( "Fire" ) ) ? SW_TRUE : SW_FALSE;
        outIntent._bReload          = inputMap.wasActionTriggered( hashed_string( "Reload" ) ) ? SW_TRUE : SW_FALSE;
        for ( int32 weaponIndex = 0; weaponIndex < kWeaponCount; ++weaponIndex )
        {
            if ( inputMap.wasActionTriggered( hashed_string( Internal::kArrWeaponAction[weaponIndex] ) ) )
                outIntent._switchWeapon = weaponIndex;
        }
        if ( inputMap.wasActionTriggered( hashed_string( "SwitchWeapon" ) ) )
        {
            const float32 direction = inputMap.getAxis1D( hashed_string( "SwitchWeapon" ) );
            if ( direction > 0.0f )
                outIntent._switchWeapon = ( _weaponIndex + 1 ) % kWeaponCount;
            else if ( direction < 0.0f )
                outIntent._switchWeapon = ( _weaponIndex + kWeaponCount - 1 ) % kWeaponCount;
        }
    }

    void ShooterPlayerComponent::tickAutoAim( float32 deltaTime, const ShooterDirectorComponent& director, FirstPersonCameraComponent& camera, PlayerIntent& outIntent )
    {
        // 가장 가까운 적의 가슴을 천천히 겨누고, 교전 거리 안이고 조준이 맞으면 쏜다. 아레나 가운데를 중심으로 원을 그리며 움직인다.
        const float3                    eye          = getEyePosition();
        float32                         bestDistance = MathUtil::kMaxFloat;
        float3                          aimPoint{ 0.0f, 0.0f, 0.0f };
        bool                            bHasTarget = false;
        const vector<ShooterEnemyView>& listView   = director.getEnemyViews();
        const ShooterEnemyView*         pView      = listView.data();
        for ( size_t viewIndex = 0; viewIndex < listView.size(); ++viewIndex )
        {
            const float3  chest    = pView[viewIndex]._position + float3{ 0.0f, pView[viewIndex]._height * 0.55f, 0.0f };
            const float32 distance = float3::getDistance( eye, chest );
            if ( distance < bestDistance )
            {
                bestDistance = distance;
                aimPoint     = chest;
                bHasTarget   = true;
            }
        }
        outIntent._move = float3{ -_position._z, 0.0f, _position._x } * 0.08f - _position * 0.04f;
        if ( bHasTarget == false )
            return;

        const FirstPersonLook& look        = camera.getLook();
        const float3           toTarget    = ( aimPoint - eye ).normalize();
        const float32          targetYaw   = MathUtil::atan2( toTarget._x, toTarget._z );
        const float32          targetPitch = MathUtil::asin( MathUtil::clamp( toTarget._y, -1.0f, 1.0f ) );
        const float32          yawError    = OrientationUtil::wrapAngle( targetYaw - look.getYaw() );
        // 사람처럼 돈다 — 오차에 비례해 돌되 각속도 상한을 둔다. 상한이 없으면 표적이 바뀔 때 한 프레임에 수십 도 돌아 3인칭 몸 · 카메라가 튄다.
        const float32 blend   = MathUtil::min( 1.0f, deltaTime * 8.0f );
        const float32 maxTurn = _autoTurnRate * deltaTime;
        const float32 yaw     = OrientationUtil::turnTowardAngle( look.getYaw(), look.getYaw() + yawError * blend, maxTurn );
        const float32 pitch   = look.getPitch() + MathUtil::clamp( ( targetPitch - look.getPitch() ) * blend, -maxTurn, maxTurn );
        camera.setAngles( yaw, pitch );
        const bool bAimed       = MathUtil::abs( yawError ) < 3.0f * MathUtil::kDegreeToRadian && bestDistance < _autoEngageDistance;
        outIntent._bTrigger     = bAimed ? SW_TRUE : SW_FALSE;
        outIntent._bJustPressed = bAimed ? SW_TRUE : SW_FALSE;
        // 가까우면 산탄총, 멀면 소총.
        const int32 wantedWeapon = bestDistance < 8.0f ? 1 : 0;
        if ( wantedWeapon != _weaponIndex && _arrWeapon[wantedWeapon].getMagazineAmmo() + _arrWeapon[wantedWeapon].getReserveAmmo() > 0 )
            outIntent._switchWeapon = wantedWeapon;
    }

    void ShooterPlayerComponent::movePlayer( const ShooterDirectorComponent& director, const float3& wishDirection, bool bJump, bool bSprint, float32 deltaTime )
    {
        float3        move   = ShooterPlayerComponentInternal::flatten( wishDirection );
        const float32 length = move.getLength();
        if ( length > 1.0f )
            move = move * ( 1.0f / length );
        const float32 speed = bSprint ? _sprintSpeed : _walkSpeed;
        float3        next  = _position + move * ( speed * deltaTime );

        if ( bJump && _bOnGround == SW_TRUE )
        {
            _verticalSpeed = _jumpSpeed;
            _bOnGround     = SW_FALSE;
        }
        _verticalSpeed -= _gravity * deltaTime;
        next._y += _verticalSpeed * deltaTime;
        if ( next._y <= 0.0f )
        {
            if ( _bOnGround == SW_FALSE )
                _soundQueue.queueEvent( ShooterPlayerComponentInternal::kSoundLand );
            next._y        = 0.0f;
            _verticalSpeed = 0.0f;
            _bOnGround     = SW_TRUE;
        }
        _position = ShooterArenaMath::resolveCircle( director.getBoxes(), next, _radius );
    }

    void ShooterPlayerComponent::fireWeapon( const ShooterDirectorComponent& director, FirstPersonCameraComponent& camera, bool bJustPressed )
    {
        using Internal      = ShooterPlayerComponentInternal;
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
        // 총구 섬광 — 총구에 잠깐 빛나는 구.
        EffectRequest flash;
        flash._position = muzzle;
        flash._size     = _bFirstPerson == SW_TRUE ? 0.05f : 0.12f;
        flash._color    = Internal::kMuzzleFlashColor;
        flash._lifetime = 0.05f;
        _listPendingEffect.push_back( flash );
        bool bAnyHit   = false;
        bool bHitCover = false;
        for ( const GameRay& ray : shot._listRay )
        {
            bool          bHitEnemy = false;
            const float32 distance  = traceShot( director, ray, weapon.getDef()._damage, bHitEnemy );
            const float3  end       = ray._origin + ray._direction * distance;
            bAnyHit                 = bAnyHit || bHitEnemy;
            // 판정은 눈에서, 탄도선은 총구에서 — 맞은 자리까지 이은 선이다.
            _listPendingTracer.push_back( TracerRequest{ muzzle, end } );
            if ( distance < weapon.getDef()._range )
            {
                EffectRequest effect;
                effect._position = end;
                effect._size     = bHitEnemy ? 0.18f : 0.1f;
                effect._color    = bHitEnemy ? Internal::kEnemyHitColor : Internal::kCoverHitColor;
                effect._lifetime = _hitEffectLifetime;
                _listPendingEffect.push_back( effect );
                bHitCover = bHitCover || ( bHitEnemy == false && end._y > 0.05f );
            }
        }
        // 한 발(산탄 여럿)에 소리는 하나 — 적을 맞혔으면 쇳소리, 아니고 상자 · 벽을 맞혔으면 나무 소리.
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
        // 반동 — 위로 튀고 옆으로 살짝.
        const float32 kick = weapon.getDef()._recoilPitch * MathUtil::kDegreeToRadian;
        camera.addRecoil( kick, kick * 0.25f * ( ( _shotCount % 2u ) == 0u ? 1.0f : -1.0f ) );
    }

    float32 ShooterPlayerComponent::traceShot( const ShooterDirectorComponent& director, const GameRay& ray, float32 damage, bool& outHitEnemy )
    {
        float32 nearest = _arrWeapon[_weaponIndex].getDef()._range;
        for ( const ShooterArenaBox& box : director.getBoxes() )
        {
            float32 distance = 0.0f;
            if ( RayMath::intersectAabb( ray, box._min, box._max, nearest, distance ) && distance < nearest )
                nearest = distance;
        }
        // 바닥
        if ( ray._direction._y < -1.0e-4f )
            nearest = MathUtil::min( nearest, -ray._origin._y / ray._direction._y );
        // 적 — 발에서 키까지의 캡슐(물리 질의 · 부위 히트박스가 들어오면 그쪽으로).
        const vector<ShooterEnemyView>& listView     = director.getEnemyViews();
        const ShooterEnemyView*         pView        = listView.data();
        const ShooterEnemyView*         pNearestView = nullptr;
        for ( size_t viewIndex = 0; viewIndex < listView.size(); ++viewIndex )
        {
            const ShooterEnemyView& view     = pView[viewIndex];
            const float3            bottom   = view._position + float3{ 0.0f, view._radius, 0.0f };
            const float3            top      = view._position + float3{ 0.0f, MathUtil::max( view._radius, view._height - view._radius ), 0.0f };
            float32                 distance = 0.0f;
            if ( RayMath::intersectCapsule( ray, bottom, top, view._radius, nearest, distance ) && distance < nearest )
            {
                nearest      = distance;
                pNearestView = pView + viewIndex;
            }
        }
        outHitEnemy = pNearestView != nullptr;
        if ( outHitEnemy )
        {
            EnemyHit hit;
            hit._enemy  = pNearestView->_object;
            hit._damage = damage;
            _listPendingHit.push_back( hit );
        }
        return nearest;
    }

    float3 ShooterPlayerComponent::findMuzzlePosition()
    {
        using Internal             = ShooterPlayerComponentInternal;
        const float3      fallback = getEyePosition() + float3{ 0.0f, -0.15f, 0.0f };
        const GameObject* pOwner   = getOwner();
        float4x4          muzzle;
        if ( _bFirstPerson == SW_FALSE )
        {
            // 몸이 든 무기 — 외형의 소켓 이름 공간(칸 이름 + 무기 소켓).
            const GameObject*                   pBody       = findBodyObject();
            const CharacterAppearanceComponent* pAppearance = pBody != nullptr ? pBody->getComponent<CharacterAppearanceComponent>() : nullptr;
            if ( pAppearance != nullptr && pAppearance->findSocketWorldTransform( hashed_string( Internal::kBodyMuzzleSocket ), muzzle ) )
                return muzzle.getTranslation();
            return fallback;
        }
        // 1인칭 — 손에 든 총(카메라 자식) × 그 무기의 총구 소켓.
        const MeshComponent*      pViewWeapon = pOwner != nullptr ? Internal::findNamed<MeshComponent>( *pOwner, Internal::kViewWeaponName ) : nullptr;
        const AppearanceDatabase* pDatabase   = game::getService<AppearanceDatabase>();
        const hashed_string       socketPath  = pDatabase != nullptr ? Internal::findItemSocketSet( *pDatabase, getWeaponItem( _weaponIndex ) ) : hashed_string{};
        const SocketSet*          pSockets    = socketPath.empty() ? nullptr : _weaponSockets.findSocketSet( socketPath );
        const SocketDef*          pMuzzle     = pSockets != nullptr ? pSockets->findSocket( hashed_string( Internal::kMuzzleSocket ) ) : nullptr;
        if ( pViewWeapon == nullptr || pMuzzle == nullptr )
            return fallback;
        return ( pMuzzle->makeLocalTransform() * pViewWeapon->getWorldMatrix() ).getTranslation();
    }

    void ShooterPlayerComponent::switchWeapon( int32 weaponIndex )
    {
        if ( weaponIndex == _weaponIndex || weaponIndex < 0 || weaponIndex >= kWeaponCount )
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
        // 이 오브젝트를 따라가는 게임 카메라 디렉터(시점 카메라)의 프리셋 모드.
        const GameObjectHandle self         = pOwner->getHandle();
        bool                   bFirstPerson = true;
        // 틱마다 묻는다 — 씬 전체를 훑지 않고 등록된 디렉터만 본다.
        for ( const CameraDirectorComponent* pCameraDirector : pManager->getComponentRegistry().getAll<CameraDirectorComponent>() )
        {
            if ( pCameraDirector->isPendingDestroy() || pCameraDirector->getTarget() != self )
                continue;
            const CameraPresetDef* pPreset = pCameraDirector->getCatalog().findPreset( pCameraDirector->getActivePresetId() );
            if ( pPreset != nullptr )
                bFirstPerson = pPreset->_view._mode == CameraPresetMode::FirstPerson;
        }
        return bFirstPerson;
    }

    void ShooterPlayerComponent::placeOverlay()
    {
        // 조준선 · 맞음 표시는 같은 오브젝트의 카메라 자식이다(같은 오브젝트의 씬 컴포넌트는 첫 씬 컴포넌트에 붙는다) — 눈앞 로컬 자리에 두면 시점을 따라간다.
        using Internal                 = ShooterPlayerComponentInternal;
        const GameObject* pOwner       = getOwner();
        CameraComponent*  pCamera      = pOwner->getComponent<CameraComponent>();
        SpriteComponent*  arrSprite[2] = { Internal::findNamed<SpriteComponent>( *pOwner, Internal::kCrosshairName ),
                                           Internal::findNamed<SpriteComponent>( *pOwner, Internal::kHitMarkerName ) };
        for ( int32 spriteIndex = 0; spriteIndex < 2; ++spriteIndex )
        {
            SpriteComponent* pSprite = arrSprite[spriteIndex];
            if ( pSprite == nullptr || pCamera == nullptr )
                continue;
            if ( pSprite->getParent() != pCamera )
                (void)pSprite->attachToComponent( pCamera, AttachRule::KeepRelative );
            const float32 distance = _crosshairDistance - 0.01f * static_cast<float32>( spriteIndex );
            const float32 size     = spriteIndex == 0 ? 0.03f : 0.05f;
            pSprite->setLocalPosition( float3{ 0.0f, 0.0f, distance } );
            pSprite->setLocalRotation( float3{ 0.0f, 0.0f, 0.0f } );
            pSprite->setLocalScale( float3{ size, size, 1.0f } );
            // 조준선은 1인칭에서만(다른 시점에서는 눈앞에 떠 있는 판이 된다), 맞음 표시는 맞힌 직후 1인칭에서만.
            const bool bWanted = _bFirstPerson == SW_TRUE && ( spriteIndex == 0 || _hitMarkerTimer > 0.0f );
            if ( pSprite->isVisible() != bWanted )
                pSprite->setVisible( bWanted );
        }
    }

    void ShooterPlayerComponent::resetRound()
    {
        _vitality.respawn();
        _downTimer           = 0.0f;
        _position            = _spawnPosition;
        _verticalSpeed       = 0.0f;
        _bOnGround           = SW_TRUE;
        _bRoundJustRestarted = SW_TRUE;
        for ( WeaponState& weapon : _arrWeapon )
            weapon.addReserveAmmo( weapon.getDef()._maxReserveAmmo );
        GameObject*                 pOwner  = getOwner();
        FirstPersonCameraComponent* pCamera = pOwner != nullptr ? pOwner->getComponent<FirstPersonCameraComponent>() : nullptr;
        if ( pCamera != nullptr )
        {
            pCamera->setAngles( 0.0f, 0.0f );
            pCamera->setEyePosition( getEyePosition() );
        }
    }

    // ------------------------------------------------------------------------------
    // 틱 뒤(게임 스레드) — 남에게 쓰는 일
    // ------------------------------------------------------------------------------
    bool ShooterPlayerComponent::hasPending() const
    {
        return _listPendingHit.empty() == false || _listPendingEffect.empty() == false || _listPendingTracer.empty() == false || _soundQueue.isEmpty() == false ||
               _bWeaponModelDirty == SW_TRUE || _bViewModeDirty == SW_TRUE || _bBodyRequested == SW_TRUE;
    }

    void ShooterPlayerComponent::scheduleFlush()
    {
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        if ( pManager == nullptr || _bFlushScheduled == SW_TRUE )
            return;
        _bFlushScheduled           = SW_TRUE;
        const ComponentHandle self = getHandle();
        pManager->executeOrDeferPostTick( [pManager, self]()
        {
            ShooterPlayerComponent* pPlayer = static_cast<ShooterPlayerComponent*>( pManager->resolveComponent( self ) );
            if ( pPlayer != nullptr )
                pPlayer->flushPending();
        } );
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
        // 적에 피해 — 적은 움찔하거나 쓰러지고, 디렉터가 다음 틱에 센다.
        for ( const EnemyHit& hit : _listPendingHit )
        {
            GameObject*            pEnemyObject = pManager->resolveGameObject( hit._enemy );
            ShooterEnemyComponent* pEnemy       = pEnemyObject != nullptr ? pEnemyObject->getComponent<ShooterEnemyComponent>() : nullptr;
            if ( pEnemy != nullptr )
                pEnemy->applyDamage( hit._damage );
        }
        _listPendingHit.clear();
        const GameObject*         pDirectorObject = pManager->resolveGameObject( _director );
        ShooterDirectorComponent* pDirector       = pDirectorObject != nullptr ? pDirectorObject->getComponent<ShooterDirectorComponent>() : nullptr;
        if ( pDirector != nullptr )
        {
            for ( const EffectRequest& effect : _listPendingEffect )
                pDirector->spawnEffect( effect._position, effect._size, effect._color, effect._lifetime );
            for ( const TracerRequest& tracer : _listPendingTracer )
                pDirector->spawnTracer( tracer._from, tracer._to, _tracerWidth, Internal::kTracerColor, _tracerLifetime );
        }
        _listPendingEffect.clear();
        _listPendingTracer.clear();
        _soundQueue.playAll();
        if ( _bWeaponModelDirty == SW_TRUE )
        {
            _bWeaponModelDirty    = SW_FALSE;
            MeshComponent* pModel = Internal::findNamed<MeshComponent>( *pOwner, Internal::kViewWeaponName );
            if ( pModel != nullptr )
                pModel->setMeshId( Internal::kArrWeaponModel[_weaponIndex] );
            // 몸의 무기 — 외형의 MainHand 칸(같은 소켓 이름 MainHand.Muzzle 이 새 무기의 총구를 가리킨다).
            GameObject*                   pBody       = findBodyObject();
            CharacterAppearanceComponent* pAppearance = pBody != nullptr ? pBody->getComponent<CharacterAppearanceComponent>() : nullptr;
            if ( pAppearance != nullptr )
                pAppearance->setSlotItem( hashed_string( "MainHand" ), getWeaponItem( _weaponIndex ) );
        }
        if ( _bViewModeDirty == SW_TRUE )
            applyViewMode();
        // 쓰러짐 클립이 끝났다 — 디렉터가 적을 걷고 감독을 처음부터, 플레이어는 처음 자리에서.
        if ( _vitality.isAlive() == false && _downTimer >= _downTime )
        {
            if ( pDirector != nullptr )
                pDirector->restartRound();
            resetRound();
        }
    }

    void ShooterPlayerComponent::spawnBody()
    {
        _bBodyRequested             = SW_FALSE;
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        AssetManager*      pAssets  = game::getService<AssetManager>();
        if ( pManager == nullptr || pAssets == nullptr || _bodyPrefab.empty() || pManager->resolveGameObject( _body ) != nullptr )
            return;
        GameObject*             pBody   = pAssets->getPrefabCache().spawn( pManager, _bodyPrefab, "PlayerBody" );
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
        {
            pAppearance->setSlotItem( hashed_string( "MainHand" ), getWeaponItem( _weaponIndex ) );
            pAppearance->setPartsVisible( _bFirstPerson == SW_FALSE );
        }
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
        // 1인칭에서는 몸을 숨긴다 — 눈이 머리 안에 있다(머리만 숨기는 본 숨김은 PoseModifier 가 들어오면).
        GameObject*                   pBody       = findBodyObject();
        CharacterAppearanceComponent* pAppearance = pBody != nullptr ? pBody->getComponent<CharacterAppearanceComponent>() : nullptr;
        if ( pAppearance != nullptr )
            pAppearance->setPartsVisible( _bFirstPerson == SW_FALSE );
        placeOverlay();
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
                         &ShooterPlayerProbeInternal::readWeaponIndex );
    SW_AUTOMATION_PROBE( shooterPlayerAlive, "Shooter3D.PlayerAlive", "1 while the first shooter player is alive", &ShooterPlayerProbeInternal::readAlive );
} // namespace sw

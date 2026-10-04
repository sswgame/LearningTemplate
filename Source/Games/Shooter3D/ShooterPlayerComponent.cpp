#include "pch.h"

#include "Games/Shooter3D/ShooterPlayerComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Graphics/Renderer/Debug/DebugDrawQueue.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Object/Component/2D/SpriteComponent.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Telemetry/TelemetryEvent.h"
#include "Engine/Telemetry/TelemetryService.h"

#include "GameFramework/Components/FirstPersonCameraComponent.h"
#include "GameFramework/Framework/GameService.h"
#include "GameFramework/Framework/GameSound.h"

#include "Games/Shooter3D/ShooterDirectorComponent.h"
#include "Games/Shooter3D/ShooterDroneComponent.h"

namespace sw
{
    SW_LOG_CALLER( "ShooterPlayer" );

    namespace
    {
        struct ShooterPlayerComponentInternal
        {
            static constexpr float32     kCrosshairDistance                                    = 0.6f;
            static constexpr const utf8* kArrWeaponId[ShooterPlayerComponent::kWeaponCount]    = { "rifle", "shotgun", "pistol" };
            static constexpr const utf8* kArrWeaponModel[ShooterPlayerComponent::kWeaponCount] = { "game/shooter3d/models/blaster_d.mesh",
                                                                                                   "game/shooter3d/models/blaster_h.mesh",
                                                                                                   "game/shooter3d/models/blaster_a.mesh" };
            static constexpr const utf8* kViewWeaponName                                       = "ViewWeapon";
            static constexpr const utf8* kCrosshairName                                        = "Crosshair";
            static constexpr const utf8* kHitMarkerName                                        = "HitMarker";

            static constexpr float4  kDroneHitColor{ 1.0f, 0.4f, 0.2f, 1.0f };
            static constexpr float4  kCoverHitColor{ 0.9f, 0.85f, 0.6f, 1.0f };
            static constexpr float4  kTracerColor{ 1.0f, 0.9f, 0.5f, 1.0f };
            static constexpr float32 kEffectLifetime = 0.12f;
            // 사운드 이벤트 이름(shooter3d.audioevents.xml) — 플레이어 자신의 소리라 2D 로 낸다.
            static constexpr const utf8* kSoundLand     = "Land";
            static constexpr const utf8* kSoundHitDrone = "HitDrone";
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
        };
    } // namespace
} // namespace sw

namespace sw
{
    ShooterPlayerComponent::ShooterPlayerComponent()
        : _director{}
        , _spawnPosition{ 0.0f, 0.0f, -16.0f }
        , _walkSpeed{ 5.5f }
        , _sprintSpeed{ 8.5f }
        , _jumpSpeed{ 6.0f }
        , _gravity{ 18.0f }
        , _radius{ 0.35f }
        , _eyeHeight{ 1.6f }
        , _maxHealth{ 100.0f }
        , _regenDelay{ 4.0f }
        , _regenPerSecond{ 6.0f }
        , _arrWeapon{}
        , _arrTracer{}
        , _listPendingHit{}
        , _listPendingEffect{}
        , _listPendingSound{}
        , _position{ 0.0f, 0.0f, -16.0f }
        , _verticalSpeed{ 0.0f }
        , _health{ 100.0f }
        , _damageCooldown{ 0.0f }
        , _hitMarkerTimer{ 0.0f }
        , _weaponIndex{ 0 }
        , _nextTracer{ 0 }
        , _shotCount{ 0 }
        , _hitCount{ 0 }
        , _bOnGround{ SW_TRUE }
        , _bWeaponModelDirty{ SW_FALSE }
        , _bTracerAlive{ SW_FALSE }
        , _bFlushScheduled{ SW_FALSE }
        , _bRoundJustRestarted{ SW_FALSE }
        , _reserved{ 0 }
    {
        setCanEverTick( true );
    }

    ShooterPlayerComponent::~ShooterPlayerComponent() = default;

    void ShooterPlayerComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        equipWeapons();
        _position                                 = _spawnPosition;
        _health                                   = _maxHealth;
        _verticalSpeed                            = 0.0f;
        _damageCooldown                           = 0.0f;
        _weaponIndex                              = 0;
        GameObject*                     pOwner    = getOwner();
        GameObjectManager*              pManager  = pOwner != nullptr ? pOwner->getManager() : nullptr;
        FirstPersonCameraComponent*     pCamera   = pOwner != nullptr ? pOwner->getComponent<FirstPersonCameraComponent>() : nullptr;
        const ShooterDirectorComponent* pDirector = pManager != nullptr ? ShooterDirectorComponent::resolveDirector( *pManager, _director ) : nullptr;
        if ( pCamera != nullptr )
        {
            // 자동 플레이는 마우스를 잠그지 않는다 — 시점은 조준 AI 가 정한다.
            if ( pDirector != nullptr && pDirector->isAutoPlayOn() )
                pCamera->setMouseLookEnabled( false );
            pCamera->setAngles( 0.0f, 0.0f );
            pCamera->setEyePosition( getEyePosition() );
            placeOverlay();
        }
        _bWeaponModelDirty = SW_TRUE;
        scheduleFlush();
        SW_LOG_INFO( "[Shooter] arena is ready - WASD move, mouse look, LMB fire, R reload, 1/2/3 or wheel weapons, Space jump, Shift sprint, Esc mouse" );
    }

    void ShooterPlayerComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );
        _bRoundJustRestarted                      = SW_FALSE;
        GameObject*                     pOwner    = getOwner();
        GameObjectManager*              pManager  = pOwner != nullptr ? pOwner->getManager() : nullptr;
        FirstPersonCameraComponent*     pCamera   = pOwner != nullptr ? pOwner->getComponent<FirstPersonCameraComponent>() : nullptr;
        const ShooterDirectorComponent* pDirector = pManager != nullptr ? ShooterDirectorComponent::resolveDirector( *pManager, _director ) : nullptr;
        if ( pCamera == nullptr || pDirector == nullptr || deltaTime <= 0.0f )
            return;
        const float32 step = MathUtil::min( deltaTime, 0.1f );

        for ( WeaponState& weapon : _arrWeapon )
            weapon.update( step );
        float3              move{ 0.0f, 0.0f, 0.0f };
        bool                bJump        = false;
        bool                bSprint      = false;
        bool                bTrigger     = false;
        bool                bJustPressed = false;
        const InputManager* pInput       = game::getService<InputManager>();
        if ( pDirector->isAutoPlayOn() || pInput == nullptr )
            tickAutoAim( step, *pDirector, *pCamera, move, bTrigger, bJustPressed );
        else
            tickInput( *pInput, *pCamera, move, bJump, bSprint, bTrigger, bJustPressed );

        movePlayer( *pDirector, move, bJump, bSprint, step );
        if ( bTrigger || bJustPressed )
            fireWeapon( *pDirector, *pCamera, bJustPressed );

        // 체력 — 맞은 뒤 잠시 지나면 다시 찬다.
        _damageCooldown -= step;
        if ( _damageCooldown <= 0.0f )
            _health = MathUtil::min( _maxHealth, _health + _regenPerSecond * step );
        _hitMarkerTimer -= step;
        updateTracers( step );

        pCamera->setEyePosition( getEyePosition() );
        placeOverlay();
        if ( hasPending() )
            scheduleFlush();
    }

    void ShooterPlayerComponent::takeDamage( float32 amount )
    {
        if ( _bRoundJustRestarted == SW_TRUE )
            return;
        _health -= amount;
        _damageCooldown                     = _regenDelay;
        GameObject*               pOwner    = getOwner();
        GameObjectManager*        pManager  = pOwner != nullptr ? pOwner->getManager() : nullptr;
        const GameObject*         pObject   = pManager != nullptr ? pManager->resolveGameObject( _director ) : nullptr;
        ShooterDirectorComponent* pDirector = pObject != nullptr ? pObject->getComponent<ShooterDirectorComponent>() : nullptr;
        if ( pDirector != nullptr )
            pDirector->reportPlayerDamage( amount );
        if ( _health > 0.0f )
            return;
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
            pDirector->restartRound();
        }
        resetRound();
    }

    void ShooterPlayerComponent::addWaveAmmo()
    {
        for ( WeaponState& weapon : _arrWeapon )
            weapon.addReserveAmmo( weapon.getDef()._magazineSize * 2 );
    }

    void ShooterPlayerComponent::restoreHealth( float32 amount )
    {
        _health = MathUtil::min( _maxHealth, _health + MathUtil::max( 0.0f, amount ) );
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
        return _position + float3{ 0.0f, _eyeHeight, 0.0f };
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

    void ShooterPlayerComponent::tickInput( const InputManager& input, FirstPersonCameraComponent& camera, float3& outMove, bool& outJump,
                                            bool& outSprint, bool& outTrigger, bool& outJustPressed )
    {
        // 시점은 같은 오브젝트의 1인칭 카메라가 앞 그룹에서 마우스로 돌렸다(잠금 · Esc 도 거기서).
        const FirstPersonLook& look    = camera.getLook();
        const float3           forward = look.getFlatForward();
        const float3           right   = look.getFlatRight();
        if ( input.isKeyDown( Key::W ) )
            outMove = outMove + forward;
        if ( input.isKeyDown( Key::S ) )
            outMove = outMove - forward;
        if ( input.isKeyDown( Key::D ) )
            outMove = outMove + right;
        if ( input.isKeyDown( Key::A ) )
            outMove = outMove - right;
        outJump        = input.wasKeyPressed( Key::Space );
        outSprint      = input.isKeyDown( Key::LeftShift );
        outTrigger     = input.isMouseButtonDown( MouseButton::Left );
        outJustPressed = input.wasMouseButtonPressed( MouseButton::Left );

        constexpr Key kArrWeaponKey[kWeaponCount] = { Key::Digit1, Key::Digit2, Key::Digit3 };
        for ( int32 weaponIndex = 0; weaponIndex < kWeaponCount; ++weaponIndex )
        {
            if ( input.wasKeyPressed( kArrWeaponKey[weaponIndex] ) )
                switchWeapon( weaponIndex );
        }
        const float32 wheel = input.getMouseWheel();
        if ( wheel > 0.0f )
            switchWeapon( ( _weaponIndex + kWeaponCount - 1 ) % kWeaponCount );
        else if ( wheel < 0.0f )
            switchWeapon( ( _weaponIndex + 1 ) % kWeaponCount );
        if ( input.wasKeyPressed( Key::R ) && _arrWeapon[_weaponIndex].startReload() )
            SW_LOG_INFO( "[Shooter] reloading %#", _arrWeapon[_weaponIndex].getDef()._name.c_str() );
    }

    void ShooterPlayerComponent::tickAutoAim( float32 deltaTime, const ShooterDirectorComponent& director, FirstPersonCameraComponent& camera, float3& outMove,
                                              bool& outTrigger, bool& outJustPressed )
    {
        // 가장 가까운 드론을 천천히 겨누고, 조준이 맞으면 쏜다. 아레나 가운데를 중심으로 원을 그리며 움직인다.
        const float3                    eye          = getEyePosition();
        float32                         bestDistance = MathUtil::MaxFloat;
        const ShooterDroneView*         pTarget      = nullptr;
        const vector<ShooterDroneView>& listView     = director.getDroneViews();
        const ShooterDroneView*         pView        = listView.data();
        for ( size_t viewIndex = 0; viewIndex < listView.size(); ++viewIndex )
        {
            const float32 distance = float3::getDistance( eye, pView[viewIndex]._position );
            if ( distance < bestDistance )
            {
                bestDistance = distance;
                pTarget      = pView + viewIndex;
            }
        }
        outMove = float3{ -_position._z, 0.0f, _position._x } * 0.08f - _position * 0.04f;
        if ( pTarget == nullptr )
            return;

        const FirstPersonLook& look        = camera.getLook();
        const float3           toTarget    = ( pTarget->_position - eye ).normalize();
        const float32          targetYaw   = MathUtil::atan2( toTarget._x, toTarget._z );
        const float32          targetPitch = MathUtil::asin( MathUtil::clamp( toTarget._y, -1.0f, 1.0f ) );
        float32                yawError    = targetYaw - look.getYaw();
        while ( yawError > MathUtil::Pi )
            yawError -= 2.0f * MathUtil::Pi;
        while ( yawError < -MathUtil::Pi )
            yawError += 2.0f * MathUtil::Pi;
        const float32 blend = MathUtil::min( 1.0f, deltaTime * 8.0f );
        camera.setAngles( look.getYaw() + yawError * blend, look.getPitch() + ( targetPitch - look.getPitch() ) * blend );
        const bool bAimed = MathUtil::abs( yawError ) < 3.0f * MathUtil::DegreeToRadian;
        outTrigger        = bAimed;
        outJustPressed    = bAimed;
        // 가까우면 산탄총, 멀면 소총.
        const int32 wantedWeapon = bestDistance < 8.0f ? 1 : 0;
        if ( wantedWeapon != _weaponIndex && _arrWeapon[wantedWeapon].getMagazineAmmo() + _arrWeapon[wantedWeapon].getReserveAmmo() > 0 )
            switchWeapon( wantedWeapon );
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
                _listPendingSound.push_back( ShooterPlayerComponentInternal::kSoundLand );
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
        bool bAnyHit   = false;
        bool bHitCover = false;
        for ( const GameRay& ray : shot._listRay )
        {
            bool          bHitDrone = false;
            const float32 distance  = traceShot( director, ray, weapon.getDef()._damage, bHitDrone );
            const float3  end       = ray._origin + ray._direction * distance;
            bAnyHit                 = bAnyHit || bHitDrone;
            Tracer& tracer          = _arrTracer[_nextTracer];
            tracer._from            = ray._origin + float3{ 0.0f, -0.08f, 0.0f };
            tracer._to              = end;
            tracer._remaining       = 0.06f;
            _nextTracer             = ( _nextTracer + 1 ) % kTracerCount;
            _bTracerAlive           = SW_TRUE;
            if ( distance < weapon.getDef()._range )
            {
                EffectRequest effect;
                effect._position = end;
                effect._size     = bHitDrone ? 0.18f : 0.1f;
                effect._color    = bHitDrone ? Internal::kDroneHitColor : Internal::kCoverHitColor;
                _listPendingEffect.push_back( effect );
                bHitCover = bHitCover || ( bHitDrone == false && end._y > 0.05f );
            }
        }
        // 한 발(산탄 여럿)에 소리는 하나 — 드론을 맞혔으면 쇳소리, 아니고 상자 · 벽을 맞혔으면 나무 소리.
        if ( bAnyHit )
        {
            ++_hitCount;
            _hitMarkerTimer = 0.15f;
            _listPendingSound.push_back( Internal::kSoundHitDrone );
        }
        else if ( bHitCover )
        {
            _listPendingSound.push_back( Internal::kSoundHitCover );
        }
        // 반동 — 위로 튀고 옆으로 살짝.
        const float32 kick = weapon.getDef()._recoilPitch * MathUtil::DegreeToRadian;
        camera.addRecoil( kick, kick * 0.25f * ( ( _shotCount % 2u ) == 0u ? 1.0f : -1.0f ) );
    }

    float32 ShooterPlayerComponent::traceShot( const ShooterDirectorComponent& director, const GameRay& ray, float32 damage, bool& outHitDrone )
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
        const vector<ShooterDroneView>& listView     = director.getDroneViews();
        const ShooterDroneView*         pView        = listView.data();
        const ShooterDroneView*         pNearestView = nullptr;
        for ( size_t viewIndex = 0; viewIndex < listView.size(); ++viewIndex )
        {
            float32 distance = 0.0f;
            if ( RayMath::intersectSphere( ray, pView[viewIndex]._position, pView[viewIndex]._radius, nearest, distance ) && distance < nearest )
            {
                nearest      = distance;
                pNearestView = pView + viewIndex;
            }
        }
        outHitDrone = pNearestView != nullptr;
        if ( outHitDrone )
        {
            DroneHit hit;
            hit._drone  = pNearestView->_object;
            hit._damage = damage;
            _listPendingHit.push_back( hit );
        }
        return nearest;
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

    void ShooterPlayerComponent::updateTracers( float32 deltaTime )
    {
        bool bAlive = false;
        for ( Tracer& tracer : _arrTracer )
        {
            tracer._remaining -= deltaTime;
            bAlive = bAlive || tracer._remaining > 0.0f;
        }
        _bTracerAlive = bAlive ? SW_TRUE : SW_FALSE;
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
            const float32 distance = Internal::kCrosshairDistance - 0.01f * static_cast<float32>( spriteIndex );
            const float32 size     = spriteIndex == 0 ? 0.03f : 0.05f;
            pSprite->setLocalPosition( float3{ 0.0f, 0.0f, distance } );
            pSprite->setLocalRotation( float3{ 0.0f, 0.0f, 0.0f } );
            pSprite->setLocalScale( float3{ size, size, 1.0f } );
            if ( spriteIndex == 1 && pSprite->isVisible() != ( _hitMarkerTimer > 0.0f ) )
                pSprite->setVisible( _hitMarkerTimer > 0.0f );
        }
    }

    void ShooterPlayerComponent::resetRound()
    {
        _health              = _maxHealth;
        _damageCooldown      = 0.0f;
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
        return _listPendingHit.empty() == false || _listPendingEffect.empty() == false || _listPendingSound.empty() == false || _bWeaponModelDirty == SW_TRUE ||
               _bTracerAlive == SW_TRUE;
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
        // 드론에 피해 — 드론은 다음 틱에 번쩍이고, 바닥나면 디렉터가 걷는다.
        for ( const DroneHit& hit : _listPendingHit )
        {
            GameObject*            pDroneObject = pManager->resolveGameObject( hit._drone );
            ShooterDroneComponent* pDrone       = pDroneObject != nullptr ? pDroneObject->getComponent<ShooterDroneComponent>() : nullptr;
            if ( pDrone != nullptr )
                pDrone->applyDamage( hit._damage );
        }
        _listPendingHit.clear();
        const GameObject*         pDirectorObject = pManager->resolveGameObject( _director );
        ShooterDirectorComponent* pDirector       = pDirectorObject != nullptr ? pDirectorObject->getComponent<ShooterDirectorComponent>() : nullptr;
        for ( const EffectRequest& effect : _listPendingEffect )
        {
            if ( pDirector != nullptr )
                pDirector->spawnEffect( effect._position, effect._size, effect._color, Internal::kEffectLifetime );
        }
        _listPendingEffect.clear();
        for ( const utf8* pEvent : _listPendingSound )
            (void)GameSound::postEvent( hashed_string( pEvent ) );
        _listPendingSound.clear();
        if ( _bWeaponModelDirty == SW_TRUE )
        {
            _bWeaponModelDirty    = SW_FALSE;
            MeshComponent* pModel = Internal::findNamed<MeshComponent>( *pOwner, Internal::kViewWeaponName );
            if ( pModel != nullptr )
                pModel->setMeshId( Internal::kArrWeaponModel[_weaponIndex] );
        }
        // 탄도선 — 디버그 선 큐는 게임 스레드의 것이다.
        DebugDrawQueue* pDebugDraw = _bTracerAlive == SW_TRUE ? game::getService<DebugDrawQueue>() : nullptr;
        if ( pDebugDraw != nullptr )
        {
            for ( const Tracer& tracer : _arrTracer )
            {
                if ( tracer._remaining > 0.0f )
                    pDebugDraw->drawLine( tracer._from, tracer._to, Internal::kTracerColor );
            }
        }
    }
} // namespace sw

#include "pch.h"

#include "Games/Shooter3D/ShooterArena.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Graphics/Renderer/Debug/DebugDrawQueue.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Object/Component/2D/SpriteComponent.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Framework/GameService.h"
#include "GameFramework/UI/HPBarBaseComponent.h"

namespace sw
{
    SW_LOG_CALLER( "ShooterArena" );

    namespace
    {
        struct ShooterArenaInternal
        {
            static constexpr float32 kPi                  = 3.14159265f;
            static constexpr float32 kDegToRad            = kPi / 180.0f;
            static constexpr float32 kArenaHalfSize       = 20.0f;
            static constexpr float32 kWallHeight          = 4.0f;
            static constexpr float32 kPlayerRadius        = 0.35f;
            static constexpr float32 kEyeHeight           = 1.6f;
            static constexpr float32 kWalkSpeed           = 5.5f;
            static constexpr float32 kSprintSpeed         = 8.5f;
            static constexpr float32 kJumpSpeed           = 6.0f;
            static constexpr float32 kGravity             = 18.0f;
            static constexpr float32 kMouseSensitivity    = 0.0022f; ///< 라디안 / 픽셀
            static constexpr float32 kDroneRadius         = 0.5f;
            static constexpr float32 kDroneHeight         = 1.4f;
            static constexpr float32 kDroneReach          = 1.4f; ///< 이 안이면 플레이어를 때린다
            static constexpr float32 kDroneDamage         = 8.0f;
            static constexpr float32 kDroneAttackInterval = 0.8f;
            static constexpr float32 kRegenDelay          = 4.0f;
            static constexpr float32 kRegenPerSecond      = 6.0f;
            static constexpr float32 kWaveDelay           = 3.0f;
            static constexpr float32 kCrosshairDistance   = 0.6f;

            /** @brief 엄폐물 상자(가운데 · 반 크기)입니다. 높이는 바닥(y = 0)부터. */
            struct CrateLayout
            {
                float3 _center;
                float3 _halfSize;
            };

            static constexpr CrateLayout kArrCrate[] = {
                {  { -8.0f, 1.0f, -6.0f },  { 1.2f, 1.0f, 1.2f }},
                {    { 7.0f, 1.0f, 5.0f },  { 1.5f, 1.0f, 1.0f }},
                {    { 0.0f, 1.5f, 9.0f },  { 3.0f, 1.5f, 0.6f }},
                { { -10.0f, 0.75f, 8.0f }, { 1.0f, 0.75f, 1.0f }},
                {  { 10.0f, 1.0f, -9.0f },  { 1.0f, 1.0f, 2.0f }},
                {   { 4.0f, 0.6f, -4.0f },  { 0.6f, 0.6f, 0.6f }},
                {   { -4.0f, 1.0f, 3.0f },  { 0.8f, 1.0f, 2.5f }},
                { { 12.0f, 1.25f, 12.0f }, { 1.2f, 1.25f, 1.2f }},
                {{ -13.0f, 1.0f, -12.0f },  { 1.5f, 1.0f, 1.5f }},
                {  { 0.0f, 1.0f, -12.0f },  { 4.0f, 1.0f, 0.5f }},
            };

            static constexpr const utf8* kArrWeaponId[ShooterArena::kWeaponCount] = { "rifle", "shotgun", "pistol" };

            static float3 flatten( const float3& value ) { return float3{ value._x, 0.0f, value._z }; }

            /** @brief 원(XZ) 가운데에서 가장 가까운 상자 위 점까지의 벡터입니다. */
            static float3 closestPointXz( const float3& point, const float3& boxMin, const float3& boxMax )
            {
                return float3{ MathUtil::clamp( point._x, boxMin._x, boxMax._x ), point._y, MathUtil::clamp( point._z, boxMin._z, boxMax._z ) };
            }
        };
    } // namespace

    /** @brief `-gv_shooterAutoPlay=1` — 조준 · 사격 · 이동을 AI 가 합니다(입력 없이 웨이브를 넘기는 확인). */
    SW_TEST_GLOBAL_VARIABLE_INT( gv_shooterAutoPlay, 0, "Shooter3D: 조준 · 사격도 AI 가 (1=켜기)", SW_KEEP_IN_SHIPPING );
} // namespace sw

namespace sw
{
    ShooterArena::ShooterArena()
        : _stage{}
        , _look{}
        , _arrWeapon{}
        , _listBox{}
        , _listDrone{}
        , _listEffect{}
        , _listTracer{}
        , _listSpawnPoint{}
        , _pCatalog{ nullptr }
        , _playerPosition{ 0.0f, 0.0f, -16.0f }
        , _verticalSpeed{ 0.0f }
        , _playerHealth{ kMaxPlayerHealth }
        , _damageCooldown{ 0.0f }
        , _waveTimer{ ShooterArenaInternal::kWaveDelay }
        , _hitMarkerTimer{ 0.0f }
        , _statusTimer{ 0.0f }
        , _crosshair{}
        , _hitMarker{}
        , _weaponIndex{ 0 }
        , _wave{ 0 }
        , _killCount{ 0 }
        , _shotCount{ 0 }
        , _hitCount{ 0 }
        , _bOnGround{ SW_TRUE }
        , _bMouseLocked{ SW_FALSE }
        , _bSpawned{ SW_FALSE }
    {
    }

    ShooterArena::~ShooterArena() = default;

    void ShooterArena::initialize( const WeaponCatalog* pCatalog )
    {
        _pCatalog = pCatalog;
        for ( int32 weaponIndex = 0; weaponIndex < kWeaponCount; ++weaponIndex )
        {
            const WeaponDef* pDef = pCatalog != nullptr ? pCatalog->findWeapon( hashed_string( ShooterArenaInternal::kArrWeaponId[weaponIndex] ) ) : nullptr;
            if ( pDef == nullptr && pCatalog != nullptr && pCatalog->getWeapons().empty() == false )
                pDef = &pCatalog->getWeapons().front();
            if ( pDef != nullptr )
                _arrWeapon[weaponIndex].equip( *pDef, pDef->_maxReserveAmmo / 2, 0x51ed270bu + static_cast<uint32>( weaponIndex ) );
        }
        buildLayout();
        _look.setAngles( 0.0f, 0.0f );
    }

    bool ShooterArena::spawn()
    {
        if ( _bSpawned != SW_FALSE )
            return true;
        if ( _stage.begin( "Shooter3D" ) == false )
            return false;

        (void)_stage.createPrimitiveObject( "ArenaFloor", "Plane", PrimitiveLook::makeColor( float4{ 0.45f, 0.47f, 0.50f, 1.0f } ), float3{ 0.0f },
                                            float3{ ShooterArenaInternal::kArenaHalfSize * 2.0f, 1.0f, ShooterArenaInternal::kArenaHalfSize * 2.0f } );
        for ( size_t boxIndex = 0; boxIndex < _listBox.size(); ++boxIndex )
        {
            const ArenaBox& box   = _listBox[boxIndex];
            const bool      bWall = boxIndex < 4;
            const float4    color = bWall ? float4{ 0.62f, 0.64f, 0.70f, 1.0f } : float4{ 0.72f, 0.52f, 0.30f, 1.0f };
            (void)_stage.createPrimitiveObject( bWall ? "ArenaWall" : "ArenaCrate", "Cube", PrimitiveLook::makeColor( color ), ( box._min + box._max ) * 0.5f,
                                                box._max - box._min );
        }
        (void)_stage.createSun( float3{ 0.95f, 0.7f, 0.0f }, 1.5f, ShooterArenaInternal::kArenaHalfSize * 1.2f );

        // 조준선 · 맞음 표시 — Kenney 스프라이트(CC0, credits.md)를 카메라 앞에 둔다.
        GameObjectManager* pManager      = _stage.getObjectManager();
        const utf8*        arrTexture[2] = { "game/shooter3d/textures/crosshair.dds", "game/shooter3d/textures/hitmarker.dds" };
        GameObjectHandle*  arrHandle[2]  = { &_crosshair, &_hitMarker };
        for ( int32 spriteIndex = 0; spriteIndex < 2 && pManager != nullptr; ++spriteIndex )
        {
            GameObject* pObject = pManager->createGameObject( hashed_string( spriteIndex == 0 ? "Crosshair" : "HitMarker" ) );
            if ( pObject == nullptr )
                continue;
            SpriteComponent* pSprite = pObject->addComponent<SpriteComponent>();
            if ( pSprite != nullptr )
            {
                pSprite->setTextureName( arrTexture[spriteIndex] );
                pSprite->setTint( spriteIndex == 0 ? float4{ 1.0f, 1.0f, 1.0f, 0.9f } : float4{ 1.0f, 0.35f, 0.3f, 1.0f } );
                pSprite->setVisible( spriteIndex == 0 );
            }
            _stage.adoptObject( *pObject );
            *arrHandle[spriteIndex] = pObject->getHandle();
        }

        InputManager* pInput = game::getService<InputManager>();
        if ( pInput != nullptr && gv_shooterAutoPlay == 0 )
        {
            pInput->setMouseLockMode( MouseLockMode::LockedInCenter );
            pInput->setCursorVisible( false );
            _bMouseLocked = SW_TRUE;
        }
        _bSpawned  = SW_TRUE;
        _waveTimer = ShooterArenaInternal::kWaveDelay;
        _listDrone.clear();
        _listEffect.clear();
        updateOverlay();
        SW_LOG_INFO( "[Shooter] arena is ready - WASD move, mouse look, LMB fire, R reload, 1/2/3 or wheel weapons, Space jump, Shift sprint, Esc mouse" );
        return true;
    }

    void ShooterArena::despawn()
    {
        InputManager* pInput = game::getService<InputManager>();
        if ( pInput != nullptr && _bMouseLocked != SW_FALSE )
        {
            pInput->setMouseLockMode( MouseLockMode::None );
            pInput->setCursorVisible( true );
        }
        _bMouseLocked = SW_FALSE;
        _stage.clear();
        _listDrone.clear();
        _listEffect.clear();
        _listTracer.clear();
        _bSpawned = SW_FALSE;
    }

    void ShooterArena::update( float32 deltaTime )
    {
        if ( _stage.isSceneChanged() )
        {
            _stage.forget();
            _listDrone.clear();
            _listEffect.clear();
            _bSpawned = SW_FALSE;
        }
        if ( _bSpawned == SW_FALSE && spawn() == false )
            return;
        if ( deltaTime <= 0.0f )
            return;
        const float32 step = MathUtil::min( deltaTime, 0.1f );

        for ( WeaponState& weapon : _arrWeapon )
            weapon.update( step );
        updateInput( step, game::getService<InputManager>() );
        updateDrones( step );
        updateEffects( step );

        // 체력 — 맞은 뒤 잠시 지나면 다시 찬다. 바닥나면 웨이브 1 부터 다시.
        _damageCooldown -= step;
        if ( _damageCooldown <= 0.0f )
            _playerHealth = MathUtil::min( kMaxPlayerHealth, _playerHealth + ShooterArenaInternal::kRegenPerSecond * step );

        if ( _listDrone.empty() )
        {
            _waveTimer -= step;
            if ( _waveTimer <= 0.0f )
                spawnWave();
        }
        updateOverlay();
        logStatus( step );
    }

    // ------------------------------------------------------------------------------
    // 배치
    // ------------------------------------------------------------------------------
    void ShooterArena::buildLayout()
    {
        constexpr float32 kHalf   = ShooterArenaInternal::kArenaHalfSize;
        constexpr float32 kHeight = ShooterArenaInternal::kWallHeight;
        _listBox.clear();
        _listBox.push_back( ArenaBox{
            float3{-kHalf - 1.0f,    0.0f, -kHalf - 1.0f},
            float3{ kHalf + 1.0f, kHeight,        -kHalf}
        } );
        _listBox.push_back( ArenaBox{
            float3{-kHalf - 1.0f,    0.0f,        kHalf},
            float3{ kHalf + 1.0f, kHeight, kHalf + 1.0f}
        } );
        _listBox.push_back( ArenaBox{
            float3{-kHalf - 1.0f,    0.0f, -kHalf},
            float3{       -kHalf, kHeight,  kHalf}
        } );
        _listBox.push_back( ArenaBox{
            float3{       kHalf,    0.0f, -kHalf},
            float3{kHalf + 1.0f, kHeight,  kHalf}
        } );
        for ( const ShooterArenaInternal::CrateLayout& crate : ShooterArenaInternal::kArrCrate )
            _listBox.push_back( ArenaBox{ crate._center - crate._halfSize, crate._center + crate._halfSize } );

        // 드론이 나오는 자리 — 벽 안쪽 둘레의 여덟 곳(상자와 겹치지 않는다).
        _listSpawnPoint.clear();
        for ( int32 pointIndex = 0; pointIndex < 8; ++pointIndex )
        {
            const float32 angle = static_cast<float32>( pointIndex ) * ShooterArenaInternal::kPi * 0.25f + 0.2f;
            float3        point{ MathUtil::sin( angle ) * ( kHalf - 2.0f ), 0.0f, MathUtil::cos( angle ) * ( kHalf - 2.0f ) };
            point = resolveCircle( point, ShooterArenaInternal::kDroneRadius + 0.2f );
            _listSpawnPoint.push_back( point );
        }
    }

    // ------------------------------------------------------------------------------
    // 플레이어
    // ------------------------------------------------------------------------------
    void ShooterArena::updateInput( float32 deltaTime, const InputManager* pInput )
    {
        float3 move{ 0.0f, 0.0f, 0.0f };
        bool   bTrigger     = false;
        bool   bJustPressed = false;
        bool   bJump        = false;
        bool   bSprint      = false;

        if ( gv_shooterAutoPlay != 0 || pInput == nullptr )
            updateAutoAim( deltaTime, move, bTrigger, bJustPressed );
        else
        {
            if ( pInput->wasKeyPressed( Key::Escape ) )
            {
                _bMouseLocked               = _bMouseLocked != SW_FALSE ? SW_FALSE : SW_TRUE;
                InputManager* pMutableInput = game::getService<InputManager>();
                if ( pMutableInput != nullptr )
                {
                    pMutableInput->setMouseLockMode( _bMouseLocked != SW_FALSE ? MouseLockMode::LockedInCenter : MouseLockMode::None );
                    pMutableInput->setCursorVisible( _bMouseLocked == SW_FALSE );
                }
            }
            if ( _bMouseLocked != SW_FALSE )
            {
                const int2 mouseDelta = pInput->getMouseDelta();
                _look.addMouseDelta( static_cast<float32>( mouseDelta._x ), static_cast<float32>( mouseDelta._y ), ShooterArenaInternal::kMouseSensitivity );
            }
            const float3 forward = _look.getFlatForward();
            const float3 right   = _look.getFlatRight();
            if ( pInput->isKeyDown( Key::W ) )
                move = move + forward;
            if ( pInput->isKeyDown( Key::S ) )
                move = move - forward;
            if ( pInput->isKeyDown( Key::D ) )
                move = move + right;
            if ( pInput->isKeyDown( Key::A ) )
                move = move - right;
            bJump        = pInput->wasKeyPressed( Key::Space );
            bSprint      = pInput->isKeyDown( Key::LeftShift );
            bTrigger     = pInput->isMouseButtonDown( MouseButton::Left );
            bJustPressed = pInput->wasMouseButtonPressed( MouseButton::Left );

            constexpr Key kArrWeaponKey[kWeaponCount] = { Key::Digit1, Key::Digit2, Key::Digit3 };
            for ( int32 weaponIndex = 0; weaponIndex < kWeaponCount; ++weaponIndex )
            {
                if ( pInput->wasKeyPressed( kArrWeaponKey[weaponIndex] ) )
                    switchWeapon( weaponIndex );
            }
            const float32 wheel = pInput->getMouseWheel();
            if ( wheel > 0.0f )
                switchWeapon( ( _weaponIndex + kWeaponCount - 1 ) % kWeaponCount );
            else if ( wheel < 0.0f )
                switchWeapon( ( _weaponIndex + 1 ) % kWeaponCount );
            if ( pInput->wasKeyPressed( Key::R ) && _arrWeapon[_weaponIndex].startReload() )
                SW_LOG_INFO( "[Shooter] reloading %#", _arrWeapon[_weaponIndex].getDef()._name.c_str() );
        }

        movePlayer( move, bJump, bSprint, deltaTime );
        if ( bTrigger || bJustPressed )
            fireWeapon( bTrigger, bJustPressed );
    }

    void ShooterArena::updateAutoAim( float32 deltaTime, float3& outMove, bool& outTrigger, bool& outJustPressed )
    {
        // 가장 가까운 드론을 천천히 겨누고, 조준이 맞으면 쏜다. 아레나 가운데를 중심으로 원을 그리며 움직인다.
        const float3      eye          = getEyePosition();
        float32           bestDistance = MathUtil::MaxFloat;
        const ArenaDrone* pTarget      = nullptr;
        for ( const ArenaDrone& drone : _listDrone )
        {
            const float32 distance = float3::getDistance( eye, drone._position );
            if ( distance < bestDistance )
            {
                bestDistance = distance;
                pTarget      = &drone;
            }
        }
        const float3 orbit = float3{ -_playerPosition._z, 0.0f, _playerPosition._x } * 0.08f - _playerPosition * 0.04f;
        outMove            = orbit;
        if ( pTarget == nullptr )
            return;

        const float3  toTarget    = ( pTarget->_position - eye ).normalize();
        const float32 targetYaw   = MathUtil::atan2( toTarget._x, toTarget._z );
        const float32 targetPitch = MathUtil::asin( MathUtil::clamp( toTarget._y, -1.0f, 1.0f ) );
        float32       yawError    = targetYaw - _look.getYaw();
        while ( yawError > ShooterArenaInternal::kPi )
            yawError -= 2.0f * ShooterArenaInternal::kPi;
        while ( yawError < -ShooterArenaInternal::kPi )
            yawError += 2.0f * ShooterArenaInternal::kPi;
        const float32 blend = MathUtil::min( 1.0f, deltaTime * 8.0f );
        _look.setAngles( _look.getYaw() + yawError * blend, _look.getPitch() + ( targetPitch - _look.getPitch() ) * blend );
        const bool bAimed = MathUtil::abs( yawError ) < 3.0f * ShooterArenaInternal::kDegToRad;
        outTrigger        = bAimed;
        outJustPressed    = bAimed;
        // 가까우면 산탄총, 멀면 소총.
        const int32 wantedWeapon = bestDistance < 8.0f ? 1 : 0;
        if ( wantedWeapon != _weaponIndex && _arrWeapon[wantedWeapon].getMagazineAmmo() + _arrWeapon[wantedWeapon].getReserveAmmo() > 0 )
            switchWeapon( wantedWeapon );
    }

    void ShooterArena::movePlayer( const float3& wishDirection, bool bJump, bool bSprint, float32 deltaTime )
    {
        float3        move   = ShooterArenaInternal::flatten( wishDirection );
        const float32 length = move.getLength();
        if ( length > 1.0f )
            move = move * ( 1.0f / length );
        const float32 speed = bSprint ? ShooterArenaInternal::kSprintSpeed : ShooterArenaInternal::kWalkSpeed;
        float3        next  = _playerPosition + move * ( speed * deltaTime );

        if ( bJump && _bOnGround != SW_FALSE )
        {
            _verticalSpeed = ShooterArenaInternal::kJumpSpeed;
            _bOnGround     = SW_FALSE;
        }
        _verticalSpeed -= ShooterArenaInternal::kGravity * deltaTime;
        next._y += _verticalSpeed * deltaTime;
        if ( next._y <= 0.0f )
        {
            next._y        = 0.0f;
            _verticalSpeed = 0.0f;
            _bOnGround     = SW_TRUE;
        }
        _playerPosition = resolveCircle( next, ShooterArenaInternal::kPlayerRadius );
    }

    void ShooterArena::fireWeapon( bool bTriggerHeld, bool bJustPressed )
    {
        (void)bTriggerHeld;
        WeaponState& weapon = _arrWeapon[_weaponIndex];
        GameRay      aim;
        aim._origin    = getEyePosition();
        aim._direction = _look.getForward();
        WeaponShot             shot;
        const WeaponFireResult result = weapon.pullTrigger( aim, bJustPressed, shot );
        if ( result == WeaponFireResult::EmptyMagazine )
            SW_LOG_INFO( "[Shooter] %# empty - reloading (%# in reserve)", weapon.getDef()._name.c_str(), weapon.getReserveAmmo() );
        else if ( result == WeaponFireResult::OutOfAmmo && bJustPressed )
            SW_LOG_INFO( "[Shooter] %# is out of ammo - switch weapons", weapon.getDef()._name.c_str() );
        if ( result != WeaponFireResult::Fired )
            return;

        ++_shotCount;
        bool bAnyHit = false;
        for ( const GameRay& ray : shot._listRay )
        {
            bool          bHitDrone = false;
            const float32 distance  = traceShot( ray, weapon.getDef()._damage, bHitDrone );
            const float3  end       = ray._origin + ray._direction * distance;
            bAnyHit                 = bAnyHit || bHitDrone;
            _listTracer.push_back( ArenaTracer{
                ray._origin + float3{ 0.0f, -0.08f, 0.0f },
                end, 0.06f
            } );
            if ( distance < weapon.getDef()._range )
                spawnEffect( end, bHitDrone ? 0.18f : 0.1f, bHitDrone ? float4{ 1.0f, 0.4f, 0.2f, 1.0f } : float4{ 0.9f, 0.85f, 0.6f, 1.0f }, 0.12f );
        }
        if ( bAnyHit )
        {
            ++_hitCount;
            _hitMarkerTimer = 0.15f;
        }
        // 반동 — 위로 튀고 옆으로 살짝.
        const float32 kick = weapon.getDef()._recoilPitch * ShooterArenaInternal::kDegToRad;
        _look.addRecoil( kick, kick * 0.25f * ( ( _shotCount % 2u ) == 0u ? 1.0f : -1.0f ) );
    }

    float32 ShooterArena::traceShot( const GameRay& ray, float32 damage, bool& outHitDrone )
    {
        const float32 range        = _arrWeapon[_weaponIndex].getDef()._range;
        float32       nearest      = range;
        int32         nearestDrone = -1;
        for ( const ArenaBox& box : _listBox )
        {
            float32 distance = 0.0f;
            if ( RayMath::intersectAabb( ray, box._min, box._max, nearest, distance ) && distance < nearest )
                nearest = distance;
        }
        // 바닥
        if ( ray._direction._y < -1.0e-4f )
        {
            const float32 floorDistance = -ray._origin._y / ray._direction._y;
            nearest                     = MathUtil::min( nearest, floorDistance );
        }
        for ( size_t droneIndex = 0; droneIndex < _listDrone.size(); ++droneIndex )
        {
            float32 distance = 0.0f;
            if ( RayMath::intersectSphere( ray, _listDrone[droneIndex]._position, ShooterArenaInternal::kDroneRadius, nearest, distance ) && distance < nearest )
            {
                nearest      = distance;
                nearestDrone = static_cast<int32>( droneIndex );
            }
        }
        outHitDrone = nearestDrone >= 0;
        if ( outHitDrone )
        {
            ArenaDrone& drone = _listDrone[static_cast<size_t>( nearestDrone )];
            drone._health -= damage;
            drone._flashTimer = 0.08f;
        }
        return nearest;
    }

    void ShooterArena::switchWeapon( int32 weaponIndex )
    {
        if ( weaponIndex == _weaponIndex || weaponIndex < 0 || weaponIndex >= kWeaponCount )
            return;
        _weaponIndex = weaponIndex;
        SW_LOG_INFO( "[Shooter] %# - %#/%#", _arrWeapon[weaponIndex].getDef()._name.c_str(), _arrWeapon[weaponIndex].getMagazineAmmo(),
                     _arrWeapon[weaponIndex].getReserveAmmo() );
    }

    void ShooterArena::damagePlayer( float32 amount )
    {
        _playerHealth -= amount;
        _damageCooldown = ShooterArenaInternal::kRegenDelay;
        if ( _playerHealth > 0.0f )
            return;
        SW_LOG_INFO( "[Shooter] you were overrun on wave %# after %# kills - starting over", _wave, _killCount );
        for ( ArenaDrone& drone : _listDrone )
            _stage.destroyObject( drone._object );
        _listDrone.clear();
        _playerHealth   = kMaxPlayerHealth;
        _playerPosition = float3{ 0.0f, 0.0f, -16.0f };
        _look.setAngles( 0.0f, 0.0f );
        _wave      = 0;
        _killCount = 0;
        _waveTimer = ShooterArenaInternal::kWaveDelay;
        for ( WeaponState& weapon : _arrWeapon )
            weapon.addReserveAmmo( weapon.getDef()._maxReserveAmmo );
    }

    float3 ShooterArena::resolveCircle( const float3& position, float32 radius ) const
    {
        float3 resolved = position;
        // 두 번 — 모서리에서 두 상자에 동시에 닿아도 빠져나온다.
        for ( int32 iteration = 0; iteration < 2; ++iteration )
        {
            for ( const ArenaBox& box : _listBox )
            {
                if ( resolved._y >= box._max._y )
                    continue; // 상자 위 — 뛰어 오른 것은 막지 않는다(높이가 낮은 상자)
                const float3  closest = ShooterArenaInternal::closestPointXz( resolved, box._min, box._max );
                float3        offset  = float3{ resolved._x - closest._x, 0.0f, resolved._z - closest._z };
                const float32 length  = offset.getLength();
                if ( length >= radius )
                    continue;
                if ( length < 1.0e-5f )
                {
                    // 가운데가 상자 안 — 가장 얕은 면으로 민다.
                    const float32 arrPush[4] = { resolved._x - box._min._x, box._max._x - resolved._x, resolved._z - box._min._z, box._max._z - resolved._z };
                    int32         shallow    = 0;
                    for ( int32 sideIndex = 1; sideIndex < 4; ++sideIndex )
                        shallow = arrPush[sideIndex] < arrPush[shallow] ? sideIndex : shallow;
                    if ( shallow == 0 )
                        resolved._x = box._min._x - radius;
                    else if ( shallow == 1 )
                        resolved._x = box._max._x + radius;
                    else if ( shallow == 2 )
                        resolved._z = box._min._z - radius;
                    else
                        resolved._z = box._max._z + radius;
                    continue;
                }
                offset   = offset * ( radius / length );
                resolved = float3{ closest._x + offset._x, resolved._y, closest._z + offset._z };
            }
        }
        return resolved;
    }

    float3 ShooterArena::getEyePosition() const
    {
        return _playerPosition + float3{ 0.0f, ShooterArenaInternal::kEyeHeight, 0.0f };
    }

    // ------------------------------------------------------------------------------
    // 드론 · 효과
    // ------------------------------------------------------------------------------
    void ShooterArena::spawnWave()
    {
        ++_wave;
        const uint32  droneCount = 3u + 2u * _wave;
        const float32 health     = 30.0f + 10.0f * static_cast<float32>( _wave );
        const float32 speed      = MathUtil::min( 6.5f, 2.6f + 0.3f * static_cast<float32>( _wave ) );
        for ( uint32 droneIndex = 0; droneIndex < droneCount; ++droneIndex )
        {
            const float3 base     = _listSpawnPoint[droneIndex % _listSpawnPoint.size()];
            const float3 position = base + float3{ 0.0f, ShooterArenaInternal::kDroneHeight, 0.0f } +
                                    float3{ static_cast<float32>( droneIndex / _listSpawnPoint.size() ) * 1.2f, 0.0f, 0.0f };
            GameObject* pObject =
                _stage.createPrimitiveObject( "Drone", "Sphere", PrimitiveLook::makeColor( float4{ 0.85f, 0.15f, 0.2f, 1.0f } ), position,
                                              float3{ ShooterArenaInternal::kDroneRadius * 2.0f } );
            if ( pObject == nullptr )
                continue;
            (void)pObject->addComponent<HPBarBaseComponent>();
            ArenaDrone drone;
            drone._object    = pObject->getHandle();
            drone._position  = position;
            drone._health    = health;
            drone._maxHealth = health;
            drone._speed     = speed;
            drone._bobPhase  = static_cast<float32>( droneIndex ) * 1.3f;
            _listDrone.push_back( drone );
        }
        // 웨이브마다 탄을 조금 채운다.
        for ( WeaponState& weapon : _arrWeapon )
            weapon.addReserveAmmo( weapon.getDef()._magazineSize * 2 );
        _waveTimer = ShooterArenaInternal::kWaveDelay;
        SW_LOG_INFO( "[Shooter] wave %# - %# drones (%# HP, %# m/s)", _wave, droneCount, static_cast<int32>( health ), speed );
    }

    void ShooterArena::updateDrones( float32 deltaTime )
    {
        const float3 target = getEyePosition() + float3{ 0.0f, -0.3f, 0.0f };
        for ( size_t droneIndex = 0; droneIndex < _listDrone.size(); )
        {
            ArenaDrone& drone = _listDrone[droneIndex];
            if ( drone._health <= 0.0f )
            {
                ++_killCount;
                spawnEffect( drone._position, 1.4f, float4{ 1.0f, 0.6f, 0.15f, 1.0f }, 0.2f );
                _stage.destroyObject( drone._object );
                _listDrone[droneIndex] = _listDrone.back();
                _listDrone.pop_back();
                continue;
            }

            // 다가온다 — 이웃 드론과 떨어지고 상자를 돌아간다(밀려난다).
            float3        toTarget = target - drone._position;
            const float32 distance = toTarget.getLength();
            float3        steer    = distance > 1.0e-4f ? toTarget * ( 1.0f / distance ) : float3{ 0.0f };
            for ( const ArenaDrone& other : _listDrone )
            {
                const float3  away       = drone._position - other._position;
                const float32 awayLength = away.getLength();
                if ( &other != &drone && awayLength < 1.4f && awayLength > 1.0e-4f )
                    steer = steer + away * ( 0.8f / awayLength );
            }
            drone._bobPhase += deltaTime * 3.0f;
            if ( distance > ShooterArenaInternal::kDroneReach * 0.8f )
            {
                float3 next           = drone._position + ShooterArenaInternal::flatten( steer ) * ( drone._speed * deltaTime );
                next._y               = ShooterArenaInternal::kDroneHeight + MathUtil::sin( drone._bobPhase ) * 0.25f;
                const float3 resolved = resolveCircle( float3{ next._x, 0.0f, next._z }, ShooterArenaInternal::kDroneRadius );
                drone._position       = float3{ resolved._x, next._y, resolved._z };
            }
            drone._attackCooldown -= deltaTime;
            if ( distance < ShooterArenaInternal::kDroneReach && drone._attackCooldown <= 0.0f )
            {
                drone._attackCooldown = ShooterArenaInternal::kDroneAttackInterval;
                damagePlayer( ShooterArenaInternal::kDroneDamage );
                if ( _listDrone.empty() )
                    return; // 쓰러져 판이 다시 시작됐다
            }

            drone._flashTimer -= deltaTime;
            GameObject* pObject = _stage.resolveObject( drone._object );
            if ( pObject != nullptr )
            {
                if ( MeshComponent* pMesh = pObject->getComponent<MeshComponent>() )
                {
                    pMesh->setLocalPosition( drone._position );
                    const uint8 bFlashing = drone._flashTimer > 0.0f ? SW_TRUE : SW_FALSE;
                    if ( bFlashing != drone._bFlashing )
                    {
                        drone._bFlashing = bFlashing;
                        _stage.setLook( *pMesh, PrimitiveLook::makeColor( bFlashing != SW_FALSE ? float4{ 1.0f, 1.0f, 1.0f, 1.0f } : float4{ 0.85f, 0.15f, 0.2f, 1.0f } ) );
                    }
                }
                if ( HPBarBaseComponent* pBar = pObject->getComponent<HPBarBaseComponent>() )
                    pBar->setTargetRatio( MathUtil::max( 0.0f, drone._health / drone._maxHealth ) );
            }
            ++droneIndex;
        }
    }

    void ShooterArena::spawnEffect( const float3& position, float32 size, const float4& color, float32 lifetime )
    {
        GameObject* pObject = _stage.createPrimitiveObject( "ShotEffect", "Sphere", PrimitiveLook::makeColor( color ), position, float3{ size } );
        if ( pObject != nullptr )
            _listEffect.push_back( ArenaEffect{ pObject->getHandle(), lifetime } );
    }

    void ShooterArena::updateEffects( float32 deltaTime )
    {
        for ( size_t effectIndex = 0; effectIndex < _listEffect.size(); )
        {
            _listEffect[effectIndex]._remaining -= deltaTime;
            if ( _listEffect[effectIndex]._remaining > 0.0f )
            {
                ++effectIndex;
                continue;
            }
            _stage.destroyObject( _listEffect[effectIndex]._object );
            _listEffect[effectIndex] = _listEffect.back();
            _listEffect.pop_back();
        }

        DebugDrawQueue* pDebugDraw = game::getService<DebugDrawQueue>();
        for ( size_t tracerIndex = 0; tracerIndex < _listTracer.size(); )
        {
            ArenaTracer& tracer = _listTracer[tracerIndex];
            tracer._remaining -= deltaTime;
            if ( tracer._remaining <= 0.0f )
            {
                _listTracer[tracerIndex] = _listTracer.back();
                _listTracer.pop_back();
                continue;
            }
            if ( pDebugDraw != nullptr )
                pDebugDraw->drawLine( tracer._from, tracer._to, float4{ 1.0f, 0.9f, 0.5f, 1.0f } );
            ++tracerIndex;
        }
        _hitMarkerTimer -= deltaTime;
    }

    void ShooterArena::updateOverlay()
    {
        // 1인칭 카메라 — 오일러 피치는 아래가 + 라서 시점의 피치(위가 +)를 뒤집는다.
        const float3 eye   = getEyePosition();
        const float3 euler = _look.computeCameraEuler();
        _stage.placeCamerasWithRotation( eye, euler, 75.0f * ShooterArenaInternal::kDegToRad, 120.0f );

        const float3 forward      = _look.getForward();
        GameObject*  arrObject[2] = { _stage.resolveObject( _crosshair ), _stage.resolveObject( _hitMarker ) };
        for ( int32 spriteIndex = 0; spriteIndex < 2; ++spriteIndex )
        {
            SpriteComponent* pSprite = arrObject[spriteIndex] != nullptr ? arrObject[spriteIndex]->getComponent<SpriteComponent>() : nullptr;
            if ( pSprite == nullptr )
                continue;
            const float32 distance = ShooterArenaInternal::kCrosshairDistance - 0.01f * static_cast<float32>( spriteIndex );
            const float32 size     = spriteIndex == 0 ? 0.03f : 0.05f;
            pSprite->setLocalPosition( eye + forward * distance );
            pSprite->setLocalRotation( euler );
            pSprite->setLocalScale( float3{ size, size, 1.0f } );
            if ( spriteIndex == 1 )
                pSprite->setVisible( _hitMarkerTimer > 0.0f );
        }
    }

    void ShooterArena::logStatus( float32 deltaTime )
    {
        _statusTimer += deltaTime;
        if ( _statusTimer < 5.0f )
            return;
        _statusTimer                                = 0.0f;
        [[maybe_unused]] const WeaponState& weapon  = _arrWeapon[_weaponIndex];
        [[maybe_unused]] const uint32       percent = _shotCount > 0u ? _hitCount * 100u / _shotCount : 0u;
        SW_LOG_INFO( "[Shooter] wave %# · kills %# · HP %# · %# %#/%# · drones %# · accuracy %#%%", _wave, _killCount, static_cast<int32>( _playerHealth ),
                     weapon.getDef()._name.c_str(), weapon.getMagazineAmmo(), weapon.getReserveAmmo(), static_cast<uint32>( _listDrone.size() ), percent );
    }
} // namespace sw

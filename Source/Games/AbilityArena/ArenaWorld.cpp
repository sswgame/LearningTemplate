#include "pch.h"

#include "Games/AbilityArena/ArenaWorld.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/Mesh/MeshUtil.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Object/Component/3D/DirectionalLightComponent.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/GameObject/CameraRegistry.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"

#include "GameFramework/Ability/AbilityCatalog.h"
#include "GameFramework/Ability/AbilitySystemComponent.h"
#include "GameFramework/Ability/CombatAttributeSet.h"
#include "GameFramework/Framework/GameService.h"
#include "GameFramework/Framework/GameSound.h"
#include "GameFramework/UI/HealthBarComponent.h"

namespace sw
{
    SW_LOG_CALLER( "ArenaWorld" );

    namespace
    {
        struct ArenaWorldInternal
        {
            static constexpr float32 kArenaHalfSize      = 14.0f; ///< 아레나 한 변의 절반(m) — 유닛은 이 안에 갇힌다
            static constexpr float32 kWaveRadius         = 11.0f; ///< 적이 서는 원의 반지름(m)
            static constexpr float32 kUnitRadius         = 0.5f;  ///< 유닛 몸 반지름(m) — 투사체 맞음 · 서로 밀어내기
            static constexpr float32 kProjectileRadius   = 0.25f;
            static constexpr float32 kDeathLinger        = 0.8f;  ///< 쓰러진 뒤 걷기까지(s)
            static constexpr float32 kPlayerRespawnDelay = 2.5f;  ///< 플레이어가 다시 서기까지(s)
            static constexpr float32 kStatusLogInterval  = 5.0f;  ///< 상태 로그 간격(s)
            static constexpr float32 kGruntReach         = 1.4f;  ///< 근접 적이 멈추고 때리는 거리(m)
            static constexpr float32 kCasterPreferredMin = 5.0f;  ///< 원거리 적이 물러나는 거리(m)
            static constexpr float32 kCasterPreferredMax = 9.0f;  ///< 원거리 적이 다가오는 거리(m)
            static constexpr float32 kCasterFireRange    = 12.0f; ///< 원거리 적이 쏘는 거리(m)
            static constexpr float32 kPi                 = 3.14159265f;
            /**
             * @brief Kenney Mini Dungeon 모델의 배율입니다. 키트는 바닥 한 칸이 1, 사람 키가 0.78 이라 2 배면 키 1.56 m · 칸 2 m 가 된다
             *        (유닛 몸 반지름 0.5 m 와 맞고, 아레나 28 m 가 바닥 14 칸이다).
             */
            static constexpr float32     kModelScale     = 2.0f;
            static constexpr float32     kTileSize       = 2.0f; ///< 바닥 · 벽 한 칸(m) = 키트 1 × kModelScale
            static constexpr float32     kProjectileLift = 0.9f; ///< 투사체를 그리는 높이(m) — 맞음 판정은 발 높이 그대로, 모습만 가슴께로
            static constexpr const utf8* kPaletteTexture = "game/abilityarena/textures/dungeon_colormap.dds";

            static string makeModelPath( const utf8* pName ) { return string( "game/abilityarena/models/" ) + pName + ".mesh"; }

            /** @brief 키 하나 → 입력 번호입니다. 같은 번호에 키가 둘이면 어느 쪽이든 누르면 눌림이다. */
            struct KeyBinding
            {
                Key   _key;
                int32 _inputId;
            };

            static constexpr KeyBinding kArrKeyBinding[] = {
                {        Key::J,    ArenaWorld::kInputMelee},
                {    Key::Space,    ArenaWorld::kInputMelee},
                {        Key::K, ArenaWorld::kInputFireball},
                {   Key::Digit2, ArenaWorld::kInputFireball},
                {        Key::L,     ArenaWorld::kInputHeal},
                {   Key::Digit3,     ArenaWorld::kInputHeal},
                {Key::LeftShift,     ArenaWorld::kInputDash},
                {   Key::Digit4,     ArenaWorld::kInputDash},
            };

            /** @brief XZ 평면 성분만 남긴 단위 벡터입니다. 길이가 거의 0 이면 @p fallback 입니다. */
            static float3 flattenDirection( const float3& direction, const float3& fallback )
            {
                const float3  flat   = float3{ direction._x, 0.0f, direction._z };
                const float32 length = flat.getLength();
                if ( length < 1.0e-4f )
                    return fallback;
                return float3{ flat._x / length, 0.0f, flat._z / length };
            }

            /** @brief 유닛 색을 머티리얼 인스턴스로 만듭니다. @p bPalette 면 키트 팔레트 텍스처를 함께 건다(모델용). */
            static shared_ptr<MaterialInstance> createTint( Material* pMaterial, const float4& color, bool bPalette )
            {
                if ( pMaterial == nullptr )
                    return nullptr;
                shared_ptr<MaterialInstance> pInstance = MaterialInstance::create( pMaterial );
                if ( pInstance == nullptr )
                    return nullptr;
                pInstance->setVectorParameter( hashed_string( "color" ), color );
                if ( bPalette )
                    pInstance->setTextureParameter( hashed_string( "albedoMap" ), kPaletteTexture );
                return pInstance;
            }

            /** @brief 종류마다의 어빌리티 세트 id 입니다. */
            static hashed_string findAbilitySetId( ArenaUnitKind kind )
            {
                switch ( kind )
                {
                    case ArenaUnitKind::Player:
                        return hashed_string( "Player" );
                    case ArenaUnitKind::Grunt:
                        return hashed_string( "Grunt" );
                    case ArenaUnitKind::Caster:
                        return hashed_string( "Caster" );
                }
                return hashed_string{};
            }
        };
    } // namespace

    /**
     * @brief `-gv_arenaAutoPlay=1` — 플레이어도 AI 가 움직입니다(입력 없이 한 판을 끝까지 돌리는 확인 · 화면 녹화용).
     * @details 배포본 실행 파일로도 돌릴 수 있게 남긴다: `App -gv_arenaAutoPlay=1 -gv_profileFrames=1200`.
     */
    SW_TEST_GLOBAL_VARIABLE_INT( gv_arenaAutoPlay, 0, "AbilityArena: 플레이어도 AI 가 조종 (1=켜기)", SW_KEEP_IN_SHIPPING );
} // namespace sw

namespace sw
{
    ArenaWorld::ArenaWorld()
        : _listUnit{}
        , _listProjectile{}
        , _listStageObject{}
        , _pProjectileMesh{ nullptr }
        , _pPlayerMaterial{ nullptr }
        , _pGruntMaterial{ nullptr }
        , _pCasterMaterial{ nullptr }
        , _pProjectileMaterial{ nullptr }
        , _pStageMaterial{ nullptr }
        , _pCatalog{ nullptr }
        , _sceneGeneration{ 0 }
        , _playerRespawnTimer{ -1.0f }
        , _statusLogTimer{ 0.0f }
        , _wave{ 0 }
        , _killCount{ 0 }
        , _bSpawned{ SW_FALSE }
    {
    }

    ArenaWorld::~ArenaWorld() = default;

    bool ArenaWorld::spawn( const AbilityCatalog* pCatalog )
    {
        if ( _bSpawned == SW_TRUE )
            return true;

        SceneManager* pSceneManager = game::getService<SceneManager>();
        if ( pSceneManager == nullptr )
            return false;
        Scene* pScene = pSceneManager->getActiveScene();
        if ( pScene == nullptr )
            pScene = pSceneManager->createEmptyActiveScene( "AbilityArena" );
        if ( pScene == nullptr )
        {
            SW_LOG_WARNING( "[Arena] could not get an active scene" );
            return false;
        }
        GameObjectManager* pManager = pScene->getObjectManager();
        if ( pManager == nullptr || ensureRenderAssets( *pScene ) == false )
            return false;

        (void)pScene->ensureDefaultCameras(); // 카메라가 이미 있으면 그대로 — 매 프레임 플레이어를 따라 옮긴다
        _pCatalog        = pCatalog;
        _sceneGeneration = pSceneManager->getSceneGeneration();
        _bSpawned        = SW_TRUE;
        _wave            = 0;
        _killCount       = 0;

        spawnStage( *pManager );
        (void)spawnUnit( ArenaUnitKind::Player, float3{ 0.0f, 0.0f, 0.0f }, 1 ); // 실패는 안에서 알린다 — 다음 프레임에 다시 세운다
        spawnWave();
        updateCamera();
        SW_LOG_INFO( "[Arena] arena is ready - WASD move, J/Space melee, K/2 fireball, L/3 heal, LeftShift/4 dash" );
        return true;
    }

    void ArenaWorld::despawn()
    {
        GameObjectManager* pManager = findObjectManager();
        if ( pManager != nullptr )
        {
            for ( const ArenaUnit& unit : _listUnit )
            {
                if ( GameObject* pObject = pManager->resolveGameObject( unit._object ) )
                    pManager->destroyObject( pObject );
            }
            for ( const ArenaProjectile& projectile : _listProjectile )
            {
                if ( GameObject* pObject = pManager->resolveGameObject( projectile._object ) )
                    pManager->destroyObject( pObject );
            }
            for ( const GameObjectHandle& handle : _listStageObject )
            {
                if ( GameObject* pObject = pManager->resolveGameObject( handle ) )
                    pManager->destroyObject( pObject );
            }
        }
        _listUnit.clear();
        _listProjectile.clear();
        _listStageObject.clear();
        _playerRespawnTimer = -1.0f;
        _bSpawned           = SW_FALSE;
    }

    void ArenaWorld::update( float32 deltaTime )
    {
        SceneManager* pSceneManager = game::getService<SceneManager>();
        if ( pSceneManager == nullptr )
            return;

        // 활성 씬이 바뀌었으면(에디터가 다른 씬을 열었다) 들고 있던 핸들은 옛 씬의 것이다 — 새 씬에 다시 세운다.
        if ( _bSpawned == SW_TRUE && _sceneGeneration != pSceneManager->getSceneGeneration() )
        {
            _listUnit.clear();
            _listProjectile.clear();
            _listStageObject.clear();
            _bSpawned = SW_FALSE;
        }
        if ( _bSpawned == SW_FALSE && spawn( _pCatalog ) == false )
            return;
        if ( deltaTime <= 0.0f )
            return;

        updatePlayer( deltaTime, game::getService<InputManager>() );
        updateEnemies( deltaTime );
        updateProjectiles( deltaTime );
        updateDeaths( deltaTime );
        updateUnitFacing();
        updateCamera();
        logStatus( deltaTime );
    }

    AbilitySystemComponent* ArenaWorld::findNearestHostile( const AbilitySystemComponent& from, float32 maxRange ) const
    {
        const ArenaUnit*     pFromUnit = findUnit( from );
        const MeshComponent* pFromMesh = pFromUnit != nullptr ? findMesh( pFromUnit->_object ) : nullptr;
        if ( pFromMesh == nullptr )
            return nullptr;

        const float3            origin          = pFromMesh->getWorldPosition();
        AbilitySystemComponent* pNearest        = nullptr;
        float32                 nearestDistance = maxRange;
        for ( const ArenaUnit& unit : _listUnit )
        {
            AbilitySystemComponent* pCandidate = findAbilitySystem( unit._object );
            const MeshComponent*    pMesh      = findMesh( unit._object );
            const bool              bSkip      = pCandidate == nullptr || pMesh == nullptr || pCandidate == &from || unit._deathTimer >= 0.0f ||
                               pCandidate->hasMatchingTag( CombatAttributeSet::getDeadTag() ) || isHostile( from, *pCandidate ) == false;
            if ( bSkip )
                continue;
            const float32 distance = float3::getDistance( origin, pMesh->getWorldPosition() );
            if ( distance <= nearestDistance )
            {
                nearestDistance = distance;
                pNearest        = pCandidate;
            }
        }
        return pNearest;
    }

    float3 ArenaWorld::getFacing( const AbilitySystemComponent& unit ) const
    {
        const ArenaUnit* pUnit = findUnit( unit );
        return pUnit != nullptr ? pUnit->_facing : float3{ 0.0f, 0.0f, 1.0f };
    }

    void ArenaWorld::launchProjectile( const AbilitySystemComponent& from, const GameplayEffectSpec& spec, const GameplayEffectSpec& extraSpec, float32 speed,
                                       float32 range )
    {
        const ArenaUnit*     pUnit    = findUnit( from );
        const MeshComponent* pMesh    = pUnit != nullptr ? findMesh( pUnit->_object ) : nullptr;
        GameObjectManager*   pManager = findObjectManager();
        if ( pMesh == nullptr || pManager == nullptr || _pProjectileMesh == nullptr )
            return;

        ArenaProjectile projectile;
        projectile._spec           = spec;
        projectile._extraSpec      = extraSpec;
        projectile._position       = pMesh->getWorldPosition() + pUnit->_facing * ( ArenaWorldInternal::kUnitRadius + ArenaWorldInternal::kProjectileRadius );
        projectile._velocity       = pUnit->_facing * speed;
        projectile._remainingRange = range;
        projectile._bFromPlayer    = from.hasMatchingTag( "Team.Player"_tag ) ? SW_TRUE : SW_FALSE;

        // 어빌리티는 틱 안(이벤트로 발동)에서도 쏠 수 있다 — 틱 중이면 오브젝트를 틱 직후에 만든다.
        pManager->executeOrDeferPostTick( SW_DELEGATE_LAMBDA( GameObjectManager::PostTickDelegate, [this, pManager, projectile]()
        {
            ArenaProjectile spawned = projectile;
            GameObject*     pObject = createMeshObject( *pManager, "ArenaProjectile", _pProjectileMesh, _pProjectileMaterial,
                                                        spawned._position + float3{ 0.0f, ArenaWorldInternal::kProjectileLift, 0.0f },
                                                        float3{ ArenaWorldInternal::kProjectileRadius * 2.0f } );
            if ( pObject == nullptr )
                return;
            spawned._object = pObject->getHandle();
            _listProjectile.push_back( std::move( spawned ) );
        } ) );
    }

    // ------------------------------------------------------------------------------
    // 찾기
    // ------------------------------------------------------------------------------
    Scene* ArenaWorld::findActiveScene() const
    {
        SceneManager* pSceneManager = game::getService<SceneManager>();
        return pSceneManager != nullptr ? pSceneManager->getActiveScene() : nullptr;
    }

    GameObjectManager* ArenaWorld::findObjectManager() const
    {
        Scene* pScene = findActiveScene();
        return pScene != nullptr ? pScene->getObjectManager() : nullptr;
    }

    AbilitySystemComponent* ArenaWorld::findAbilitySystem( GameObjectHandle object ) const
    {
        GameObjectManager* pManager = findObjectManager();
        GameObject*        pObject  = pManager != nullptr ? pManager->resolveGameObject( object ) : nullptr;
        return pObject != nullptr ? pObject->getComponent<AbilitySystemComponent>() : nullptr;
    }

    MeshComponent* ArenaWorld::findMesh( GameObjectHandle object ) const
    {
        GameObjectManager* pManager = findObjectManager();
        GameObject*        pObject  = pManager != nullptr ? pManager->resolveGameObject( object ) : nullptr;
        return pObject != nullptr ? pObject->getComponent<MeshComponent>() : nullptr;
    }

    ArenaWorld::ArenaUnit* ArenaWorld::findUnit( const AbilitySystemComponent& abilitySystem )
    {
        return const_cast<ArenaUnit*>( static_cast<const ArenaWorld*>( this )->findUnit( abilitySystem ) );
    }

    const ArenaWorld::ArenaUnit* ArenaWorld::findUnit( const AbilitySystemComponent& abilitySystem ) const
    {
        const GameObject* pOwner = abilitySystem.getOwner();
        if ( pOwner == nullptr )
            return nullptr;
        const GameObjectHandle handle = pOwner->getHandle();
        for ( const ArenaUnit& unit : _listUnit )
        {
            if ( unit._object == handle )
                return &unit;
        }
        return nullptr;
    }

    bool ArenaWorld::isHostile( const AbilitySystemComponent& lhs, const AbilitySystemComponent& rhs ) const
    {
        const bool bLhsPlayer = lhs.hasMatchingTag( "Team.Player"_tag );
        const bool bRhsPlayer = rhs.hasMatchingTag( "Team.Player"_tag );
        const bool bLhsEnemy  = lhs.hasMatchingTag( "Team.Enemy"_tag );
        const bool bRhsEnemy  = rhs.hasMatchingTag( "Team.Enemy"_tag );
        return ( bLhsPlayer && bRhsEnemy ) || ( bLhsEnemy && bRhsPlayer );
    }

    // ------------------------------------------------------------------------------
    // 세우기
    // ------------------------------------------------------------------------------
    GameObject* ArenaWorld::createMeshObject( GameObjectManager& manager, const utf8* pName, const shared_ptr<Mesh>& mesh,
                                              const shared_ptr<MaterialInstance>& material, const float3& position, const float3& scale )
    {
        GameObject* pObject = manager.createGameObject( hashed_string( pName ) );
        if ( pObject == nullptr )
            return nullptr;
        MeshComponent* pMesh = pObject->addComponent<MeshComponent>();
        if ( pMesh == nullptr )
        {
            manager.destroyObject( pObject );
            return nullptr;
        }
        pMesh->setMesh( mesh );
        Scene* pScene = findActiveScene();
        if ( pScene != nullptr && pScene->getMaterial() != nullptr )
            pMesh->setMaterial( pScene->getMaterial() ); // 직접 만든 씬은 기본 머티리얼이 붙지 않는다(BenchScene 참고)
        if ( material != nullptr )
            pMesh->setMaterialInstance( material );
        pMesh->setLocalPosition( position );
        pMesh->setLocalScale( scale );
        pMesh->setVisible( true );
        return pObject;
    }

    GameObject* ArenaWorld::createModelObject( GameObjectManager& manager, const utf8* pName, const utf8* pModel, const shared_ptr<MaterialInstance>& material,
                                               const float3& position, float32 yaw )
    {
        GameObject*    pObject = createMeshObject( manager, pName, nullptr, material, position, float3{ ArenaWorldInternal::kModelScale } );
        MeshComponent* pMesh   = pObject != nullptr ? pObject->getComponent<MeshComponent>() : nullptr;
        if ( pMesh == nullptr )
            return nullptr;
        // 메시 캐시의 공유 메시를 경로로 받는다 — 읽지 못하면(경고는 캐시가 경로마다 한 번) 오브젝트를 걷는다.
        pMesh->setMeshId( ArenaWorldInternal::makeModelPath( pModel ) );
        if ( pMesh->getRawMesh() == nullptr )
        {
            manager.destroyObject( pObject );
            return nullptr;
        }
        pMesh->setLocalRotation( float3{ 0.0f, yaw, 0.0f } );
        return pObject;
    }

    void ArenaWorld::addStageModel( GameObjectManager& manager, const utf8* pModel, const float3& position, float32 yaw )
    {
        GameObject* pObject = createModelObject( manager, "ArenaProp", pModel, _pStageMaterial, position, yaw );
        if ( pObject != nullptr )
            _listStageObject.push_back( pObject->getHandle() );
    }

    bool ArenaWorld::spawnUnit( ArenaUnitKind kind, const float3& position, int32 level )
    {
        GameObjectManager* pManager = findObjectManager();
        if ( pManager == nullptr )
            return false;

        // 플레이어는 사람, 적은 오크 — 근접 · 원거리는 색(빨강 · 보라)으로 가른다.
        const bool                          bPlayer  = kind == ArenaUnitKind::Player;
        const utf8*                         pModel   = bPlayer ? "character_human" : "character_orc";
        const shared_ptr<MaterialInstance>& material = bPlayer ? _pPlayerMaterial : ( kind == ArenaUnitKind::Caster ? _pCasterMaterial : _pGruntMaterial );
        const utf8*                         pName    = bPlayer ? "ArenaPlayer" : ( kind == ArenaUnitKind::Caster ? "ArenaCaster" : "ArenaGrunt" );

        GameObject* pObject = createModelObject( *pManager, pName, pModel, material, position, 0.0f );
        if ( pObject == nullptr )
            return false;

        AbilitySystemComponent* pAbilitySystem = pObject->addComponent<AbilitySystemComponent>();
        if ( pAbilitySystem == nullptr )
        {
            pManager->destroyObject( pObject );
            return false;
        }
        // 카탈로그는 정하지 않는다 — 게임 서비스(`AbilityCatalog`)를 쓴다. 포인터를 박아 두면 모듈이 다시 올라온 뒤 옛 카탈로그를 가리킨다.
        pAbilitySystem->setShowDamageNumbers( true );
        (void)pObject->addComponent<HealthBarComponent>(); // 없어도 게임은 돈다 — 체력 비율은 어빌리티 시스템이 맞춘다

        const hashed_string setId = ArenaWorldInternal::findAbilitySetId( kind );
        if ( pAbilitySystem->grantAbilitySet( setId ) == false )
        {
            SW_LOG_WARNING( "[Arena] ability set '%#' is missing - is abilities.xml loaded?", setId.c_str() );
            pManager->destroyObject( pObject );
            return false;
        }
        // 웨이브가 오르면 적의 어빌리티 레벨이 오른다(이펙트의 perLevel 이 붙은 값만 커진다).
        if ( level > 1 )
        {
            const float32 levelScale = 1.0f + 0.15f * static_cast<float32>( level - 1 );
            (void)pAbilitySystem->setAttributeBaseValue( CombatAttributes::maxHealth(), pAbilitySystem->getAttributeBaseValue( CombatAttributes::maxHealth() ) * levelScale );
            (void)pAbilitySystem->setAttributeBaseValue( CombatAttributes::health(), pAbilitySystem->getAttributeBaseValue( CombatAttributes::maxHealth() ) );
            (void)pAbilitySystem->setAttributeBaseValue( CombatAttributes::attackPower(), pAbilitySystem->getAttributeBaseValue( CombatAttributes::attackPower() ) * levelScale );
        }

        ArenaUnit unit;
        unit._object = pObject->getHandle();
        unit._kind   = kind;
        unit._facing = ArenaWorldInternal::flattenDirection( float3{ 0.0f, 0.0f, 0.0f } - position, float3{ 0.0f, 0.0f, 1.0f } );
        _listUnit.push_back( unit );
        return true;
    }

    void ArenaWorld::spawnWave()
    {
        ++_wave;
        (void)GameSound::play( "game/abilityarena/sounds/maximize_001.ogg" );
        const uint32 gruntCount  = 2u + _wave;
        const uint32 casterCount = _wave / 2u;
        const uint32 totalCount  = gruntCount + casterCount;
        const int32  level       = 1 + static_cast<int32>( ( _wave - 1u ) / 2u );
        for ( uint32 spawnIndex = 0; spawnIndex < totalCount; ++spawnIndex )
        {
            const float32       angle    = 2.0f * ArenaWorldInternal::kPi * static_cast<float32>( spawnIndex ) / static_cast<float32>( totalCount );
            const float3        position = float3{ MathUtil::cos( angle ) * ArenaWorldInternal::kWaveRadius, 0.0f, MathUtil::sin( angle ) * ArenaWorldInternal::kWaveRadius };
            const ArenaUnitKind kind     = spawnIndex < gruntCount ? ArenaUnitKind::Grunt : ArenaUnitKind::Caster;
            (void)spawnUnit( kind, position, level ); // 하나가 실패해도 나머지는 선다
        }
        SW_LOG_INFO( "[Arena] wave %# - %# grunts, %# casters (level %#)", _wave, gruntCount, casterCount, level );
    }

    void ArenaWorld::spawnStage( GameObjectManager& manager )
    {
        // 바닥 — 2 m 칸을 아레나보다 한 칸 넓게 깔고(가끔 금 간 칸), 그 둘레에 벽. 귀퉁이는 기둥, 북쪽 벽에 깃발, 귀퉁이에 상자 · 물약 · 동전 · 바위 · 함정.
        using Internal           = ArenaWorldInternal;
        const int32   halfCount  = static_cast<int32>( Internal::kArenaHalfSize / Internal::kTileSize ) + 1;
        const float32 wallOffset = static_cast<float32>( halfCount ) * Internal::kTileSize + Internal::kTileSize * 0.5f;
        uint32        detailSeed = 0x9E3779B9u;
        for ( int32 row = -halfCount; row < halfCount; ++row )
        {
            for ( int32 column = -halfCount; column < halfCount; ++column )
            {
                detailSeed ^= detailSeed << 13;
                detailSeed ^= detailSeed >> 17;
                detailSeed ^= detailSeed << 5;
                const float3 center{ ( static_cast<float32>( column ) + 0.5f ) * Internal::kTileSize, 0.0f, ( static_cast<float32>( row ) + 0.5f ) * Internal::kTileSize };
                addStageModel( manager, detailSeed % 7u == 0u ? "floor_detail" : "floor", center, static_cast<float32>( detailSeed % 4u ) * Internal::kPi * 0.5f );
            }
        }
        for ( int32 index = -halfCount; index < halfCount; ++index )
        {
            const float32 along = ( static_cast<float32>( index ) + 0.5f ) * Internal::kTileSize;
            addStageModel( manager, "wall", float3{ along, 0.0f, wallOffset }, 0.0f );
            addStageModel( manager, "wall", float3{ along, 0.0f, -wallOffset }, 0.0f );
            addStageModel( manager, "wall", float3{ wallOffset, 0.0f, along }, 0.0f );
            addStageModel( manager, "wall", float3{ -wallOffset, 0.0f, along }, 0.0f );
        }
        const float32 corner = wallOffset - Internal::kTileSize;
        addStageModel( manager, "column", float3{ corner, 0.0f, corner }, 0.0f );
        addStageModel( manager, "column", float3{ -corner, 0.0f, corner }, 0.0f );
        addStageModel( manager, "column", float3{ corner, 0.0f, -corner }, 0.0f );
        addStageModel( manager, "column", float3{ -corner, 0.0f, -corner }, 0.0f );
        // 깃발은 칸의 -Z 가장자리에 붙어 있다 — 북쪽 벽 안쪽 면에 걸리게 π 돌려 벽 바로 앞 칸에 둔다.
        addStageModel( manager, "banner", float3{ -6.0f, 0.0f, corner }, Internal::kPi );
        addStageModel( manager, "banner", float3{ 6.0f, 0.0f, corner }, Internal::kPi );
        addStageModel( manager, "chest", float3{ corner - 1.8f, 0.0f, corner }, Internal::kPi * 1.25f );
        addStageModel( manager, "potion", float3{ corner, 0.0f, corner - 2.0f }, 0.0f );
        addStageModel( manager, "coin", float3{ corner - 2.0f, 0.0f, corner - 1.6f }, 0.4f );
        addStageModel( manager, "rocks", float3{ -corner, 0.0f, -corner + 2.2f }, 0.0f );
        addStageModel( manager, "trap", float3{ -corner + 2.2f, 0.0f, -corner }, 0.0f );

        GameObject* pLightObject = manager.createGameObject( hashed_string( "ArenaSun" ) );
        if ( pLightObject == nullptr )
            return;
        DirectionalLightComponent* pLight = pLightObject->addComponent<DirectionalLightComponent>();
        if ( pLight != nullptr )
        {
            pLight->setShadowExtent( ArenaWorldInternal::kArenaHalfSize * 1.3f );
            pLight->setShadowDistance( ArenaWorldInternal::kArenaHalfSize * 2.0f );
            pLight->setCastShadow( true );
            pLight->setIntensity( 1.6f );
        }
        _listStageObject.push_back( pLightObject->getHandle() );
    }

    bool ArenaWorld::ensureRenderAssets( Scene& scene )
    {
        if ( _pProjectileMesh == nullptr )
            _pProjectileMesh = MeshUtil::createPrimitive( "Sphere" );
        if ( _pProjectileMesh == nullptr )
        {
            SW_LOG_WARNING( "[Arena] could not create primitive meshes" );
            return false;
        }

        // 색은 종류마다 인스턴스 하나를 모두가 나눠 쓴다 — 유닛마다 만들지 않는다(배치가 갈리고, DX12 의 인스턴스 생성 경로를 매 스폰에 태우지 않는다).
        // 유닛 모델은 팔레트 텍스처에 편 색을 반쯤 곱해 옷 색이 편(파랑 · 빨강 · 보라)으로 읽힌다.
        Material* pMaterial = scene.getMaterial();
        if ( _pPlayerMaterial == nullptr )
            _pPlayerMaterial = ArenaWorldInternal::createTint( pMaterial, float4{ 0.55f, 0.75f, 1.0f, 1.0f }, true );
        if ( _pGruntMaterial == nullptr )
            _pGruntMaterial = ArenaWorldInternal::createTint( pMaterial, float4{ 1.0f, 0.5f, 0.45f, 1.0f }, true );
        if ( _pCasterMaterial == nullptr )
            _pCasterMaterial = ArenaWorldInternal::createTint( pMaterial, float4{ 0.8f, 0.55f, 1.0f, 1.0f }, true );
        if ( _pProjectileMaterial == nullptr )
            _pProjectileMaterial = ArenaWorldInternal::createTint( pMaterial, float4{ 1.0f, 0.6f, 0.1f, 1.0f }, false );
        if ( _pStageMaterial == nullptr )
            _pStageMaterial = ArenaWorldInternal::createTint( pMaterial, float4{ 1.0f, 1.0f, 1.0f, 1.0f }, true );
        return true;
    }

    // ------------------------------------------------------------------------------
    // 갱신
    // ------------------------------------------------------------------------------
    void ArenaWorld::updatePlayer( float32 deltaTime, const InputManager* pInput )
    {
        ArenaUnit* pPlayer = nullptr;
        for ( ArenaUnit& unit : _listUnit )
        {
            if ( unit._kind == ArenaUnitKind::Player )
            {
                pPlayer = &unit;
                break;
            }
        }

        if ( pPlayer == nullptr )
        {
            // 쓰러진 플레이어는 걷힌 뒤 잠시 있다가 가운데에 다시 선다(웨이브는 이어진다).
            if ( _playerRespawnTimer < 0.0f )
                _playerRespawnTimer = ArenaWorldInternal::kPlayerRespawnDelay;
            _playerRespawnTimer -= deltaTime;
            if ( _playerRespawnTimer <= 0.0f )
            {
                _playerRespawnTimer = -1.0f;
                (void)spawnUnit( ArenaUnitKind::Player, float3{ 0.0f, 0.0f, 0.0f }, 1 ); // 실패하면 다음 프레임에 다시
            }
            return;
        }

        AbilitySystemComponent* pAbilitySystem = findAbilitySystem( pPlayer->_object );
        if ( pAbilitySystem == nullptr || pPlayer->_deathTimer >= 0.0f )
            return;

        if ( gv_arenaAutoPlay != 0 || pInput == nullptr )
        {
            updateAutoPlayer( *pPlayer, *pAbilitySystem, deltaTime );
            return;
        }

        // 입력 → 어빌리티 입력 번호. 눌림 · 뗌을 그대로 넘긴다(차지 · 콤보 어빌리티가 뗌을 받는다).
        for ( const ArenaWorldInternal::KeyBinding& binding : ArenaWorldInternal::kArrKeyBinding )
        {
            if ( pInput->wasKeyPressed( binding._key ) )
                pAbilitySystem->abilityInputPressed( binding._inputId );
            if ( pInput->wasKeyReleased( binding._key ) )
                pAbilitySystem->abilityInputReleased( binding._inputId );
        }

        float3 direction{ 0.0f, 0.0f, 0.0f };
        if ( pInput->isKeyDown( Key::W ) || pInput->isKeyDown( Key::Up ) )
            direction._z += 1.0f;
        if ( pInput->isKeyDown( Key::S ) || pInput->isKeyDown( Key::Down ) )
            direction._z -= 1.0f;
        if ( pInput->isKeyDown( Key::D ) || pInput->isKeyDown( Key::Right ) )
            direction._x += 1.0f;
        if ( pInput->isKeyDown( Key::A ) || pInput->isKeyDown( Key::Left ) )
            direction._x -= 1.0f;

        // 대시 중에는 입력과 상관없이 바라보는 쪽으로 미끄러진다(이동 속도 ×3 은 이펙트가 건다).
        if ( pAbilitySystem->hasMatchingTag( "State.Dashing"_tag ) )
            direction = pPlayer->_facing;
        if ( direction.getLengthSquared() > 0.0f )
            moveUnit( *pPlayer, *pAbilitySystem, direction, deltaTime );
    }

    void ArenaWorld::updateAutoPlayer( ArenaUnit& player, AbilitySystemComponent& abilitySystem, float32 deltaTime )
    {
        // 반쯤 깎이면 회복, 가까우면 근접, 멀면 다가가며 화염구, 둘러싸이면 대시로 빠진다 — 입력 번호만 누른다(규칙은 어빌리티가 지킨다).
        const float32 healthRatio = abilitySystem.getAttributeValue( CombatAttributes::health() ) /
                                    MathUtil::max( 1.0f, abilitySystem.getAttributeValue( CombatAttributes::maxHealth() ) );
        if ( healthRatio < 0.5f )
            tapInput( abilitySystem, kInputHeal );

        AbilitySystemComponent* pTarget     = findNearestHostile( abilitySystem, 100.0f );
        const MeshComponent*    pSelf       = findMesh( player._object );
        const ArenaUnit*        pTargetUnit = pTarget != nullptr ? findUnit( *pTarget ) : nullptr;
        const MeshComponent*    pTargetMesh = pTargetUnit != nullptr ? findMesh( pTargetUnit->_object ) : nullptr;
        if ( pSelf == nullptr || pTargetMesh == nullptr )
            return;

        const float3  toTarget = pTargetMesh->getWorldPosition() - pSelf->getWorldPosition();
        const float32 distance = toTarget.getLength();
        player._facing         = ArenaWorldInternal::flattenDirection( toTarget, player._facing );

        uint32 nearbyEnemyCount = 0;
        for ( const ArenaUnit& unit : _listUnit )
        {
            const MeshComponent* pMesh = unit._kind != ArenaUnitKind::Player ? findMesh( unit._object ) : nullptr;
            if ( pMesh != nullptr && unit._deathTimer < 0.0f && float3::getDistance( pMesh->getWorldPosition(), pSelf->getWorldPosition() ) < 2.5f )
                ++nearbyEnemyCount;
        }
        if ( nearbyEnemyCount >= 3 )
        {
            player._facing = float3{ 0.0f, 0.0f, 0.0f } - player._facing;
            tapInput( abilitySystem, kInputDash );
        }

        if ( abilitySystem.hasMatchingTag( "State.Dashing"_tag ) )
        {
            moveUnit( player, abilitySystem, player._facing, deltaTime );
            return;
        }
        if ( distance > 2.0f )
        {
            tapInput( abilitySystem, kInputFireball );
            moveUnit( player, abilitySystem, toTarget, deltaTime );
        }
        else
        {
            tapInput( abilitySystem, kInputMelee );
        }
    }

    void ArenaWorld::updateEnemies( float32 deltaTime )
    {
        for ( ArenaUnit& unit : _listUnit )
        {
            if ( unit._kind == ArenaUnitKind::Player || unit._deathTimer >= 0.0f )
                continue;
            AbilitySystemComponent* pAbilitySystem = findAbilitySystem( unit._object );
            const MeshComponent*    pSelf          = findMesh( unit._object );
            if ( pAbilitySystem == nullptr || pSelf == nullptr || pAbilitySystem->hasMatchingTag( CombatAttributeSet::getDeadTag() ) )
                continue;

            AbilitySystemComponent* pTarget     = findNearestHostile( *pAbilitySystem, 100.0f );
            const ArenaUnit*        pTargetUnit = pTarget != nullptr ? findUnit( *pTarget ) : nullptr;
            const MeshComponent*    pTargetMesh = pTargetUnit != nullptr ? findMesh( pTargetUnit->_object ) : nullptr;
            if ( pTargetMesh == nullptr )
                continue; // 플레이어가 다시 서기를 기다린다

            const float3  toTarget = pTargetMesh->getWorldPosition() - pSelf->getWorldPosition();
            const float32 distance = toTarget.getLength();
            unit._facing           = ArenaWorldInternal::flattenDirection( toTarget, unit._facing );

            if ( unit._kind == ArenaUnitKind::Grunt )
            {
                if ( distance > ArenaWorldInternal::kGruntReach )
                    moveUnit( unit, *pAbilitySystem, toTarget, deltaTime );
                else
                    tapInput( *pAbilitySystem, kInputMelee );
                continue;
            }

            // 원거리 — 적당한 거리를 지키며 쏜다.
            if ( distance > ArenaWorldInternal::kCasterPreferredMax )
                moveUnit( unit, *pAbilitySystem, toTarget, deltaTime );
            else if ( distance < ArenaWorldInternal::kCasterPreferredMin )
                moveUnit( unit, *pAbilitySystem, float3{ 0.0f, 0.0f, 0.0f } - toTarget, deltaTime );
            unit._facing = ArenaWorldInternal::flattenDirection( toTarget, unit._facing ); // 물러나도 플레이어를 본다
            if ( distance <= ArenaWorldInternal::kCasterFireRange )
                tapInput( *pAbilitySystem, kInputFireball );
        }

        // 적끼리 겹치지 않게 살짝 밀어낸다(물리 없이 — 유닛 수가 적다).
        for ( size_t lhsIndex = 0; lhsIndex < _listUnit.size(); ++lhsIndex )
        {
            MeshComponent* pLhs = findMesh( _listUnit[lhsIndex]._object );
            if ( pLhs == nullptr || _listUnit[lhsIndex]._kind == ArenaUnitKind::Player )
                continue;
            for ( size_t rhsIndex = lhsIndex + 1; rhsIndex < _listUnit.size(); ++rhsIndex )
            {
                MeshComponent* pRhs = findMesh( _listUnit[rhsIndex]._object );
                if ( pRhs == nullptr || _listUnit[rhsIndex]._kind == ArenaUnitKind::Player )
                    continue;
                const float3  apart    = pRhs->getLocalPosition() - pLhs->getLocalPosition();
                const float32 distance = apart.getLength();
                const float32 overlap  = ArenaWorldInternal::kUnitRadius * 2.0f - distance;
                if ( overlap <= 0.0f || distance < 1.0e-4f )
                    continue;
                const float3 push = apart * ( 0.5f * overlap / distance );
                pLhs->setLocalPosition( pLhs->getLocalPosition() - push );
                pRhs->setLocalPosition( pRhs->getLocalPosition() + push );
            }
        }
    }

    void ArenaWorld::updateProjectiles( float32 deltaTime )
    {
        GameObjectManager* pManager = findObjectManager();
        if ( pManager == nullptr )
            return;

        for ( size_t projectileIndex = _listProjectile.size(); projectileIndex > 0; --projectileIndex )
        {
            ArenaProjectile& projectile = _listProjectile[projectileIndex - 1];
            GameObject*      pObject    = pManager->resolveGameObject( projectile._object );
            bool             bFinished  = pObject == nullptr;
            if ( bFinished == false )
            {
                const float3 step    = projectile._velocity * deltaTime;
                projectile._position = projectile._position + step;
                projectile._remainingRange -= step.getLength();

                MeshComponent* pMesh = pObject->getComponent<MeshComponent>();
                if ( pMesh != nullptr )
                    pMesh->setLocalPosition( projectile._position + float3{ 0.0f, ArenaWorldInternal::kProjectileLift, 0.0f } );

                // 처음 닿은 적대 유닛 하나. 같은 편 · 쓰러진 유닛은 지나간다.
                for ( const ArenaUnit& unit : _listUnit )
                {
                    const bool bSameTeam = ( unit._kind == ArenaUnitKind::Player ) == ( projectile._bFromPlayer == SW_TRUE );
                    if ( bSameTeam || unit._deathTimer >= 0.0f )
                        continue;
                    AbilitySystemComponent* pTarget     = findAbilitySystem( unit._object );
                    const MeshComponent*    pTargetMesh = findMesh( unit._object );
                    if ( pTarget == nullptr || pTargetMesh == nullptr || pTarget->hasMatchingTag( CombatAttributeSet::getDeadTag() ) )
                        continue;
                    const float32 reach = ArenaWorldInternal::kUnitRadius + ArenaWorldInternal::kProjectileRadius;
                    if ( float3::getDistance( pTargetMesh->getWorldPosition(), projectile._position ) > reach )
                        continue;

                    // 게임 갱신은 씬 틱 밖이다 — 대상에게 바로 건다. 무적이면 이펙트의 BlockedTag 가 막는다.
                    if ( projectile._spec.isValid() )
                        (void)pTarget->applyGameplayEffectSpecToSelf( projectile._spec );
                    if ( projectile._extraSpec.isValid() )
                        (void)pTarget->applyGameplayEffectSpecToSelf( projectile._extraSpec );
                    (void)GameSound::play( "game/abilityarena/sounds/impact_punch_medium_000.ogg" );
                    bFinished = true;
                    break;
                }
                const bool bOutOfArena = MathUtil::abs( projectile._position._x ) > ArenaWorldInternal::kArenaHalfSize + 2.0f ||
                                         MathUtil::abs( projectile._position._z ) > ArenaWorldInternal::kArenaHalfSize + 2.0f;
                if ( projectile._remainingRange <= 0.0f || bOutOfArena )
                    bFinished = true;
            }

            if ( bFinished )
            {
                if ( pObject != nullptr )
                    pManager->destroyObject( pObject );
                _listProjectile.erase( _listProjectile.begin() + static_cast<ptrdiff_t>( projectileIndex - 1 ) );
            }
        }
    }

    void ArenaWorld::updateDeaths( float32 deltaTime )
    {
        GameObjectManager* pManager = findObjectManager();
        if ( pManager == nullptr )
            return;

        uint32 aliveEnemyCount = 0;
        for ( size_t unitIndex = _listUnit.size(); unitIndex > 0; --unitIndex )
        {
            ArenaUnit&              unit           = _listUnit[unitIndex - 1];
            GameObject*             pObject        = pManager->resolveGameObject( unit._object );
            AbilitySystemComponent* pAbilitySystem = pObject != nullptr ? pObject->getComponent<AbilitySystemComponent>() : nullptr;
            if ( pAbilitySystem == nullptr )
            {
                _listUnit.erase( _listUnit.begin() + static_cast<ptrdiff_t>( unitIndex - 1 ) );
                continue;
            }

            if ( unit._deathTimer < 0.0f && pAbilitySystem->hasMatchingTag( CombatAttributeSet::getDeadTag() ) )
            {
                unit._deathTimer = ArenaWorldInternal::kDeathLinger;
                if ( unit._kind == ArenaUnitKind::Player )
                {
                    SW_LOG_INFO( "[Arena] the player fell on wave %# after %# kills", _wave, _killCount );
                    (void)GameSound::play( "game/abilityarena/sounds/error_004.ogg" );
                }
                else
                {
                    ++_killCount;
                    (void)GameSound::play( "game/abilityarena/sounds/impact_punch_heavy_000.ogg" );
                }
            }
            if ( unit._deathTimer >= 0.0f )
            {
                // 쓰러진 유닛은 납작해지다가 걷힌다.
                unit._deathTimer -= deltaTime;
                MeshComponent* pMesh = pObject->getComponent<MeshComponent>();
                const float32  scale = ArenaWorldInternal::kModelScale;
                if ( pMesh != nullptr )
                    pMesh->setLocalScale( float3{ scale, scale * MathUtil::max( 0.05f, unit._deathTimer / ArenaWorldInternal::kDeathLinger ), scale } );
                if ( unit._deathTimer <= 0.0f )
                {
                    pManager->destroyObject( pObject );
                    _listUnit.erase( _listUnit.begin() + static_cast<ptrdiff_t>( unitIndex - 1 ) );
                }
                continue;
            }
            if ( unit._kind != ArenaUnitKind::Player )
                ++aliveEnemyCount;
        }

        bool bAnyEnemyLingering = false;
        for ( const ArenaUnit& unit : _listUnit )
        {
            if ( unit._kind != ArenaUnitKind::Player )
                bAnyEnemyLingering = true;
        }
        if ( aliveEnemyCount == 0 && bAnyEnemyLingering == false )
            spawnWave();
    }

    void ArenaWorld::updateUnitFacing()
    {
        // 모델의 앞(+Z)을 바라보는 쪽으로 — AI 는 움직이지 않고도 몸을 돌린다.
        for ( const ArenaUnit& unit : _listUnit )
        {
            MeshComponent* pMesh = unit._deathTimer < 0.0f ? findMesh( unit._object ) : nullptr;
            if ( pMesh != nullptr )
                pMesh->setLocalRotation( float3{ 0.0f, MathUtil::atan2( unit._facing._x, unit._facing._z ), 0.0f } );
        }
    }

    void ArenaWorld::updateCamera()
    {
        GameObjectManager* pManager = findObjectManager();
        if ( pManager == nullptr )
            return;

        float3 focus{ 0.0f, 0.0f, 0.0f };
        for ( const ArenaUnit& unit : _listUnit )
        {
            const MeshComponent* pMesh = unit._kind == ArenaUnitKind::Player ? findMesh( unit._object ) : nullptr;
            if ( pMesh != nullptr )
            {
                focus = pMesh->getWorldPosition();
                break;
            }
        }

        // 씬의 카메라를 모두 맞춘다 — 에디터 게임 뷰도 같은 카메라 등록부를 본다(BenchScene::frameCameras 와 같은 이유).
        for ( CameraComponent* pCamera : pManager->getCameraRegistry().getAll() )
        {
            if ( pCamera == nullptr )
                continue;
            pCamera->setLocalPosition( focus + float3{ 0.0f, 15.0f, -11.0f } );
            pCamera->setFarPlane( MathUtil::max( pCamera->getFarPlane(), 120.0f ) );
            pCamera->lookAt( focus );
        }
    }

    void ArenaWorld::logStatus( float32 deltaTime )
    {
        _statusLogTimer += deltaTime;
        if ( _statusLogTimer < ArenaWorldInternal::kStatusLogInterval )
            return;
        _statusLogTimer = 0.0f;

        for ( const ArenaUnit& unit : _listUnit )
        {
            if ( unit._kind != ArenaUnitKind::Player )
                continue;
            const AbilitySystemComponent* pAbilitySystem = findAbilitySystem( unit._object );
            if ( pAbilitySystem == nullptr )
                continue;
            [[maybe_unused]] const int32 health = static_cast<int32>( pAbilitySystem->getAttributeValue( CombatAttributes::health() ) );
            [[maybe_unused]] const int32 mana   = static_cast<int32>( pAbilitySystem->getAttributeValue( CombatAttributes::mana() ) );
            SW_LOG_INFO( "[Arena] wave %# · kills %# · player HP %# · MP %# · units %# · projectiles %#", _wave, _killCount, health, mana,
                         static_cast<uint32>( _listUnit.size() ), static_cast<uint32>( _listProjectile.size() ) );
        }
    }

    void ArenaWorld::moveUnit( ArenaUnit& unit, const AbilitySystemComponent& abilitySystem, const float3& direction, float32 deltaTime )
    {
        MeshComponent* pMesh = findMesh( unit._object );
        if ( pMesh == nullptr )
            return;
        const float3 flatDirection = ArenaWorldInternal::flattenDirection( direction, unit._facing );
        unit._facing               = flatDirection;

        // 이동 속도는 어트리뷰트의 current 다 — 대시(×3) · 둔화 이펙트가 여기에 그대로 반영된다.
        const float32 speed    = abilitySystem.getAttributeValue( CombatAttributes::moveSpeed() );
        float3        position = pMesh->getLocalPosition() + flatDirection * ( speed * deltaTime );
        position._x            = MathUtil::clamp( position._x, -ArenaWorldInternal::kArenaHalfSize, ArenaWorldInternal::kArenaHalfSize );
        position._z            = MathUtil::clamp( position._z, -ArenaWorldInternal::kArenaHalfSize, ArenaWorldInternal::kArenaHalfSize );
        pMesh->setLocalPosition( position );
    }

    void ArenaWorld::tapInput( AbilitySystemComponent& abilitySystem, int32 inputId )
    {
        abilitySystem.abilityInputPressed( inputId );
        abilitySystem.abilityInputReleased( inputId );
    }
} // namespace sw

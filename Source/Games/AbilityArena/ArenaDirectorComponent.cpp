#include "pch.h"

#include "Games/AbilityArena/ArenaDirectorComponent.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Resource/AssetManager.h"
#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Ability/AbilityCatalog.h"
#include "GameFramework/Ability/AbilitySystemComponent.h"
#include "GameFramework/Ability/CombatAttributeSet.h"
#include "GameFramework/Framework/GameService.h"
#include "GameFramework/Framework/GameSound.h"
#include "GameFramework/Utility/StateArchiveUtil.h"

#include "Games/AbilityArena/ArenaProjectileComponent.h"

namespace sw
{
    SW_LOG_CALLER( "ArenaDirector" );

    namespace
    {
        struct ArenaDirectorComponentInternal
        {
            static constexpr float32 kStatusLogInterval = 5.0f;        ///< 상태 로그 간격(s)
            static constexpr uint32  kStateTag          = 0x414E5241u; ///< 'ARNA'
            static constexpr uint32  kStateVersion      = 1;
            static constexpr float32 kUnitRadius        = 0.5f; ///< 투사체가 쏜 쪽 몸 밖에서 나오는 거리
            static constexpr float32 kProjectileRadius  = 0.25f;
            static constexpr int32   kProjectileTint    = 3; ///< `_arrTint` 의 투사체 칸(앞 셋은 `ArenaUnitKind` 순)

            static constexpr const utf8* kSoundWave       = "game/abilityarena/sounds/maximize_001.ogg";
            static constexpr const utf8* kSoundPlayerFell = "game/abilityarena/sounds/error_004.ogg";
            static constexpr const utf8* kSoundEnemyFell  = "game/abilityarena/sounds/impact_punch_heavy_000.ogg";

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
     * @brief `-gv_arenaAutoPlay=1` — 디렉터의 자동 전투를 켭니다(씬의 `_bAutoPlay` 가 꺼져 있어도). 플레이어도 AI 가 움직인다.
     * @details 배포본 실행 파일로도 돌릴 수 있게 남긴다: `App -gv_arenaAutoPlay=1 -gv_profileFrames=1200`.
     */
    SW_TEST_GLOBAL_VARIABLE_INT( gv_arenaAutoPlay, 0, "AbilityArena: 플레이어도 AI 가 조종 (1=켜기)", SW_KEEP_IN_SHIPPING );
} // namespace sw

namespace sw
{
    ArenaDirectorComponent::ArenaDirectorComponent()
        : _playerPrefab{}
        , _gruntPrefab{}
        , _casterPrefab{}
        , _projectilePrefab{}
        , _arenaHalfSize{ 14.0f }
        , _waveRadius{ 11.0f }
        , _playerRespawnDelay{ 2.5f }
        , _playerTint{ 0.55f, 0.75f, 1.0f, 1.0f }
        , _gruntTint{ 1.0f, 0.5f, 0.45f, 1.0f }
        , _casterTint{ 0.8f, 0.55f, 1.0f, 1.0f }
        , _projectileTint{ 1.0f, 0.6f, 0.1f, 1.0f }
        , _bAutoPlay{ false }
        , _listUnit{}
        , _listUnitView{}
        , _listProjectile{}
        , _listPendingUnit{}
        , _listPendingSound{}
        , _pendingStateBytes{}
        , _arrTint{}
        , _playerFocus{ 0.0f, 0.0f, 0.0f }
        , _playerRespawnTimer{ -1.0f }
        , _statusLogTimer{ 0.0f }
        , _wave{ 0 }
        , _killCount{ 0 }
        , _bStarted{ SW_FALSE }
        , _bRuntimeSpawned{ SW_FALSE }
        , _bFlushScheduled{ SW_FALSE }
        , _reserved{ 0 }
    {
        setCanEverTick( true );
    }

    ArenaDirectorComponent::~ArenaDirectorComponent() = default;

    void ArenaDirectorComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        // 판의 규칙은 앞 그룹 — 컨트롤러 · 투사체 · 카메라가 같은 프레임에 이 결과를 읽는다.
        setTickGroup( TickGroup::PrePhysics );
        const AbilityCatalog* pCatalog = game::getService<AbilityCatalog>();
        if ( pCatalog == nullptr || pCatalog->getAbilitySetCount() == 0 )
        {
            SW_LOG_WARNING( "[Arena] the ability catalog is not loaded - the arena cannot start" );
            return;
        }
        _bStarted = SW_TRUE;
        _wave     = 0;
        if ( _pendingStateBytes.empty() == false )
            applyPendingState();
        SW_LOG_INFO( "[Arena] arena is ready - WASD move, J/Space melee, K/2 fireball, L/3 heal, LeftShift/4 dash" );
    }

    void ArenaDirectorComponent::writeState( Archive& outArchive ) const
    {
        StateArchiveUtil::writeHeader( outArchive, ArenaDirectorComponentInternal::kStateTag, ArenaDirectorComponentInternal::kStateVersion );
        outArchive << _wave;
        outArchive << _killCount;
    }

    void ArenaDirectorComponent::restoreState( vector<uint8>&& bytes )
    {
        _pendingStateBytes = std::move( bytes );
        if ( _bStarted == SW_TRUE )
            applyPendingState();
    }

    void ArenaDirectorComponent::applyPendingState()
    {
        Archive    archive( _pendingStateBytes.data(), _pendingStateBytes.size() );
        uint32     wave      = 0;
        uint32     killCount = 0;
        const bool bHeader   = StateArchiveUtil::readHeader( archive, ArenaDirectorComponentInternal::kStateTag, ArenaDirectorComponentInternal::kStateVersion );
        archive >> wave;
        archive >> killCount;
        _pendingStateBytes.clear();
        if ( bHeader == false || archive.isError() || archive.getRemainingBytes() != 0 )
        {
            SW_LOG_WARNING( "[Arena] the saved arena state does not match this build - starting from wave 1" );
            return;
        }
        _wave      = wave;
        _killCount = killCount;
        despawnRuntime(); // 다음 틱이 플레이어와 이 웨이브를 다시 세운다
        SW_LOG_INFO( "[Arena] arena state restored - wave %#, %# kills", _wave, _killCount );
    }

    void ArenaDirectorComponent::onEndPlay()
    {
        despawnRuntime();
        _bStarted = SW_FALSE;
        Component::onEndPlay();
    }

    void ArenaDirectorComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );
        if ( _bStarted == SW_FALSE )
            return;
        if ( _bRuntimeSpawned == SW_FALSE )
        {
            // 처음(또는 상태 저장 전에 걷은 뒤) — 플레이어와 지금 웨이브를 세운다.
            _bRuntimeSpawned = SW_TRUE;
            requestUnit( ArenaUnitKind::Player, float3{ 0.0f, 0.0f, 0.0f }, 1 );
            requestWave( _wave == 0 );
        }
        if ( deltaTime > 0.0f )
        {
            updateUnits( deltaTime );
            updatePlayerRespawn( deltaTime );
            pruneProjectiles();
            logStatus( deltaTime );
        }
        updateUnitViews();
        if ( _listPendingUnit.empty() == false || _listPendingSound.empty() == false )
            scheduleFlush();
    }

    void ArenaDirectorComponent::despawnRuntime()
    {
        GameObjectManager* pManager = getObjectManager();
        if ( pManager != nullptr )
        {
            for ( const ArenaUnit& unit : _listUnit )
            {
                GameObject* pObject = pManager->resolveGameObject( unit._object );
                if ( pObject != nullptr )
                    pManager->destroyObject( pObject );
            }
            for ( const GameObjectHandle& handle : _listProjectile )
            {
                GameObject* pObject = pManager->resolveGameObject( handle );
                if ( pObject != nullptr )
                    pManager->destroyObject( pObject );
            }
        }
        _listUnit.clear();
        _listUnitView.clear();
        _listProjectile.clear();
        _listPendingUnit.clear();
        _playerRespawnTimer = -1.0f;
        _bRuntimeSpawned    = SW_FALSE;
    }

    // ------------------------------------------------------------------------------
    // 다른 컴포넌트가 읽는 것
    // ------------------------------------------------------------------------------
    const ArenaUnitView* ArenaDirectorComponent::findUnitView( GameObjectHandle object ) const
    {
        const ArenaUnitView* pView = _listUnitView.data();
        for ( size_t viewIndex = 0; viewIndex < _listUnitView.size(); ++viewIndex )
        {
            if ( pView[viewIndex]._object == object )
                return pView + viewIndex;
        }
        return nullptr;
    }

    const ArenaUnitView* ArenaDirectorComponent::findNearestHostileView( GameObjectHandle from, float32 maxRange ) const
    {
        const ArenaUnitView* pFrom = findUnitView( from );
        if ( pFrom == nullptr )
            return nullptr;
        const ArenaUnitView* pView           = _listUnitView.data();
        const ArenaUnitView* pNearest        = nullptr;
        float32              nearestDistance = maxRange;
        for ( size_t viewIndex = 0; viewIndex < _listUnitView.size(); ++viewIndex )
        {
            const ArenaUnitView& candidate = pView[viewIndex];
            const bool           bHostile  = ( pFrom->_bPlayerTeam == SW_TRUE && candidate._bEnemyTeam == SW_TRUE ) ||
                                  ( pFrom->_bEnemyTeam == SW_TRUE && candidate._bPlayerTeam == SW_TRUE );
            if ( candidate._object == from || candidate._bAlive == SW_FALSE || bHostile == false )
                continue;
            const float32 distance = float3::getDistance( pFrom->_position, candidate._position );
            if ( distance <= nearestDistance )
            {
                nearestDistance = distance;
                pNearest        = &candidate;
            }
        }
        return pNearest;
    }

    AbilitySystemComponent* ArenaDirectorComponent::findNearestHostile( const AbilitySystemComponent& from, float32 maxRange ) const
    {
        const GameObject*    pFromOwner = from.getOwner();
        GameObjectManager*   pManager   = getObjectManager();
        const ArenaUnitView* pNearest   = pFromOwner != nullptr ? findNearestHostileView( pFromOwner->getHandle(), maxRange ) : nullptr;
        GameObject*          pTarget    = pNearest != nullptr && pManager != nullptr ? pManager->resolveGameObject( pNearest->_object ) : nullptr;
        return pTarget != nullptr ? pTarget->getComponent<AbilitySystemComponent>() : nullptr;
    }

    void ArenaDirectorComponent::launchProjectile( const AbilitySystemComponent& from, const float3& facing, const GameplayEffectSpec& spec,
                                                   const GameplayEffectSpec& extraSpec, float32 speed, float32 range ) const
    {
        using Internal                 = ArenaDirectorComponentInternal;
        const GameObject*    pFrom     = from.getOwner();
        const ArenaUnitView* pFromView = pFrom != nullptr ? findUnitView( pFrom->getHandle() ) : nullptr;
        GameObjectManager*   pManager  = getObjectManager();
        if ( pFromView == nullptr || pManager == nullptr )
            return;

        ProjectileRequest request;
        request._spec        = spec;
        request._extraSpec   = extraSpec;
        request._position    = pFromView->_position + facing * ( Internal::kUnitRadius + Internal::kProjectileRadius );
        request._velocity    = facing * speed;
        request._range       = range;
        request._bFromPlayer = from.hasMatchingTag( "Team.Player"_tag ) ? SW_TRUE : SW_FALSE;
        // 어빌리티는 워커(컨트롤러의 틱)에서 쏜다 — 오브젝트는 틱 뒤 게임 스레드에서 세운다. 그 사이 디렉터가 사라질 수 있으니 핸들로 다시 찾는다.
        const ComponentHandle self = getHandle();
        pManager->executeOrDeferPostTick( SW_DELEGATE_LAMBDA( GameObjectManager::PostTickDelegate, [pManager, self, request]()
        {
            ArenaDirectorComponent* pDirector = static_cast<ArenaDirectorComponent*>( pManager->resolveComponent( self ) );
            if ( pDirector != nullptr )
                pDirector->spawnProjectile( request );
        } ) );
    }

    bool ArenaDirectorComponent::isAutoPlayOn() const
    {
        return _bAutoPlay || gv_arenaAutoPlay != 0;
    }

    const ArenaDirectorComponent* ArenaDirectorComponent::resolveDirector( const GameObjectManager& manager, GameObjectHandle director )
    {
        const GameObject* pObject = manager.resolveGameObject( director );
        return pObject != nullptr ? pObject->getComponent<ArenaDirectorComponent>() : nullptr;
    }

    const ArenaDirectorComponent* ArenaDirectorComponent::findForUnit( const AbilitySystemComponent& unit )
    {
        const GameObject*               pOwner      = unit.getOwner();
        const GameObjectManager*        pManager    = pOwner != nullptr ? pOwner->getManager() : nullptr;
        const ArenaControllerComponent* pController = pOwner != nullptr ? pOwner->getComponent<ArenaControllerComponent>() : nullptr;
        if ( pManager == nullptr || pController == nullptr )
            return nullptr;
        return resolveDirector( *pManager, pController->getDirector() );
    }

    // ------------------------------------------------------------------------------
    // 스폰(요청은 틱 안 · 세우기는 틱 뒤 게임 스레드)
    // ------------------------------------------------------------------------------
    void ArenaDirectorComponent::requestWave( bool bAdvance )
    {
        if ( bAdvance || _wave == 0 )
            ++_wave;
        playSound( ArenaDirectorComponentInternal::kSoundWave );
        const uint32 gruntCount  = 2u + _wave;
        const uint32 casterCount = _wave / 2u;
        const uint32 totalCount  = gruntCount + casterCount;
        const int32  level       = 1 + static_cast<int32>( ( _wave - 1u ) / 2u );
        for ( uint32 spawnIndex = 0; spawnIndex < totalCount; ++spawnIndex )
        {
            const float32       angle = 2.0f * MathUtil::Pi * static_cast<float32>( spawnIndex ) / static_cast<float32>( totalCount );
            const float3        position{ MathUtil::cos( angle ) * _waveRadius, 0.0f, MathUtil::sin( angle ) * _waveRadius };
            const ArenaUnitKind kind = spawnIndex < gruntCount ? ArenaUnitKind::Grunt : ArenaUnitKind::Caster;
            requestUnit( kind, position, level );
        }
        SW_LOG_INFO( "[Arena] wave %# - %# grunts, %# casters (level %#)", _wave, gruntCount, casterCount, level );
    }

    void ArenaDirectorComponent::requestUnit( ArenaUnitKind kind, const float3& position, int32 level )
    {
        SpawnRequest request;
        request._kind     = kind;
        request._position = position;
        request._level    = level;
        _listPendingUnit.push_back( request );
    }

    void ArenaDirectorComponent::scheduleFlush()
    {
        if ( _bFlushScheduled == SW_TRUE )
            return;
        GameObjectManager* pManager = getObjectManager();
        if ( pManager == nullptr )
            return;
        _bFlushScheduled = SW_TRUE;
        // 틱 안이면 틱 뒤로 미뤄진다. 그 사이에 디렉터가 사라질 수 있으니 핸들로 다시 찾는다.
        const ComponentHandle self = getHandle();
        pManager->executeOrDeferPostTick( [pManager, self]()
        {
            ArenaDirectorComponent* pDirector = static_cast<ArenaDirectorComponent*>( pManager->resolveComponent( self ) );
            if ( pDirector != nullptr )
                pDirector->flushPending();
        } );
    }

    void ArenaDirectorComponent::flushPending()
    {
        _bFlushScheduled            = SW_FALSE;
        GameObjectManager* pManager = getObjectManager();
        if ( pManager == nullptr || _bStarted == SW_FALSE )
            return;
        for ( const SpawnRequest& request : _listPendingUnit )
            (void)spawnUnit( *pManager, request ); // 하나가 실패해도 나머지는 선다 — 실패한 플레이어는 다시 서기 시간이 지나 다시 세운다
        _listPendingUnit.clear();
        for ( const utf8* pPath : _listPendingSound )
            (void)GameSound::play( pPath );
        _listPendingSound.clear();
    }

    bool ArenaDirectorComponent::spawnUnit( GameObjectManager& manager, const SpawnRequest& request )
    {
        AssetManager* pAssetManager = game::getService<AssetManager>();
        const bool    bPlayer       = request._kind == ArenaUnitKind::Player;
        const string& prefabPath    = bPlayer ? _playerPrefab : ( request._kind == ArenaUnitKind::Caster ? _casterPrefab : _gruntPrefab );
        const utf8*   pName         = bPlayer ? "ArenaPlayer" : ( request._kind == ArenaUnitKind::Caster ? "ArenaCaster" : "ArenaGrunt" );
        if ( pAssetManager == nullptr || prefabPath.empty() )
            return false;
        GameObject*               pObject        = pAssetManager->getPrefabCache().spawn( &manager, prefabPath, pName );
        MeshComponent*            pMesh          = pObject != nullptr ? pObject->getComponent<MeshComponent>() : nullptr;
        AbilitySystemComponent*   pAbilitySystem = pObject != nullptr ? pObject->getComponent<AbilitySystemComponent>() : nullptr;
        ArenaControllerComponent* pController    = pObject != nullptr ? pObject->getComponent<ArenaControllerComponent>() : nullptr;
        if ( pMesh == nullptr || pAbilitySystem == nullptr || pController == nullptr )
        {
            SW_LOG_WARNING( "[Arena] prefab '%#' needs a mesh, an ability system and an arena controller", prefabPath.c_str() );
            if ( pObject != nullptr )
                manager.destroyObject( pObject );
            return false;
        }

        // 카탈로그는 정하지 않는다 — 게임 서비스(`AbilityCatalog`)를 쓴다. 포인터를 박아 두면 모듈이 다시 올라온 뒤 옛 카탈로그를 가리킨다.
        const hashed_string setId = ArenaDirectorComponentInternal::findAbilitySetId( request._kind );
        if ( pAbilitySystem->grantAbilitySet( setId ) == false )
        {
            SW_LOG_WARNING( "[Arena] ability set '%#' is missing - is abilities.xml loaded?", setId.c_str() );
            manager.destroyObject( pObject );
            return false;
        }
        // 웨이브가 오르면 적의 체력 · 공격력이 오른다(이펙트의 perLevel 이 붙은 값만 커진다).
        if ( request._level > 1 )
        {
            const float32 levelScale = 1.0f + 0.15f * static_cast<float32>( request._level - 1 );
            (void)pAbilitySystem->setAttributeBaseValue( CombatAttributes::maxHealth(), pAbilitySystem->getAttributeBaseValue( CombatAttributes::maxHealth() ) * levelScale );
            (void)pAbilitySystem->setAttributeBaseValue( CombatAttributes::health(), pAbilitySystem->getAttributeBaseValue( CombatAttributes::maxHealth() ) );
            (void)pAbilitySystem->setAttributeBaseValue( CombatAttributes::attackPower(), pAbilitySystem->getAttributeBaseValue( CombatAttributes::attackPower() ) * levelScale );
        }

        applyTint( *pMesh, static_cast<int32>( request._kind ) );
        pMesh->setLocalPosition( request._position );
        const float3 facing = ArenaControllerComponent::flattenDirection( float3{ 0.0f, 0.0f, 0.0f } - request._position, float3{ 0.0f, 0.0f, 1.0f } );
        pController->assignDirector( getOwner()->getHandle(), facing );

        ArenaUnit unit;
        unit._object = pObject->getHandle();
        unit._kind   = request._kind;
        _listUnit.push_back( unit );
        return true;
    }

    void ArenaDirectorComponent::spawnProjectile( const ProjectileRequest& request )
    {
        GameObjectManager* pManager      = getObjectManager();
        AssetManager*      pAssetManager = game::getService<AssetManager>();
        if ( pManager == nullptr || pAssetManager == nullptr || _projectilePrefab.empty() || _bStarted == SW_FALSE )
            return;
        GameObject*               pObject     = pAssetManager->getPrefabCache().spawn( pManager, _projectilePrefab, "ArenaProjectile" );
        ArenaProjectileComponent* pProjectile = pObject != nullptr ? pObject->getComponent<ArenaProjectileComponent>() : nullptr;
        if ( pProjectile == nullptr )
        {
            if ( pObject != nullptr )
                pManager->destroyObject( pObject );
            return;
        }
        MeshComponent* pMesh = pObject->getComponent<MeshComponent>();
        if ( pMesh != nullptr )
            applyTint( *pMesh, ArenaDirectorComponentInternal::kProjectileTint );
        pProjectile->launch( getOwner()->getHandle(), request._position, request._velocity, request._range, request._spec, request._extraSpec,
                             request._bFromPlayer == SW_TRUE );
        pruneProjectiles();
        _listProjectile.push_back( pObject->getHandle() );
    }

    void ArenaDirectorComponent::playSound( const utf8* pPath )
    {
        _listPendingSound.push_back( pPath );
    }

    void ArenaDirectorComponent::applyTint( MeshComponent& mesh, int32 tintIndex )
    {
        shared_ptr<MaterialInstance>& tint = _arrTint[MathUtil::clamp( tintIndex, 0, ArenaDirectorComponentInternal::kProjectileTint )];
        if ( tint == nullptr && mesh.getMaterial() != nullptr )
        {
            const float4 arrColor[4] = { _playerTint, _gruntTint, _casterTint, _projectileTint };
            tint                     = MaterialInstance::create( mesh.getMaterial() );
            if ( tint != nullptr )
                tint->setVectorParameter( hashed_string( "color" ), arrColor[MathUtil::clamp( tintIndex, 0, 3 )] );
        }
        if ( tint != nullptr )
            mesh.setMaterialInstance( tint );
    }

    // ------------------------------------------------------------------------------
    // 갱신(PrePhysics — 이 그룹에는 디렉터만 돈다)
    // ------------------------------------------------------------------------------
    void ArenaDirectorComponent::updateUnits( float32 deltaTime )
    {
        GameObjectManager* pManager = getObjectManager();
        if ( pManager == nullptr )
            return;
        uint32 aliveEnemyCount    = 0;
        bool   bAnyEnemyLingering = false;
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
                unit._deathTimer = kDeathLinger;
                if ( unit._kind == ArenaUnitKind::Player )
                {
                    SW_LOG_INFO( "[Arena] the player fell on wave %# after %# kills", _wave, _killCount );
                    playSound( ArenaDirectorComponentInternal::kSoundPlayerFell );
                }
                else
                {
                    ++_killCount;
                    playSound( ArenaDirectorComponentInternal::kSoundEnemyFell );
                }
            }
            if ( unit._deathTimer >= 0.0f )
            {
                // 컨트롤러가 납작하게 만드는 동안 기다렸다가 걷는다.
                unit._deathTimer -= deltaTime;
                if ( unit._deathTimer <= 0.0f )
                {
                    pManager->destroyObject( pObject );
                    _listUnit.erase( _listUnit.begin() + static_cast<ptrdiff_t>( unitIndex - 1 ) );
                    continue;
                }
                if ( unit._kind != ArenaUnitKind::Player )
                    bAnyEnemyLingering = true;
                continue;
            }
            if ( unit._kind != ArenaUnitKind::Player )
                ++aliveEnemyCount;
        }
        const bool bWaveCleared = aliveEnemyCount == 0 && bAnyEnemyLingering == false && _listPendingUnit.empty();
        if ( bWaveCleared )
            requestWave( true );
    }

    void ArenaDirectorComponent::updateUnitViews()
    {
        // 이번 프레임의 모습 — 컨트롤러 · 투사체 · 카메라가 뒤 그룹에서 읽는다. 틱 전 자리다(틱 안의 쓰기는 틱 뒤에 보인다).
        _listUnitView.clear();
        _playerFocus                = float3{ 0.0f, 0.0f, 0.0f };
        GameObjectManager* pManager = getObjectManager();
        if ( pManager == nullptr )
            return;
        bool bFocusSet = false;
        for ( const ArenaUnit& unit : _listUnit )
        {
            GameObject*                   pObject        = pManager->resolveGameObject( unit._object );
            const MeshComponent*          pMesh          = pObject != nullptr ? pObject->getComponent<MeshComponent>() : nullptr;
            const AbilitySystemComponent* pAbilitySystem = pObject != nullptr ? pObject->getComponent<AbilitySystemComponent>() : nullptr;
            if ( pMesh == nullptr || pAbilitySystem == nullptr )
                continue;
            ArenaUnitView view;
            view._object      = unit._object;
            view._position    = pMesh->getWorldPosition();
            view._kind        = unit._kind;
            view._bAlive      = unit._deathTimer < 0.0f && pAbilitySystem->hasMatchingTag( CombatAttributeSet::getDeadTag() ) == false ? SW_TRUE : SW_FALSE;
            view._bPlayerTeam = pAbilitySystem->hasMatchingTag( "Team.Player"_tag ) ? SW_TRUE : SW_FALSE;
            view._bEnemyTeam  = pAbilitySystem->hasMatchingTag( "Team.Enemy"_tag ) ? SW_TRUE : SW_FALSE;
            _listUnitView.push_back( view );
            if ( unit._kind == ArenaUnitKind::Player && bFocusSet == false )
            {
                _playerFocus = view._position;
                bFocusSet    = true;
            }
        }
    }

    void ArenaDirectorComponent::updatePlayerRespawn( float32 deltaTime )
    {
        bool bPlayerPresent = hasPendingUnit( ArenaUnitKind::Player );
        for ( const ArenaUnit& unit : _listUnit )
            bPlayerPresent = bPlayerPresent || unit._kind == ArenaUnitKind::Player;
        if ( bPlayerPresent )
        {
            _playerRespawnTimer = -1.0f;
            return;
        }
        // 쓰러진 플레이어는 걷힌 뒤 잠시 있다가 가운데에 다시 선다(웨이브는 이어진다).
        if ( _playerRespawnTimer < 0.0f )
            _playerRespawnTimer = _playerRespawnDelay;
        _playerRespawnTimer -= deltaTime;
        if ( _playerRespawnTimer <= 0.0f )
        {
            _playerRespawnTimer = -1.0f;
            requestUnit( ArenaUnitKind::Player, float3{ 0.0f, 0.0f, 0.0f }, 1 );
        }
    }

    void ArenaDirectorComponent::logStatus( float32 deltaTime )
    {
        _statusLogTimer += deltaTime;
        if ( _statusLogTimer < ArenaDirectorComponentInternal::kStatusLogInterval )
            return;
        _statusLogTimer             = 0.0f;
        GameObjectManager* pManager = getObjectManager();
        if ( pManager == nullptr )
            return;
        for ( const ArenaUnit& unit : _listUnit )
        {
            GameObject*                   pObject        = unit._kind == ArenaUnitKind::Player ? pManager->resolveGameObject( unit._object ) : nullptr;
            const AbilitySystemComponent* pAbilitySystem = pObject != nullptr ? pObject->getComponent<AbilitySystemComponent>() : nullptr;
            if ( pAbilitySystem == nullptr )
                continue;
            [[maybe_unused]] const int32 health = static_cast<int32>( pAbilitySystem->getAttributeValue( CombatAttributes::health() ) );
            [[maybe_unused]] const int32 mana   = static_cast<int32>( pAbilitySystem->getAttributeValue( CombatAttributes::mana() ) );
            SW_LOG_INFO( "[Arena] wave %# · kills %# · player HP %# · MP %# · units %# · projectiles %#", _wave, _killCount, health, mana,
                         static_cast<uint32>( _listUnit.size() ), static_cast<uint32>( _listProjectile.size() ) );
        }
    }

    bool ArenaDirectorComponent::hasPendingUnit( ArenaUnitKind kind ) const
    {
        for ( const SpawnRequest& request : _listPendingUnit )
        {
            if ( request._kind == kind )
                return true;
        }
        return false;
    }

    void ArenaDirectorComponent::pruneProjectiles()
    {
        // 다 난 투사체는 스스로 지운다 — 목록은 상태 저장 전에 걷을 것만 든다(풀리지 않는 핸들은 덜어 낸다).
        GameObjectManager* pManager = getObjectManager();
        for ( size_t projectileIndex = _listProjectile.size(); projectileIndex > 0; --projectileIndex )
        {
            if ( pManager == nullptr || pManager->resolveGameObject( _listProjectile[projectileIndex - 1] ) == nullptr )
            {
                _listProjectile[projectileIndex - 1] = _listProjectile.back();
                _listProjectile.pop_back();
            }
        }
    }

    GameObjectManager* ArenaDirectorComponent::getObjectManager() const
    {
        GameObject* pOwner = getOwner();
        return pOwner != nullptr ? pOwner->getManager() : nullptr;
    }
} // namespace sw

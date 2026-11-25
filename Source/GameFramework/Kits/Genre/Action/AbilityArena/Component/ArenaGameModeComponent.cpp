#include "pch.h"

#include "GameFramework/Kits/Genre/Action/AbilityArena/Component/ArenaGameModeComponent.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Encoding/FourCCUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Foundation/WorldOrigin.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"
#include "Engine/Visual/Component/MeshComponent.h"

#include "GameFramework/Kits/Genre/Action/AbilityArena/ArenaProjectileQuery.h"
#include "GameFramework/Kits/Genre/Action/AbilityArena/Component/ArenaUnitComponent.h"
#include "GameFramework/Runtime/Character/Combat/Projectile/ProjectileDef.h"
#include "GameFramework/Runtime/Character/Combat/Projectile/ResolvedShot.h"
#include "GameFramework/Runtime/Character/Control/ControlSystem.h"
#include "GameFramework/Runtime/Character/Control/Controller/AIControllerComponent.h"
#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"
#include "GameFramework/Runtime/Foundation/Service/GameService.h"
#include "GameFramework/Runtime/Gameplay/Ability/AbilityCatalog.h"
#include "GameFramework/Runtime/Gameplay/Ability/AbilitySystemComponent.h"
#include "GameFramework/Runtime/Gameplay/Ability/CombatAttributeSet.h"

namespace sw
{
    namespace
    {
        struct ArenaGameModeComponentInternal
        {
            static constexpr uint32 kStateTag     = FourCCUtil::make( "ARNA" );
            static constexpr uint32 kStateVersion = 1;

            // 사운드 이벤트 이름 — 클립 · 범위 · 상한은 팩의 사운드 이벤트 데이터(audioevents.xml)가 정한다.
            static constexpr const utf8* kEventWave       = "ArenaWaveStart";
            static constexpr const utf8* kEventPlayerFell = "ArenaPlayerFell";
            static constexpr const utf8* kEventEnemyFell  = "ArenaEnemyFell";

            [[nodiscard]] static bool readPlayerX( const ArenaGameModeComponent& gameMode, float64& outValue )
            {
                float3 position{};
                if ( gameMode.findUnitPosition( gameMode.getPlayerObject(), position ) == false )
                    return false;
                outValue = static_cast<float64>( position._x );
                return true;
            }

            [[nodiscard]] static bool readPlayerZ( const ArenaGameModeComponent& gameMode, float64& outValue )
            {
                float3 position{};
                if ( gameMode.findUnitPosition( gameMode.getPlayerObject(), position ) == false )
                    return false;
                outValue = static_cast<float64>( position._z );
                return true;
            }

            /**
             * @brief 맞은 유닛 @p target 에 짐의 스펙을 틱 뒤(게임 스레드)에 겁니다. 대상은 다른 워커가 틱하고 있을 수 있다. 무적이면 이펙트의 BlockedTag 가 막는다.
             * @details 맞은 자리(탄이 닿은 점 · 나는 방향)를 피해 스펙에 싣는다 — 이 탄으로 쓰러지면 치명 신호가 그 방향을 쓴다(`AbilityEffectSpec::setHit`).
             */
            static void applyCargo( GameObjectManager& manager, GameObjectHandle target, const AbilityEffectSpec& cargoSpec, const AbilityEffectSpec& extraSpec, const ProjectileEvent& event )
            {
                GameObjectManager* pManager = &manager;
                AbilityEffectSpec  spec     = cargoSpec;
                HitInfo            hit;
                hit._point     = manager.getWorldOrigin().toLocal( event._hit._position );
                hit._normal    = event._hit._normal;
                hit._direction = event._projectile.getSpeed() > MathUtil::kEpsilon ? event._projectile._velocity.normalize() : float3{};
                spec.setHit( hit );
                manager.executeOrDeferPostTick( SW_DELEGATE_LAMBDA( GameObjectManager::PostTickDelegate, [pManager, target, spec, extraSpec]()
                {
                    GameObject*             pTarget        = pManager->resolveGameObject( target );
                    AbilitySystemComponent* pAbilitySystem = pTarget != nullptr ? pTarget->getComponent<AbilitySystemComponent>() : nullptr;
                    if ( pAbilitySystem == nullptr )
                        return;
                    if ( spec.isValid() )
                        (void)pAbilitySystem->applyGameplayEffectSpecToSelf( spec ); // 대상 태그 조건에 막히면 무효 핸들 — 안 걸리는 것이 규칙이다
                    if ( extraSpec.isValid() )
                        (void)pAbilitySystem->applyGameplayEffectSpecToSelf( extraSpec ); // 대상 태그 조건에 막히면 무효 핸들 — 안 걸리는 것이 규칙이다
                    // 피격음은 이펙트의 큐(`AbilityPresentation.Hit.*`)가 카탈로그의 `<AbilityPresentation>` 로 낸다 — 여기서 따로 내지 않는다.
                } ) );
            }
        };
    } // namespace

    SW_AUTOMATION_COMPONENT_WATCH( arenaPlayerX, "Arena.PlayerX", "World X of the arena player", ArenaGameModeComponent, &ArenaGameModeComponentInternal::readPlayerX );
    SW_AUTOMATION_COMPONENT_WATCH( arenaPlayerZ, "Arena.PlayerZ", "World Z of the arena player", ArenaGameModeComponent, &ArenaGameModeComponentInternal::readPlayerZ );
    SW_AUTOMATION_GETTER_WATCH( arenaPlayerShotCount, "Arena.PlayerShotCount", "Projectiles the player team fired this game", ArenaGameModeComponent, &ArenaGameModeComponent::getPlayerShotCount );
    SW_AUTOMATION_GETTER_WATCH( arenaKillCount, "Arena.KillCount", "Enemies felled this game", ArenaGameModeComponent, &ArenaGameModeComponent::getKillCount );
    SW_AUTOMATION_GETTER_WATCH( arenaPlayerControllerKind, "Arena.PlayerControllerKind", "0 = the player controller holds the player, 1 = the auto battle AI", ArenaGameModeComponent, &ArenaGameModeComponent::isPlayerDrivenByAI );
} // namespace sw

namespace sw
{
    ArenaGameModeComponent::ArenaGameModeComponent()
        : _playerAbilitySet{}
        , _gruntAbilitySet{}
        , _casterAbilitySet{}
        , _playerPrefab{}
        , _gruntPrefab{}
        , _casterPrefab{}
        , _gruntAIPrefab{}
        , _casterAIPrefab{}
        , _autoBattleAIPrefab{}
        , _enemyPool{}
        , _enemyAIPool{}
        , _arenaHalfSize{ 14.0f }
        , _waveRadius{ 11.0f }
        , _baseGruntCount{ 2u }
        , _wavesPerCaster{ 2u }
        , _wavesPerLevel{ 2u }
        , _statScalePerLevel{ 0.15f }
        , _hostileSearchRadius{ 0.0f }
        , _deathLifeSpan{ 0.8f }
        , _playerRespawnDelay{ 2.5f }
        , _statusLogInterval{ 5.0f }
        , _unitRadius{ 0.5f }
        , _listUnit{}
        , _listUnitView{}
        , _projectiles{}
        , _listProjectileCargo{}
        , _listPendingUnit{}
        , _playerFocus{ 0.0f, 0.0f, 0.0f }
        , _playerObject{}
        , _autoBattleObject{}
        , _playerRespawn{}
        , _wave{ 0 }
        , _killCount{ 0 }
        , _playerShotCount{ 0 }
        , _nextUnitID{ 0 }
        , _bUnitsRequested{ SW_FALSE }
        , _bPossessionDirty{ SW_FALSE }
        , _reserved{ 0 }
    {
        _projectiles.setPlaneNormal( float3{ 0.0f, 1.0f, 0.0f } ); // 탄은 아레나 바닥 위 발 높이 평면에서 난다
    }

    ArenaGameModeComponent::~ArenaGameModeComponent() = default;

    bool ArenaGameModeComponent::startGame()
    {
        const AbilityCatalog* pCatalog = game::findSubsystem<const AbilityCatalog>();
        if ( pCatalog == nullptr || pCatalog->getAbilitySetCount() == 0 )
        {
            SW_LOG_ERROR_ID( "ArenaGameMode.AbilityCatalogNotLoaded", "[Arena] the ability catalog is not loaded - the arena cannot start" );
            return false;
        }
        if ( _hostileSearchRadius <= 0.0f )
        {
            SW_LOG_ERROR_ID( "ArenaGameMode.HostileSearchRadiusMissing", "[Arena] the game mode needs _hostileSearchRadius above 0 - the arena cannot start" );
            return false;
        }
        bool bSetsKnown = true;
        for ( const CatalogKey<AbilitySetDef>* pSetID : { &_playerAbilitySet, &_gruntAbilitySet, &_casterAbilitySet } )
        {
            if ( pCatalog->findAbilitySet( pSetID->key() ) != nullptr )
                continue;
            SW_LOG_ERROR_ID( "ArenaGameMode.DirectorAbilitySetNot", "[Arena] the game mode's ability set '%#' is not in the ability catalog - the arena cannot start", pSetID->c_str() );
            bSetsKnown = false;
        }
        if ( bSetsKnown == false )
            return false;
        startStatusLog( _statusLogInterval );
        return true;
    }

    const hashed_string& ArenaGameModeComponent::findAbilitySetID( ArenaUnitKind kind ) const
    {
        switch ( kind )
        {
            case ArenaUnitKind::Player:
                return _playerAbilitySet.key();
            case ArenaUnitKind::Grunt:
                return _gruntAbilitySet.key();
            case ArenaUnitKind::Caster:
                return _casterAbilitySet.key();
        }
        return _playerAbilitySet.key(); // 열거자는 셋뿐이다(covered-switch) — 닿지 않는다
    }

    void ArenaGameModeComponent::onGameStarted()
    {
        SW_LOG_INFO_ID( "ArenaGameMode.ArenaReadyWASDMove", "[Arena] arena is ready - WASD move, J/Space melee, K/2 fireball, L/3 heal, LeftShift/4 dash" );
    }

    void ArenaGameModeComponent::serializeGameState( StateIO& io )
    {
        // 웨이브 · 처치 수는 상태 칸이다(`GameModeComponent::serializeState` 가 싣는다) — 손 본문은 판 머리만.
        io.header( ArenaGameModeComponentInternal::kStateTag, ArenaGameModeComponentInternal::kStateVersion );
        io.end();
    }

    void ArenaGameModeComponent::onStateRestored( bool bRestored )
    {
        // 어느 쪽이든 정리된다. 다음 틱이 플레이어와 현재 웨이브를 다시 스폰한다.
        if ( bRestored )
            SW_LOG_INFO_ID( "ArenaGameMode.ArenaStateRestoredWave", "[Arena] arena state restored - wave %#, %# kills", _wave, _killCount );
        else
            SW_LOG_WARNING_ID( "ArenaGameMode.SavedArenaStateDoes", "[Arena] the saved arena state does not match this build - starting from wave 1" );
    }

    void ArenaGameModeComponent::updateGame( float32 deltaTime )
    {
        if ( _bUnitsRequested == SW_FALSE )
        {
            // 처음이거나 상태를 저장하기 전에 정리한 뒤다. 플레이어와 현재 웨이브를 스폰한다.
            _bUnitsRequested = SW_TRUE;
            requestUnit( ArenaUnitKind::Player, float3{ 0.0f, 0.0f, 0.0f }, 1 );
            requestWave( _wave == 0 );
        }
        if ( deltaTime > 0.0f )
        {
            updateUnits( deltaTime );
            updatePlayerRespawn( deltaTime );
        }
        updateUnitViews();
        if ( deltaTime > 0.0f )
            stepProjectiles( deltaTime ); // 방금 적은 스냅샷으로 맞음을 본다

        // 빙의는 틱이 끝난 뒤에 한다. 자동 플레이 스위치와 플레이어 폰을 조종하는 조종자가 다르면 플러시를 잡는다(`hasPendingSpawn`).
        const PawnComponent* pPlayerPawn = findPlayerPawn();
        if ( pPlayerPawn != nullptr )
        {
            const bool bWantAI = isAutoPlayOn();
            _bPossessionDirty  = pPlayerPawn->isPossessed() == false || bWantAI != isPlayerDrivenByAI() ? SW_TRUE : SW_FALSE;
        }
    }

    void ArenaGameModeComponent::onViewsDespawned()
    {
        _listUnit.clear();
        _listUnitView.clear();
        _projectiles.clear(); // 짐(스펙)은 런타임 값이라 상태에 싣지 않는다 — 유닛과 함께 걷는다
        _listProjectileCargo.clear();
        _listPendingUnit.clear();
        _playerRespawn.reset();
        _autoBattleObject = GameObjectHandle{};
        _bUnitsRequested  = SW_FALSE;
        _bPossessionDirty = SW_FALSE;
    }

    // ------------------------------------------------------------------------------
    // 다른 컴포넌트가 읽는 것
    // ------------------------------------------------------------------------------
    const ArenaUnitView* ArenaGameModeComponent::findUnitView( GameObjectHandle object ) const
    {
        const ArenaUnitView* pView = _listUnitView.data();
        for ( size_t viewIndex = 0; viewIndex < _listUnitView.size(); ++viewIndex )
        {
            if ( pView[viewIndex]._object == object )
                return pView + viewIndex;
        }
        return nullptr;
    }

    const ArenaUnitView* ArenaGameModeComponent::findNearestHostileView( GameObjectHandle from, float32 maxRange ) const
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
            const bool bNotHostileTarget = candidate._object == from || candidate._bAlive == SW_FALSE || bHostile == false;
            if ( bNotHostileTarget )
                continue;
            const float32 distance = float3::getDistance( pFrom->_position._value, candidate._position._value );
            if ( distance <= nearestDistance )
            {
                nearestDistance = distance;
                pNearest        = &candidate;
            }
        }
        return pNearest;
    }

    bool ArenaGameModeComponent::findUnitPosition( GameObjectHandle object, float3& outPosition ) const
    {
        const GameObjectManager* pManager = getObjectManager();
        const GameObject*        pObject  = pManager != nullptr && findUnitView( object ) != nullptr ? pManager->resolveGameObject( object ) : nullptr;
        // 유닛 틱(병렬)에서도 불린다 — 트랜스폼은 남 읽기 길로 찾는다(`kTickReadSafe`).
        const MeshComponent* pMesh = pObject != nullptr ? pObject->getTickSafeComponent<MeshComponent>() : nullptr;
        if ( pMesh == nullptr )
            return false;
        outPosition = pMesh->getWorldPosition();
        return true;
    }

    bool ArenaGameModeComponent::findNearestHostilePosition( GameObjectHandle from, float3& outPosition ) const
    {
        const ArenaUnitView* pNearest = findNearestHostileView( from, _hostileSearchRadius );
        return pNearest != nullptr && findUnitPosition( pNearest->_object, outPosition );
    }

    bool ArenaGameModeComponent::isPlayerDrivenByAI() const
    {
        const GameObjectManager* pManager    = getObjectManager();
        const PawnComponent*     pPlayerPawn = findPlayerPawn();
        return pManager != nullptr && pPlayerPawn != nullptr && ControlSystem::isPawnControlledBy( *pManager, *pPlayerPawn, _autoBattleObject );
    }

    const PawnComponent* ArenaGameModeComponent::findPlayerPawn() const
    {
        return findOn<PawnComponent>( _playerObject );
    }

    GameObjectHandle ArenaGameModeComponent::findNearestHostile( const AbilitySystemComponent& from, float32 maxRange ) const
    {
        const GameObject*    pFromOwner = from.getOwner();
        const ArenaUnitView* pNearest   = pFromOwner != nullptr ? findNearestHostileView( pFromOwner->getHandle(), maxRange ) : nullptr;
        return pNearest != nullptr ? pNearest->_object : GameObjectHandle{};
    }

    void ArenaGameModeComponent::launchProjectile( const AbilitySystemComponent& from, const float3& facing, const hashed_string& projectileID, const AbilityEffectSpec& spec,
                                                   const AbilityEffectSpec& extraSpec ) const
    {
        const GameObject*        pFrom     = from.getOwner();
        const ArenaUnitView*     pFromView = pFrom != nullptr ? findUnitView( pFrom->getHandle() ) : nullptr;
        const GameObjectManager* pManager  = getObjectManager();
        if ( pFromView == nullptr || pManager == nullptr )
            return;

        ProjectileRequest request;
        request._spec         = spec;
        request._extraSpec    = extraSpec;
        request._projectileID = projectileID;
        request._origin       = pFromView->_position._value;
        request._direction    = facing;
        request._bFromPlayer  = from.getOwnedTagCounts().hasMatchingTag( "Team.Player"_tag ) ? SW_TRUE : SW_FALSE;
        // 어빌리티는 워커(유닛의 틱)에서 쏘고, 오브젝트는 틱이 끝난 뒤 게임 스레드에서 스폰한다. 그 사이 디렉터가 사라질 수 있으니 핸들로 다시 찾는다.
        const TComponentHandle<ArenaGameModeComponent> self{ this };
        pManager->executeOrDeferPostTickOnScene( [self, request]( GameObjectManager& sceneManager )
        {
            ArenaGameModeComponent* pGameMode = sceneManager.resolveComponent( self );
            if ( pGameMode != nullptr )
                pGameMode->spawnProjectile( request );
        } );
    }

    const ArenaGameModeComponent* ArenaGameModeComponent::findForUnit( const AbilitySystemComponent& unit )
    {
        const GameObject*         pOwner   = unit.getOwner();
        const GameObjectManager*  pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        const ArenaUnitComponent* pUnit    = pOwner != nullptr ? pOwner->getComponent<ArenaUnitComponent>() : nullptr;
        if ( pManager == nullptr || pUnit == nullptr )
            return nullptr;
        return resolve<ArenaGameModeComponent>( *pManager, pUnit->getGameMode() );
    }

    const ArenaGameModeComponent* ArenaGameModeComponent::findForPawn( const PawnComponent& pawn )
    {
        const GameObject*         pOwner   = pawn.getOwner();
        const GameObjectManager*  pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        const ArenaUnitComponent* pUnit    = pOwner != nullptr ? pOwner->getComponent<ArenaUnitComponent>() : nullptr;
        if ( pManager == nullptr || pUnit == nullptr )
            return nullptr;
        return resolve<ArenaGameModeComponent>( *pManager, pUnit->getGameMode() );
    }

    // ------------------------------------------------------------------------------
    // 스폰. 요청은 틱 안에서 하고, 스폰은 틱이 끝난 뒤 게임 스레드에서 한다.
    // ------------------------------------------------------------------------------
    void ArenaGameModeComponent::requestWave( bool bAdvance )
    {
        if ( bAdvance || _wave == 0 )
            ++_wave;
        getSoundQueue().queueEvent( ArenaGameModeComponentInternal::kEventWave );
        const uint32 gruntCount  = _baseGruntCount + _wave;
        const uint32 casterCount = _wave / _wavesPerCaster;
        const uint32 totalCount  = gruntCount + casterCount;
        const int32  level       = 1 + static_cast<int32>( ( _wave - 1u ) / _wavesPerLevel );
        for ( uint32 spawnIndex = 0; spawnIndex < totalCount; ++spawnIndex )
        {
            const float32       angle = 2.0f * MathUtil::kPi * static_cast<float32>( spawnIndex ) / static_cast<float32>( totalCount );
            const float3        position{ MathUtil::cos( angle ) * _waveRadius, 0.0f, MathUtil::sin( angle ) * _waveRadius };
            const ArenaUnitKind kind = spawnIndex < gruntCount ? ArenaUnitKind::Grunt : ArenaUnitKind::Caster;
            requestUnit( kind, position, level );
        }
        SW_LOG_INFO_ID( "ArenaGameMode.WaveGruntsCastersLevel", "[Arena] wave %# - %# grunts, %# casters (level %#)", _wave, gruntCount, casterCount, level );
    }

    void ArenaGameModeComponent::requestUnit( ArenaUnitKind kind, const float3& position, int32 level )
    {
        SpawnRequest request;
        request._kind     = kind;
        request._position = position;
        request._level    = level;
        _listPendingUnit.push_back( request );
    }

    void ArenaGameModeComponent::onFlush( GameObjectManager& manager, bool bRespawnViews )
    {
        (void)bRespawnViews; // 플레이어와 웨이브는 틱이 요청한다(`_bUnitsRequested`). 여기서는 쌓인 것만 스폰한다.
        for ( const SpawnRequest& request : _listPendingUnit )
        {
            (void)spawnUnit( manager, request ); // 하나가 실패해도 나머지는 스폰한다. 실패한 플레이어는 다시 등장할 시간이 지나면 다시 스폰한다.
        }
        _listPendingUnit.clear();
        syncPlayerPossession( manager );
    }

    bool ArenaGameModeComponent::spawnUnit( GameObjectManager& manager, const SpawnRequest& request )
    {
        const bool              bPlayer             = request._kind == ArenaUnitKind::Player;
        const string&           prefabPath          = findUnitPrefabPath( request._kind );
        const utf8*             pName               = bPlayer ? "ArenaPlayer" : ( request._kind == ArenaUnitKind::Caster ? "ArenaCaster" : "ArenaGrunt" );
        GameObject*             pObject             = spawnPrefab( manager, prefabPath, pName, bPlayer ? PrefabPoolPolicy{} : _enemyPool ); // 플레이어 몸은 다시 등장할 때만 스폰한다
        GameObjectHandle        handle              = pObject != nullptr ? pObject->getHandle() : GameObjectHandle{};
        MeshComponent*          pMesh               = pObject != nullptr ? pObject->getComponent<MeshComponent>() : nullptr;
        AbilitySystemComponent* pAbilitySystem      = pObject != nullptr ? pObject->getComponent<AbilitySystemComponent>() : nullptr;
        ArenaUnitComponent*     pUnitComponent      = pObject != nullptr ? pObject->getComponent<ArenaUnitComponent>() : nullptr;
        PawnComponent*          pPawn               = pObject != nullptr ? pObject->getComponent<PawnComponent>() : nullptr;
        const bool              bMissingPrefabParts = pMesh == nullptr || pAbilitySystem == nullptr || pUnitComponent == nullptr || pPawn == nullptr;
        if ( bMissingPrefabParts )
        {
            SW_LOG_WARNING_ID( "ArenaGameMode.PrefabNeedsMeshAbility", "[Arena] prefab '%#' needs a mesh, an ability system, a pawn and an arena unit", prefabPath.c_str() );
            releaseSpawned( manager, prefabPath, handle );
            return false;
        }

        // 카탈로그는 이 컴포넌트가 정하지 않는다. 게임 서비스(`AbilityCatalog`)를 쓴다. 포인터를 박아 두면 모듈이 다시 로드된 뒤 옛 카탈로그를 가리킨다.
        const hashed_string& setID = findAbilitySetID( request._kind );
        if ( pAbilitySystem->grantAbilitySet( setID ) == false )
        {
            SW_LOG_WARNING_ID( "ArenaGameMode.AbilitySetMissingAbilities", "[Arena] ability set '%#' is missing - is abilities.xml loaded?", setID.c_str() );
            releaseSpawned( manager, prefabPath, handle );
            return false;
        }
        // 웨이브가 오르면 적의 체력과 공격력이 오른다(이펙트의 perLevel 이 붙은 값만 커진다).
        if ( request._level > 1 )
        {
            const float32           levelScale  = 1.0f + _statScalePerLevel * static_cast<float32>( request._level - 1 );
            const optional<float32> maxHealth   = pAbilitySystem->getAttributeBaseValue( CombatAttributes::maxHealth() );
            const optional<float32> attackPower = pAbilitySystem->getAttributeBaseValue( CombatAttributes::attackPower() );
            if ( maxHealth.has_value() == false || attackPower.has_value() == false )
            {
                SW_LOG_ERROR_ID( "ArenaGameMode.AbilitySetGivesNo", "[Arena] ability set '%#' gives no MaxHealth / AttackPower - the unit is not spawned", setID.c_str() );
                releaseSpawned( manager, prefabPath, handle );
                return false;
            }
            (void)pAbilitySystem->setAttributeBaseValue( CombatAttributes::maxHealth(), *maxHealth * levelScale );
            (void)pAbilitySystem->setAttributeBaseValue( CombatAttributes::health(), *maxHealth * levelScale );
            (void)pAbilitySystem->setAttributeBaseValue( CombatAttributes::attackPower(), *attackPower * levelScale );
        }

        pMesh->setLocalPosition( manager.getWorldOrigin().toLocal( request._position ) );
        const float3 facing = ArenaUnitComponent::flattenDirection( float3{ 0.0f, 0.0f, 0.0f } - request._position, float3{ 0.0f, 0.0f, 1.0f } );
        pUnitComponent->assignGameMode( getOwner()->getHandle(), facing );

        ArenaUnit unit;
        unit._object = pObject->getHandle();
        unit._unitID = _nextUnitID++;
        unit._kind   = request._kind;
        if ( bPlayer == false )
            unit._controller = spawnEnemyController( manager, request._kind, *pPawn );
        _listUnit.push_back( unit );
        return true;
    }

    GameObjectHandle ArenaGameModeComponent::spawnEnemyController( GameObjectManager& manager, ArenaUnitKind kind, PawnComponent& pawn )
    {
        const string&          prefabPath = findEnemyAIPrefabPath( kind );
        GameObject*            pObject    = spawnPrefab( manager, prefabPath, kind == ArenaUnitKind::Caster ? "ArenaCasterAI" : "ArenaGruntAI", _enemyAIPool );
        AIControllerComponent* pAI        = pObject != nullptr ? pObject->getComponent<AIControllerComponent>() : nullptr;
        if ( pAI == nullptr )
        {
            SW_LOG_WARNING_ID( "ArenaGameMode.AIPrefabNoAI", "[Arena] AI prefab '%#' has no AI controller - the enemy stands still", prefabPath.c_str() );
            GameObjectHandle handle = pObject != nullptr ? pObject->getHandle() : GameObjectHandle{};
            releaseSpawned( manager, prefabPath, handle );
            return GameObjectHandle{};
        }
        pAI->possess( pawn );
        return pObject->getHandle();
    }

    const string& ArenaGameModeComponent::findUnitPrefabPath( ArenaUnitKind kind ) const
    {
        if ( kind == ArenaUnitKind::Player )
            return _playerPrefab.path();
        return ( kind == ArenaUnitKind::Caster ? _casterPrefab : _gruntPrefab ).path();
    }

    const string& ArenaGameModeComponent::findEnemyAIPrefabPath( ArenaUnitKind kind ) const
    {
        return ( kind == ArenaUnitKind::Caster ? _casterAIPrefab : _gruntAIPrefab ).path();
    }

    void ArenaGameModeComponent::retireUnit( GameObjectManager& manager, ArenaUnit& inoutUnit )
    {
        // 조종자를 먼저 돌려준다 — 되돌리기가 몸을 놓은 뒤 몸의 폰이 비어 돌아간다.
        if ( inoutUnit._kind != ArenaUnitKind::Player )
            releaseSpawned( manager, findEnemyAIPrefabPath( inoutUnit._kind ), inoutUnit._controller );
        releaseSpawned( manager, findUnitPrefabPath( inoutUnit._kind ), inoutUnit._object );
    }

    void ArenaGameModeComponent::syncPlayerPossession( GameObjectManager& manager )
    {
        _bPossessionDirty    = SW_FALSE;
        PawnComponent* pPawn = findOn<PawnComponent>( _playerObject );
        if ( pPawn == nullptr )
            return;
        if ( isAutoPlayOn() )
        {
            GameObject*            pObject = manager.resolveGameObject( _autoBattleObject );
            AIControllerComponent* pAI     = pObject != nullptr ? pObject->getComponent<AIControllerComponent>() : nullptr;
            if ( pAI == nullptr )
            {
                pObject           = spawnPrefab( manager, _autoBattleAIPrefab, "ArenaAutoBattleAI" );
                pAI               = pObject != nullptr ? pObject->getComponent<AIControllerComponent>() : nullptr;
                _autoBattleObject = pObject != nullptr ? pObject->getHandle() : GameObjectHandle{};
            }
            if ( pAI == nullptr )
            {
                SW_LOG_WARNING_ID( "ArenaGameMode.AutoBattlePrefabNo", "[Arena] auto battle prefab '%#' has no AI controller - auto play cannot take the player", _autoBattleAIPrefab.c_str() );
                return;
            }
            if ( isPlayerDrivenByAI() == false )
                pAI->possess( *pPawn );
            return;
        }
        // 자동 플레이를 끄면 플레이어 0 의 조종자가 되찾는다(씬에 없으면 만든다).
        (void)ControlSystem::returnPawnToPlayer( manager, *pPawn, _autoBattleObject );
    }

    void ArenaGameModeComponent::spawnProjectile( const ProjectileRequest& request )
    {
        if ( isStarted() == false )
            return;
        // 날아가는 값은 탄 정의다(어빌리티는 어느 탄인지와 실을 스펙만 정한다). 아레나에는 벽이 없어 날지 않거나 사거리가 없는 탄은 받지 않는다.
        const ProjectileCatalog* pCatalog    = game::findSubsystem<const ProjectileCatalog>();
        const ProjectileDef*     pProjectile = pCatalog != nullptr ? pCatalog->find( request._projectileID ) : nullptr;
        if ( pProjectile == nullptr || pProjectile->_speed <= 0.0f || pProjectile->_range <= 0.0f )
        {
            SW_LOG_ERROR_ID( "ArenaGameMode.ProjectileUnusable", "[Arena] projectile '%#' is not in the projectile catalog or does not fly with a range - not fired", request._projectileID.c_str() );
            return;
        }
        const ResolvedShot shot       = ResolvedShot::resolve( *pProjectile, {}, {} );
        const float3       origin     = request._origin + request._direction * ( _unitRadius + shot._radius );
        ProjectileState    projectile = ProjectileState::launch( shot, origin, request._direction, 0.0f ); // 바닥 평면에 묶여 중력은 의미가 없다
        projectile._team              = request._bFromPlayer == SW_TRUE ? ArenaProjectileQuery::kTeamPlayer : ArenaProjectileQuery::kTeamEnemy;
        ProjectileCargo cargo;
        cargo._spec         = request._spec;
        cargo._extraSpec    = request._extraSpec;
        cargo._projectileID = _projectiles.spawn( projectile );
        _listProjectileCargo.push_back( cargo );
        if ( request._bFromPlayer == SW_TRUE )
            ++_playerShotCount;
    }

    const ProjectileState* ArenaGameModeComponent::findProjectile( uint32 projectileID ) const
    {
        const vector<ProjectileState>& listProjectile = _projectiles.getProjectiles();
        const ProjectileState*         pProjectile    = listProjectile.data(); // 여러 모습이 같은 그룹에서 함께 읽으므로 첨자 대신 포인터로
        for ( size_t index = 0; index < listProjectile.size(); ++index )
        {
            if ( pProjectile[index]._id == projectileID )
                return pProjectile + index;
        }
        return nullptr;
    }

    void ArenaGameModeComponent::stepProjectiles( float32 deltaTime )
    {
        const ArenaProjectileQuery query( _listUnitView, _unitRadius, _arenaHalfSize + 2.0f );
        GameObjectManager*         pManager = getObjectManager();
        for ( const ProjectileEvent& event : _projectiles.step( deltaTime, query ) )
        {
            const uint32         projectileID = event._projectile._id;
            const bool           bUnitHit     = event._kind == ProjectileEvent::Kind::Hit && event._hit._kind == ProjectileHitKind::Target && pManager != nullptr;
            const ArenaUnitView* pVictim      = bUnitHit ? query.findView( event._hit._other ) : nullptr;
            if ( pVictim != nullptr )
            {
                for ( const ProjectileCargo& cargo : _listProjectileCargo )
                {
                    if ( cargo._projectileID == projectileID )
                        ArenaGameModeComponentInternal::applyCargo( *pManager, pVictim->_object, cargo._spec, cargo._extraSpec, event );
                }
            }
            if ( event._projectile._bAlive == SW_FALSE )
                dropCargo( projectileID );
        }
    }

    void ArenaGameModeComponent::dropCargo( uint32 projectileID )
    {
        for ( size_t index = 0; index < _listProjectileCargo.size(); ++index )
        {
            if ( _listProjectileCargo[index]._projectileID != projectileID )
                continue;
            _listProjectileCargo[index] = _listProjectileCargo.back();
            _listProjectileCargo.pop_back();
            return;
        }
    }

    // ------------------------------------------------------------------------------
    // 갱신(PrePhysics). 이 그룹에서는 디렉터만 돈다.
    // ------------------------------------------------------------------------------
    void ArenaGameModeComponent::updateUnits( float32 deltaTime )
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
                retireUnit( *pManager, unit ); // 몸이 사라졌다 — 조종자는 돌려준다
                _listUnit.erase( _listUnit.begin() + static_cast<ptrdiff_t>( unitIndex - 1 ) );
                continue;
            }
            if ( unit._death.has_value() == false && pAbilitySystem->getOwnedTagCounts().hasMatchingTag( CombatAttributeSet::getDeadTag() ) )
            {
                unit._death = Countdown( _deathLifeSpan );
                if ( unit._kind == ArenaUnitKind::Player )
                {
                    SW_LOG_INFO_ID( "ArenaGameMode.PlayerFellWaveAfter", "[Arena] the player fell on wave %# after %# kills", _wave, _killCount );
                    getSoundQueue().queueEvent( ArenaGameModeComponentInternal::kEventPlayerFell );
                }
                else
                {
                    ++_killCount;
                    getSoundQueue().queueEvent( ArenaGameModeComponentInternal::kEventEnemyFell );
                }
            }
            if ( unit._death.has_value() )
            {
                // 유닛이 납작해지는 동안 기다렸다가 정리한다. 조종하던 AI 조종자도 함께 정리한다.
                (void)unit._death->update( deltaTime );
                if ( unit._death->isActive() == false )
                {
                    retireUnit( *pManager, unit );
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

    void ArenaGameModeComponent::updateUnitViews()
    {
        // 이번 프레임의 스냅샷이다. 유닛과 투사체가 뒤 그룹에서 읽으며 틱 전 위치다(틱 안의 쓰기는 틱이 끝난 뒤에 보인다). 이번 프레임 위치가 필요한 쪽은
        // 트랜스폼을 읽는다. 조종자의 판단은 다음 틱 전에(`findUnitPosition`), 카메라는 물리 뒤 단계에서 읽는다.
        _listUnitView.clear();
        _playerObject               = GameObjectHandle{};
        GameObjectManager* pManager = getObjectManager();
        if ( pManager == nullptr )
            return;
        _playerFocus   = pManager->getWorldOrigin().toLocal( float3{ 0.0f, 0.0f, 0.0f } );
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
            view._unitID      = unit._unitID;
            view._position    = AbsolutePosition{ pManager->getWorldOrigin().toAbsolute( pMesh->getWorldPosition() ) };
            view._kind        = unit._kind;
            view._bAlive      = unit._death.has_value() == false && pAbilitySystem->getOwnedTagCounts().hasMatchingTag( CombatAttributeSet::getDeadTag() ) == false ? SW_TRUE : SW_FALSE;
            view._bPlayerTeam = pAbilitySystem->getOwnedTagCounts().hasMatchingTag( "Team.Player"_tag ) ? SW_TRUE : SW_FALSE;
            view._bEnemyTeam  = pAbilitySystem->getOwnedTagCounts().hasMatchingTag( "Team.Enemy"_tag ) ? SW_TRUE : SW_FALSE;
            _listUnitView.push_back( view );
            if ( unit._kind == ArenaUnitKind::Player && bFocusSet == false )
            {
                _playerFocus  = pMesh->getWorldPosition();
                _playerObject = view._object;
                bFocusSet     = true;
            }
        }
    }

    void ArenaGameModeComponent::updatePlayerRespawn( float32 deltaTime )
    {
        bool bPlayerPresent = hasPendingUnit( ArenaUnitKind::Player );
        for ( const ArenaUnit& unit : _listUnit )
        {
            bPlayerPresent = bPlayerPresent || unit._kind == ArenaUnitKind::Player;
        }
        if ( bPlayerPresent )
        {
            _playerRespawn.reset();
            return;
        }
        // 쓰러진 플레이어는 정리된 뒤 잠시 있다가 중앙에 다시 등장한다(웨이브는 이어진다).
        if ( _playerRespawn.has_value() == false )
            _playerRespawn = Countdown( _playerRespawnDelay );
        (void)_playerRespawn->update( deltaTime );
        if ( _playerRespawn->isActive() )
            return;
        _playerRespawn.reset();
        requestUnit( ArenaUnitKind::Player, float3{ 0.0f, 0.0f, 0.0f }, 1 );
    }

    void ArenaGameModeComponent::logStatus() const
    {
        const GameObjectManager* pManager = getObjectManager();
        if ( pManager == nullptr )
            return;
        for ( const ArenaUnit& unit : _listUnit )
        {
            const GameObject*             pObject        = unit._kind == ArenaUnitKind::Player ? pManager->resolveGameObject( unit._object ) : nullptr;
            const AbilitySystemComponent* pAbilitySystem = pObject != nullptr ? pObject->getComponent<AbilitySystemComponent>() : nullptr;
            if ( pAbilitySystem == nullptr )
                continue;
            const optional<float32> healthValue = pAbilitySystem->getAttributeValue( CombatAttributes::health() );
            const optional<float32> manaValue   = pAbilitySystem->getAttributeValue( CombatAttributes::mana() );
            if ( healthValue.has_value() == false || manaValue.has_value() == false )
                continue; // 전투 세트가 없는 플레이어는 상태 줄이 없다
            [[maybe_unused]] const int32 health = static_cast<int32>( *healthValue );
            [[maybe_unused]] const int32 mana   = static_cast<int32>( *manaValue );
            SW_LOG_INFO_ID( "ArenaGameMode.WaveKillsPlayerHP", "[Arena] wave %# · kills %# · player HP %# · MP %# · units %# · projectiles %#", _wave, _killCount, health, mana,
                            static_cast<uint32>( _listUnit.size() ), static_cast<uint32>( _projectiles.getProjectiles().size() ) );
        }
    }

    bool ArenaGameModeComponent::hasPendingUnit( ArenaUnitKind kind ) const
    {
        return VectorUtil::containsBy<&SpawnRequest::_kind>( _listPendingUnit, kind );
    }
} // namespace sw

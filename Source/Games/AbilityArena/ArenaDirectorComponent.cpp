#include "pch.h"

#include "Games/AbilityArena/ArenaDirectorComponent.h"

#include "Core/Common/FourCcUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Automation/AutomationProbe.h"
#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/GameObject/ComponentRegistry.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Utility/GameAutoplay.h"

#include "GameFramework/Base/Actor/Control/Controller/AIControllerComponent.h"
#include "GameFramework/Base/Actor/Control/Controller/PlayerControllerComponent.h"
#include "GameFramework/Base/Actor/Control/Pawn/PawnComponent.h"
#include "GameFramework/Base/Foundation/Framework/GameService.h"
#include "GameFramework/Base/Foundation/Utility/StateArchiveUtil.h"
#include "GameFramework/Base/Gameplay/Ability/AbilityCatalog.h"
#include "GameFramework/Base/Gameplay/Ability/AbilitySystemComponent.h"
#include "GameFramework/Base/Gameplay/Ability/CombatAttributeSet.h"

#include "Games/AbilityArena/ArenaProjectileComponent.h"

namespace sw
{
    SW_LOG_CALLER( "ArenaDirector" );

    namespace
    {
        struct ArenaDirectorComponentInternal
        {
            static constexpr uint32 kStateTag       = FourCcUtil::make( "ARNA" );
            static constexpr uint32 kStateVersion   = 1;
            static constexpr int32  kProjectileTint = 3; ///< `applyTint` 의 투사체 번호(앞 셋은 `ArenaUnitKind` 순)

            static constexpr const utf8* kSoundWave       = "game/abilityarena/sounds/maximize_001.ogg";
            static constexpr const utf8* kSoundPlayerFell = "game/abilityarena/sounds/error_004.ogg";
            static constexpr const utf8* kSoundEnemyFell  = "game/abilityarena/sounds/impact_punch_heavy_000.ogg";

            /** @brief 종류마다의 어빌리티 세트 id 입니다. */
            static hashed_string findAbilitySetID( ArenaUnitKind kind )
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

            /** @brief 탐침이 읽는 디렉터 — 씬의 첫 아레나 디렉터입니다(탐침은 단언 단계에서만 불린다 — 프레임 경로가 아니다). */
            static const ArenaDirectorComponent* findProbeDirector( const GameObjectManager* pManager )
            {
                const ArenaDirectorComponent* pFound = nullptr;
                if ( pManager != nullptr )
                {
                    pManager->forEachComponentOfType<ArenaDirectorComponent>( [&pFound]( const ArenaDirectorComponent* pDirector )
                    {
                        if ( pFound == nullptr )
                            pFound = pDirector;
                    } );
                }
                return pFound;
            }

            [[nodiscard]] static bool readPlayerX( const GameObjectManager* pManager, float64& outValue )
            {
                const ArenaDirectorComponent* pDirector = findProbeDirector( pManager );
                float3                        position{};
                if ( pDirector == nullptr || pDirector->findUnitPosition( pDirector->getPlayerObject(), position ) == false )
                    return false;
                outValue = static_cast<float64>( position._x );
                return true;
            }

            [[nodiscard]] static bool readPlayerZ( const GameObjectManager* pManager, float64& outValue )
            {
                const ArenaDirectorComponent* pDirector = findProbeDirector( pManager );
                float3                        position{};
                if ( pDirector == nullptr || pDirector->findUnitPosition( pDirector->getPlayerObject(), position ) == false )
                    return false;
                outValue = static_cast<float64>( position._z );
                return true;
            }

            [[nodiscard]] static bool readPlayerShotCount( const GameObjectManager* pManager, float64& outValue )
            {
                const ArenaDirectorComponent* pDirector = findProbeDirector( pManager );
                if ( pDirector == nullptr )
                    return false;
                outValue = pDirector->getPlayerShotCount();
                return true;
            }

            [[nodiscard]] static bool readKillCount( const GameObjectManager* pManager, float64& outValue )
            {
                const ArenaDirectorComponent* pDirector = findProbeDirector( pManager );
                if ( pDirector == nullptr )
                    return false;
                outValue = pDirector->getKillCount();
                return true;
            }

            [[nodiscard]] static bool readPlayerControllerKind( const GameObjectManager* pManager, float64& outValue )
            {
                const ArenaDirectorComponent* pDirector = findProbeDirector( pManager );
                if ( pDirector == nullptr )
                    return false;
                outValue = pDirector->isPlayerDrivenByAI() ? 1.0 : 0.0;
                return true;
            }
        };
    } // namespace

    /**
     * @brief `-gv_arenaAutoPlay=1` — 디렉터의 자동 전투를 켭니다(씬의 `_bAutoPlay` 가 꺼져 있어도). 플레이어도 AI 가 움직인다.
     * @details 배포본 실행 파일로도 돌릴 수 있게 남긴다: `App -gv_arenaAutoPlay=1 -gv_profileFrames=1200`.
     */
    SW_TEST_GLOBAL_VARIABLE_SHIPPED( int32, gv_arenaAutoPlay, 0, "AbilityArena: 플레이어도 AI 가 조종 (1=켜기)" );
    SW_GAME_AUTOPLAY( gv_arenaAutoPlay, "AbilityArena", "The auto battle AI controller possesses the player" );

    SW_AUTOMATION_PROBE( arenaPlayerX, "Arena.PlayerX", "World X of the arena player", &ArenaDirectorComponentInternal::readPlayerX );
    SW_AUTOMATION_PROBE( arenaPlayerZ, "Arena.PlayerZ", "World Z of the arena player", &ArenaDirectorComponentInternal::readPlayerZ );
    SW_AUTOMATION_PROBE( arenaPlayerShotCount, "Arena.PlayerShotCount", "Projectiles the player team fired this game", &ArenaDirectorComponentInternal::readPlayerShotCount );
    SW_AUTOMATION_PROBE( arenaKillCount, "Arena.KillCount", "Enemies felled this game", &ArenaDirectorComponentInternal::readKillCount );
    SW_AUTOMATION_PROBE( arenaPlayerControllerKind, "Arena.PlayerControllerKind", "0 = the player controller holds the player, 1 = the auto battle AI",
                         &ArenaDirectorComponentInternal::readPlayerControllerKind );
} // namespace sw

namespace sw
{
    ArenaDirectorComponent::ArenaDirectorComponent()
        : _playerPrefab{}
        , _gruntPrefab{}
        , _casterPrefab{}
        , _projectilePrefab{}
        , _gruntAIPrefab{}
        , _casterAIPrefab{}
        , _autoBattleAIPrefab{}
        , _arenaHalfSize{ 14.0f }
        , _waveRadius{ 11.0f }
        , _playerRespawnDelay{ 2.5f }
        , _playerTint{ 0.55f, 0.75f, 1.0f, 1.0f }
        , _gruntTint{ 1.0f, 0.5f, 0.45f, 1.0f }
        , _casterTint{ 0.8f, 0.55f, 1.0f, 1.0f }
        , _projectileTint{ 1.0f, 0.6f, 0.1f, 1.0f }
        , _statusLogInterval{ 5.0f }
        , _unitRadius{ 0.5f }
        , _projectileRadius{ 0.25f }
        , _listUnit{}
        , _listUnitView{}
        , _listProjectile{}
        , _listPendingUnit{}
        , _tintCache{}
        , _playerFocus{ 0.0f, 0.0f, 0.0f }
        , _playerObject{}
        , _autoBattleObject{}
        , _playerRespawnTimer{ -1.0f }
        , _statusLogTimer{ 0.0f }
        , _wave{ 0 }
        , _killCount{ 0 }
        , _playerShotCount{ 0 }
        , _bUnitsRequested{ SW_FALSE }
        , _bPossessionDirty{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    ArenaDirectorComponent::~ArenaDirectorComponent() = default;

    bool ArenaDirectorComponent::startGame()
    {
        const AbilityCatalog* pCatalog = game::getService<AbilityCatalog>();
        if ( pCatalog == nullptr || pCatalog->getAbilitySetCount() == 0 )
        {
            SW_LOG_WARNING( "[Arena] the ability catalog is not loaded - the arena cannot start" );
            return false;
        }
        _wave = 0;
        return true;
    }

    void ArenaDirectorComponent::onGameStarted()
    {
        SW_LOG_INFO( "[Arena] arena is ready - WASD move, J/Space melee, K/2 fireball, L/3 heal, LeftShift/4 dash" );
    }

    void ArenaDirectorComponent::writeState( Archive& outArchive ) const
    {
        StateArchiveUtil::writeHeader( outArchive, ArenaDirectorComponentInternal::kStateTag, ArenaDirectorComponentInternal::kStateVersion );
        outArchive << _wave;
        outArchive << _killCount;
    }

    bool ArenaDirectorComponent::readState( Archive& archive )
    {
        uint32 wave      = 0;
        uint32 killCount = 0;
        if ( StateArchiveUtil::readHeader( archive, ArenaDirectorComponentInternal::kStateTag, ArenaDirectorComponentInternal::kStateVersion ) == false )
            return false;
        archive >> wave;
        archive >> killCount;
        if ( archive.isError() || archive.getRemainingBytes() != 0 )
            return false;
        _wave      = wave;
        _killCount = killCount;
        return true;
    }

    void ArenaDirectorComponent::onStateRestored( bool bRestored )
    {
        // 어느 쪽이든 걷힌다 — 다음 틱이 플레이어와 지금 웨이브를 다시 세운다.
        if ( bRestored )
            SW_LOG_INFO( "[Arena] arena state restored - wave %#, %# kills", _wave, _killCount );
        else
            SW_LOG_WARNING( "[Arena] the saved arena state does not match this build - starting from wave 1" );
    }

    void ArenaDirectorComponent::tickGame( float32 deltaTime )
    {
        if ( _bUnitsRequested == SW_FALSE )
        {
            // 처음(또는 상태 저장 전에 걷은 뒤) — 플레이어와 지금 웨이브를 세운다.
            _bUnitsRequested = SW_TRUE;
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

        // 빙의는 틱 뒤에 — 자동 플레이 스위치와 플레이어 폰을 쥔 조종자가 다르면 플러시를 잡는다(`hasPendingSpawn`).
        const PawnComponent* pPlayerPawn = findPlayerPawn();
        if ( pPlayerPawn != nullptr )
        {
            const bool bWantAI = isAutoPlayOn();
            _bPossessionDirty  = pPlayerPawn->isPossessed() == false || bWantAI != isPlayerDrivenByAI() ? SW_TRUE : SW_FALSE;
        }
    }

    void ArenaDirectorComponent::onViewsDespawned()
    {
        _listUnit.clear();
        _listUnitView.clear();
        _listProjectile.clear();
        _listPendingUnit.clear();
        _playerRespawnTimer = -1.0f;
        _autoBattleObject   = GameObjectHandle{};
        _bUnitsRequested    = SW_FALSE;
        _bPossessionDirty   = SW_FALSE;
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

    bool ArenaDirectorComponent::findUnitPosition( GameObjectHandle object, float3& outPosition ) const
    {
        const GameObjectManager* pManager = getObjectManager();
        const GameObject*        pObject  = pManager != nullptr && findUnitView( object ) != nullptr ? pManager->resolveGameObject( object ) : nullptr;
        const MeshComponent*     pMesh    = pObject != nullptr ? pObject->getComponent<MeshComponent>() : nullptr;
        if ( pMesh == nullptr )
            return false;
        outPosition = pMesh->getWorldPosition();
        return true;
    }

    bool ArenaDirectorComponent::findNearestHostilePosition( GameObjectHandle from, float3& outPosition ) const
    {
        const ArenaUnitView* pNearest = findNearestHostileView( from, 100.0f );
        return pNearest != nullptr && findUnitPosition( pNearest->_object, outPosition );
    }

    bool ArenaDirectorComponent::isPlayerDrivenByAI() const
    {
        GameObjectManager*   pManager    = getObjectManager();
        const PawnComponent* pPlayerPawn = findPlayerPawn();
        const GameObject*    pAutoBattle = pManager != nullptr ? pManager->resolveGameObject( _autoBattleObject ) : nullptr;
        const Component*     pController = pPlayerPawn != nullptr && pManager != nullptr ? pManager->resolveComponent( pPlayerPawn->getController() ) : nullptr;
        return pController != nullptr && pAutoBattle != nullptr && pController->getOwner() == pAutoBattle;
    }

    PawnComponent* ArenaDirectorComponent::findPlayerPawn() const
    {
        GameObjectManager* pManager = getObjectManager();
        GameObject*        pObject  = pManager != nullptr ? pManager->resolveGameObject( _playerObject ) : nullptr;
        return pObject != nullptr ? pObject->getComponent<PawnComponent>() : nullptr;
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
        const GameObject*    pFrom     = from.getOwner();
        const ArenaUnitView* pFromView = pFrom != nullptr ? findUnitView( pFrom->getHandle() ) : nullptr;
        GameObjectManager*   pManager  = getObjectManager();
        if ( pFromView == nullptr || pManager == nullptr )
            return;

        ProjectileRequest request;
        request._spec        = spec;
        request._extraSpec   = extraSpec;
        request._position    = pFromView->_position + facing * ( _unitRadius + _projectileRadius );
        request._velocity    = facing * speed;
        request._range       = range;
        request._bFromPlayer = from.hasMatchingTag( "Team.Player"_tag ) ? SW_TRUE : SW_FALSE;
        // 어빌리티는 워커(유닛의 틱)에서 쏜다 — 오브젝트는 틱 뒤 게임 스레드에서 세운다. 그 사이 디렉터가 사라질 수 있으니 핸들로 다시 찾는다.
        const ComponentHandle self = getHandle();
        pManager->executeOrDeferPostTick( SW_DELEGATE_LAMBDA( GameObjectManager::PostTickDelegate, [pManager, self, request]()
        {
            ArenaDirectorComponent* pDirector = static_cast<ArenaDirectorComponent*>( pManager->resolveComponent( self ) );
            if ( pDirector != nullptr )
                pDirector->spawnProjectile( request );
        } ) );
    }

    const ArenaDirectorComponent* ArenaDirectorComponent::findForUnit( const AbilitySystemComponent& unit )
    {
        const GameObject*         pOwner   = unit.getOwner();
        const GameObjectManager*  pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        const ArenaUnitComponent* pUnit    = pOwner != nullptr ? pOwner->getComponent<ArenaUnitComponent>() : nullptr;
        if ( pManager == nullptr || pUnit == nullptr )
            return nullptr;
        return resolve<ArenaDirectorComponent>( *pManager, pUnit->getDirector() );
    }

    const ArenaDirectorComponent* ArenaDirectorComponent::findForPawn( const PawnComponent& pawn )
    {
        const GameObject*         pOwner   = pawn.getOwner();
        const GameObjectManager*  pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        const ArenaUnitComponent* pUnit    = pOwner != nullptr ? pOwner->getComponent<ArenaUnitComponent>() : nullptr;
        if ( pManager == nullptr || pUnit == nullptr )
            return nullptr;
        return resolve<ArenaDirectorComponent>( *pManager, pUnit->getDirector() );
    }

    // ------------------------------------------------------------------------------
    // 스폰(요청은 틱 안 · 세우기는 틱 뒤 게임 스레드)
    // ------------------------------------------------------------------------------
    void ArenaDirectorComponent::requestWave( bool bAdvance )
    {
        if ( bAdvance || _wave == 0 )
            ++_wave;
        getSoundQueue().queueClip( ArenaDirectorComponentInternal::kSoundWave );
        const uint32 gruntCount  = 2u + _wave;
        const uint32 casterCount = _wave / 2u;
        const uint32 totalCount  = gruntCount + casterCount;
        const int32  level       = 1 + static_cast<int32>( ( _wave - 1u ) / 2u );
        for ( uint32 spawnIndex = 0; spawnIndex < totalCount; ++spawnIndex )
        {
            const float32       angle = 2.0f * MathUtil::kPi * static_cast<float32>( spawnIndex ) / static_cast<float32>( totalCount );
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

    void ArenaDirectorComponent::onFlush( GameObjectManager& manager, bool bRespawnViews )
    {
        (void)bRespawnViews; // 플레이어 · 웨이브는 틱이 청한다(`_bUnitsRequested`) — 여기는 쌓인 것만 세운다
        for ( const SpawnRequest& request : _listPendingUnit )
        {
            (void)spawnUnit( manager, request ); // 하나가 실패해도 나머지는 선다 — 실패한 플레이어는 다시 서기 시간이 지나 다시 세운다
        }
        _listPendingUnit.clear();
        syncPlayerPossession( manager );
    }

    bool ArenaDirectorComponent::spawnUnit( GameObjectManager& manager, const SpawnRequest& request )
    {
        const bool              bPlayer        = request._kind == ArenaUnitKind::Player;
        const string&           prefabPath     = bPlayer ? _playerPrefab : ( request._kind == ArenaUnitKind::Caster ? _casterPrefab : _gruntPrefab );
        const utf8*             pName          = bPlayer ? "ArenaPlayer" : ( request._kind == ArenaUnitKind::Caster ? "ArenaCaster" : "ArenaGrunt" );
        GameObject*             pObject        = spawnPrefab( manager, prefabPath, pName );
        MeshComponent*          pMesh          = pObject != nullptr ? pObject->getComponent<MeshComponent>() : nullptr;
        AbilitySystemComponent* pAbilitySystem = pObject != nullptr ? pObject->getComponent<AbilitySystemComponent>() : nullptr;
        ArenaUnitComponent*     pUnitComponent = pObject != nullptr ? pObject->getComponent<ArenaUnitComponent>() : nullptr;
        PawnComponent*          pPawn          = pObject != nullptr ? pObject->getComponent<PawnComponent>() : nullptr;
        if ( pMesh == nullptr || pAbilitySystem == nullptr || pUnitComponent == nullptr || pPawn == nullptr )
        {
            SW_LOG_WARNING( "[Arena] prefab '%#' needs a mesh, an ability system, a pawn and an arena unit", prefabPath.c_str() );
            if ( pObject != nullptr )
                manager.destroyObject( pObject );
            return false;
        }

        // 카탈로그는 정하지 않는다 — 게임 서비스(`AbilityCatalog`)를 쓴다. 포인터를 박아 두면 모듈이 다시 올라온 뒤 옛 카탈로그를 가리킨다.
        const hashed_string setID = ArenaDirectorComponentInternal::findAbilitySetID( request._kind );
        if ( pAbilitySystem->grantAbilitySet( setID ) == false )
        {
            SW_LOG_WARNING( "[Arena] ability set '%#' is missing - is abilities.xml loaded?", setID.c_str() );
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
        const float3 facing = ArenaUnitComponent::flattenDirection( float3{ 0.0f, 0.0f, 0.0f } - request._position, float3{ 0.0f, 0.0f, 1.0f } );
        pUnitComponent->assignDirector( getOwner()->getHandle(), facing );

        ArenaUnit unit;
        unit._object = pObject->getHandle();
        unit._kind   = request._kind;
        if ( bPlayer == false )
            unit._controller = spawnEnemyController( manager, request._kind, *pPawn );
        _listUnit.push_back( unit );
        return true;
    }

    GameObjectHandle ArenaDirectorComponent::spawnEnemyController( GameObjectManager& manager, ArenaUnitKind kind, PawnComponent& pawn )
    {
        const string&          prefabPath = kind == ArenaUnitKind::Caster ? _casterAIPrefab : _gruntAIPrefab;
        GameObject*            pObject    = spawnPrefab( manager, prefabPath, kind == ArenaUnitKind::Caster ? "ArenaCasterAI" : "ArenaGruntAI" );
        AIControllerComponent* pAI        = pObject != nullptr ? pObject->getComponent<AIControllerComponent>() : nullptr;
        if ( pAI == nullptr )
        {
            SW_LOG_WARNING( "[Arena] AI prefab '%#' has no AI controller - the enemy stands still", prefabPath.c_str() );
            if ( pObject != nullptr )
                manager.destroyObject( pObject );
            return GameObjectHandle{};
        }
        pAI->possess( pawn );
        return pObject->getHandle();
    }

    void ArenaDirectorComponent::syncPlayerPossession( GameObjectManager& manager )
    {
        _bPossessionDirty    = SW_FALSE;
        PawnComponent* pPawn = findPlayerPawn();
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
                SW_LOG_WARNING( "[Arena] auto battle prefab '%#' has no AI controller - auto play cannot take the player", _autoBattleAIPrefab.c_str() );
                return;
            }
            if ( isPlayerDrivenByAI() == false )
                pAI->possess( *pPawn );
            return;
        }
        // 자동 플레이를 끄면 플레이어 0 의 조종자가 되찾는다 — 씬에 없으면 세운다(조종 시스템이 자동 빙의로 세우는 것과 같은 자리).
        PlayerControllerComponent* pPlayer = nullptr;
        for ( PlayerControllerComponent* pCandidate : manager.getComponentRegistry().getAll<PlayerControllerComponent>() )
        {
            if ( pCandidate != nullptr && pCandidate->getPlayerIndex() == 0 )
            {
                pPlayer = pCandidate;
                break;
            }
        }
        if ( pPlayer == nullptr )
        {
            GameObject* pObject = manager.createGameObject( hashed_string( "PlayerController" ) );
            pPlayer             = pObject != nullptr ? pObject->addComponent<PlayerControllerComponent>() : nullptr;
        }
        if ( pPlayer != nullptr && pPlayer->getPawn() != pPawn->getHandle() )
            pPlayer->possess( *pPawn );
    }

    void ArenaDirectorComponent::spawnProjectile( const ProjectileRequest& request )
    {
        GameObjectManager* pManager = getObjectManager();
        if ( pManager == nullptr || isStarted() == false )
            return;
        GameObject*               pObject     = spawnPrefab( *pManager, _projectilePrefab, "ArenaProjectile" );
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
        if ( request._bFromPlayer == SW_TRUE )
            ++_playerShotCount;
        pruneProjectiles();
        _listProjectile.push_back( pObject->getHandle() );
    }

    void ArenaDirectorComponent::applyTint( MeshComponent& mesh, int32 tintIndex )
    {
        const float4 arrColor[4] = { _playerTint, _gruntTint, _casterTint, _projectileTint };
        _tintCache.apply( mesh, arrColor[MathUtil::clamp( tintIndex, 0, ArenaDirectorComponentInternal::kProjectileTint )] );
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
                    getSoundQueue().queueClip( ArenaDirectorComponentInternal::kSoundPlayerFell );
                }
                else
                {
                    ++_killCount;
                    getSoundQueue().queueClip( ArenaDirectorComponentInternal::kSoundEnemyFell );
                }
            }
            if ( unit._deathTimer >= 0.0f )
            {
                // 유닛이 납작해지는 동안 기다렸다가 걷는다 — 쥐고 있던 AI 조종자도 같이.
                unit._deathTimer -= deltaTime;
                if ( unit._deathTimer <= 0.0f )
                {
                    destroySpawned( *pManager, unit._controller );
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
        // 이번 프레임의 모습 — 유닛 · 투사체가 뒤 그룹에서 읽는다. 틱 전 자리다(틱 안의 쓰기는 틱 뒤에 보인다). 이번 프레임 자리가 필요한 쪽은
        // 트랜스폼을 읽는다 — 조종자의 판단은 다음 틱 전에(`findUnitPosition`), 카메라는 물리 뒤 단계에서.
        _listUnitView.clear();
        _playerFocus                = float3{ 0.0f, 0.0f, 0.0f };
        _playerObject               = GameObjectHandle{};
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
                _playerFocus  = view._position;
                _playerObject = view._object;
                bFocusSet     = true;
            }
        }
    }

    void ArenaDirectorComponent::updatePlayerRespawn( float32 deltaTime )
    {
        bool bPlayerPresent = hasPendingUnit( ArenaUnitKind::Player );
        for ( const ArenaUnit& unit : _listUnit )
        {
            bPlayerPresent = bPlayerPresent || unit._kind == ArenaUnitKind::Player;
        }
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
        if ( _statusLogTimer < _statusLogInterval )
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

} // namespace sw

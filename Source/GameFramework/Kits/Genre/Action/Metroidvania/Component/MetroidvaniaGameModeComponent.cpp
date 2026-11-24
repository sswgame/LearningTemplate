#include "pch.h"

#include "GameFramework/Kits/Genre/Action/Metroidvania/Component/MetroidvaniaGameModeComponent.h"

#include "Core/Encoding/FourCCUtil.h"
#include "Core/Foundation/Constants.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/fixed_string.h"
#include "Core/String/formatString.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Runtime/Foundation/Framework/Minigame/MinigameStack.h"
#include "GameFramework/Runtime/Foundation/GameData/StatBlock.h"
#include "GameFramework/Runtime/Gameplay/Inventory/ItemStackList.h"

namespace sw
{
    namespace
    {
        struct MetroidvaniaGameModeComponentInternal
        {
            static constexpr uint32 kStateTag     = FourCCUtil::make( "MVGM" );
            static constexpr uint32 kStateVersion = 1;

            static constexpr const utf8* kWinResult  = "win";
            static constexpr const utf8* kDamageStat = "damage"; ///< 부적 능력치 이름 — 플레이어 공격력에 더한다(카탈로그 `<Charm><Stats damage>`)

            /** @brief 칸 한 변(m)입니다. 씬의 자리와 방 사각형이 이 격자의 월드 좌표다. */
            static constexpr float32 kTileSize = 1.0f;

            // 사운드 이벤트 이름 — 클립 · 범위 · 상한은 게임의 사운드 이벤트 데이터(audioevents.xml)가 정한다.
            static constexpr const utf8* kEventSwing           = "MetroidvaniaSwing";
            static constexpr const utf8* kEventEnemyHit        = "MetroidvaniaEnemyHit";
            static constexpr const utf8* kEventPlayerHit       = "MetroidvaniaPlayerHit";
            static constexpr const utf8* kEventBlocked         = "MetroidvaniaBlocked";
            static constexpr const utf8* kEventParried         = "MetroidvaniaParried";
            static constexpr const utf8* kEventEnemyKilled     = "MetroidvaniaEnemyKilled";
            static constexpr const utf8* kEventBossDefeated    = "MetroidvaniaBossDefeated";
            static constexpr const utf8* kEventPickup          = "MetroidvaniaPickup";
            static constexpr const utf8* kEventRested          = "MetroidvaniaRested";
            static constexpr const utf8* kEventFlask           = "MetroidvaniaFlask";
            static constexpr const utf8* kEventFastTravel      = "MetroidvaniaFastTravel";
            static constexpr const utf8* kEventDied            = "MetroidvaniaDied";
            static constexpr const utf8* kEventCorpseRecovered = "MetroidvaniaCorpseRecovered";
            static constexpr const utf8* kEventWon             = "MetroidvaniaWon";

            /** @brief 죽음 · 휴식 알림 @p kind 에 낼 소리입니다. 소리가 없는 알림이면 nullptr 입니다. */
            static const utf8* findSound( MetroidvaniaDeathRestEventKind kind )
            {
                switch ( kind )
                {
                    case MetroidvaniaDeathRestEventKind::Rested:
                    {
                        return kEventRested;
                    }
                    case MetroidvaniaDeathRestEventKind::Died:
                    {
                        return kEventDied;
                    }
                    case MetroidvaniaDeathRestEventKind::CorpseRecovered:
                    {
                        return kEventCorpseRecovered;
                    }
                    case MetroidvaniaDeathRestEventKind::FlaskUsed:
                    {
                        return kEventFlask;
                    }
                    case MetroidvaniaDeathRestEventKind::EnemyKilled:
                    {
                        return kEventEnemyKilled;
                    }
                    case MetroidvaniaDeathRestEventKind::BossDefeated:
                    {
                        return kEventBossDefeated;
                    }
                    default:
                    {
                        return nullptr;
                    }
                }
            }

            /** @brief 플레이어가 공격을 받은 결과 @p result 에 낼 소리입니다. 이미 죽었으면 nullptr 입니다. */
            static const utf8* findDefenseSound( MetroidvaniaDefenseResult result )
            {
                switch ( result )
                {
                    case MetroidvaniaDefenseResult::Hit:
                    case MetroidvaniaDefenseResult::GuardBroken:
                    {
                        return kEventPlayerHit;
                    }
                    case MetroidvaniaDefenseResult::Blocked:
                    {
                        return kEventBlocked;
                    }
                    case MetroidvaniaDefenseResult::Parried:
                    {
                        return kEventParried;
                    }
                    default:
                    {
                        return nullptr;
                    }
                }
            }
        };
    } // namespace

    SW_AUTOMATION_GETTER_WATCH( metroidvaniaPhase, "Metroidvania.Phase", "Metroidvania phase (0 explore, 1 dead, 2 won)", MetroidvaniaGameModeComponent, &MetroidvaniaGameModeComponent::getPhase );
    SW_AUTOMATION_GETTER_WATCH( metroidvaniaPlayerX, "Metroidvania.PlayerX", "Player body X (m)", MetroidvaniaGameModeComponent, &MetroidvaniaGameModeComponent::getPlayerX );
    SW_AUTOMATION_GETTER_WATCH( metroidvaniaAbilities, "Metroidvania.Abilities", "Abilities gained", MetroidvaniaGameModeComponent, &MetroidvaniaGameModeComponent::getAbilityCount );
    SW_AUTOMATION_GETTER_WATCH( metroidvaniaCurrency, "Metroidvania.Currency", "Currency carried", MetroidvaniaGameModeComponent, &MetroidvaniaGameModeComponent::getCurrency );
    SW_AUTOMATION_GETTER_WATCH( metroidvaniaVisited, "Metroidvania.VisitedAreas", "Rooms visited", MetroidvaniaGameModeComponent, &MetroidvaniaGameModeComponent::getVisitedAreaCount );

    MetroidvaniaGameModeComponent::MetroidvaniaGameModeComponent()
        : _areaDataPath{}
        , _listTerrainRow{}
        , _startPosition{ 2.5f, 1.5f }
        , _fallY{ -3.0f }
        , _markerReach{ 1.0f }
        , _listEnemyKind{}
        , _listEnemyPosition{}
        , _listPickup{}
        , _listPickupPosition{}
        , _listSite{}
        , _listSitePosition{}
        , _goalFlag{}
        , _playerDamage{ 50.0f }
        , _playerPoiseDamage{ 10.0f }
        , _playerReach{ 2.0f }
        , _playerAttackInterval{ 0.35f }
        , _enemyDamage{ 10.0f }
        , _bossDamage{ 15.0f }
        , _bossDamageTakenScale{ 0.4f }
        , _enemyReach{ 1.8f }
        , _enemyStartup{ 0.5f }
        , _enemyInterval{ 1.0f }
        , _respawnDelaySeconds{ 1.0f }
        , _resultHoldSeconds{ 3.0f }
        , _seed{ 7 }
        , _pCatalog{}
        , _pAreas{}
        , _graph{}
        , _terrain{}
        , _motor{}
        , _abilities{}
        , _mapState{}
        , _deathRest{}
        , _loadout{}
        , _player{}
        , _wallet{}
        , _flags{}
        , _random{}
        , _timer{}
        , _attackCooldown{}
        , _respawnDelay{}
        , _resultHold{}
        , _listEnemy{}
        , _listEvent{}
        , _command{}
        , _area{}
        , _phase{ MetroidvaniaPhase::Explore }
    {
    }

    MetroidvaniaGameModeComponent::~MetroidvaniaGameModeComponent() = default;

    bool MetroidvaniaGameModeComponent::validatePlacement() const
    {
        const bool bEnemyMatch  = _listEnemyKind.size() == _listEnemyPosition.size();
        const bool bPickupMatch = _listPickup.size() == _listPickupPosition.size();
        const bool bSiteMatch   = _listSite.size() == _listSitePosition.size();
        if ( bEnemyMatch == false || bPickupMatch == false || bSiteMatch == false || _goalFlag.empty() )
        {
            SW_LOG_ERROR_ID( "Metroidvania.LayoutMismatch", "Metroidvania game mode: id and position lists differ in length (enemies %# / %#, pickups %# / %#, sites %# / %#) or the goal flag is empty - the game does not open",
                             _listEnemyKind.size(), _listEnemyPosition.size(), _listPickup.size(), _listPickupPosition.size(), _listSite.size(), _listSitePosition.size() );
            return false;
        }
        bool bAllFound = true;
        for ( const CatalogKey<MetroidvaniaEnemyDef>& enemy : _listEnemyKind )
        {
            if ( _pCatalog->findEnemy( enemy.key() ) != nullptr )
                continue;
            SW_LOG_ERROR_ID( "Metroidvania.UnknownEnemy", "Metroidvania game mode: enemy '%#' is not in the catalog - the game does not open", enemy.c_str() );
            bAllFound = false;
        }
        for ( const CatalogKey<MetroidvaniaPickupDef>& pickup : _listPickup )
        {
            if ( _pCatalog->findPickup( pickup.key() ) != nullptr )
                continue;
            SW_LOG_ERROR_ID( "Metroidvania.UnknownPickup", "Metroidvania game mode: pickup '%#' is not in the catalog - the game does not open", pickup.c_str() );
            bAllFound = false;
        }
        for ( const CatalogKey<MetroidvaniaSiteDef>& site : _listSite )
        {
            if ( _pCatalog->findSite( site.key() ) != nullptr )
                continue;
            SW_LOG_ERROR_ID( "Metroidvania.UnknownSite", "Metroidvania game mode: site '%#' is not in the catalog - the game does not open", site.c_str() );
            bAllFound = false;
        }
        return bAllFound;
    }

    bool MetroidvaniaGameModeComponent::populateTerrain()
    {
        string text;
        for ( const string& row : _listTerrainRow )
        {
            text += row;
            text += '\n';
        }
        if ( _listTerrainRow.empty() || _terrain.loadFromXMLText( text, MetroidvaniaGameModeComponentInternal::kTileSize, float2{ 0.0f, 0.0f } ) == false )
        {
            SW_LOG_ERROR_ID( "Metroidvania.TerrainUnreadable", "Metroidvania game mode: the scene has no readable terrain rows - the game does not open" );
            return false;
        }
        return true;
    }

    bool MetroidvaniaGameModeComponent::populateEnemies( vector<MetroidvaniaEnemy>& outListEnemy ) const
    {
        outListEnemy.clear();
        for ( size_t index = 0; index < _listEnemyKind.size(); ++index )
        {
            fixed_string<constant::buffer::kMax32> spawnID;
            formatstring( spawnID.data(), spawnID.capacity(), "enemy%#", index );
            MetroidvaniaEnemy enemy;
            enemy._spawnID  = hashed_string( spawnID.view() );
            enemy._enemyID  = _listEnemyKind[index].key();
            enemy._position = _listEnemyPosition[index];
            enemy._bBoss    = _pCatalog->findEnemy( enemy._enemyID )->_bBoss ? SW_TRUE : SW_FALSE; // 있는지는 `validatePlacement` 가 보았다
            if ( enemy._duelist.initialize( *_pCatalog ) == false )
                return false;
            if ( enemy._bBoss == SW_TRUE )
                enemy._duelist.setDamageTakenScale( _bossDamageTakenScale );
            outListEnemy.push_back( enemy );
        }
        return true;
    }

    bool MetroidvaniaGameModeComponent::startGame()
    {
        if ( acquireBoundCatalog( _pCatalog ) == false || acquireRequired( _areaDataPath, _pAreas ) == false )
            return false;
        if ( validatePlacement() == false || populateTerrain() == false || populateEnemies( _listEnemy ) == false || _player.initialize( *_pCatalog ) == false )
            return false;
        _graph = *_pAreas;
        _graph.resetState();
        _wallet.clear();
        _flags.clear();
        _abilities.initialize( *_pCatalog );
        _mapState.initialize( *_pCatalog, _graph );
        if ( _deathRest.initialize( *_pCatalog, _wallet, nullptr ) == false ) // 이 미니게임의 적은 전리품 표를 적지 않는다 — 적으면 여기서 멈춘다
            return false;
        _loadout.initialize( *_pCatalog );
        _motor = PlatformerMotor2D{};
        _abilities.applyToMotor( _motor );
        _motor.setPosition( _startPosition );
        _random = GameRandom( _seed );
        _timer.reset();
        _attackCooldown.clear();
        _respawnDelay.clear();
        _resultHold.clear();
        _command = MetroidvaniaPlayerCommand{};
        stepMarkers();
        return true;
    }

    void MetroidvaniaGameModeComponent::onGameStarted()
    {
        SW_LOG_INFO_ID( "Metroidvania.Started", "[Metroidvania] %# rooms, %# enemies, %# pickups - the player input arrives as commands", _graph.getAreas().size(), _listEnemy.size(), _listPickup.size() );
    }

    bool MetroidvaniaGameModeComponent::submitPlayerInput( const MetroidvaniaPlayerCommand& command )
    {
        if ( isAcceptingInput() == false )
            return false;
        const MetroidvaniaPlayerCommand queued = _command;
        _command                               = command;
        _command._bJumpPressed                 = static_cast<uint8>( command._bJumpPressed | queued._bJumpPressed );
        _command._bDashPressed                 = static_cast<uint8>( command._bDashPressed | queued._bDashPressed );
        _command._bAttackPressed               = static_cast<uint8>( command._bAttackPressed | queued._bAttackPressed );
        _command._bParryPressed                = static_cast<uint8>( command._bParryPressed | queued._bParryPressed );
        _command._bUsePressed                  = static_cast<uint8>( command._bUsePressed | queued._bUsePressed );
        _command._bFlaskPressed                = static_cast<uint8>( command._bFlaskPressed | queued._bFlaskPressed );
        return true;
    }

    int32 MetroidvaniaGameModeComponent::getCurrency() const
    {
        return _pCatalog != nullptr ? static_cast<int32>( _wallet.getBalance( _pCatalog->getRules()._currency ) ) : 0;
    }

    bool MetroidvaniaGameModeComponent::readCameraFocus( GameObjectHandle& outFocusObject, float3& outFocusPosition ) const
    {
        outFocusObject   = GameObjectHandle{};
        outFocusPosition = toAbsolute( _motor.getPosition() )._value;
        if ( const GameObjectManager* const pManager = getObjectManager() )
            outFocusPosition = pManager->getWorldOrigin().toLocal( outFocusPosition );
        return isStarted();
    }

    const AreaDef* MetroidvaniaGameModeComponent::findAreaAt( const float2& position ) const
    {
        for ( const AreaDef& area : _graph.getAreas() )
        {
            const bool bInsideX = area._x <= position._x && position._x < area._x + area._width;
            const bool bInsideY = area._y <= position._y && position._y < area._y + area._height;
            if ( bInsideX && bInsideY )
                return &area;
        }
        return nullptr;
    }

    optional<int32> MetroidvaniaGameModeComponent::findSiteInReach() const
    {
        for ( size_t index = 0; index < _listSite.size(); ++index )
        {
            if ( float2::getDistanceSquared( _listSitePosition[index], _motor.getPosition() ) <= _markerReach * _markerReach )
                return static_cast<int32>( index );
        }
        return nullopt;
    }

    float2 MetroidvaniaGameModeComponent::findSitePosition( const hashed_string& siteID ) const
    {
        for ( size_t index = 0; index < _listSite.size(); ++index )
        {
            if ( _listSite[index].key() == siteID )
                return _listSitePosition[index];
        }
        return _startPosition; // 쉰 적이 없으면 시작 자리다
    }

    void MetroidvaniaGameModeComponent::stepMovement( float32 deltaTime )
    {
        PlatformerInput input;
        input._move         = _command._move;
        input._bJumpHeld    = _command._bJumpHeld;
        input._bJumpPressed = _command._bJumpPressed;
        input._bDashPressed = _command._bDashPressed;
        input               = _abilities.filterInput( input );
        if ( input._bDashPressed == SW_TRUE && _player.tryDodge() == false )
            input._bDashPressed = SW_FALSE; // 스태미나가 모자라면 대시하지 못한다
        _motor.update( _terrain, input, deltaTime );
    }

    void MetroidvaniaGameModeComponent::hitEnemy( MetroidvaniaEnemy& enemy )
    {
        StatBlock stats;
        _loadout.mergeStats( stats );
        const float32 damage = _player.computeAttackDamage( _playerDamage + stats.getValue( hashed_string( MetroidvaniaGameModeComponentInternal::kDamageStat ) ), enemy._duelist );
        (void)enemy._duelist.receiveAttack( damage, _playerPoiseDamage ); // 막지 않는 적이라 결과는 체력으로 본다
        getSoundQueue().queueEvent( MetroidvaniaGameModeComponentInternal::kEventEnemyHit );
        if ( enemy._duelist.getHealthMachine().isAlive() )
            return;
        ItemStackList drops; // 전리품 표를 쓰지 않는 적이라 비어 있다
        (void)_deathRest.registerKill( enemy._spawnID, enemy._enemyID, _flags, _random, drops );
    }

    void MetroidvaniaGameModeComponent::swing()
    {
        const float2  position = _motor.getPosition();
        const float32 facing   = static_cast<float32>( _motor.getFacing() );
        for ( MetroidvaniaEnemy& enemy : _listEnemy )
        {
            const float2 offset   = enemy._position - position;
            const bool   bInFront = offset._x * facing >= 0.0f;
            if ( _deathRest.isSpawnAlive( enemy._spawnID ) && bInFront && offset.getLengthSquared() <= _playerReach * _playerReach )
                hitEnemy( enemy );
        }
    }

    void MetroidvaniaGameModeComponent::stepPlayerCombat( float32 deltaTime )
    {
        _player.setDamageTakenScale( _loadout.computeDamageTakenScale() ); // 넘겨 낀 부적의 대가(아니면 1)
        _player.setGuarding( _command._bGuardHeld == SW_TRUE );
        if ( _command._bParryPressed == SW_TRUE )
            _player.pressParry();
        _player.update( deltaTime );
        (void)_attackCooldown.update( deltaTime ); // 끝났는지는 아래 isActive 로 본다
        const bool bCanSwing = _command._bAttackPressed == SW_TRUE && _attackCooldown.isActive() == false && _player.tryAttack();
        if ( bCanSwing )
        {
            _attackCooldown.start( _playerAttackInterval );
            getSoundQueue().queueEvent( MetroidvaniaGameModeComponentInternal::kEventSwing );
            swing();
        }
        if ( _command._bFlaskPressed == SW_TRUE )
            (void)_deathRest.drinkFlask( _player.getHealthMachine() ); // 충전이 없으면 할 일이 없다
    }

    void MetroidvaniaGameModeComponent::stepEnemies( float32 deltaTime )
    {
        for ( MetroidvaniaEnemy& enemy : _listEnemy )
        {
            if ( _deathRest.isSpawnAlive( enemy._spawnID ) == false )
                continue;
            enemy._duelist.update( deltaTime );
            enemy._duelist.getHealthMachine().discardEvents();
            (void)enemy._cooldown.update( deltaTime ); // 끝났는지는 아래 isActive 로 본다
            const bool bStaggered = enemy._duelist.getHealthMachine().isPoiseBroken();
            const bool bInReach   = float2::getDistanceSquared( enemy._position, _motor.getPosition() ) <= _enemyReach * _enemyReach;
            if ( bStaggered )
            {
                enemy._startup.clear(); // 무너진 적의 준비 동작은 끊긴다
                continue;
            }
            if ( enemy._startup.update( deltaTime ) )
            {
                enemy._cooldown.start( _enemyInterval );
                if ( bInReach == false )
                    continue;
                // 패리 · 막기는 전투원이 정한다.
                const MetroidvaniaDefenseOutcome defense = _player.receiveAttack( enemy._bBoss == SW_TRUE ? _bossDamage : _enemyDamage, 0.0f );
                if ( const utf8* const pSound = MetroidvaniaGameModeComponentInternal::findDefenseSound( defense._result ) )
                    getSoundQueue().queueEvent( pSound );
            }
            else if ( bInReach && enemy._startup.isActive() == false && enemy._cooldown.isActive() == false )
            {
                enemy._startup.start( _enemyStartup );
            }
        }
    }

    void MetroidvaniaGameModeComponent::collectPickup( const hashed_string& pickupID )
    {
        const MetroidvaniaPickupDef* const pPickup = _mapState.collectPickup( pickupID );
        if ( pPickup == nullptr )
            return;
        if ( pPickup->_ability.empty() == false && _abilities.grantAbility( pPickup->_ability, _flags ) )
            _abilities.applyToMotor( _motor );
        if ( pPickup->_charm.empty() == false && _loadout.grantCharm( pPickup->_charm ) )
            (void)_loadout.equip( pPickup->_charm ); // 슬롯이 모자라면 가방에 남는다
        getSoundQueue().queueEvent( MetroidvaniaGameModeComponentInternal::kEventPickup );
        SW_LOG_INFO_ID( "Metroidvania.Pickup", "[Metroidvania] picked up '%#' - %# abilities, completion %# percent", pPickup->_id.c_str(), getAbilityCount(), _mapState.computeCompletionPercent() );
    }

    void MetroidvaniaGameModeComponent::stepMarkers()
    {
        const float2         position = _motor.getPosition();
        const AreaDef* const pArea    = findAreaAt( position );
        if ( pArea != nullptr && pArea->_id != _area )
        {
            _area = pArea->_id;
            if ( _mapState.enterArea( _area ) )
                SW_LOG_INFO_ID( "Metroidvania.AreaEntered", "[Metroidvania] room '%#' explored for the first time", _area.c_str() );
        }
        for ( size_t index = 0; index < _listPickup.size(); ++index )
        {
            if ( float2::getDistanceSquared( _listPickupPosition[index], position ) <= _markerReach * _markerReach )
                collectPickup( _listPickup[index].key() );
        }
        (void)_deathRest.tryRecoverCorpse( _area, position ); // 시체 곁이 아니면 할 일이 없다(되찾음은 알림으로 읽는다)
    }

    void MetroidvaniaGameModeComponent::restAt( const MetroidvaniaSiteDef& site )
    {
        if ( _deathRest.rest( site._id, _player.getHealthMachine() ) == false )
            return;
        // 쉬면 보스가 아닌 적이 다시 나온다 — 전투원도 처음 상태로 세운다(보스의 처치 기록은 남는다).
        (void)populateEnemies( _listEnemy ); // 카탈로그 규칙은 판을 열 때 받아들여졌다
        const AreaDef* const pArea = _graph.findArea( site._area );
        if ( pArea == nullptr )
            return;
        if ( _mapState.buyRegionMap( pArea->_region, _wallet, _pCatalog->getRules()._currency ) == MetroidvaniaMapPurchase::Bought )
            SW_LOG_INFO_ID( "Metroidvania.MapBought", "[Metroidvania] bought the map of '%#'", pArea->_region.c_str() );
    }

    void MetroidvaniaGameModeComponent::interactWithSite()
    {
        const optional<int32> found = findSiteInReach();
        if ( found.has_value() == false )
            return;
        const hashed_string              siteID = _listSite[static_cast<size_t>( *found )].key();
        const MetroidvaniaSiteDef* const pSite  = _pCatalog->findSite( siteID );
        if ( pSite == nullptr )
            return;
        const bool bOpened = _mapState.activateSite( siteID );
        if ( pSite->_bRest )
        {
            restAt( *pSite );
            return;
        }
        if ( bOpened || pSite->_bFastTravel == false )
            return; // 처음 쓰면 열기만 한다
        for ( size_t step = 1; step < _listSite.size(); ++step )
        {
            const size_t next = ( static_cast<size_t>( *found ) + step ) % _listSite.size();
            if ( _mapState.canFastTravel( siteID, _listSite[next].key() ) == false )
                continue;
            _motor.setPosition( _listSitePosition[next] );
            _motor.setVelocity( float2{ 0.0f, 0.0f } );
            getSoundQueue().queueEvent( MetroidvaniaGameModeComponentInternal::kEventFastTravel );
            SW_LOG_INFO_ID( "Metroidvania.FastTravel", "[Metroidvania] travelled from '%#' to '%#'", siteID.c_str(), _listSite[next].c_str() );
            return;
        }
    }

    void MetroidvaniaGameModeComponent::killPlayer()
    {
        // 죽음은 시체 · 되살아날 곳 · 물약 · 적 부활을 한 번에 정하고 체력을 되살린다(키트 계약) — 몸은 기다린 뒤 세운다.
        (void)_deathRest.die( _area, _motor.getPosition(), _player.getHealthMachine() );
        (void)populateEnemies( _listEnemy ); // 카탈로그 규칙은 판을 열 때 받아들여졌다
        _phase = MetroidvaniaPhase::Dead;
        _respawnDelay.start( _respawnDelaySeconds );
    }

    void MetroidvaniaGameModeComponent::respawnPlayer()
    {
        _phase = MetroidvaniaPhase::Explore;
        _motor.setPosition( findSitePosition( _deathRest.getRespawnSite() ) );
        _motor.setVelocity( float2{ 0.0f, 0.0f } );
    }

    void MetroidvaniaGameModeComponent::stepExplore( float32 deltaTime )
    {
        stepMovement( deltaTime );
        stepPlayerCombat( deltaTime );
        stepEnemies( deltaTime );
        const bool bKilledByTerrain = _motor.getPosition()._y < _fallY || _motor.hasEvent( PlatformerEvent::kTouchedHazard );
        if ( bKilledByTerrain )
            _player.getHealthMachine().kill();
        if ( _player.getHealthMachine().isAlive() == false )
        {
            killPlayer();
            return;
        }
        _player.getHealthMachine().discardEvents();
        stepMarkers();
        if ( _command._bUsePressed == SW_TRUE )
            interactWithSite();
        if ( _flags.hasFlag( _goalFlag ) == false )
            return;
        _phase = MetroidvaniaPhase::Won;
        _resultHold.start( _resultHoldSeconds );
        getSoundQueue().queueEvent( MetroidvaniaGameModeComponentInternal::kEventWon );
        SW_LOG_INFO_ID( "Metroidvania.Won", "[Metroidvania] '%#' is set - won with %# currency, completion %# percent", _goalFlag.c_str(), getCurrency(), _mapState.computeCompletionPercent() );
    }

    void MetroidvaniaGameModeComponent::settle()
    {
        _listEvent.clear();
        _deathRest.pollEvents( _listEvent );
        for ( const MetroidvaniaDeathRestEvent& event : _listEvent )
        {
            if ( const utf8* const pSound = MetroidvaniaGameModeComponentInternal::findSound( event._type ) )
                getSoundQueue().queueEvent( pSound );
            if ( event._type == MetroidvaniaDeathRestEventKind::Rested )
                SW_LOG_INFO_ID( "Metroidvania.Rested", "[Metroidvania] rested at '%#'", event._id.c_str() );
            else if ( event._type == MetroidvaniaDeathRestEventKind::Died )
                SW_LOG_INFO_ID( "Metroidvania.Died", "[Metroidvania] died - back to '%#'", event._id.empty() ? "start" : event._id.c_str() );
            else if ( event._type == MetroidvaniaDeathRestEventKind::CorpseRecovered )
                SW_LOG_INFO_ID( "Metroidvania.CorpseRecovered", "[Metroidvania] recovered %# currency", event._amount );
            else if ( event._type == MetroidvaniaDeathRestEventKind::EnemyKilled )
                SW_LOG_INFO_ID( "Metroidvania.EnemyKilled", "[Metroidvania] '%#' felled (+%#)", event._id.c_str(), event._amount );
        }
    }

    void MetroidvaniaGameModeComponent::updateGame( float32 deltaTime )
    {
        if ( deltaTime <= 0.0f )
            return;
        if ( _phase == MetroidvaniaPhase::Won )
        {
            if ( _resultHold.update( deltaTime ) )
                (void)finishMinigame( hashed_string( MetroidvaniaGameModeComponentInternal::kWinResult ) ); // 흐름이 없으면 베이스가 알린다
            return;
        }
        const int32 stepCount = _timer.consume( deltaTime );
        for ( int32 step = 0; step < stepCount && _phase != MetroidvaniaPhase::Won; ++step )
        {
            if ( _phase == MetroidvaniaPhase::Explore )
                stepExplore( _timer.getStep() );
            else if ( _respawnDelay.update( _timer.getStep() ) )
                respawnPlayer();
            MetroidvaniaPlayerCommand held; // 누름은 걸음 하나가 쓴다
            held._move       = _command._move;
            held._bJumpHeld  = _command._bJumpHeld;
            held._bGuardHeld = _command._bGuardHeld;
            _command         = held;
        }
        settle();
    }

    void MetroidvaniaGameModeComponent::onFlush( GameObjectManager& manager, bool bRespawnViews )
    {
        // 디렉터는 세울 오브젝트가 없다 — 모습은 클라이언트 모듈(`MetroidvaniaViewComponent`)이 읽어 보인다.
        (void)manager;
        (void)bRespawnViews;
    }

    void MetroidvaniaGameModeComponent::collectMinigameResult( vector<MinigameResultValue>& outListValue ) const
    {
        addMoneyResult( outListValue, getCurrency() );
    }

    void MetroidvaniaGameModeComponent::serializeGameState( StateIO& io )
    {
        io.header( MetroidvaniaGameModeComponentInternal::kStateTag, MetroidvaniaGameModeComponentInternal::kStateVersion );
        if ( io.isOk() == false || io.check( io.isWriting() || ( _pCatalog != nullptr && _pAreas != nullptr ) ) == false )
            return;
        // 방 · 지형 · 배치는 씬 데이터로 `startGame` 이 이미 세웠다. 규칙 객체는 같은 카탈로그로 연 사본에 읽는다(그래프 · 지갑은 이 디렉터의 것을 빌린다).
        PlatformerMotor2D          motor     = _motor;
        MetroidvaniaAbilitySet     abilities = _abilities;
        MetroidvaniaMapState       mapState  = _mapState;
        MetroidvaniaDeathRestState deathRest = _deathRest;
        MetroidvaniaTrinketLoadout loadout   = _loadout;
        MetroidvaniaDuelist        player    = _player;
        Wallet                     wallet    = _wallet;
        GameFlags                  flags     = _flags;
        GameRandom                 random    = _random;
        FixedStepTimer             timer     = _timer;
        vector<MetroidvaniaEnemy>  listEnemy = _listEnemy;
        vector<hashed_string>      listVisited;
        vector<hashed_string>      listDiscovered;
        if ( io.isReading() )
        {
            abilities.initialize( *_pCatalog );
            mapState.initialize( *_pCatalog, _graph );
            if ( io.check( deathRest.initialize( *_pCatalog, _wallet, nullptr ) ) == false )
                return;
            loadout.initialize( *_pCatalog );
            if ( io.check( player.initialize( *_pCatalog ) && populateEnemies( listEnemy ) ) == false )
                return;
        }
        else
        {
            _graph.fillVisitedAreas( listVisited );
            _graph.fillDiscoveredAreas( listDiscovered );
        }
        Countdown attackCooldown = _attackCooldown;
        Countdown respawnDelay   = _respawnDelay;
        Countdown resultHold     = _resultHold;
        io.value( motor );
        io.value( abilities );
        io.value( mapState );
        io.value( deathRest );
        io.value( loadout );
        io.value( player );
        io.value( wallet );
        io.value( flags );
        io.value( random );
        io.value( timer );
        io.value( attackCooldown );
        io.value( respawnDelay );
        io.value( resultHold );
        io.list( listVisited );
        io.list( listDiscovered );
        io.expect( static_cast<uint32>( listEnemy.size() ) ); // 적은 씬 배치가 정한다
        for ( MetroidvaniaEnemy& enemy : listEnemy )
        {
            io.value( enemy._duelist );
            io.value( enemy._startup );
            io.value( enemy._cooldown );
            if ( io.isOk() == false )
                return;
        }
        io.end();
        if ( io.isWriting() || io.isOk() == false )
            return;
        abilities.applyToMotor( motor ); // 몸 설정은 얻은 능력에서 나온다(상태 바이트에 없다)
        _graph = *_pAreas;               // 다시 읽힌 방 정의가 이긴다
        _graph.restoreState( listVisited, listDiscovered );
        _motor          = motor;
        _abilities      = std::move( abilities );
        _mapState       = std::move( mapState );
        _deathRest      = std::move( deathRest );
        _loadout        = std::move( loadout );
        _player         = player;
        _wallet         = std::move( wallet );
        _flags          = std::move( flags );
        _random         = random;
        _timer          = timer;
        _attackCooldown = attackCooldown;
        _respawnDelay   = respawnDelay;
        _resultHold     = resultHold;
        _listEnemy      = std::move( listEnemy );
    }
} // namespace sw

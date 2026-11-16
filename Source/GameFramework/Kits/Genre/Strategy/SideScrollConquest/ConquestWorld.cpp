#include "pch.h"

#include "GameFramework/Kits/Genre/Strategy/SideScrollConquest/ConquestWorld.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Foundation/Constants.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Runtime/Foundation/GameData/DepletingValue.h"

namespace sw
{
    namespace
    {
        struct ConquestWorldInternal
        {
            static constexpr uint32  kSiteStateMinBytes = 18; ///< 거점 하나의 최소 바이트 — 이름(4) + 성문, 성벽, 점령(12) + 주인, 점령 편(2)
            static constexpr float32 kFarAway           = 1.0e9f;
            static constexpr float32 kGateStandOff      = 0.5f;  ///< 성문 앞에서 멈추는 거리
            static constexpr float32 kLadderReach       = 1.0f;  ///< 사다리가 이 거리 안에 있으면 그 성문을 넘는다
            static constexpr float32 kPlantTolerance    = 0.05f; ///< 사다리가 걸칠 위치에 이만큼 가까우면 걸친 것으로 본다
            static constexpr float32 kApproachRatio     = 0.9f;  ///< 사거리의 이 비율까지만 다가간다(경계에서 떨지 않게 하려는 값이다)

            static float32 computeDirection( ConquestTeam team ) { return team == ConquestTeam::Enemy ? -1.0f : 1.0f; }

            static bool isHostile( ConquestTeam team, ConquestTeam other )
            {
                return team != ConquestTeam::Neutral && other != ConquestTeam::Neutral && team != other;
            }

            static float32 computeApproachX( float32 x, float32 targetX, float32 range )
            {
                if ( targetX == x )
                    return x;
                const float32 side = targetX > x ? 1.0f : -1.0f;
                return targetX - side * range * kApproachRatio;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    ConquestWorld::ConquestWorld()
        : _listSite{}
        , _listBuilding{}
        , _listUnit{}
        , _eventBuffer{}
        , _resource{}
        , _commander{}
        , _timer{}
        , _pCatalog{ nullptr }
        , _elapsed{ 0.0f }
        , _incomeTimer{ 0.0f }
        , _waveTimer{ 0.0f }
        , _nextUnitID{ 1 }
        , _bVictory{ SW_FALSE }
        , _bDefeat{ SW_FALSE }
    {
    }

    void ConquestWorld::initialize( const ConquestCatalog* pCatalog )
    {
        _pCatalog = pCatalog;
        _listSite.clear();
        _listBuilding.clear();
        _listUnit.clear();
        _eventBuffer.clear();
        _resource.clear();
        _elapsed     = 0.0f;
        _incomeTimer = 0.0f;
        _waveTimer   = 0.0f;
        _nextUnitID  = 1;
        _bVictory    = SW_FALSE;
        _bDefeat     = SW_FALSE;
        if ( pCatalog == nullptr )
            return;
        const ConquestRules& rules = pCatalog->getRules();
        _timer                     = FixedStepTimer( rules._fixedStep, 1.0f );
        _resource.merge( rules._startResources );
        for ( const ConquestSiteDef& def : pCatalog->getSites() )
        {
            ConquestSite site;
            site._pDef       = &def;
            site._gateHealth = def._gateHealth;
            site._wallHealth = def._wallHealth;
            site._owner      = def._owner;
            _listSite.push_back( site );
        }
        for ( const ConquestSiteDef& def : pCatalog->getSites() )
        {
            for ( const ConquestGarrisonDef& garrison : def._listGarrison )
            {
                for ( int32 count = 0; count < garrison._count; ++count )
                {
                    (void)spawnUnit( garrison._unitID, def._owner, def._x, ConquestOrder::Hold ); // 모르는 유닛이면 -1 이다. 카탈로그 로드가 이미 경고했다
                }
            }
        }
        _commander                 = ConquestCommander{};
        _commander._health         = rules._commanderHealth;
        const optional<int32> home = findHomeSiteIndex();
        _commander._x              = home.has_value() ? _listSite[static_cast<size_t>( *home )]._pDef->_x : 0.0f;
    }

    void ConquestWorld::update( float32 deltaTime )
    {
        if ( _pCatalog == nullptr )
            return;
        const int32 stepCount = _timer.consume( deltaTime );
        for ( int32 stepIndex = 0; stepIndex < stepCount; ++stepIndex )
        {
            step( _timer.getStep() );
        }
    }

    void ConquestWorld::setCommanderMove( float32 axis )
    {
        _commander._moveAxis = MathUtil::clamp( axis, -1.0f, 1.0f );
    }

    void ConquestWorld::issueOrder( ConquestOrder order )
    {
        const float32 spacing = _pCatalog != nullptr ? _pCatalog->getRules()._formationSpacing : 1.0f;
        for ( ConquestUnit& unit : _listUnit )
        {
            if ( unit._bAlive == SW_FALSE || unit._squadSlot < 0 )
                continue;
            unit._order = order;
            if ( order == ConquestOrder::Hold )
                unit._holdX = _commander._x - _commander._facing * static_cast<float32>( unit._squadSlot + 1 ) * spacing;
        }
    }

    ConquestResult ConquestWorld::placeBuilding( const hashed_string& buildingID, const hashed_string& siteID )
    {
        const ConquestBuildingDef* pDef = _pCatalog != nullptr ? _pCatalog->findBuilding( buildingID ) : nullptr;
        if ( pDef == nullptr )
            return ConquestResult::UnknownDef;
        int32 siteIndex = constant::invalid_index::kInt32;
        for ( size_t index = 0; index < _listSite.size(); ++index )
        {
            if ( _listSite[index]._pDef->_id == siteID )
                siteIndex = static_cast<int32>( index );
        }
        if ( siteIndex < 0 )
            return ConquestResult::UnknownDef;
        const ConquestSite& site = _listSite[static_cast<size_t>( siteIndex )];
        if ( site._owner != ConquestTeam::Player )
            return ConquestResult::SiteNotOwned;
        int32 usedSlots = 0;
        for ( const ConquestBuilding& building : _listBuilding )
        {
            if ( building._siteIndex == siteIndex )
                ++usedSlots;
        }
        if ( usedSlots >= site._pDef->_buildSlots )
            return ConquestResult::NoBuildSlot;
        if ( _resource.canAfford( pDef->_cost ) == false )
            return ConquestResult::NotEnoughResources;
        (void)_resource.trySpend( pDef->_cost ); // 바로 위의 canAfford 를 통과했으므로 항상 성공한다
        ConquestBuilding building;
        building._pDef      = pDef;
        building._siteIndex = siteIndex;
        _listBuilding.push_back( building );
        return ConquestResult::Ok;
    }

    ConquestResult ConquestWorld::assignWorkers( int32 buildingIndex, int32 workerCount )
    {
        if ( buildingIndex < 0 || buildingIndex >= static_cast<int32>( _listBuilding.size() ) )
            return ConquestResult::InvalidBuilding;
        ConquestBuilding& building = _listBuilding[static_cast<size_t>( buildingIndex )];
        if ( _listSite[static_cast<size_t>( building._siteIndex )]._owner != ConquestTeam::Player )
            return ConquestResult::SiteNotOwned;
        const int32 wanted = MathUtil::clamp( workerCount, 0, building._pDef->_workerSlots );
        if ( wanted - building._workers > computeFreeWorkers() )
            return ConquestResult::NoFreeWorkers;
        building._workers = wanted;
        return ConquestResult::Ok;
    }

    ConquestResult ConquestWorld::trainUnit( int32 buildingIndex, const hashed_string& unitID )
    {
        if ( buildingIndex < 0 || buildingIndex >= static_cast<int32>( _listBuilding.size() ) )
            return ConquestResult::InvalidBuilding;
        ConquestBuilding&      building = _listBuilding[static_cast<size_t>( buildingIndex )];
        const ConquestUnitDef* pUnit    = _pCatalog->findUnit( unitID );
        if ( pUnit == nullptr )
            return ConquestResult::UnknownDef;
        if ( _listSite[static_cast<size_t>( building._siteIndex )]._owner != ConquestTeam::Player )
            return ConquestResult::SiteNotOwned;
        bool bTrainable = false;
        for ( const hashed_string& trainable : building._pDef->_listTrainable )
        {
            bTrainable = bTrainable || trainable == unitID;
        }
        if ( bTrainable == false )
            return ConquestResult::CannotTrainHere;
        if ( computePopulation() + pUnit->_population > computePopulationCap() )
            return ConquestResult::PopulationCap;
        if ( _resource.canAfford( pUnit->_cost ) == false )
            return ConquestResult::NotEnoughResources;
        (void)_resource.trySpend( pUnit->_cost ); // 바로 위의 canAfford 를 통과했으므로 항상 성공한다
        building._listQueue.push_back( unitID );
        return ConquestResult::Ok;
    }

    int32 ConquestWorld::spawnUnit( const hashed_string& unitID, ConquestTeam team, float32 x, ConquestOrder order )
    {
        const ConquestUnitDef* pDef = _pCatalog != nullptr ? _pCatalog->findUnit( unitID ) : nullptr;
        if ( pDef == nullptr || team == ConquestTeam::Neutral )
            return constant::invalid_index::kInt32;
        ConquestUnit unit;
        unit._pDef      = pDef;
        unit._x         = x;
        unit._holdX     = x;
        unit._health    = pDef->_health;
        unit._unitID    = _nextUnitID++;
        unit._team      = team;
        unit._order     = order;
        unit._squadSlot = team == ConquestTeam::Player ? getSquadSize() : constant::invalid_index::kInt32;
        _listUnit.push_back( unit );
        return unit._unitID;
    }

    void ConquestWorld::addResource( const hashed_string& resource, float32 amount )
    {
        if ( amount < 0.0f )
        {
            SW_LOG_ERROR_ID( "ConquestWorld.NegativeResource", "ConquestWorld: refused to add %# of '%#' - it must not be negative", amount, resource.c_str() );
            return;
        }
        _resource.addValue( resource, amount );
    }

    void ConquestWorld::pollEvents( const ConsumerLease& lease, vector<ConquestEvent>& outListEvent )
    {
        _eventBuffer.moveAllTo( lease, outListEvent );
    }

    void ConquestWorld::serializeState( StateIO& io )
    {
        if ( io.check( _pCatalog != nullptr ) == false )
            return;
        // 거점은 카탈로그의 거점마다 하나이다 — 저장한 거점은 id 로 짝짓는다(감사 A-6: 거점을 더하거나 순서를 바꾼 패치 뒤에도 성문 · 점령이 남는다).
        // 카탈로그에서 빠진 거점의 상태는 버리고, 새 거점은 지금 값 그대로다. 건물은 저장 번호를 지금 번호로 옮긴다. 이름(4) + 성문, 성벽, 점령(12) + 주인, 점령 편(2)
        uint32 savedSiteCount = static_cast<uint32>( _listSite.size() );
        io.count( ConquestWorldInternal::kSiteStateMinBytes, savedSiteCount );
        if ( io.isOk() == false )
            return;
        vector<int32> listSiteMap; // 저장 번호 → 지금 번호(카탈로그에서 빠졌으면 -1)
        for ( uint32 savedIndex = 0; savedIndex < savedSiteCount; ++savedIndex )
        {
            ConquestSite  site   = io.isWriting() ? _listSite[savedIndex] : ConquestSite{};
            hashed_string siteID = io.isWriting() ? site._pDef->_id : hashed_string{};
            io.value( siteID );
            io.value( site._gateHealth );
            io.value( site._wallHealth );
            io.value( site._captureProgress );
            io.enumValue( site._owner, ConquestTeam::Enemy );
            io.enumValue( site._captureTeam, ConquestTeam::Enemy );
            if ( io.isWriting() )
                continue;
            if ( io.isOk() == false )
                return;
            ConquestSite* const pCurrent = VectorUtil::findIf( _listSite, [&]( const ConquestSite& current )
            { return current._pDef->_id == siteID; } );
            if ( pCurrent == nullptr )
            {
                listSiteMap.push_back( constant::invalid_index::kInt32 );
                continue;
            }
            const int32            currentIndex = static_cast<int32>( pCurrent - _listSite.data() );
            const ConquestSiteDef& def          = *pCurrent->_pDef;
            if ( io.check( VectorUtil::contains( listSiteMap, currentIndex ) == false && 0.0f <= site._gateHealth && site._gateHealth <= def._gateHealth && 0.0f <= site._wallHealth && site._wallHealth <= def._wallHealth &&
                           0.0f <= site._captureProgress && site._captureProgress <= _pCatalog->getRules()._captureTime ) == false )
                return;
            site._pDef = &def;
            *pCurrent  = site;
            listSiteMap.push_back( currentIndex );
        }
        const int32 siteCount = static_cast<int32>( savedSiteCount );
        // 건물마다 이름(4) + 대기열 수(4) + 진행 둘(8) + 슬롯, 일꾼(8)
        io.list( _listBuilding, 24, [&]( ConquestBuilding& building )
        {
            hashed_string buildingID = io.isWriting() ? building._pDef->_id : hashed_string{};
            io.value( buildingID );
            if ( io.isReading() )
            {
                building._pDef = _pCatalog->findBuilding( buildingID );
                if ( io.check( building._pDef != nullptr ) == false )
                    return;
            }
            io.list( building._listQueue, 4, [&]( hashed_string& unitID )
            {
                io.value( unitID );
                if ( io.isReading() )
                {
                    io.check( VectorUtil::contains( building._pDef->_listTrainable, unitID ) && _pCatalog->findUnit( unitID ) != nullptr );
                }
            } );
            io.value( building._cycleProgress );
            io.value( building._trainProgress );
            io.value( building._siteIndex );
            io.value( building._workers );
            if ( io.check( 0 <= building._siteIndex && building._siteIndex < siteCount && 0 <= building._workers && building._workers <= building._pDef->_workerSlots && 0.0f <= building._cycleProgress && 0.0f <= building._trainProgress ) == false )
                return;
            if ( io.isReading() )
            {
                // 저장 번호 → 지금 번호. 빠진 거점 위의 건물은 놓을 자리가 없어 거절한다(건물을 조용히 버리지 않는다).
                building._siteIndex = listSiteMap[static_cast<size_t>( building._siteIndex )];
                io.check( building._siteIndex != constant::invalid_index::kInt32 );
            }
        } );
        // 병사마다 이름(4) + 위치, 체력, 쿨다운, 대기 위치, 준 피해(20) + 번호, 부대 슬롯(8) + 편, 명령, 생존, 사다리(4)
        io.list( _listUnit, 36, [&]( ConquestUnit& unit )
        {
            hashed_string unitID = io.isWriting() ? unit._pDef->_id : hashed_string{};
            io.value( unitID );
            if ( io.isReading() )
            {
                unit._pDef = _pCatalog->findUnit( unitID );
            }
            io.value( unit._x );
            io.value( unit._health );
            io.value( unit._attackCooldown );
            io.value( unit._holdX );
            io.value( unit._damageDealt );
            io.value( unit._unitID );
            io.value( unit._squadSlot );
            io.enumValue( unit._team, ConquestTeam::Enemy );
            io.enumValue( unit._order, ConquestOrder::Charge );
            io.flag( unit._bAlive );
            io.flag( unit._bPlanted );
            if ( io.isWriting() || io.check( unit._pDef != nullptr && unit._bAlive == SW_TRUE ) == false )
                return;
            // 걸음 끝에 죽은 병사를 지우므로 상태에 든 병사는 살아 있다.
            const bool bSquadValid   = unit._squadSlot >= constant::invalid_index::kInt32 && ( unit._team == ConquestTeam::Player || unit._squadSlot == constant::invalid_index::kInt32 );
            const bool bPlantedValid = unit._bPlanted == SW_FALSE || unit._pDef->_siegeRole == ConquestSiegeRole::Ladder;
            io.check( unit._team != ConquestTeam::Neutral && 0.0f < unit._health && unit._health <= unit._pDef->_health && unit._unitID >= 1 && bSquadValid && bPlantedValid );
        } );
        io.value( _resource );
        io.value( _commander._x );
        io.value( _commander._health );
        io.value( _commander._attackCooldown );
        io.value( _commander._respawnTimer );
        io.value( _commander._moveAxis );
        io.value( _commander._facing );
        io.flag( _commander._bAlive );
        io.value( _timer );
        io.value( _elapsed );
        io.value( _incomeTimer );
        io.value( _waveTimer );
        io.value( _nextUnitID );
        io.flag( _bVictory );
        io.flag( _bDefeat );
        if ( io.isReading() && io.isOk() )
        {
            io.check( isStateConsistent() );
            _eventBuffer.clear();
        }
    }

    bool ConquestWorld::isStateConsistent() const
    {
        const ConquestRules& rules = _pCatalog->getRules();
        for ( const StatValue& value : _resource.getValues() )
        {
            if ( value._value < 0.0f )
                return false;
        }
        // 지휘관: 쓰러지면 체력이 0 이고, 입력과 바라보는 쪽은 setCommanderMove 가 정한 범위다.
        const bool bCommanderHealthValid = _commander._bAlive == SW_TRUE ? ( 0.0f < _commander._health && _commander._health <= rules._commanderHealth ) : _commander._health == 0.0f;
        const bool bCommanderInputValid  = -1.0f <= _commander._moveAxis && _commander._moveAxis <= 1.0f && ( _commander._facing == 1.0f || _commander._facing == -1.0f );
        const bool bTimersValid          = _elapsed >= 0.0f && _incomeTimer >= 0.0f && _waveTimer >= 0.0f;
        if ( bCommanderHealthValid == false || bCommanderInputValid == false || bTimersValid == false || ( _bVictory == SW_TRUE && _bDefeat == SW_TRUE ) )
            return false;

        // 건물: 잃은 거점의 건물은 일꾼과 대기열을 비운다(`stepCapture`). 거점의 슬롯과 일꾼 수를 넘지 않는다.
        if ( computeFreeWorkers() < 0 )
            return false;
        for ( const ConquestBuilding& building : _listBuilding )
        {
            const ConquestSite& site     = _listSite[static_cast<size_t>( building._siteIndex )];
            const bool          bOwned   = site._owner == ConquestTeam::Player;
            const int32         siteUsed = VectorUtil::countBy<&ConquestBuilding::_siteIndex>( _listBuilding, building._siteIndex );
            if ( siteUsed > site._pDef->_buildSlots || ( bOwned == false && ( building._workers != 0 || building._listQueue.empty() == false ) ) )
                return false;
        }

        // 병사 번호는 겹치지 않고 다음 번호보다 작다 — 겹치면 `findUnit` 이 다른 병사를 돌려주고, 새 병사가 옛 번호를 받는다.
        if ( VectorUtil::findDuplicateBy<&ConquestUnit::_unitID>( _listUnit ) != nullptr || _nextUnitID < 1 )
            return false;
        for ( const ConquestUnit& unit : _listUnit )
        {
            if ( unit._unitID >= _nextUnitID )
                return false;
        }
        return true;
    }

    int32 ConquestWorld::getResource( const hashed_string& resource ) const
    {
        return static_cast<int32>( _resource.getValue( resource ) + 0.5f );
    }

    int32 ConquestWorld::computePopulation() const
    {
        int32 population = 0;
        for ( const ConquestUnit& unit : _listUnit )
        {
            if ( unit._bAlive == SW_TRUE && unit._team == ConquestTeam::Player )
                population += unit._pDef->_population;
        }
        for ( const ConquestBuilding& building : _listBuilding )
        {
            for ( const hashed_string& queued : building._listQueue )
            {
                const ConquestUnitDef* pUnit = _pCatalog->findUnit( queued );
                population += pUnit != nullptr ? pUnit->_population : 0;
            }
        }
        return population;
    }

    int32 ConquestWorld::computePopulationCap() const
    {
        if ( _pCatalog == nullptr )
            return 0;
        int32 cap = _pCatalog->getRules()._basePopulation;
        for ( const ConquestSite& site : _listSite )
        {
            if ( site._owner == ConquestTeam::Player )
                cap += site._pDef->_housing;
        }
        for ( const ConquestBuilding& building : _listBuilding )
        {
            if ( _listSite[static_cast<size_t>( building._siteIndex )]._owner == ConquestTeam::Player )
                cap += building._pDef->_housing;
        }
        return cap;
    }

    int32 ConquestWorld::computeFreeWorkers() const
    {
        int32 total = 0;
        for ( const ConquestSite& site : _listSite )
        {
            if ( site._owner == ConquestTeam::Player )
                total += site._pDef->_workers;
        }
        for ( const ConquestBuilding& building : _listBuilding )
        {
            total -= building._workers;
        }
        return total;
    }

    int32 ConquestWorld::computeTerritory() const
    {
        int32 territory = 0;
        for ( const ConquestSite& site : _listSite )
        {
            if ( site._owner == ConquestTeam::Player )
                ++territory;
        }
        return territory;
    }

    int32 ConquestWorld::computeWaveSize() const
    {
        if ( _pCatalog == nullptr )
            return 0;
        const ConquestRules& rules       = _pCatalog->getRules();
        const int32          byTime      = static_cast<int32>( _elapsed / 60.0f * rules._waveCountPerMinute );
        const int32          byTerritory = MathUtil::max( 0, computeTerritory() - 1 ) * rules._waveCountPerSite;
        return rules._waveBaseCount + byTime + byTerritory;
    }

    const ConquestSite* ConquestWorld::findSite( const hashed_string& siteID ) const
    {
        return VectorUtil::findIf( _listSite, [&]( const ConquestSite& site )
        { return site._pDef->_id == siteID; } );
    }

    const ConquestUnit* ConquestWorld::findUnit( int32 unitID ) const
    {
        return VectorUtil::findBy<&ConquestUnit::_unitID>( _listUnit, unitID );
    }

    int32 ConquestWorld::getSquadSize() const
    {
        int32 squadSize = 0;
        for ( const ConquestUnit& unit : _listUnit )
        {
            if ( unit._bAlive == SW_TRUE && unit._squadSlot >= 0 )
                ++squadSize;
        }
        return squadSize;
    }

    void ConquestWorld::step( float32 deltaTime )
    {
        if ( _bVictory == SW_TRUE || _bDefeat == SW_TRUE )
            return;
        _elapsed += deltaTime;
        stepCommander( deltaTime );
        for ( int32 unitIndex = 0; unitIndex < static_cast<int32>( _listUnit.size() ); ++unitIndex )
        {
            stepUnit( unitIndex, deltaTime );
        }
        VectorUtil::removeIf( _listUnit, []( const ConquestUnit& unit )
        { return unit._bAlive == SW_FALSE; } );
        refreshSquadSlots();
        stepCapture( deltaTime );
        stepEconomy( deltaTime );
        stepWaves( deltaTime );
        refreshResult();
    }

    void ConquestWorld::stepCommander( float32 deltaTime )
    {
        const ConquestRules& rules = _pCatalog->getRules();
        if ( _commander._bAlive == SW_FALSE )
        {
            _commander._respawnTimer.update( deltaTime );
            const optional<int32> home = findHomeSiteIndex();
            if ( _commander._respawnTimer.isActive() || home.has_value() == false )
                return;
            _commander._bAlive = SW_TRUE;
            _commander._health = rules._commanderHealth;
            _commander._x      = _listSite[static_cast<size_t>( *home )]._pDef->_x;
            _commander._attackCooldown.clear();
            pushEvent( ConquestEvent::Kind::CommanderRespawned, hashed_string(), 0, ConquestTeam::Player );
            return;
        }
        if ( _commander._moveAxis != 0.0f )
        {
            _commander._facing = _commander._moveAxis > 0.0f ? 1.0f : -1.0f;
            _commander._x      = clampMove( ConquestTeam::Player, _commander._x, _commander._x + _commander._moveAxis * rules._commanderSpeed * deltaTime );
        }
        _commander._attackCooldown.update( deltaTime );
        if ( _commander._attackCooldown.isActive() )
            return;
        const Target target = findNearestTarget( ConquestTeam::Player, _commander._x, rules._commanderRange, true, true );
        if ( target.isValid() == false )
            return;
        float32 dealt = 0.0f;
        applyAttack( ConquestTeam::Player, rules._commanderDamage, rules._commanderStructureRate, target, dealt );
        _commander._attackCooldown.restart( rules._commanderAttackInterval ); // 늦은 만큼을 이어받아, 공격 빈도가 걸음 크기에 매이지 않게 한다
    }

    void ConquestWorld::stepUnit( int32 unitIndex, float32 deltaTime )
    {
        ConquestUnit& unit = _listUnit[static_cast<size_t>( unitIndex )];
        if ( unit._bAlive == SW_FALSE )
            return;
        const ConquestRules&   rules     = _pCatalog->getRules();
        const ConquestUnitDef& def       = *unit._pDef;
        const float32          direction = ConquestWorldInternal::computeDirection( unit._team );
        const float32          stepSize  = def._speed * deltaTime;
        unit._attackCooldown.update( deltaTime );
        const auto moveToward = [&]( float32 goalX )
        {
            const float32 delta = MathUtil::clamp( goalX - unit._x, -stepSize, stepSize );
            unit._x             = clampMove( unit._team, unit._x, unit._x + delta );
        };

        if ( def._siegeRole == ConquestSiegeRole::Ladder )
        {
            // 사다리: 돌격 명령이면(적은 항상) 앞쪽의 가장 가까운 성문에 가서 걸친다. 싸우지 않는다.
            if ( unit._order == ConquestOrder::Charge || unit._team == ConquestTeam::Enemy )
            {
                float32 bestDistance = ConquestWorldInternal::kFarAway;
                float32 plantX       = 0.0f;
                for ( const ConquestSite& site : _listSite )
                {
                    if ( ConquestWorldInternal::isHostile( unit._team, site._owner ) == false || site._gateHealth <= 0.0f )
                        continue;
                    const float32 candidateX = computeGateX( site, unit._team ) - direction * ConquestWorldInternal::kGateStandOff;
                    const float32 ahead      = ( candidateX - unit._x ) * direction;
                    if ( ahead >= -ConquestWorldInternal::kLadderReach && MathUtil::abs( ahead ) < bestDistance )
                    {
                        bestDistance = MathUtil::abs( ahead );
                        plantX       = candidateX;
                    }
                }
                if ( bestDistance < ConquestWorldInternal::kFarAway )
                {
                    unit._bPlanted = MathUtil::abs( unit._x - plantX ) <= ConquestWorldInternal::kPlantTolerance ? SW_TRUE : SW_FALSE;
                    if ( unit._bPlanted == SW_FALSE )
                        moveToward( plantX );
                    return;
                }
            }
            unit._bPlanted = SW_FALSE;
            moveToward( computeFormationX( unit ) );
            return;
        }

        const bool    bRam        = def._siegeRole == ConquestSiegeRole::Ram;
        const float32 searchRange = unit._order == ConquestOrder::Charge ? ConquestWorldInternal::kFarAway : rules._aggroRange;
        Target        target      = findNearestTarget( unit._team, unit._x, searchRange, bRam == false, true );
        if ( target.isValid() )
        {
            const float32 distance             = MathUtil::abs( target._x - unit._x );
            const bool    bFollowLeashExceeded = unit._order == ConquestOrder::Follow && unit._squadSlot >= 0 && _commander._bAlive == SW_TRUE && MathUtil::abs( target._x - _commander._x ) > rules._followLeash;
            if ( bFollowLeashExceeded )
                target = Target{};
            else if ( unit._order == ConquestOrder::Hold && distance > def._range )
                target = Target{};
        }
        if ( target.isValid() == false )
        {
            if ( unit._order == ConquestOrder::Charge )
            {
                // 싸울 상대가 없으면 앞쪽의 가장 가까운 남의 거점으로 점령하러 간다. 그것도 없으면 그 자리에 있는다.
                float32 goalX = unit._x;
                float32 best  = ConquestWorldInternal::kFarAway;
                for ( const ConquestSite& site : _listSite )
                {
                    const float32 ahead                = ( site._pDef->_x - unit._x ) * direction;
                    const bool    bUncapturedSiteAhead = site._owner != unit._team && ahead >= -site._pDef->_captureRadius && MathUtil::abs( ahead ) < best;
                    if ( bUncapturedSiteAhead )
                    {
                        best  = MathUtil::abs( ahead );
                        goalX = site._pDef->_x;
                    }
                }
                moveToward( goalX );
            }
            else
                moveToward( computeFormationX( unit ) );
            return;
        }
        if ( MathUtil::abs( target._x - unit._x ) > def._range )
        {
            moveToward( ConquestWorldInternal::computeApproachX( unit._x, target._x, def._range ) );
            return;
        }
        if ( unit._attackCooldown.isActive() )
            return;
        const float32 damage = def._damage * computeMoraleScale( unit._team, unit._x );
        applyAttack( unit._team, damage, def._structureScale, target, unit._damageDealt );
        unit._attackCooldown.restart( def._attackInterval ); // 늦은 만큼을 이어받아, 공격 빈도가 걸음 크기에 매이지 않게 한다
    }

    void ConquestWorld::stepCapture( float32 deltaTime )
    {
        const ConquestRules& rules = _pCatalog->getRules();
        for ( int32 siteIndex = 0; siteIndex < static_cast<int32>( _listSite.size() ); ++siteIndex )
        {
            ConquestSite& site          = _listSite[static_cast<size_t>( siteIndex )];
            const float32 radius        = site._pDef->_captureRadius;
            bool          bPlayerInside = _commander._bAlive == SW_TRUE && MathUtil::abs( _commander._x - site._pDef->_x ) <= radius;
            bool          bEnemyInside  = false;
            for ( const ConquestUnit& unit : _listUnit )
            {
                if ( MathUtil::abs( unit._x - site._pDef->_x ) > radius )
                    continue;
                bPlayerInside = bPlayerInside || unit._team == ConquestTeam::Player;
                bEnemyInside  = bEnemyInside || unit._team == ConquestTeam::Enemy;
            }
            ConquestTeam contender = ConquestTeam::Neutral;
            if ( bPlayerInside && bEnemyInside == false )
                contender = ConquestTeam::Player;
            else if ( bEnemyInside && bPlayerInside == false )
                contender = ConquestTeam::Enemy;
            const bool bBarred    = site._gateHealth > 0.0f && site._owner != ConquestTeam::Neutral && isLaddered( siteIndex, contender ) == false;
            const bool bNoCapture = contender == ConquestTeam::Neutral || contender == site._owner || bBarred;
            if ( bNoCapture )
            {
                site._captureProgress = MathUtil::max( 0.0f, site._captureProgress - deltaTime );
                continue;
            }
            if ( site._captureTeam != contender )
            {
                site._captureTeam     = contender;
                site._captureProgress = 0.0f;
            }
            site._captureProgress += deltaTime;
            if ( site._captureProgress < rules._captureTime )
                continue;
            const ConquestTeam previousOwner = site._owner;
            site._owner                      = contender;
            site._captureProgress            = 0.0f;
            site._captureTeam                = ConquestTeam::Neutral;
            if ( previousOwner == ConquestTeam::Player )
            {
                // 잃은 거점의 건물은 멈춘다. 일꾼은 풀려나고 대기열은 사라진다.
                for ( ConquestBuilding& building : _listBuilding )
                {
                    if ( building._siteIndex != siteIndex )
                        continue;
                    building._workers = 0;
                    building._listQueue.clear();
                    building._trainProgress = 0.0f;
                }
                releaseExcessWorkers();
            }
            pushEvent( ConquestEvent::Kind::SiteCaptured, site._pDef->_id, 0, contender );
        }
    }

    void ConquestWorld::stepEconomy( float32 deltaTime )
    {
        const ConquestRules& rules = _pCatalog->getRules();
        for ( ConquestBuilding& building : _listBuilding )
        {
            const ConquestSite& site = _listSite[static_cast<size_t>( building._siteIndex )];
            if ( site._owner != ConquestTeam::Player )
                continue;
            const ConquestBuildingDef& def = *building._pDef;
            if ( def._produces.empty() == false && building._workers > 0 )
            {
                building._cycleProgress += deltaTime;
                while ( building._cycleProgress >= def._cycleTime )
                {
                    building._cycleProgress -= def._cycleTime;
                    const int32 amount = def._amountPerWorker * building._workers;
                    _resource.addValue( def._produces, static_cast<float32>( amount ) );
                    pushEvent( ConquestEvent::Kind::ResourceProduced, def._produces, amount, ConquestTeam::Player );
                }
            }
            if ( building._listQueue.empty() == false )
            {
                const ConquestUnitDef* pUnit = _pCatalog->findUnit( building._listQueue.front() );
                building._trainProgress += deltaTime;
                if ( pUnit != nullptr && building._trainProgress >= pUnit->_trainTime )
                {
                    building._trainProgress    = 0.0f;
                    const hashed_string unitID = building._listQueue.front();
                    building._listQueue.erase( building._listQueue.begin() );
                    const int32 spawnedID = spawnUnit( unitID, ConquestTeam::Player, site._pDef->_x, ConquestOrder::Follow );
                    pushEvent( ConquestEvent::Kind::UnitTrained, unitID, spawnedID, ConquestTeam::Player );
                }
            }
        }

        _incomeTimer += deltaTime;
        if ( _incomeTimer < rules._incomeInterval )
            return;
        _incomeTimer -= rules._incomeInterval;
        int32 payingSites = 0;
        for ( const ConquestSite& site : _listSite )
        {
            if ( site._owner != ConquestTeam::Player || site._pDef->_income.isEmpty() )
                continue;
            _resource.merge( site._pDef->_income );
            ++payingSites;
        }
        if ( payingSites > 0 )
            pushEvent( ConquestEvent::Kind::IncomePaid, hashed_string(), payingSites, ConquestTeam::Player );
    }

    void ConquestWorld::stepWaves( float32 deltaTime )
    {
        const ConquestRules& rules = _pCatalog->getRules();
        if ( rules._waveInterval <= 0.0f || _pCatalog->findUnit( rules._waveUnit ) == nullptr )
            return;
        _waveTimer += deltaTime;
        if ( _waveTimer < rules._waveInterval )
            return;
        _waveTimer -= rules._waveInterval;
        // 웨이브는 가장 오른쪽 적 거점에서 나온다(없으면 반격할 곳이 없다).
        const ConquestSite* pSpawnSite = nullptr;
        for ( const ConquestSite& site : _listSite )
        {
            if ( site._owner == ConquestTeam::Enemy && ( pSpawnSite == nullptr || site._pDef->_x > pSpawnSite->_pDef->_x ) )
                pSpawnSite = &site;
        }
        if ( pSpawnSite == nullptr )
            return;
        const int32 waveSize = computeWaveSize();
        for ( int32 index = 0; index < waveSize; ++index )
        {
            // 모르는 waveUnit 이면 -1 이다. 카탈로그 로드가 이미 경고했다
            (void)spawnUnit( rules._waveUnit, ConquestTeam::Enemy, pSpawnSite->_pDef->_x + static_cast<float32>( index ) * rules._formationSpacing, ConquestOrder::Charge );
        }
        pushEvent( ConquestEvent::Kind::WaveSpawned, pSpawnSite->_pDef->_id, waveSize, ConquestTeam::Enemy );
    }

    void ConquestWorld::refreshSquadSlots()
    {
        int32 slot = 0;
        for ( ConquestUnit& unit : _listUnit )
        {
            if ( unit._squadSlot >= 0 )
                unit._squadSlot = slot++;
        }
    }

    void ConquestWorld::releaseExcessWorkers()
    {
        // 다른 거점의 건물이 잃은 거점의 일꾼을 쓰고 있었을 수 있다. 빼지 않으면 없는 일꾼이 계속 생산한다.
        int32 excess = -computeFreeWorkers();
        for ( size_t index = _listBuilding.size(); index > 0 && excess > 0; --index )
        {
            ConquestBuilding& building = _listBuilding[index - 1];
            const int32       released = MathUtil::min( building._workers, excess );
            building._workers -= released;
            excess -= released;
        }
    }

    void ConquestWorld::refreshResult()
    {
        bool bEnemyHolds  = false;
        bool bPlayerHolds = false;
        for ( const ConquestSite& site : _listSite )
        {
            bEnemyHolds  = bEnemyHolds || site._owner == ConquestTeam::Enemy;
            bPlayerHolds = bPlayerHolds || site._owner == ConquestTeam::Player;
        }
        if ( bEnemyHolds == false && _bVictory == SW_FALSE )
        {
            _bVictory = SW_TRUE;
            pushEvent( ConquestEvent::Kind::Victory, hashed_string(), 0, ConquestTeam::Player );
        }
        else if ( bPlayerHolds == false && _bDefeat == SW_FALSE )
        {
            _bDefeat = SW_TRUE;
            pushEvent( ConquestEvent::Kind::Defeat, hashed_string(), 0, ConquestTeam::Enemy );
        }
    }

    ConquestWorld::Target ConquestWorld::findNearestTarget( ConquestTeam team, float32 x, float32 maxDistance, bool bUnits, bool bStructures ) const
    {
        Target  best;
        float32 bestDistance = maxDistance;
        if ( bUnits )
        {
            for ( int32 unitIndex = 0; unitIndex < static_cast<int32>( _listUnit.size() ); ++unitIndex )
            {
                const ConquestUnit& other = _listUnit[static_cast<size_t>( unitIndex )];
                if ( other._bAlive == SW_FALSE || ConquestWorldInternal::isHostile( team, other._team ) == false )
                    continue;
                const float32 distance = MathUtil::abs( other._x - x );
                if ( distance <= bestDistance && ( best.isValid() == false || distance < bestDistance ) )
                {
                    best            = Target{};
                    best._unitIndex = unitIndex;
                    best._x         = other._x;
                    bestDistance    = distance;
                }
            }
            if ( team == ConquestTeam::Enemy && _commander._bAlive == SW_TRUE )
            {
                const float32 distance = MathUtil::abs( _commander._x - x );
                if ( distance <= bestDistance && ( best.isValid() == false || distance < bestDistance ) )
                {
                    best             = Target{};
                    best._bCommander = SW_TRUE;
                    best._x          = _commander._x;
                    bestDistance     = distance;
                }
            }
        }
        if ( bStructures )
        {
            for ( int32 siteIndex = 0; siteIndex < static_cast<int32>( _listSite.size() ); ++siteIndex )
            {
                const ConquestSite& site = _listSite[static_cast<size_t>( siteIndex )];
                if ( ConquestWorldInternal::isHostile( team, site._owner ) == false )
                    continue;
                // 성문은 누구나 노린다(사다리가 걸쳐 있으면 넘으므로 노리지 않는다). 성벽은 충차만 노리며, `bUnits` 가 거짓인 구조물 전용 탐색이다.
                const bool bGate = site._gateHealth > 0.0f && isLaddered( siteIndex, team ) == false;
                const bool bWall = site._gateHealth <= 0.0f && site._wallHealth > 0.0f && bUnits == false;
                if ( bGate == false && bWall == false )
                    continue;
                const float32 gateX    = computeGateX( site, team );
                const float32 distance = MathUtil::abs( gateX - x );
                if ( distance <= bestDistance && ( best.isValid() == false || distance < bestDistance ) )
                {
                    best            = Target{};
                    best._siteIndex = siteIndex;
                    best._x         = gateX;
                    bestDistance    = distance;
                }
            }
        }
        return best;
    }

    float32 ConquestWorld::computeGateX( const ConquestSite& site, ConquestTeam attacker ) const
    {
        return site._pDef->_x - ConquestWorldInternal::computeDirection( attacker ) * site._pDef->_captureRadius;
    }

    bool ConquestWorld::isLaddered( int32 siteIndex, ConquestTeam attacker ) const
    {
        if ( siteIndex < 0 || attacker == ConquestTeam::Neutral )
            return false;
        const float32 direction = ConquestWorldInternal::computeDirection( attacker );
        const float32 plantX    = computeGateX( _listSite[static_cast<size_t>( siteIndex )], attacker ) - direction * ConquestWorldInternal::kGateStandOff;
        for ( const ConquestUnit& unit : _listUnit )
        {
            const bool bLadderReaches = unit._bAlive == SW_TRUE && unit._team == attacker && unit._bPlanted == SW_TRUE && unit._pDef->_siegeRole == ConquestSiegeRole::Ladder && MathUtil::abs( unit._x - plantX ) <= ConquestWorldInternal::kLadderReach;
            if ( bLadderReaches )
                return true;
        }
        return false;
    }

    float32 ConquestWorld::clampMove( ConquestTeam team, float32 fromX, float32 toX ) const
    {
        float32       result    = toX;
        const float32 direction = ConquestWorldInternal::computeDirection( team );
        for ( int32 siteIndex = 0; siteIndex < static_cast<int32>( _listSite.size() ); ++siteIndex )
        {
            const ConquestSite& site             = _listSite[static_cast<size_t>( siteIndex )];
            const bool          bGateNotBlocking = ConquestWorldInternal::isHostile( team, site._owner ) == false || site._gateHealth <= 0.0f || isLaddered( siteIndex, team );
            if ( bGateNotBlocking )
                continue;
            const float32 stopX = computeGateX( site, team ) - direction * ConquestWorldInternal::kGateStandOff;
            // 성문 앞(공격하는 쪽)에 있던 것만 막는다. 이미 안쪽에 있는 것은 그대로 둔다.
            if ( ( stopX - fromX ) * direction >= -1.0e-4f && ( result - stopX ) * direction > 0.0f )
                result = stopX;
        }
        return result;
    }

    float32 ConquestWorld::computeFormationX( const ConquestUnit& unit ) const
    {
        if ( unit._squadSlot < 0 || unit._order == ConquestOrder::Hold )
            return unit._holdX;
        if ( _commander._bAlive == SW_FALSE )
            return unit._x;
        const float32 spacing = _pCatalog->getRules()._formationSpacing;
        return _commander._x - _commander._facing * static_cast<float32>( unit._squadSlot + 1 ) * spacing;
    }

    float32 ConquestWorld::computeMoraleScale( ConquestTeam team, float32 x ) const
    {
        const ConquestRules& rules          = _pCatalog->getRules();
        const bool           bNoMoraleBonus = team != ConquestTeam::Player || _commander._bAlive == SW_FALSE || MathUtil::abs( x - _commander._x ) > rules._moraleRadius;
        if ( bNoMoraleBonus )
            return 1.0f;
        return 1.0f + rules._moraleDamageBonus;
    }

    void ConquestWorld::applyAttack( ConquestTeam attacker, float32 baseDamage, float32 structureScale, const Target& target, float32& inoutDealt )
    {
        const ConquestRules& rules = _pCatalog->getRules();
        if ( target._unitIndex >= 0 )
        {
            ConquestUnit& victim = _listUnit[static_cast<size_t>( target._unitIndex )];
            float32       damage = baseDamage;
            for ( const ConquestSite& site : _listSite )
            {
                const bool bWallProtects = site._owner == victim._team && site._wallHealth > 0.0f && MathUtil::abs( victim._x - site._pDef->_x ) <= site._pDef->_captureRadius;
                if ( bWallProtects )
                {
                    damage *= 1.0f - rules._wallProtection;
                    break;
                }
            }
            const bool bDepleted = DepletingValue::take( victim._health, damage )._bDepleted == SW_TRUE;
            inoutDealt += damage;
            if ( bDepleted && victim._bAlive == SW_TRUE )
            {
                victim._bAlive = SW_FALSE;
                pushEvent( ConquestEvent::Kind::UnitDied, victim._pDef->_id, victim._unitID, victim._team );
            }
            return;
        }
        if ( target._bCommander == SW_TRUE )
        {
            const bool bDepleted = DepletingValue::take( _commander._health, baseDamage )._bDepleted == SW_TRUE;
            inoutDealt += baseDamage;
            if ( bDepleted && _commander._bAlive == SW_TRUE )
            {
                _commander._bAlive = SW_FALSE;
                _commander._respawnTimer.start( rules._commanderRespawnTime );
                pushEvent( ConquestEvent::Kind::CommanderDied, hashed_string(), 0, ConquestTeam::Player );
            }
            return;
        }
        if ( target._siteIndex < 0 )
            return;
        ConquestSite& site   = _listSite[static_cast<size_t>( target._siteIndex )];
        const float32 damage = baseDamage * structureScale;
        inoutDealt += damage;
        if ( site._gateHealth > 0.0f )
        {
            if ( DepletingValue::take( site._gateHealth, damage )._bDepleted == SW_TRUE )
            {
                pushEvent( ConquestEvent::Kind::GateBroken, site._pDef->_id, 0, attacker );
            }
            return;
        }
        if ( site._wallHealth > 0.0f )
        {
            if ( DepletingValue::take( site._wallHealth, damage )._bDepleted == SW_TRUE )
            {
                pushEvent( ConquestEvent::Kind::WallBroken, site._pDef->_id, 0, attacker );
            }
        }
    }

    optional<int32> ConquestWorld::findHomeSiteIndex() const
    {
        optional<int32> home;
        for ( int32 siteIndex = 0; siteIndex < static_cast<int32>( _listSite.size() ); ++siteIndex )
        {
            const ConquestSite& site = _listSite[static_cast<size_t>( siteIndex )];
            if ( site._owner == ConquestTeam::Player && ( home.has_value() == false || site._pDef->_x < _listSite[static_cast<size_t>( *home )]._pDef->_x ) )
                home = siteIndex;
        }
        return home;
    }

    void ConquestWorld::pushEvent( ConquestEvent::Kind kind, const hashed_string& id, int32 value, ConquestTeam team )
    {
        ConquestEvent event;
        event._kind  = kind;
        event._id    = id;
        event._value = value;
        event._team  = team;
        _eventBuffer.push( event );
    }
} // namespace sw

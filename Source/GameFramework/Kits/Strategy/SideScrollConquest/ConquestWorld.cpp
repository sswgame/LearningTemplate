#include "pch.h"

#include "GameFramework/Kits/Strategy/SideScrollConquest/ConquestWorld.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    namespace
    {
        struct ConquestWorldInternal
        {
            static constexpr float32 kFarAway                = 1.0e9f;
            static constexpr float32 kGateStandOff           = 0.5f;  ///< 성문 앞에서 멈추는 거리
            static constexpr float32 kLadderReach            = 1.0f;  ///< 사다리가 이 거리 안이면 그 성문을 넘는다
            static constexpr float32 kPlantTolerance         = 0.05f; ///< 사다리가 걸칠 자리에 이만큼 가까우면 걸친다
            static constexpr float32 kApproachRatio          = 0.9f;  ///< 사거리의 이 비율까지 다가간다(경계에서 떨지 않게)
            static constexpr float32 kCommanderStructureRate = 0.25f; ///< 지휘관이 구조물에 주는 피해 배율

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
    const utf8* toString( ConquestResult result )
    {
        switch ( result )
        {
            case ConquestResult::Ok:
                return "Ok";
            case ConquestResult::UnknownDef:
                return "UnknownDef";
            case ConquestResult::InvalidBuilding:
                return "InvalidBuilding";
            case ConquestResult::SiteNotOwned:
                return "SiteNotOwned";
            case ConquestResult::NoBuildSlot:
                return "NoBuildSlot";
            case ConquestResult::NotEnoughResources:
                return "NotEnoughResources";
            case ConquestResult::PopulationCap:
                return "PopulationCap";
            case ConquestResult::CannotTrainHere:
                return "CannotTrainHere";
            case ConquestResult::NoFreeWorkers:
                return "NoFreeWorkers";
        }
        return "?";
    }

    ConquestWorld::ConquestWorld()
        : _listSite{}
        , _listBuilding{}
        , _listUnit{}
        , _listEvent{}
        , _resource{}
        , _commander{}
        , _timer{}
        , _pCatalog{ nullptr }
        , _elapsed{ 0.0f }
        , _incomeTimer{ 0.0f }
        , _waveTimer{ 0.0f }
        , _nextUnitId{ 1 }
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
        _listEvent.clear();
        _resource.clear();
        _elapsed     = 0.0f;
        _incomeTimer = 0.0f;
        _waveTimer   = 0.0f;
        _nextUnitId  = 1;
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
                    (void)spawnUnit( garrison._unitId, def._owner, def._x, ConquestOrder::Hold );
            }
        }
        _commander         = ConquestCommander{};
        _commander._health = rules._commanderHealth;
        const int32 home   = findHomeSiteIndex();
        _commander._x      = home >= 0 ? _listSite[static_cast<size_t>( home )]._pDef->_x : 0.0f;
    }

    void ConquestWorld::update( float32 deltaTime )
    {
        if ( _pCatalog == nullptr )
            return;
        const int32 stepCount = _timer.consume( deltaTime );
        for ( int32 stepIndex = 0; stepIndex < stepCount; ++stepIndex )
            step( _timer.getStep() );
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

    ConquestResult ConquestWorld::placeBuilding( const hashed_string& buildingId, const hashed_string& siteId )
    {
        const ConquestBuildingDef* pDef = _pCatalog != nullptr ? _pCatalog->findBuilding( buildingId ) : nullptr;
        if ( pDef == nullptr )
            return ConquestResult::UnknownDef;
        int32 siteIndex = -1;
        for ( size_t index = 0; index < _listSite.size(); ++index )
        {
            if ( _listSite[index]._pDef->_id == siteId )
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
        (void)_resource.trySpend( pDef->_cost );
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

    ConquestResult ConquestWorld::trainUnit( int32 buildingIndex, const hashed_string& unitId )
    {
        if ( buildingIndex < 0 || buildingIndex >= static_cast<int32>( _listBuilding.size() ) )
            return ConquestResult::InvalidBuilding;
        ConquestBuilding&      building = _listBuilding[static_cast<size_t>( buildingIndex )];
        const ConquestUnitDef* pUnit    = _pCatalog->findUnit( unitId );
        if ( pUnit == nullptr )
            return ConquestResult::UnknownDef;
        if ( _listSite[static_cast<size_t>( building._siteIndex )]._owner != ConquestTeam::Player )
            return ConquestResult::SiteNotOwned;
        bool bTrainable = false;
        for ( const hashed_string& trainable : building._pDef->_listTrainable )
            bTrainable = bTrainable || trainable == unitId;
        if ( bTrainable == false )
            return ConquestResult::CannotTrainHere;
        if ( computePopulation() + pUnit->_population > computePopulationCap() )
            return ConquestResult::PopulationCap;
        if ( _resource.canAfford( pUnit->_cost ) == false )
            return ConquestResult::NotEnoughResources;
        (void)_resource.trySpend( pUnit->_cost );
        building._listQueue.push_back( unitId );
        return ConquestResult::Ok;
    }

    int32 ConquestWorld::spawnUnit( const hashed_string& unitId, ConquestTeam team, float32 x, ConquestOrder order )
    {
        const ConquestUnitDef* pDef = _pCatalog != nullptr ? _pCatalog->findUnit( unitId ) : nullptr;
        if ( pDef == nullptr || team == ConquestTeam::Neutral )
            return -1;
        ConquestUnit unit;
        unit._pDef      = pDef;
        unit._x         = x;
        unit._holdX     = x;
        unit._health    = pDef->_health;
        unit._unitId    = _nextUnitId++;
        unit._team      = team;
        unit._order     = order;
        unit._squadSlot = team == ConquestTeam::Player ? getSquadSize() : -1;
        _listUnit.push_back( unit );
        return unit._unitId;
    }

    void ConquestWorld::drainEvents( vector<ConquestEvent>& outListEvent )
    {
        outListEvent.insert( outListEvent.end(), _listEvent.begin(), _listEvent.end() );
        _listEvent.clear();
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
            total -= building._workers;
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

    const ConquestSite* ConquestWorld::findSite( const hashed_string& siteId ) const
    {
        for ( const ConquestSite& site : _listSite )
        {
            if ( site._pDef->_id == siteId )
                return &site;
        }
        return nullptr;
    }

    const ConquestUnit* ConquestWorld::findUnit( int32 unitId ) const
    {
        for ( const ConquestUnit& unit : _listUnit )
        {
            if ( unit._unitId == unitId )
                return &unit;
        }
        return nullptr;
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
            stepUnit( unitIndex, deltaTime );
        _listUnit.erase( std::remove_if( _listUnit.begin(), _listUnit.end(), []( const ConquestUnit& unit )
        { return unit._bAlive == SW_FALSE; } ),
                         _listUnit.end() );
        refreshSquadSlots();
        stepCapture( deltaTime );
        stepEconomy( deltaTime );
        stepWaves( deltaTime );
        refreshOutcome();
    }

    void ConquestWorld::stepCommander( float32 deltaTime )
    {
        const ConquestRules& rules = _pCatalog->getRules();
        if ( _commander._bAlive == SW_FALSE )
        {
            _commander._respawnTimer.tick( deltaTime );
            const int32 home = findHomeSiteIndex();
            if ( _commander._respawnTimer.isActive() || home < 0 )
                return;
            _commander._bAlive = SW_TRUE;
            _commander._health = rules._commanderHealth;
            _commander._x      = _listSite[static_cast<size_t>( home )]._pDef->_x;
            _commander._attackCooldown.clear();
            pushEvent( ConquestEvent::Kind::CommanderRespawned, hashed_string(), 0, ConquestTeam::Player );
            return;
        }
        if ( _commander._moveAxis != 0.0f )
        {
            _commander._facing = _commander._moveAxis > 0.0f ? 1.0f : -1.0f;
            _commander._x      = clampMove( ConquestTeam::Player, _commander._x, _commander._x + _commander._moveAxis * rules._commanderSpeed * deltaTime );
        }
        _commander._attackCooldown.tick( deltaTime );
        if ( _commander._attackCooldown.isActive() )
            return;
        const Target target = findNearestTarget( ConquestTeam::Player, _commander._x, rules._commanderRange, true, true );
        if ( target.isValid() == false )
            return;
        float32 dealt = 0.0f;
        applyAttack( ConquestTeam::Player, rules._commanderDamage, ConquestWorldInternal::kCommanderStructureRate, target, dealt );
        _commander._attackCooldown.start( rules._commanderAttackInterval );
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
        unit._attackCooldown.tick( deltaTime );
        const auto moveToward = [&]( float32 goalX )
        {
            const float32 delta = MathUtil::clamp( goalX - unit._x, -stepSize, stepSize );
            unit._x             = clampMove( unit._team, unit._x, unit._x + delta );
        };

        if ( def._siegeRole == ConquestSiegeRole::Ladder )
        {
            // 사다리: 돌격이면(적은 늘) 앞쪽의 가장 가까운 성문에 가서 걸친다. 싸우지 않는다.
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
            const float32 distance = MathUtil::abs( target._x - unit._x );
            if ( unit._order == ConquestOrder::Follow && unit._squadSlot >= 0 && _commander._bAlive == SW_TRUE && MathUtil::abs( target._x - _commander._x ) > rules._followLeash )
                target = Target{};
            else if ( unit._order == ConquestOrder::Hold && distance > def._range )
                target = Target{};
        }
        if ( target.isValid() == false )
        {
            if ( unit._order == ConquestOrder::Charge )
            {
                // 싸울 상대가 없으면 앞쪽의 가장 가까운 남의 거점으로 간다(점령하러). 그마저 없으면 그 자리.
                float32 goalX = unit._x;
                float32 best  = ConquestWorldInternal::kFarAway;
                for ( const ConquestSite& site : _listSite )
                {
                    const float32 ahead = ( site._pDef->_x - unit._x ) * direction;
                    if ( site._owner != unit._team && ahead >= -site._pDef->_captureRadius && MathUtil::abs( ahead ) < best )
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
        unit._attackCooldown.start( def._attackInterval );
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
            const bool bBarred = site._gateHealth > 0.0f && site._owner != ConquestTeam::Neutral && isLaddered( siteIndex, contender ) == false;
            if ( contender == ConquestTeam::Neutral || contender == site._owner || bBarred )
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
                // 잃은 거점의 건물은 멈춘다 — 일꾼은 풀려나고 대기열은 사라진다.
                for ( ConquestBuilding& building : _listBuilding )
                {
                    if ( building._siteIndex != siteIndex )
                        continue;
                    building._workers = 0;
                    building._listQueue.clear();
                    building._trainProgress = 0.0f;
                }
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
                    const hashed_string unitId = building._listQueue.front();
                    building._listQueue.erase( building._listQueue.begin() );
                    const int32 spawnedId = spawnUnit( unitId, ConquestTeam::Player, site._pDef->_x, ConquestOrder::Follow );
                    pushEvent( ConquestEvent::Kind::UnitTrained, unitId, spawnedId, ConquestTeam::Player );
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
            (void)spawnUnit( rules._waveUnit, ConquestTeam::Enemy, pSpawnSite->_pDef->_x + static_cast<float32>( index ) * rules._formationSpacing, ConquestOrder::Charge );
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

    void ConquestWorld::refreshOutcome()
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
                // 성문은 누구나(사다리가 걸쳐 있으면 넘으므로 노리지 않는다), 성벽은 충차만(`bUnits` 가 거짓인 구조물 전용 탐색).
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
            if ( unit._bAlive == SW_TRUE && unit._team == attacker && unit._bPlanted == SW_TRUE && unit._pDef->_siegeRole == ConquestSiegeRole::Ladder && MathUtil::abs( unit._x - plantX ) <= ConquestWorldInternal::kLadderReach )
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
            const ConquestSite& site = _listSite[static_cast<size_t>( siteIndex )];
            if ( ConquestWorldInternal::isHostile( team, site._owner ) == false || site._gateHealth <= 0.0f || isLaddered( siteIndex, team ) )
                continue;
            const float32 stopX = computeGateX( site, team ) - direction * ConquestWorldInternal::kGateStandOff;
            // 성문 앞(공격하는 쪽)에 있던 것만 막는다 — 이미 안쪽에 있는 것은 그대로 둔다.
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
        const ConquestRules& rules = _pCatalog->getRules();
        if ( team != ConquestTeam::Player || _commander._bAlive == SW_FALSE || MathUtil::abs( x - _commander._x ) > rules._moraleRadius )
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
                if ( site._owner == victim._team && site._wallHealth > 0.0f && MathUtil::abs( victim._x - site._pDef->_x ) <= site._pDef->_captureRadius )
                {
                    damage *= 1.0f - rules._wallProtection;
                    break;
                }
            }
            victim._health -= damage;
            inoutDealt += damage;
            if ( victim._health <= 0.0f && victim._bAlive == SW_TRUE )
            {
                victim._bAlive = SW_FALSE;
                pushEvent( ConquestEvent::Kind::UnitDied, victim._pDef->_id, victim._unitId, victim._team );
            }
            return;
        }
        if ( target._bCommander == SW_TRUE )
        {
            _commander._health -= baseDamage;
            inoutDealt += baseDamage;
            if ( _commander._health <= 0.0f && _commander._bAlive == SW_TRUE )
            {
                _commander._bAlive = SW_FALSE;
                _commander._health = 0.0f;
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
            site._gateHealth -= damage;
            if ( site._gateHealth <= 0.0f )
            {
                site._gateHealth = 0.0f;
                pushEvent( ConquestEvent::Kind::GateBroken, site._pDef->_id, 0, attacker );
            }
            return;
        }
        if ( site._wallHealth > 0.0f )
        {
            site._wallHealth -= damage;
            if ( site._wallHealth <= 0.0f )
            {
                site._wallHealth = 0.0f;
                pushEvent( ConquestEvent::Kind::WallBroken, site._pDef->_id, 0, attacker );
            }
        }
    }

    int32 ConquestWorld::findHomeSiteIndex() const
    {
        int32 home = -1;
        for ( int32 siteIndex = 0; siteIndex < static_cast<int32>( _listSite.size() ); ++siteIndex )
        {
            const ConquestSite& site = _listSite[static_cast<size_t>( siteIndex )];
            if ( site._owner == ConquestTeam::Player && ( home < 0 || site._pDef->_x < _listSite[static_cast<size_t>( home )]._pDef->_x ) )
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
        _listEvent.push_back( event );
    }
} // namespace sw

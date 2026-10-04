#include "pch.h"

#include "GameFramework/Kits/Strategy/RealTimeStrategy/RtsAiController.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"

namespace sw
{
    namespace
    {
        struct RtsAiControllerInternal
        {
            static const hashed_string& getThreatKey()
            {
                static const hashed_string kThreatKey( "Threat" );
                return kThreatKey;
            }

            static float32 computeFlatDistance( const float3& lhs, const float3& rhs )
            {
                const float32 dx = lhs._x - rhs._x;
                const float32 dz = lhs._z - rhs._z;
                return MathUtil::sqrt( dx * dx + dz * dz );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    RtsAiController::RtsAiController()
        : _tree{}
        , _runner{}
        , _blackboard{}
        , _settings{}
        , _pWorld{ nullptr }
        , _thinkTimer{}
        , _player{ -1 }
        , _attackWaveCount{ 0 }
    {
    }

    void RtsAiController::initialize( RtsWorld* pWorld, int32 player, const RtsAiSettings& settings )
    {
        _pWorld   = pWorld;
        _player   = player;
        _settings = settings;
        _thinkTimer.clear();
        _attackWaveCount = 0;
        _blackboard.clear();
        makeTree();
        _runner.initialize( &_tree );
    }

    void RtsAiController::makeTree()
    {
        _tree              = BehaviorTree{};
        const int32 root   = _tree.addSelector( -1, "Root", true );
        const int32 threat = _tree.addBlackboardCondition( root, RtsAiControllerInternal::getThreatKey(), BlackboardCompare::IsSet, 0.0f, BehaviorAbortMode::LowerPriority );
        _tree.addAction( threat, "Defend", &RtsAiController::taskDefend );
        const int32 economy = _tree.addSequence( root, "Economy" );
        _tree.addAction( _tree.addForceSuccess( economy ), "GatherIdle", &RtsAiController::taskGatherIdle );
        _tree.addAction( _tree.addForceSuccess( economy ), "TrainWorkers", &RtsAiController::taskTrainWorkers );
        _tree.addAction( _tree.addForceSuccess( economy ), "BuildSupply", &RtsAiController::taskBuildSupply );
        _tree.addAction( _tree.addForceSuccess( economy ), "BuildProduction", &RtsAiController::taskBuildProduction );
        _tree.addAction( _tree.addForceSuccess( economy ), "TrainArmy", &RtsAiController::taskTrainArmy );
        _tree.addAction( _tree.addForceSuccess( economy ), "Attack", &RtsAiController::taskAttack );
    }

    void RtsAiController::update( float32 deltaTime )
    {
        if ( _pWorld == nullptr )
            return;
        const RtsPlayer* pPlayer = _pWorld->findPlayer( _player );
        if ( pPlayer == nullptr || pPlayer->_bDefeated )
            return;
        _thinkTimer.tick( deltaTime );
        if ( _thinkTimer.isActive() )
            return;
        _thinkTimer.start( _settings._thinkInterval );
        _runner.tick( _blackboard, this, _settings._thinkInterval );
    }

    void RtsAiController::notify( const RtsEvent& event )
    {
        if ( event._kind != RtsEvent::Kind::UnderAttack || event._player != _player )
            return;
        const RtsUnit* pDepot = _pWorld->findUnit( findDepot() );
        if ( pDepot != nullptr && RtsAiControllerInternal::computeFlatDistance( pDepot->_position, event._position ) > _settings._defendRadius )
            return;
        _blackboard.setVector( RtsAiControllerInternal::getThreatKey(), event._position );
    }

    void RtsAiController::writeState( Archive& outArchive ) const
    {
        outArchive << _thinkTimer._remaining;
        outArchive << _attackWaveCount;
    }

    bool RtsAiController::readState( Archive& archive )
    {
        float32 thinkTimer      = 0.0f;
        int32   attackWaveCount = 0;
        archive >> thinkTimer;
        archive >> attackWaveCount;
        if ( archive.isError() || attackWaveCount < 0 )
            return false;
        _thinkTimer._remaining = thinkTimer;
        _attackWaveCount       = attackWaveCount;
        return true;
    }

    RtsUnitId RtsAiController::findDepot() const
    {
        RtsUnitId depotId{};
        _pWorld->forEachUnit( [&]( const RtsUnit& unit )
        {
            if ( depotId.isValid() == false && unit._owner == _player && unit._pDef->_id == _settings._depotId && unit.isConstructed() )
                depotId = unit._id;
        } );
        return depotId;
    }

    void RtsAiController::collectArmy( vector<RtsUnitId>& outListUnit ) const
    {
        outListUnit.clear();
        _pWorld->forEachUnit( [&]( const RtsUnit& unit )
        {
            if ( unit._owner == _player && unit.isMobile() && unit._pDef->_bWorker == SW_FALSE && unit._pDef->canAttack() )
                outListUnit.push_back( unit._id );
        } );
    }

    bool RtsAiController::orderConstruction( const hashed_string& buildingId )
    {
        const RtsUnit* pDepot = _pWorld->findUnit( findDepot() );
        if ( pDepot == nullptr )
            return false;
        int2 cell{};
        if ( _pWorld->findBuildSite( buildingId, pDepot->_position, 3, 12, cell ) == false )
            return false;
        // 짐 없이 노는 일꾼 → 광물로 가는 일꾼 순으로.
        RtsUnitId workerId{};
        int32     bestScore = 3;
        _pWorld->forEachUnit( [&]( const RtsUnit& unit )
        {
            if ( unit._owner != _player || unit._pDef->_id != _settings._workerId )
                return;
            const RtsOrder* pOrder = unit.findOrder();
            int32           score  = 3;
            if ( pOrder == nullptr )
                score = 0;
            else if ( pOrder->_type == RtsOrderType::Gather && unit._cargoAmount == 0 && unit._gatherPhase != RtsGatherPhase::Harvesting )
                score = 1;
            else if ( pOrder->_type == RtsOrderType::Gather && unit._cargoAmount == 0 )
                score = 2;
            if ( score < bestScore )
            {
                bestScore = score;
                workerId  = unit._id;
            }
        } );
        if ( workerId.isValid() == false || _pWorld->issueBuild( workerId, buildingId, cell ) != RtsCommandResult::Ok )
            return false;
        // 다 지으면 다시 캐러 간다.
        const RtsUnitId mineralId = _pWorld->findNearestResource( pDepot->_position, RtsResourceType::Minerals, 16.0f );
        if ( mineralId.isValid() )
            (void)_pWorld->issueGather( workerId, mineralId, true );
        return true;
    }

    // ------------------------------------------------------------------------------
    // 작업
    // ------------------------------------------------------------------------------
    BehaviorStatus RtsAiController::taskDefend( BehaviorContext& context )
    {
        RtsAiController&  self   = *static_cast<RtsAiController*>( context._pOwner );
        const float3      threat = context._pBlackboard->getVector( RtsAiControllerInternal::getThreatKey() );
        vector<RtsUnitId> listArmy;
        self.collectArmy( listArmy );
        if ( listArmy.empty() == false )
            (void)self._pWorld->issueGroupMove( listArmy, threat, true );
        context._pBlackboard->clearValue( RtsAiControllerInternal::getThreatKey() );
        return BehaviorStatus::Success;
    }

    BehaviorStatus RtsAiController::taskGatherIdle( BehaviorContext& context )
    {
        RtsAiController& self   = *static_cast<RtsAiController*>( context._pOwner );
        RtsWorld&        world  = *self._pWorld;
        const RtsUnit*   pDepot = world.findUnit( self.findDepot() );
        if ( pDepot == nullptr )
            return BehaviorStatus::Failure;
        vector<RtsUnitId> listIdle;
        world.forEachUnit( [&]( const RtsUnit& unit )
        {
            if ( unit._owner == self._player && unit._pDef->_bWorker && unit.isIdle() )
                listIdle.push_back( unit._id );
        } );
        for ( const RtsUnitId workerId : listIdle )
        {
            const RtsUnitId mineralId = world.findNearestResource( pDepot->_position, RtsResourceType::Minerals, 16.0f );
            if ( mineralId.isValid() == false )
                return BehaviorStatus::Failure;
            (void)world.issueGather( workerId, mineralId );
        }
        return BehaviorStatus::Success;
    }

    BehaviorStatus RtsAiController::taskTrainWorkers( BehaviorContext& context )
    {
        RtsAiController& self   = *static_cast<RtsAiController*>( context._pOwner );
        RtsWorld&        world  = *self._pWorld;
        const RtsUnit*   pDepot = world.findUnit( self.findDepot() );
        if ( pDepot == nullptr || pDepot->_listProduction.empty() == false || world.countPlanned( self._player, self._settings._workerId ) >= self._settings._workerTarget )
            return BehaviorStatus::Failure;
        return world.train( pDepot->_id, self._settings._workerId ) == RtsCommandResult::Ok ? BehaviorStatus::Success : BehaviorStatus::Failure;
    }

    BehaviorStatus RtsAiController::taskBuildSupply( BehaviorContext& context )
    {
        RtsAiController& self    = *static_cast<RtsAiController*>( context._pOwner );
        RtsWorld&        world   = *self._pWorld;
        const RtsPlayer* pPlayer = world.findPlayer( self._player );
        if ( pPlayer->_supplyCap >= world.getCatalog()->getSupplyMax() || pPlayer->_supplyCap - pPlayer->_supplyUsed > self._settings._supplyMargin )
            return BehaviorStatus::Failure;
        // 짓는 중인(또는 지으러 가는) 보급 건물이 있으면 기다린다.
        if ( world.countPlanned( self._player, self._settings._supplyId ) > world.countUnits( self._player, self._settings._supplyId, false ) )
            return BehaviorStatus::Failure;
        return self.orderConstruction( self._settings._supplyId ) ? BehaviorStatus::Success : BehaviorStatus::Failure;
    }

    BehaviorStatus RtsAiController::taskBuildProduction( BehaviorContext& context )
    {
        RtsAiController& self  = *static_cast<RtsAiController*>( context._pOwner );
        RtsWorld&        world = *self._pWorld;
        if ( world.countPlanned( self._player, self._settings._productionId ) >= self._settings._productionTarget )
            return BehaviorStatus::Failure;
        // 일꾼을 어느 정도 모은 뒤에.
        if ( world.countUnits( self._player, self._settings._workerId, false ) < self._settings._workerTarget / 2 )
            return BehaviorStatus::Failure;
        return self.orderConstruction( self._settings._productionId ) ? BehaviorStatus::Success : BehaviorStatus::Failure;
    }

    BehaviorStatus RtsAiController::taskTrainArmy( BehaviorContext& context )
    {
        RtsAiController&  self  = *static_cast<RtsAiController*>( context._pOwner );
        RtsWorld&         world = *self._pWorld;
        vector<RtsUnitId> listIdleProducer;
        world.forEachUnit( [&]( const RtsUnit& unit )
        {
            if ( unit._owner == self._player && unit._pDef->_id == self._settings._productionId && unit.isConstructed() && unit._listProduction.empty() )
                listIdleProducer.push_back( unit._id );
        } );
        bool bTrained = false;
        for ( const RtsUnitId producerId : listIdleProducer )
            bTrained = world.train( producerId, self._settings._armyUnitId ) == RtsCommandResult::Ok || bTrained;
        return bTrained ? BehaviorStatus::Success : BehaviorStatus::Failure;
    }

    BehaviorStatus RtsAiController::taskAttack( BehaviorContext& context )
    {
        RtsAiController&  self  = *static_cast<RtsAiController*>( context._pOwner );
        RtsWorld&         world = *self._pWorld;
        vector<RtsUnitId> listArmy;
        self.collectArmy( listArmy );
        vector<RtsUnitId> listIdle;
        for ( const RtsUnitId unitId : listArmy )
        {
            const RtsUnit* pUnit = world.findUnit( unitId );
            if ( pUnit != nullptr && pUnit->isIdle() && pUnit->_attackTarget.isValid() == false )
                listIdle.push_back( unitId );
        }
        if ( static_cast<int32>( listIdle.size() ) < self._settings._attackArmySize )
            return BehaviorStatus::Failure;
        // 가장 가까운 적 플레이어의 시작 지점으로.
        const RtsPlayer* pSelf        = world.findPlayer( self._player );
        float32          bestDistance = MathUtil::MaxFloat;
        float3           target{};
        bool             bFound = false;
        for ( int32 player = 0; player < world.getPlayerCount(); ++player )
        {
            const RtsPlayer* pOther = world.findPlayer( player );
            if ( pOther->_bDefeated || world.areEnemies( self._player, player ) == false )
                continue;
            const float32 distance = RtsAiControllerInternal::computeFlatDistance( pSelf->_startPosition, pOther->_startPosition );
            if ( distance < bestDistance )
            {
                bestDistance = distance;
                target       = pOther->_startPosition;
                bFound       = true;
            }
        }
        if ( bFound == false )
            return BehaviorStatus::Failure;
        (void)world.issueGroupMove( listIdle, target, true );
        ++self._attackWaveCount;
        return BehaviorStatus::Success;
    }
} // namespace sw

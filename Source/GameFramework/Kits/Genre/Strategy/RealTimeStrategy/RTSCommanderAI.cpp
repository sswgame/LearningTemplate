#include "pch.h"

#include "GameFramework/Kits/Genre/Strategy/RealTimeStrategy/RTSCommanderAI.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"

namespace sw
{
    namespace
    {
        struct RTSCommanderAIInternal
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
    RTSCommanderAI::RTSCommanderAI()
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

    void RTSCommanderAI::initialize( RTSWorld* pWorld, int32 player, const RTSCommanderAISettings& settings )
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

    void RTSCommanderAI::makeTree()
    {
        _tree              = BehaviorTree{};
        const int32 root   = _tree.addSelector( -1, "Root", true );
        const int32 threat = _tree.addBlackboardCondition( root, RTSCommanderAIInternal::getThreatKey(), BlackboardCompare::IsSet, 0.0f, BehaviorAbortMode::LowerPriority );
        _tree.addAction( threat, "Defend", &RTSCommanderAI::taskDefend );
        const int32 economy = _tree.addSequence( root, "Economy" );
        _tree.addAction( _tree.addForceSuccess( economy ), "GatherIdle", &RTSCommanderAI::taskGatherIdle );
        _tree.addAction( _tree.addForceSuccess( economy ), "TrainWorkers", &RTSCommanderAI::taskTrainWorkers );
        _tree.addAction( _tree.addForceSuccess( economy ), "BuildSupply", &RTSCommanderAI::taskBuildSupply );
        _tree.addAction( _tree.addForceSuccess( economy ), "BuildProduction", &RTSCommanderAI::taskBuildProduction );
        _tree.addAction( _tree.addForceSuccess( economy ), "TrainArmy", &RTSCommanderAI::taskTrainArmy );
        _tree.addAction( _tree.addForceSuccess( economy ), "Attack", &RTSCommanderAI::taskAttack );
    }

    void RTSCommanderAI::update( float32 deltaTime )
    {
        if ( _pWorld == nullptr )
            return;
        const RTSPlayer* pPlayer = _pWorld->findPlayer( _player );
        if ( pPlayer == nullptr || pPlayer->_bDefeated )
            return;
        _thinkTimer.tick( deltaTime );
        if ( _thinkTimer.isActive() )
            return;
        _thinkTimer.start( _settings._thinkInterval );
        _runner.tick( _blackboard, this, _settings._thinkInterval );
    }

    void RTSCommanderAI::notify( const RTSEvent& event )
    {
        if ( event._kind != RTSEvent::Kind::UnderAttack || event._player != _player )
            return;
        const RTSUnit* pDepot = _pWorld->findUnit( findDepot() );
        if ( pDepot != nullptr && RTSCommanderAIInternal::computeFlatDistance( pDepot->_position, event._position ) > _settings._defendRadius )
            return;
        _blackboard.setVector( RTSCommanderAIInternal::getThreatKey(), event._position );
    }

    void RTSCommanderAI::writeState( Archive& outArchive ) const
    {
        outArchive << _thinkTimer._remaining;
        outArchive << _attackWaveCount;
    }

    bool RTSCommanderAI::readState( Archive& archive )
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

    RTSUnitID RTSCommanderAI::findDepot() const
    {
        RTSUnitID depotID{};
        _pWorld->forEachUnit( [&]( const RTSUnit& unit )
        {
            if ( depotID.isValid() == false && unit._owner == _player && unit._pDef->_id == _settings._depotID && unit.isConstructed() )
                depotID = unit._id;
        } );
        return depotID;
    }

    void RTSCommanderAI::collectArmy( vector<RTSUnitID>& outListUnit ) const
    {
        outListUnit.clear();
        _pWorld->forEachUnit( [&]( const RTSUnit& unit )
        {
            if ( unit._owner == _player && unit.isMobile() && unit._pDef->_bWorker == SW_FALSE && unit._pDef->canAttack() )
                outListUnit.push_back( unit._id );
        } );
    }

    bool RTSCommanderAI::orderConstruction( const hashed_string& buildingID )
    {
        const RTSUnit* pDepot = _pWorld->findUnit( findDepot() );
        if ( pDepot == nullptr )
            return false;
        int2 cell{};
        if ( _pWorld->findBuildSite( buildingID, pDepot->_position, 3, 12, cell ) == false )
            return false;
        // 짐 없이 노는 일꾼 → 광물로 가는 일꾼 순으로.
        RTSUnitID workerID{};
        int32     bestScore = 3;
        _pWorld->forEachUnit( [&]( const RTSUnit& unit )
        {
            if ( unit._owner != _player || unit._pDef->_id != _settings._workerID )
                return;
            const RTSOrder* pOrder = unit.findOrder();
            int32           score  = 3;
            if ( pOrder == nullptr )
                score = 0;
            else if ( pOrder->_type == RTSOrderType::Gather && unit._cargoAmount == 0 && unit._gatherPhase != RTSGatherPhase::Harvesting )
                score = 1;
            else if ( pOrder->_type == RTSOrderType::Gather && unit._cargoAmount == 0 )
                score = 2;
            if ( score < bestScore )
            {
                bestScore = score;
                workerID  = unit._id;
            }
        } );
        if ( workerID.isValid() == false || _pWorld->issueBuild( workerID, buildingID, cell ) != RTSCommandResult::Ok )
            return false;
        // 다 지으면 다시 캐러 간다.
        const RTSUnitID mineralID = _pWorld->findNearestResource( pDepot->_position, RTSResourceType::Minerals, 16.0f );
        if ( mineralID.isValid() )
            (void)_pWorld->issueGather( workerID, mineralID, true );
        return true;
    }

    // ------------------------------------------------------------------------------
    // 작업
    // ------------------------------------------------------------------------------
    BehaviorStatus RTSCommanderAI::taskDefend( BehaviorContext& context )
    {
        RTSCommanderAI&   self   = *static_cast<RTSCommanderAI*>( context._pOwner );
        const float3      threat = context._pBlackboard->getVector( RTSCommanderAIInternal::getThreatKey() );
        vector<RTSUnitID> listArmy;
        self.collectArmy( listArmy );
        if ( listArmy.empty() == false )
            (void)self._pWorld->issueGroupMove( listArmy, threat, true );
        context._pBlackboard->clearValue( RTSCommanderAIInternal::getThreatKey() );
        return BehaviorStatus::Success;
    }

    BehaviorStatus RTSCommanderAI::taskGatherIdle( BehaviorContext& context )
    {
        RTSCommanderAI& self   = *static_cast<RTSCommanderAI*>( context._pOwner );
        RTSWorld&       world  = *self._pWorld;
        const RTSUnit*  pDepot = world.findUnit( self.findDepot() );
        if ( pDepot == nullptr )
            return BehaviorStatus::Failure;
        vector<RTSUnitID> listIdle;
        world.forEachUnit( [&]( const RTSUnit& unit )
        {
            if ( unit._owner == self._player && unit._pDef->_bWorker && unit.isIdle() )
                listIdle.push_back( unit._id );
        } );
        for ( const RTSUnitID workerID : listIdle )
        {
            const RTSUnitID mineralID = world.findNearestResource( pDepot->_position, RTSResourceType::Minerals, 16.0f );
            if ( mineralID.isValid() == false )
                return BehaviorStatus::Failure;
            (void)world.issueGather( workerID, mineralID );
        }
        return BehaviorStatus::Success;
    }

    BehaviorStatus RTSCommanderAI::taskTrainWorkers( BehaviorContext& context )
    {
        RTSCommanderAI& self   = *static_cast<RTSCommanderAI*>( context._pOwner );
        RTSWorld&       world  = *self._pWorld;
        const RTSUnit*  pDepot = world.findUnit( self.findDepot() );
        if ( pDepot == nullptr || pDepot->_listProduction.empty() == false || world.countPlanned( self._player, self._settings._workerID ) >= self._settings._workerTarget )
            return BehaviorStatus::Failure;
        return world.train( pDepot->_id, self._settings._workerID ) == RTSCommandResult::Ok ? BehaviorStatus::Success : BehaviorStatus::Failure;
    }

    BehaviorStatus RTSCommanderAI::taskBuildSupply( BehaviorContext& context )
    {
        RTSCommanderAI&  self    = *static_cast<RTSCommanderAI*>( context._pOwner );
        RTSWorld&        world   = *self._pWorld;
        const RTSPlayer* pPlayer = world.findPlayer( self._player );
        if ( pPlayer->_supplyCap >= world.getCatalog()->getSupplyMax() || pPlayer->_supplyCap - pPlayer->_supplyUsed > self._settings._supplyMargin )
            return BehaviorStatus::Failure;
        // 짓는 중인(또는 지으러 가는) 보급 건물이 있으면 기다린다.
        if ( world.countPlanned( self._player, self._settings._supplyID ) > world.countUnits( self._player, self._settings._supplyID, false ) )
            return BehaviorStatus::Failure;
        return self.orderConstruction( self._settings._supplyID ) ? BehaviorStatus::Success : BehaviorStatus::Failure;
    }

    BehaviorStatus RTSCommanderAI::taskBuildProduction( BehaviorContext& context )
    {
        RTSCommanderAI& self  = *static_cast<RTSCommanderAI*>( context._pOwner );
        RTSWorld&       world = *self._pWorld;
        if ( world.countPlanned( self._player, self._settings._productionID ) >= self._settings._productionTarget )
            return BehaviorStatus::Failure;
        // 일꾼을 어느 정도 모은 뒤에.
        if ( world.countUnits( self._player, self._settings._workerID, false ) < self._settings._workerTarget / 2 )
            return BehaviorStatus::Failure;
        return self.orderConstruction( self._settings._productionID ) ? BehaviorStatus::Success : BehaviorStatus::Failure;
    }

    BehaviorStatus RTSCommanderAI::taskTrainArmy( BehaviorContext& context )
    {
        RTSCommanderAI&   self  = *static_cast<RTSCommanderAI*>( context._pOwner );
        RTSWorld&         world = *self._pWorld;
        vector<RTSUnitID> listIdleProducer;
        world.forEachUnit( [&]( const RTSUnit& unit )
        {
            if ( unit._owner == self._player && unit._pDef->_id == self._settings._productionID && unit.isConstructed() && unit._listProduction.empty() )
                listIdleProducer.push_back( unit._id );
        } );
        bool bTrained = false;
        for ( const RTSUnitID producerID : listIdleProducer )
        {
            bTrained = world.train( producerID, self._settings._armyUnitID ) == RTSCommandResult::Ok || bTrained;
        }
        return bTrained ? BehaviorStatus::Success : BehaviorStatus::Failure;
    }

    BehaviorStatus RTSCommanderAI::taskAttack( BehaviorContext& context )
    {
        RTSCommanderAI&   self  = *static_cast<RTSCommanderAI*>( context._pOwner );
        RTSWorld&         world = *self._pWorld;
        vector<RTSUnitID> listArmy;
        self.collectArmy( listArmy );
        vector<RTSUnitID> listIdle;
        for ( const RTSUnitID unitID : listArmy )
        {
            const RTSUnit* pUnit = world.findUnit( unitID );
            if ( pUnit != nullptr && pUnit->isIdle() && pUnit->_attackTarget.isValid() == false )
                listIdle.push_back( unitID );
        }
        if ( static_cast<int32>( listIdle.size() ) < self._settings._attackArmySize )
            return BehaviorStatus::Failure;
        // 가장 가까운 적 플레이어의 시작 지점으로.
        const RTSPlayer* pSelf        = world.findPlayer( self._player );
        float32          bestDistance = MathUtil::kMaxFloat;
        float3           target{};
        bool             bFound = false;
        for ( int32 player = 0; player < world.getPlayerCount(); ++player )
        {
            const RTSPlayer* pOther = world.findPlayer( player );
            if ( pOther->_bDefeated || world.areEnemies( self._player, player ) == false )
                continue;
            const float32 distance = RTSCommanderAIInternal::computeFlatDistance( pSelf->_startPosition, pOther->_startPosition );
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

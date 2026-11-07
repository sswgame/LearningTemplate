#include "pch.h"

#include "GameFramework/Kits/Genre/Strategy/RealTimeStrategy/RTSCommanderAI.h"

#include "Core/Foundation/Constants.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"

namespace sw
{
    namespace
    {
        const hashed_string& getThreatKey()
        {
            static const hashed_string kThreatKey( "Threat" );
            return kThreatKey;
        }
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
        , _player{ constant::invalid_index::kInt32 }
        , _attackWaveCount{ 0 }
    {
    }

    void RTSCommanderAI::initialize( RTSWorld* pWorld, int32 player, const RTSCommanderAISettings& settings, const BehaviorTree& tree )
    {
        SW_ASSERT( tree.isValid() && "the commander behavior tree must be read from data (BehaviorTreeAsset) before the AI starts" );
        _pWorld   = pWorld;
        _player   = player;
        _settings = settings;
        _thinkTimer.clear();
        _attackWaveCount = 0;
        _blackboard.clear();
        _tree = tree; // 러너는 이 객체의 트리를 가리킨다 - 데이터 객체가 먼저 사라져도 된다
        _runner.initialize( &_tree );
    }

    void RTSCommanderAI::update( float32 deltaTime )
    {
        if ( _pWorld == nullptr )
            return;
        const RTSPlayer* pPlayer = _pWorld->findPlayer( _player );
        if ( pPlayer == nullptr || pPlayer->_bDefeated )
            return;
        if ( _thinkTimer.updateRepeat( deltaTime, _settings._thinkInterval, true ) ) // 늦음을 다음 간격으로 넘겨 생각 빈도가 프레임률에 매이지 않는다
            _runner.tick( _blackboard, this, _settings._thinkInterval );
    }

    void RTSCommanderAI::notify( const RTSEvent& event )
    {
        if ( event._kind != RTSEvent::Kind::UnderAttack || event._player != _player )
            return;
        const RTSUnit* pDepot = _pWorld->findUnit( findDepot() );
        if ( pDepot != nullptr && float3::getHorizontalDistance( pDepot->_position, event._position ) > _settings._defendRadius )
            return;
        _blackboard.setVector( getThreatKey(), event._position );
    }

    void RTSCommanderAI::serializeState( StateIO& io )
    {
        io.value( _thinkTimer ); // 무한대 타이머는 AI 를 영영 세우는데, 아카이브의 실수 읽기가 유한만 받는다
        io.value( _attackWaveCount );
        io.check( 0 <= _attackWaveCount );
    }

    RTSUnitID RTSCommanderAI::findDepot() const
    {
        RTSUnitID depotID{};
        _pWorld->forEachUnit( [&]( const RTSUnit& unit )
        {
            const bool bDepotCandidate = depotID.isValid() == false && unit._owner == _player && unit._pDef->_id == _settings._depotID && unit.isConstructed();
            if ( bDepotCandidate )
                depotID = unit._id;
        } );
        return depotID;
    }

    void RTSCommanderAI::collectArmy( vector<RTSUnitID>& outListUnit ) const
    {
        outListUnit.clear();
        _pWorld->forEachUnit( [&]( const RTSUnit& unit )
        {
            const bool bCombatUnit = unit._owner == _player && unit.isMobile() && unit._pDef->_bWorker == SW_FALSE && unit._pDef->canAttack();
            if ( bCombatUnit )
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
        // 짐 없이 노는 일꾼을 먼저, 광물로 가는 일꾼을 그다음에 고른다.
        RTSUnitID workerID{};
        int32     bestScore = 3;
        _pWorld->forEachUnit( [&]( const RTSUnit& unit )
        {
            if ( unit._owner != _player || unit._pDef->_id != _settings._workerID )
                return;
            const RTSOrder* pOrder                   = unit.findOrder();
            int32           score                    = 3;
            const bool      bGatheringWithEmptyCargo = pOrder != nullptr && pOrder->_type == RTSOrderType::Gather && unit._cargoAmount == 0;
            if ( pOrder == nullptr )
                score = 0;
            else if ( bGatheringWithEmptyCargo && unit._gatherPhase != RTSGatherPhase::Harvesting )
                score = 1;
            else if ( bGatheringWithEmptyCargo )
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
    ActionResult RTSCommanderAI::taskDefend( ActionContext& context, const VocabularyNoParams& /*params*/ )
    {
        RTSCommanderAI&             self   = *static_cast<RTSCommanderAI*>( context._pOwner );
        Blackboard&                 board  = *context._world.find<BlackboardWriteSlot>(); // 러너가 틱마다 채운다
        const std::optional<float3> threat = board.getVector( getThreatKey() );
        if ( threat.has_value() == false )
            return ActionResult::Failed; // 위협 자리가 없으면 막을 곳이 없다
        vector<RTSUnitID> listArmy;
        self.collectArmy( listArmy );
        if ( listArmy.empty() == false )
            (void)self._pWorld->issueGroupMove( listArmy, *threat, true );
        board.clearValue( getThreatKey() );
        return ActionResult::Succeeded;
    }

    ActionResult RTSCommanderAI::taskGatherIdle( ActionContext& context, const VocabularyNoParams& /*params*/ )
    {
        RTSCommanderAI& self   = *static_cast<RTSCommanderAI*>( context._pOwner );
        RTSWorld&       world  = *self._pWorld;
        const RTSUnit*  pDepot = world.findUnit( self.findDepot() );
        if ( pDepot == nullptr )
            return ActionResult::Failed;
        vector<RTSUnitID> listIdle;
        world.forEachUnit( [&]( const RTSUnit& unit )
        {
            const bool bIdleWorker = unit._owner == self._player && unit._pDef->_bWorker && unit.isIdle();
            if ( bIdleWorker )
                listIdle.push_back( unit._id );
        } );
        for ( const RTSUnitID workerID : listIdle )
        {
            const RTSUnitID mineralID = world.findNearestResource( pDepot->_position, RTSResourceType::Minerals, 16.0f );
            if ( mineralID.isValid() == false )
                return ActionResult::Failed;
            (void)world.issueGather( workerID, mineralID );
        }
        return ActionResult::Succeeded;
    }

    ActionResult RTSCommanderAI::taskTrainWorkers( ActionContext& context, const VocabularyNoParams& /*params*/ )
    {
        RTSCommanderAI& self               = *static_cast<RTSCommanderAI*>( context._pOwner );
        RTSWorld&       world              = *self._pWorld;
        const RTSUnit*  pDepot             = world.findUnit( self.findDepot() );
        const bool      bCannotTrainWorker = pDepot == nullptr || pDepot->_listProduction.empty() == false || world.countPlanned( self._player, self._settings._workerID ) >= self._settings._workerTarget;
        if ( bCannotTrainWorker )
            return ActionResult::Failed;
        return world.train( pDepot->_id, self._settings._workerID ) == RTSCommandResult::Ok ? ActionResult::Succeeded : ActionResult::Failed;
    }

    ActionResult RTSCommanderAI::taskBuildSupply( ActionContext& context, const VocabularyNoParams& /*params*/ )
    {
        RTSCommanderAI&  self    = *static_cast<RTSCommanderAI*>( context._pOwner );
        RTSWorld&        world   = *self._pWorld;
        const RTSPlayer* pPlayer = world.findPlayer( self._player );
        if ( pPlayer->_supplyCap >= world.getCatalog()->getSupplyMax() || pPlayer->_supplyCap - pPlayer->_supplyUsed > self._settings._supplyMargin )
            return ActionResult::Failed;
        // 짓고 있거나 지으러 가는 보급 건물이 있으면 기다린다.
        if ( world.countPlanned( self._player, self._settings._supplyID ) > world.countUnits( self._player, self._settings._supplyID, false ) )
            return ActionResult::Failed;
        return self.orderConstruction( self._settings._supplyID ) ? ActionResult::Succeeded : ActionResult::Failed;
    }

    ActionResult RTSCommanderAI::taskBuildProduction( ActionContext& context, const VocabularyNoParams& /*params*/ )
    {
        RTSCommanderAI& self  = *static_cast<RTSCommanderAI*>( context._pOwner );
        RTSWorld&       world = *self._pWorld;
        if ( world.countPlanned( self._player, self._settings._productionID ) >= self._settings._productionTarget )
            return ActionResult::Failed;
        // 일꾼을 어느 정도 모은 뒤에 시작한다.
        if ( world.countUnits( self._player, self._settings._workerID, false ) < self._settings._workerTarget / 2 )
            return ActionResult::Failed;
        return self.orderConstruction( self._settings._productionID ) ? ActionResult::Succeeded : ActionResult::Failed;
    }

    ActionResult RTSCommanderAI::taskTrainArmy( ActionContext& context, const VocabularyNoParams& /*params*/ )
    {
        RTSCommanderAI&   self  = *static_cast<RTSCommanderAI*>( context._pOwner );
        RTSWorld&         world = *self._pWorld;
        vector<RTSUnitID> listIdleProducer;
        world.forEachUnit( [&]( const RTSUnit& unit )
        {
            const bool bIdleProducer = unit._owner == self._player && unit._pDef->_id == self._settings._productionID && unit.isConstructed() && unit._listProduction.empty();
            if ( bIdleProducer )
                listIdleProducer.push_back( unit._id );
        } );
        bool bTrained = false;
        for ( const RTSUnitID producerID : listIdleProducer )
        {
            bTrained = world.train( producerID, self._settings._armyUnitID ) == RTSCommandResult::Ok || bTrained;
        }
        return bTrained ? ActionResult::Succeeded : ActionResult::Failed;
    }

    ActionResult RTSCommanderAI::taskAttack( ActionContext& context, const VocabularyNoParams& /*params*/ )
    {
        RTSCommanderAI&   self  = *static_cast<RTSCommanderAI*>( context._pOwner );
        RTSWorld&         world = *self._pWorld;
        vector<RTSUnitID> listArmy;
        self.collectArmy( listArmy );
        vector<RTSUnitID> listIdle;
        for ( const RTSUnitID unitID : listArmy )
        {
            const RTSUnit* pUnit         = world.findUnit( unitID );
            const bool     bIdleAttacker = pUnit != nullptr && pUnit->isIdle() && pUnit->_attackTarget.isValid() == false;
            if ( bIdleAttacker )
                listIdle.push_back( unitID );
        }
        if ( static_cast<int32>( listIdle.size() ) < self._settings._attackArmySize )
            return ActionResult::Failed;
        // 가장 가까운 적 플레이어의 시작 지점으로 간다.
        const RTSPlayer* pSelf        = world.findPlayer( self._player );
        float32          bestDistance = MathUtil::kMaxFloat;
        float3           target{};
        bool             bFound = false;
        for ( int32 player = 0; player < world.getPlayerCount(); ++player )
        {
            const RTSPlayer* pOther = world.findPlayer( player );
            if ( pOther->_bDefeated || world.areEnemies( self._player, player ) == false )
                continue;
            const float32 distance = float3::getHorizontalDistance( pSelf->_startPosition, pOther->_startPosition );
            if ( distance < bestDistance )
            {
                bestDistance = distance;
                target       = pOther->_startPosition;
                bFound       = true;
            }
        }
        if ( bFound == false )
            return ActionResult::Failed;
        (void)world.issueGroupMove( listIdle, target, true );
        ++self._attackWaveCount;
        return ActionResult::Succeeded;
    }

    // 지휘관 트리 데이터(`commander.bt.xml`)가 `task` 속성으로 가리키는 이름이다.
    SW_GAME_ACTION( RTSDefend, "rts.defend", VocabularyNoParams, &RTSCommanderAI::taskDefend );
    SW_GAME_ACTION( RTSGatherIdle, "rts.gatherIdle", VocabularyNoParams, &RTSCommanderAI::taskGatherIdle );
    SW_GAME_ACTION( RTSTrainWorkers, "rts.trainWorkers", VocabularyNoParams, &RTSCommanderAI::taskTrainWorkers );
    SW_GAME_ACTION( RTSBuildSupply, "rts.buildSupply", VocabularyNoParams, &RTSCommanderAI::taskBuildSupply );
    SW_GAME_ACTION( RTSBuildProduction, "rts.buildProduction", VocabularyNoParams, &RTSCommanderAI::taskBuildProduction );
    SW_GAME_ACTION( RTSTrainArmy, "rts.trainArmy", VocabularyNoParams, &RTSCommanderAI::taskTrainArmy );
    SW_GAME_ACTION( RTSAttack, "rts.attack", VocabularyNoParams, &RTSCommanderAI::taskAttack );
} // namespace sw

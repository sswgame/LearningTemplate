#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Foundation/Utility/StateArchiveUtil.h"
#include "GameFramework/Base/Gameplay/Inventory/Shop.h"
#include "GameFramework/Base/Gameplay/Match/TeamAttitude.h"
#include "GameFramework/Kits/Genre/Strategy/RealTimeStrategy/RTSWorld.h"

#include <algorithm>

namespace sw
{
    namespace
    {
        struct RTSWorldStateInternal
        {
            static constexpr uint32 kMinUnitStateBytes   = 128; ///< 유닛 하나의 상태가 최소한 차지하는 바이트 수(개수 상한 계산용)
            static constexpr uint32 kMinOrderStateBytes  = 34;  ///< 명령 하나의 상태가 최소한 차지하는 바이트 수
            static constexpr uint32 kMinPlayerStateBytes = 30;  ///< 플레이어 하나의 상태가 최소한 차지하는 바이트 수
        };
    } // namespace

    // ------------------------------------------------------------------------------
    // 상태 쓰기와 읽기(핫 리로드, 세이브)
    // ------------------------------------------------------------------------------
    void RTSWorld::writeState( Archive& outArchive ) const
    {
        outArchive << _grid.getWidth();
        outArchive << _grid.getHeight();
        outArchive << _listTerrainBlocked;
        outArchive << static_cast<uint32>( _listUnit.size() );
        for ( size_t index = 0; index < _listUnit.size(); ++index )
        {
            const RTSUnit& unit = _listUnit[index];
            outArchive << _listGeneration[index];
            StateArchiveUtil::writeName( outArchive, unit._pDef != nullptr ? unit._pDef->_id : hashed_string{} );
            outArchive << static_cast<uint32>( unit._listOrder.size() );
            for ( const RTSOrder& order : unit._listOrder )
            {
                StateArchiveUtil::writeName( outArchive, order._buildID );
                outArchive << order._target;
                StateArchiveUtil::writeInt2( outArchive, order._buildCell );
                outArchive << order._targetUnit.packed();
                outArchive << order._arriveRadius;
                outArchive << static_cast<uint8>( order._type );
                outArchive << static_cast<uint8>( order._pFlowField != nullptr ? SW_TRUE : SW_FALSE );
            }
            outArchive << static_cast<uint32>( unit._listProduction.size() );
            for ( const hashed_string& productionID : unit._listProduction )
            {
                StateArchiveUtil::writeName( outArchive, productionID );
            }
            outArchive << unit._position;
            outArchive << unit._rallyPoint;
            outArchive << unit._moveGoal;
            StateArchiveUtil::writeInt2( outArchive, unit._cell );
            outArchive << unit._id.packed();
            outArchive << unit._attackTarget.packed();
            outArchive << unit._gatherTarget.packed();
            outArchive << unit._harvester.packed();
            outArchive << unit._builder.packed();
            outArchive << unit._linkedResource.packed();
            outArchive << unit._hp;
            outArchive << unit._cooldown._remaining;
            outArchive << unit._buildProgress;
            outArchive << unit._productionTimer;
            outArchive << unit._gatherTimer;
            outArchive << unit._owner;
            outArchive << unit._resourceLeft;
            outArchive << unit._cargoAmount;
            outArchive << unit._supplyReserved;
            outArchive << static_cast<uint8>( unit._cargoType );
            outArchive << static_cast<uint8>( unit._gatherPhase );
            outArchive << unit._bAlive;
            outArchive << unit._bHasRally;
            outArchive << unit._bSupplyBlocked;
        }
        outArchive << static_cast<uint32>( _listFreeSlot.size() );
        for ( const uint32 slot : _listFreeSlot )
        {
            outArchive << slot;
        }
        outArchive << static_cast<uint32>( _listPlayer.size() );
        for ( const RTSPlayer& player : _listPlayer )
        {
            outArchive << player._startPosition;
            outArchive << player._supplyUsed;
            outArchive << player._supplyCap;
            outArchive << player._team;
            outArchive << player._nextUnderAttackTime;
            outArchive << player._bDefeated;
            outArchive << player._bHadBuilding;
        }
        outArchive << static_cast<uint32>( _listTeamVisibility.size() );
        for ( const vector<uint8>& listVisibility : _listTeamVisibility )
        {
            outArchive << listVisibility;
        }
        StateArchiveUtil::writeStepTimer( outArchive, _stepTimer );
        outArchive << _time;
        outArchive << _visionTimer._remaining;
        outArchive << _teamCount;
        outArchive << _winningTeam;
    }

    bool RTSWorld::readState( Archive& archive )
    {
        using Internal = RTSWorldStateInternal;
        int32 width    = 0;
        int32 height   = 0;
        archive >> width;
        archive >> height;
        const bool bInvalidWorldHeader = archive.isError() || width != _grid.getWidth() || height != _grid.getHeight() || _pCatalog == nullptr;
        if ( bInvalidWorldHeader )
            return false;
        vector<uint8> listTerrainBlocked;
        archive >> listTerrainBlocked;
        if ( archive.isError() || listTerrainBlocked.size() != _listTerrainBlocked.size() )
            return false;

        uint32 unitCount = 0;
        if ( StateArchiveUtil::readCount( archive, Internal::kMinUnitStateBytes, unitCount ) == false )
            return false;
        deque<RTSUnit> listUnit;
        vector<uint32> listGeneration( unitCount );
        vector<uint8>  listFlowFieldOrder; // 무리 이동 명령마다 흐름장이 있었는지(유닛, 명령 순서)
        for ( uint32 index = 0; index < unitCount; ++index )
        {
            RTSUnit&      unit = listUnit.emplace_back();
            hashed_string defID;
            archive >> listGeneration[index];
            if ( StateArchiveUtil::readName( archive, defID ) == false )
                return false;
            unit._pDef = defID.empty() ? nullptr : _pCatalog->findUnit( defID );
            if ( defID.empty() == false && unit._pDef == nullptr )
                return false; // 카탈로그에서 빠진 유닛이라 판을 맞출 수 없다
            uint32 orderCount = 0;
            if ( StateArchiveUtil::readCount( archive, Internal::kMinOrderStateBytes, orderCount ) == false )
                return false;
            for ( uint32 orderIndex = 0; orderIndex < orderCount; ++orderIndex )
            {
                RTSOrder& order      = unit._listOrder.emplace_back();
                uint64    targetUnit = 0;
                uint8     type       = 0;
                uint8     bFlowField = SW_FALSE;
                if ( StateArchiveUtil::readName( archive, order._buildID ) == false )
                    return false;
                archive >> order._target;
                StateArchiveUtil::readInt2( archive, order._buildCell );
                archive >> targetUnit;
                archive >> order._arriveRadius;
                archive >> type;
                archive >> bFlowField;
                if ( archive.isError() || type > static_cast<uint8>( RTSOrderType::Hold ) )
                    return false;
                order._targetUnit = RTSUnitID::fromPacked( targetUnit );
                order._type       = static_cast<RTSOrderType>( type );
                listFlowFieldOrder.push_back( bFlowField );
            }
            uint32 productionCount = 0;
            if ( StateArchiveUtil::readCount( archive, sizeof( uint32 ), productionCount ) == false )
                return false;
            for ( uint32 productionIndex = 0; productionIndex < productionCount; ++productionIndex )
            {
                hashed_string productionID;
                if ( StateArchiveUtil::readName( archive, productionID ) == false )
                    return false;
                unit._listProduction.push_back( productionID );
            }
            uint64 id             = 0;
            uint64 attackTarget   = 0;
            uint64 gatherTarget   = 0;
            uint64 harvester      = 0;
            uint64 builder        = 0;
            uint64 linkedResource = 0;
            uint8  cargoType      = 0;
            uint8  gatherPhase    = 0;
            archive >> unit._position;
            archive >> unit._rallyPoint;
            archive >> unit._moveGoal;
            StateArchiveUtil::readInt2( archive, unit._cell );
            archive >> id;
            archive >> attackTarget;
            archive >> gatherTarget;
            archive >> harvester;
            archive >> builder;
            archive >> linkedResource;
            archive >> unit._hp;
            archive >> unit._cooldown._remaining;
            archive >> unit._buildProgress;
            archive >> unit._productionTimer;
            archive >> unit._gatherTimer;
            archive >> unit._owner;
            archive >> unit._resourceLeft;
            archive >> unit._cargoAmount;
            archive >> unit._supplyReserved;
            archive >> cargoType;
            archive >> gatherPhase;
            archive >> unit._bAlive;
            archive >> unit._bHasRally;
            archive >> unit._bSupplyBlocked;
            const bool bEnumValid       = cargoType <= static_cast<uint8>( RTSResourceType::None ) && gatherPhase <= static_cast<uint8>( RTSGatherPhase::Returning );
            const bool bAliveValid      = unit._bAlive == SW_FALSE || unit._pDef != nullptr;
            const bool bInvalidUnitData = archive.isError() || bEnumValid == false || bAliveValid == false;
            if ( bInvalidUnitData )
                return false;
            unit._id             = RTSUnitID::fromPacked( id );
            unit._attackTarget   = RTSUnitID::fromPacked( attackTarget );
            unit._gatherTarget   = RTSUnitID::fromPacked( gatherTarget );
            unit._harvester      = RTSUnitID::fromPacked( harvester );
            unit._builder        = RTSUnitID::fromPacked( builder );
            unit._linkedResource = RTSUnitID::fromPacked( linkedResource );
            unit._cargoType      = static_cast<RTSResourceType>( cargoType );
            unit._gatherPhase    = static_cast<RTSGatherPhase>( gatherPhase );
            // 경로와 흐름장은 싣지 않는다. 맨 앞 명령을 처음부터 다시 진행시키면 이 격자에서 경로를 다시 구한다.
            unit._bOrderStarted = SW_FALSE;
            unit._repathTimer.clear();
        }

        uint32 freeSlotCount = 0;
        if ( StateArchiveUtil::readCount( archive, sizeof( uint32 ), freeSlotCount ) == false )
            return false;
        vector<uint32> listFreeSlot( freeSlotCount );
        for ( uint32& slot : listFreeSlot )
        {
            archive >> slot;
            if ( slot >= unitCount )
                archive.setError();
        }
        uint32 playerCount = 0;
        // 지갑은 플레이어를 추가한 쪽이 빌려 준 것이라 싣지 않는다. 같은 수의 플레이어를 먼저 추가한 월드에만 읽어야 한다.
        const bool bInvalidPlayerData = archive.isError() || StateArchiveUtil::readCount( archive, Internal::kMinPlayerStateBytes, playerCount ) == false || playerCount != _listPlayer.size();
        if ( bInvalidPlayerData )
            return false;
        vector<RTSPlayer> listPlayer( playerCount );
        for ( uint32 playerIndex = 0; playerIndex < playerCount; ++playerIndex )
        {
            listPlayer[playerIndex]._pWallet = _listPlayer[playerIndex]._pWallet;
        }
        for ( RTSPlayer& player : listPlayer )
        {
            archive >> player._startPosition;
            archive >> player._supplyUsed;
            archive >> player._supplyCap;
            archive >> player._team;
            archive >> player._nextUnderAttackTime;
            archive >> player._bDefeated;
            archive >> player._bHadBuilding;
        }
        uint32 teamCount = 0;
        if ( archive.isError() || StateArchiveUtil::readCount( archive, sizeof( uint32 ), teamCount ) == false )
            return false;
        vector<vector<uint8>> listTeamVisibility( teamCount );
        for ( vector<uint8>& listVisibility : listTeamVisibility )
        {
            archive >> listVisibility;
            if ( listVisibility.size() != _listTerrainBlocked.size() )
                archive.setError();
        }
        FixedStepTimer stepTimer   = _stepTimer;
        float32        time        = 0.0f;
        float32        visionTimer = 0.0f;
        int32          teams       = 0;
        int32          winningTeam = -1;
        if ( archive.isError() || StateArchiveUtil::readStepTimer( archive, stepTimer ) == false )
            return false;
        archive >> time;
        archive >> visionTimer;
        archive >> teams;
        archive >> winningTeam;
        if ( archive.isError() || teams != static_cast<int32>( teamCount ) )
            return false;

        _listTerrainBlocked = std::move( listTerrainBlocked );
        _listUnit           = std::move( listUnit );
        _listGeneration     = std::move( listGeneration );
        _listFreeSlot       = std::move( listFreeSlot );
        _listPlayer         = std::move( listPlayer );
        _listTeamVisibility = std::move( listTeamVisibility );
        _eventBuffer.clear();
        _listFlowField.clear();
        _stepTimer              = stepTimer;
        _time                   = time;
        _visionTimer._remaining = visionTimer;
        _teamCount              = teams;
        _winningTeam            = winningTeam;

        // 격자는 땅과 서 있는 건물, 자원의 발자국으로 다시 칠한다. 같은 데이터에서 나오므로 저장하지 않는다.
        const float32 cellSize = _grid.getCellSize();
        const float3  origin   = _grid.getOrigin();
        _grid.initialize( width, height, cellSize, origin );
        for ( int32 y = 0; y < height; ++y )
        {
            for ( int32 x = 0; x < width; ++x )
            {
                if ( _listTerrainBlocked[static_cast<size_t>( _grid.computeIndex( int2{ x, y } ) )] != SW_FALSE || _land.isBlocked( x, y ) )
                    _grid.setBlocked( x, y, true );
            }
        }
        size_t flowFieldOrderIndex = 0;
        for ( RTSUnit& unit : _listUnit )
        {
            const bool bStaticBlocker = unit._bAlive != SW_FALSE && unit.isMobile() == false && unit._pDef->_bExtractor == SW_FALSE;
            if ( bStaticBlocker )
                placeFootprint( unit, true );
            if ( unit._bAlive != SW_FALSE && unit.isMobile() )
            {
                NavAgentSettings agentSettings;
                agentSettings._radius   = unit._pDef->_radius;
                agentSettings._maxSpeed = MathUtil::max( 0.01f, unit._pDef->_speed );
                unit._agent.setSettings( agentSettings );
                unit._agent.setPosition( unit._position );
            }
            for ( RTSOrder& order : unit._listOrder )
            {
                const bool bFlowField = listFlowFieldOrder[flowFieldOrderIndex++] != SW_FALSE;
                if ( bFlowField && unit._bAlive != SW_FALSE )
                    order._pFlowField = acquireFlowField( _grid.computeCell( order._target ) );
            }
        }
        rebuildBuckets();
        return true;
    }
} // namespace sw

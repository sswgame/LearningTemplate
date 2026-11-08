#include "pch.h"

#include "Games/FrontierTown/FrontierSkirmishGameModeComponent.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"
#include "Engine/Visual/Component/MeshComponent.h"

#include "GameFramework/Kits/Genre/Strategy/RealTimeStrategy/RTSCatalog.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Gameplay/SessionState/SessionStateComponent.h"

#include "Games/FrontierTown/FrontierTownData.h"

namespace sw
{
} // namespace sw

namespace sw
{
    FrontierSkirmishGameModeComponent::FrontierSkirmishGameModeComponent()
        : _unitPrefab{}
        , _workerUnit{}
        , _soldierUnit{}
        , _barracksUnit{}
        , _barracksX{ 20 }
        , _barracksY{ 20 }
        , _workerStart{ 2.5f, 0.0f, 2.5f }
        , _patrolA{ 2.5f, 0.0f, 6.5f }
        , _patrolB{ 10.5f, 0.0f, 6.5f }
        , _unitColor{ 0.30f, 0.45f, 0.85f, 1.0f }
        , _buildingColor{ 0.55f, 0.30f, 0.30f, 1.0f }
        , _world{}
        , _tintCache{}
        , _listUnitView{}
        , _worker{}
        , _soldier{}
        , _tickCount{ 0 }
        , _bPatrolToB{ SW_TRUE }
        , _bViewsDirty{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    FrontierSkirmishGameModeComponent::~FrontierSkirmishGameModeComponent() = default;

    void FrontierSkirmishGameModeComponent::serializeState( StateIO& io )
    {
        SessionStateComponent* pState = io.isReading() ? SessionStateComponent::findOnOwner( *this ) : nullptr;
        if ( io.check( pState != nullptr ) == false )
            return;
        io.header( kStateTag, kStateVersion );
        int32  tickCount    = _tickCount;
        uint8  bPatrolToB   = _bPatrolToB;
        uint64 workerValue  = _worker.packed();
        uint64 soldierValue = _soldier.packed();
        io.value( tickCount );
        io.value( bPatrolToB );
        io.value( workerValue );
        io.value( soldierValue );
        if ( io.isOk() == false )
            return;
        // RTSWorld 는 옮길 수 없다(경로 풀이가 같은 객체의 격자를 참조한다) — 읽기는 지형을 다시 세운 _world 에 바로 한다. 유닛 · 시야 복원은 RTSWorld 직렬화가 임시 값에 읽고 다 읽은 뒤에만 옮긴다.
        if ( io.isReading() && io.check( initializeWorld( _world, *pState ) ) == false )
            return;
        io.value( _world );
        io.end();
        if ( io.isWriting() || io.isOk() == false )
            return;
        _tickCount   = tickCount;
        _bPatrolToB  = bPatrolToB == SW_FALSE ? SW_FALSE : SW_TRUE;
        _worker      = RTSUnitID::fromPacked( workerValue );
        _soldier     = RTSUnitID::fromPacked( soldierValue );
        _bViewsDirty = SW_TRUE;
    }

    bool FrontierSkirmishGameModeComponent::initializeWorld( RTSWorld& outWorld, SessionStateComponent& state )
    {
        const RTSCatalog* pCatalog = game::findSubsystem<const RTSCatalog>();
        if ( pCatalog == nullptr )
            return false;
        if ( outWorld.initialize( pCatalog, FrontierTownData::kLandSize, FrontierTownData::kLandSize, RTSSettings{} ) == false )
            return false;
        outWorld.bindLand( &state.getLand(), int2{ 0, 0 } );
        (void)outWorld.addPlayer( 0, &state.getWallet(), float3{ 2.0f, 0.0f, 2.0f } ); // 플레이어 0 이 공유 지갑을 쓴다
        return true;
    }

    bool FrontierSkirmishGameModeComponent::startGame()
    {
        // 도시 디렉터가 먼저 열었으면 아무것도 하지 않는다 — 같은 설정이라 어느 쪽이 먼저 열어도 같은 판이다.
        SessionStateComponent* pState = SessionStateComponent::openOnOwner( *this, FrontierTownData::makeStateSettings() );
        if ( pState == nullptr )
            return false;
        if ( initializeWorld( _world, *pState ) == false )
        {
            SW_LOG_ERROR_ID( "FrontierSkirmish.SkirmishCannotStartRTS", "[Frontier] the skirmish cannot start - the RTS unit catalog is not loaded" );
            return false;
        }
        // 유닛 id 는 씬 데이터가 정본이다(C++ 에 기본값이 없다). 비었거나 카탈로그에 없으면 조용히 건너뛰지 않고 판을 열지 않는다.
        const RTSCatalog* pCatalog    = game::findSubsystem<const RTSCatalog>();
        bool              bUnitsKnown = true;
        for ( const string* pUnitID : { &_workerUnit, &_soldierUnit, &_barracksUnit } )
        {
            if ( pCatalog != nullptr && pCatalog->find( hashed_string( *pUnitID ) ) != nullptr )
                continue;
            SW_LOG_ERROR_ID( "FrontierSkirmish.SkirmishUnitEmptyNot", "[Frontier] the skirmish unit '%#' is empty or not in the RTS unit catalog - the skirmish cannot start", pUnitID->c_str() );
            bUnitsKnown = false;
        }
        if ( bUnitsKnown == false )
            return false;
        if ( pState->isFreshGame() )
        {
            _worker  = _world.spawnUnit( hashed_string( _workerUnit ), 0, _workerStart );
            _soldier = _world.spawnUnit( hashed_string( _soldierUnit ), 0, _patrolA );
        }
        _tickCount   = 0;
        _bPatrolToB  = SW_TRUE;
        _bViewsDirty = SW_TRUE;
        return true;
    }

    void FrontierSkirmishGameModeComponent::issueOpeningOrders()
    {
        // 도시가 첫 틱에 땅을 칠한 뒤다 — 병영 자리가 도시 땅이면 RTS 가 거절한다(Occupied 계열). 그러면 씬 배치를 고친다.
        const RTSCommandResult buildResult = _world.issueBuild( _worker, hashed_string( _barracksUnit ), int2{ _barracksX, _barracksY } );
        if ( buildResult != RTSCommandResult::Ok )
            SW_LOG_ERROR_ID( "FrontierSkirmish.WorkerCannotBuildCheck", "[Frontier] the worker cannot build '%#' at (%#, %#) (%#) - check the scene layout", _barracksUnit, _barracksX, _barracksY, static_cast<int32>( buildResult ) );
        const RTSCommandResult moveResult = _world.issueMove( _soldier, _patrolB );
        if ( moveResult != RTSCommandResult::Ok )
            SW_LOG_ERROR_ID( "FrontierSkirmish.SoldierCannotStartPatrol", "[Frontier] the soldier cannot start its patrol (%#)", static_cast<int32>( moveResult ) );
    }

    void FrontierSkirmishGameModeComponent::continuePatrol()
    {
        const RTSUnit* pSoldier = _world.findUnit( _soldier );
        if ( pSoldier == nullptr || pSoldier->isIdle() == false )
            return;
        _bPatrolToB = _bPatrolToB == SW_TRUE ? SW_FALSE : SW_TRUE;
        (void)_world.issueMove( _soldier, _bPatrolToB == SW_TRUE ? _patrolB : _patrolA ); // 경로가 막혀 거절되면 다음 틱에 반대쪽으로 다시 해 본다
    }

    void FrontierSkirmishGameModeComponent::updateGame( float32 deltaTime )
    {
        if ( ++_tickCount == 2 )
            issueOpeningOrders();
        else if ( _tickCount > 2 )
            continuePatrol();
        _world.update( deltaTime );
        _bViewsDirty = SW_TRUE; // 유닛이 움직였을 수 있다 — 틱 뒤 플러시가 모습을 옮긴다
    }

    void FrontierSkirmishGameModeComponent::onFlush( GameObjectManager& manager, bool bRespawnViews )
    {
        if ( bRespawnViews == false && _bViewsDirty == SW_FALSE )
            return;
        syncUnitViews( manager );
        _bViewsDirty = SW_FALSE;
    }

    void FrontierSkirmishGameModeComponent::onViewsDespawned()
    {
        _listUnitView.clear();
    }

    void FrontierSkirmishGameModeComponent::syncUnitViews( GameObjectManager& manager )
    {
        // shortcut: 살아 있는 유닛 수가 그대로면 모습을 순서대로 옮기고, 바뀌면(병영이 서거나 유닛이 죽으면) 모두 다시 세운다 — 유닛이 많아지면 유닛마다의 모습 컴포넌트로 바꾼다.
        vector<const RTSUnit*> listUnit;
        _world.forEachUnit( [&listUnit]( const RTSUnit& unit )
        { listUnit.push_back( &unit ); } );
        if ( listUnit.size() != _listUnitView.size() )
        {
            destroySpawned( manager, _listUnitView );
            for ( size_t unitIndex = 0; unitIndex < listUnit.size(); ++unitIndex )
            {
                GameObject* pView = spawnPrefab( manager, _unitPrefab, "FrontierUnit" );
                _listUnitView.push_back( pView != nullptr ? pView->getHandle() : GameObjectHandle{} );
            }
        }
        for ( size_t index = 0; index < listUnit.size(); ++index )
        {
            const RTSUnit& unit  = *listUnit[index];
            GameObject*    pView = manager.resolveGameObject( _listUnitView[index] );
            MeshComponent* pMesh = pView != nullptr ? pView->getComponent<MeshComponent>() : nullptr;
            if ( pMesh == nullptr )
                continue;
            const bool    bBuilding = unit.isBuilding();
            const float32 size      = bBuilding && unit._pDef != nullptr ? static_cast<float32>( unit._pDef->_footprint ) : 0.6f;
            const float3  center    = bBuilding ? float3{ static_cast<float32>( unit._cell._x ) + size * 0.5f, 0.4f, static_cast<float32>( unit._cell._y ) + size * 0.5f }
                                                : float3{ unit._position._x, 0.3f, unit._position._z };
            pMesh->setLocalPosition( center );
            pMesh->setLocalScale( float3{ size * 0.9f, bBuilding ? 0.8f : 0.6f, size * 0.9f } );
            _tintCache.apply( *pMesh, bBuilding ? _buildingColor : _unitColor );
        }
    }
} // namespace sw

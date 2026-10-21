#include "pch.h"

#include "Games/FrontierTown/FrontierSkirmishDirectorComponent.h"

#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Foundation/Framework/GameService.h"
#include "GameFramework/Base/Foundation/Utility/StateArchiveUtil.h"
#include "GameFramework/Base/Gameplay/SessionState/SessionStateComponent.h"
#include "GameFramework/Kits/Genre/Strategy/RealTimeStrategy/RTSCatalog.h"

#include "Games/FrontierTown/FrontierTownData.h"

namespace sw
{
    SW_LOG_CALLER( "FrontierSkirmish" );
} // namespace sw

namespace sw
{
    FrontierSkirmishDirectorComponent::FrontierSkirmishDirectorComponent()
        : _unitPrefab{}
        , _workerUnit{ "worker" }
        , _soldierUnit{ "marine" }
        , _barracksUnit{ "barracks" }
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

    FrontierSkirmishDirectorComponent::~FrontierSkirmishDirectorComponent() = default;

    void FrontierSkirmishDirectorComponent::writeState( Archive& outArchive ) const
    {
        StateArchiveUtil::writeHeader( outArchive, kStateTag, kStateVersion );
        const uint8 bPatrolToB = _bPatrolToB;
        outArchive << _tickCount;
        outArchive << bPatrolToB;
        outArchive << _worker.packed();
        outArchive << _soldier.packed();
        _world.writeState( outArchive );
    }

    bool FrontierSkirmishDirectorComponent::initializeWorld( RTSWorld& outWorld, SessionStateComponent& state )
    {
        const RTSCatalog* pCatalog = game::findSubsystem<RTSCatalog>();
        if ( pCatalog == nullptr )
            return false;
        outWorld.initialize( pCatalog, FrontierTownData::kLandSize, FrontierTownData::kLandSize, RTSSettings{} );
        outWorld.bindLand( &state.getLand(), int2{ 0, 0 } );
        (void)outWorld.addPlayer( 0, &state.getWallet(), float3{ 2.0f, 0.0f, 2.0f } ); // 플레이어 0 이 공유 지갑을 쓴다
        return true;
    }

    bool FrontierSkirmishDirectorComponent::startGame()
    {
        SessionStateComponent* pState = SessionStateComponent::findOnOwner( *this );
        if ( pState == nullptr )
        {
            SW_LOG_WARNING( "[Frontier] the skirmish cannot start - no SessionStateComponent in front of it on this object" );
            return false;
        }
        // 도시 디렉터가 먼저 열었으면 아무것도 하지 않는다 — 같은 설정이라 어느 쪽이 먼저 열어도 같은 판이다.
        if ( pState->initialize( FrontierTownData::makeStateSettings() ) == SessionStateInitResult::Refused )
            return false;
        if ( initializeWorld( _world, *pState ) == false )
        {
            SW_LOG_WARNING( "[Frontier] the skirmish cannot start - the RTS unit catalog is not loaded" );
            return false;
        }
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

    bool FrontierSkirmishDirectorComponent::readState( Archive& archive )
    {
        SessionStateComponent* pState = SessionStateComponent::findOnOwner( *this );
        if ( pState == nullptr || StateArchiveUtil::readHeader( archive, kStateTag, kStateVersion ) == false )
            return false;
        int32  tickCount    = 0;
        uint8  bPatrolToB   = SW_TRUE;
        uint64 workerValue  = 0;
        uint64 soldierValue = 0;
        archive >> tickCount;
        archive >> bPatrolToB;
        archive >> workerValue;
        archive >> soldierValue;
        RTSWorld world;
        if ( initializeWorld( world, *pState ) == false )
            return false;
        const bool bInvalidWorldState = archive.isError() || world.readState( archive ) == false || archive.getRemainingBytes() != 0;
        if ( bInvalidWorldState )
            return false;
        _world       = std::move( world );
        _tickCount   = tickCount;
        _bPatrolToB  = bPatrolToB == SW_FALSE ? SW_FALSE : SW_TRUE;
        _worker      = RTSUnitID::fromPacked( workerValue );
        _soldier     = RTSUnitID::fromPacked( soldierValue );
        _bViewsDirty = SW_TRUE;
        return true;
    }

    void FrontierSkirmishDirectorComponent::issueOpeningOrders()
    {
        // 도시가 첫 틱에 땅을 칠한 뒤다 — 병영 자리가 도시 땅이면 RTS 가 거절한다(Occupied 계열). 그러면 씬 배치를 고친다.
        const RTSCommandResult buildResult = _world.issueBuild( _worker, hashed_string( _barracksUnit ), int2{ _barracksX, _barracksY } );
        if ( buildResult != RTSCommandResult::Ok )
            SW_LOG_ERROR( "[Frontier] the worker cannot build '%#' at (%#, %#) (%#) - check the scene layout", _barracksUnit, _barracksX, _barracksY, static_cast<int32>( buildResult ) );
        const RTSCommandResult moveResult = _world.issueMove( _soldier, _patrolB );
        if ( moveResult != RTSCommandResult::Ok )
            SW_LOG_ERROR( "[Frontier] the soldier cannot start its patrol (%#)", static_cast<int32>( moveResult ) );
    }

    void FrontierSkirmishDirectorComponent::continuePatrol()
    {
        const RTSUnit* pSoldier = _world.findUnit( _soldier );
        if ( pSoldier == nullptr || pSoldier->isIdle() == false )
            return;
        _bPatrolToB = _bPatrolToB == SW_TRUE ? SW_FALSE : SW_TRUE;
        (void)_world.issueMove( _soldier, _bPatrolToB == SW_TRUE ? _patrolB : _patrolA ); // 경로가 막혀 거절되면 다음 틱에 반대쪽으로 다시 해 본다
    }

    void FrontierSkirmishDirectorComponent::updateGame( float32 deltaTime )
    {
        if ( ++_tickCount == 2 )
            issueOpeningOrders();
        else if ( _tickCount > 2 )
            continuePatrol();
        _world.update( deltaTime );
        _bViewsDirty = SW_TRUE; // 유닛이 움직였을 수 있다 — 틱 뒤 플러시가 모습을 옮긴다
    }

    void FrontierSkirmishDirectorComponent::onFlush( GameObjectManager& manager, bool bRespawnViews )
    {
        if ( bRespawnViews == false && _bViewsDirty == SW_FALSE )
            return;
        syncUnitViews( manager );
        _bViewsDirty = SW_FALSE;
    }

    void FrontierSkirmishDirectorComponent::onViewsDespawned()
    {
        _listUnitView.clear();
    }

    void FrontierSkirmishDirectorComponent::syncUnitViews( GameObjectManager& manager )
    {
        // shortcut: 살아 있는 유닛 수가 그대로면 모습을 순서대로 옮기고, 바뀌면(병영이 서거나 유닛이 죽으면) 모두 다시 세운다 — 유닛이 많아지면 유닛마다의 모습 컴포넌트로 바꾼다.
        vector<const RTSUnit*> listUnit;
        _world.forEachUnit( [&listUnit]( const RTSUnit& unit )
        { listUnit.push_back( &unit ); } );
        if ( listUnit.size() != _listUnitView.size() )
        {
            for ( GameObjectHandle& handle : _listUnitView )
            {
                destroySpawned( manager, handle );
            }
            _listUnitView.clear();
            for ( const RTSUnit* pUnit : listUnit )
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

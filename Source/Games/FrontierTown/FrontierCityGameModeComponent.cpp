#include "pch.h"

#include "Games/FrontierTown/FrontierCityGameModeComponent.h"

#include "Engine/Input/Map/InputMap.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"
#include "Engine/Visual/Component/MeshComponent.h"

#include "GameFramework/Kits/Genre/Strategy/CityBuilder/CityCatalog.h"
#include "GameFramework/Kits/Genre/Strategy/RealTimeStrategy/RTSWorld.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Gameplay/SessionState/PlayerProgressComponent.h"
#include "GameFramework/Runtime/Gameplay/SessionState/SessionStateComponent.h"

#include "Games/FrontierTown/FrontierTownData.h"

namespace sw
{
    FrontierCityGameModeComponent::FrontierCityGameModeComponent()
        : _tilePrefab{}
        , _startingDeben{ 500 }
        , _startingMinerals{ 500 }
        , _landmarkBuilding{}
        , _landmarkX{ 4 }
        , _landmarkY{ 4 }
        , _roadFromX{ 0 }
        , _roadFromY{ 10 }
        , _roadToX{ 15 }
        , _roadToY{ 10 }
        , _fastTimeScale{ 8.0f }
        , _buildingColor{ 0.85f, 0.72f, 0.45f, 1.0f }
        , _roadColor{ 0.45f, 0.42f, 0.38f, 1.0f }
        , _city{}
        , _tintCache{}
        , _buildingViews{}
        , _roadViews{}
        , _speed{}
        , _bLaidOut{ SW_FALSE }
        , _bViewsDirty{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    FrontierCityGameModeComponent::~FrontierCityGameModeComponent() = default;

    void FrontierCityGameModeComponent::serializeGameState( StateIO& io )
    {
        SessionStateComponent*   pState    = io.isReading() ? SessionStateComponent::findOnOwner( *this ) : nullptr;
        PlayerProgressComponent* pProgress = io.isReading() ? PlayerProgressComponent::findOnOwner( *this ) : nullptr;
        if ( io.check( pState != nullptr && pProgress != nullptr ) == false )
            return;
        io.header( kStateTag, kStateVersion ); // 배치를 마쳤는지는 상태 칸이다(`GameModeComponent::serializeState`)
        if ( io.isOk() == false )
            return;
        CitySimulation restoredCity;
        if ( io.isReading() && io.check( initializeCity( restoredCity, *pState, *pProgress ) ) == false )
            return;
        CitySimulation& city = io.isWriting() ? _city : restoredCity;
        io.section( CitySimulation::kStateTag, CitySimulation::kStateVersion, [&city]( StateIO& bodyIO )
        { bodyIO.value( city ); } );
        io.end();
        if ( io.isWriting() || io.isOk() == false )
            return;
        _city        = std::move( restoredCity );
        _bViewsDirty = SW_TRUE;
    }

    bool FrontierCityGameModeComponent::initializeCity( CitySimulation& outCity, SessionStateComponent& state, PlayerProgressComponent& progress )
    {
        const CityCatalog* pCatalog = game::findSubsystem<const CityCatalog>();
        if ( pCatalog == nullptr )
            return false;
        if ( outCity.initialize( pCatalog, FrontierTownData::kLandSize, FrontierTownData::kLandSize, state.makeRefs( progress ) ) == false )
            return false;
        // 땅은 놓기 전에 빌린다 — 이미 놓인 도로 · 건물은 땅을 얻지 않는다(CitySimulation::bindLand).
        outCity.bindLand( &state.getLand(), int2{ 0, 0 } );
        return true;
    }

    bool FrontierCityGameModeComponent::startGame()
    {
        // 이 오브젝트의 첫 디렉터가 판을 연다. 시작 돈은 새 판일 때만(복원한 판 위에 덧쌓이지 않게).
        SessionStateComponent* pState = SessionStateComponent::openOnOwner( *this, FrontierTownData::makeStateSettings() );
        if ( pState == nullptr )
            return false;
        SessionStateInitResult   initResult = SessionStateInitResult::Refused;
        PlayerProgressComponent* pProgress  = PlayerProgressComponent::openOnOwner( *this, FrontierTownData::makeProgressSettings(), initResult );
        if ( pProgress == nullptr )
            return false;
        if ( initResult == SessionStateInitResult::Fresh )
        {
            // 광물 통화는 RTS 규칙 자산(유닛 카탈로그의 <Rules>)의 것이다 — C++ 기본값을 짐작하지 않는다(감사 K-4)
            const RTSCatalog* pUnits = game::findSubsystem<const RTSCatalog>();
            if ( pUnits == nullptr )
            {
                SW_LOG_ERROR_ID( "FrontierCity.UnitCatalogMissing", "[Frontier] the unit catalog is not loaded - the starting minerals have no currency" );
                return false;
            }
            const CityCatalog* pCity = game::findSubsystem<const CityCatalog>();
            if ( pCity == nullptr )
            {
                SW_LOG_ERROR_ID( "FrontierCity.CityCatalogMissing", "[Frontier] the city catalog is not loaded - the starting money has no currency" );
                return false;
            }
            pProgress->getWallet().add( pCity->getRules()._currency, _startingDeben );
            pProgress->getWallet().add( pUnits->getRules()._mineralCurrency, _startingMinerals );
        }
        if ( initializeCity( _city, *pState, *pProgress ) == false )
        {
            SW_LOG_ERROR_ID( "FrontierCity.CityCannotStartCity", "[Frontier] the city cannot start - the city catalog is not loaded" );
            return false;
        }
        _bLaidOut    = SW_FALSE;
        _bViewsDirty = SW_TRUE;
        return true;
    }

    void FrontierCityGameModeComponent::onGameStarted()
    {
        SW_LOG_INFO_ID( "FrontierCity.TownOpenHoldSpace", "[Frontier] the town is open - hold Space to fast-forward; the city and the skirmish share one land, one wallet and one clock" );
    }

    void FrontierCityGameModeComponent::layOutTown()
    {
        _bLaidOut                    = SW_TRUE;
        const CityPlaceResult result = _city.placeBuilding( _landmarkBuilding.key(), _landmarkX, _landmarkY );
        if ( result != CityPlaceResult::Ok )
            SW_LOG_ERROR_ID( "Frontier.LandmarkRefused", "[Frontier] landmark '%#' at (%#, %#) was refused (%#) - check the scene layout and city.xml", _landmarkBuilding.c_str(), _landmarkX, _landmarkY,
                             static_cast<int32>( result ) );
        const int32 roadCount = _city.placeRoadLine( int2{ _roadFromX, _roadFromY }, int2{ _roadToX, _roadToY } );
        if ( roadCount == 0 )
            SW_LOG_ERROR_ID( "FrontierCity.RoadLaidNoTile", "[Frontier] the road (%#, %#) -> (%#, %#) laid no tile - check the scene layout", _roadFromX, _roadFromY, _roadToX, _roadToY );
        _bViewsDirty = SW_TRUE;
    }

    void FrontierCityGameModeComponent::updateGame( float32 deltaTime )
    {
        SessionStateComponent* pState = SessionStateComponent::findOnOwner( *this );
        if ( pState == nullptr )
            return;
        // 첫 틱에 놓는다 — RTS 는 시작할 때 땅을 칠했으므로 땅 리비전으로 다시 칠해야 보인다(RTSWorld::bindLand).
        if ( _bLaidOut == SW_FALSE )
            layOutTown();
        // 키는 입력 맵(`data/gamesettings.xml` 의 입력 맵 묶음)이 정한다. 시계를 미는 것은 이 디렉터 하나다.
        const bool bFastForward = isAutoPlayOn() || isActionDown( hashed_string( FrontierTownData::kFastForwardAction ) );
        if ( _speed.setTimeScale( bFastForward ? _fastTimeScale : 1.0f ) == false || pState->getClock().setTimeScale( _speed.getTimeScale() ) == false )
            return;
        (void)_city.update( deltaTime, _speed.getTimeScale() ); // 시계와 같은 배율 — 시계만 밀면 빨리 감는 동안 결산만 잦고 생산 · 일꾼은 1 배속이다(감사 K-2 · K-10, 유니티 Time.timeScale 자리)
        for ( const WorldClockEvent& clockEvent : pState->getClockEvents() )
        {
            if ( clockEvent._kind == WorldClockEvent::Kind::DayChanged )
                _city.settleMonth( false );
        }
    }

    void FrontierCityGameModeComponent::onFlush( GameObjectManager& manager, bool bRespawnViews )
    {
        if ( bRespawnViews == false && _bViewsDirty == SW_FALSE )
            return;
        // 바뀐 건물 · 도로만 지우고 다시 스폰한다(예전에는 하나만 바뀌어도 모두 지우고 다시 만들었다, 감사 K-10). 처음이거나 정리한 뒤면 슬롯이 비어 모두 쌓인다.
        if ( bRespawnViews )
        {
            _buildingViews.clear();
            _roadViews.clear();
        }
        collectTileViews();
        const auto destroy = [&manager]( GameObjectHandle& inoutHandle )
        { destroySpawned( manager, inoutHandle ); };
        _buildingViews.flush( destroy, [this, &manager]( size_t buildingIndex, const CityBuildingDef* pDef )
        { return spawnBuildingView( manager, buildingIndex, *pDef ); } );
        _roadViews.flush( destroy, [this, &manager]( size_t tileIndex, uint8 )
        { return spawnRoadView( manager, tileIndex ); } );
        _bViewsDirty = SW_FALSE;
    }

    void FrontierCityGameModeComponent::onViewsDespawned()
    {
        _buildingViews.clear();
        _roadViews.clear();
    }

    void FrontierCityGameModeComponent::collectTileViews()
    {
        const vector<CityBuilding>& listBuilding = _city.getBuildings();
        for ( size_t buildingIndex = 0; buildingIndex < listBuilding.size(); ++buildingIndex )
        {
            const CityBuilding& building = listBuilding[buildingIndex];
            _buildingViews.show( buildingIndex, building._bAlive != SW_FALSE ? building._pDef : nullptr );
        }
        for ( int32 y = 0; y < _city.getHeight(); ++y )
        {
            for ( int32 x = 0; x < _city.getWidth(); ++x )
            {
                const CityTile* pTile = _city.findTile( x, y );
                _roadViews.show( static_cast<size_t>( y * _city.getWidth() + x ), pTile != nullptr && pTile->_bRoad != SW_FALSE ? SW_TRUE : SW_FALSE );
            }
        }
    }

    GameObjectHandle FrontierCityGameModeComponent::spawnBuildingView( GameObjectManager& manager, size_t buildingIndex, const CityBuildingDef& def )
    {
        // 건물은 크기만큼 늘린 판 하나, 도로는 칸마다 판 하나 — 위에서 보는 직교 카메라라 높이는 얕다.
        const CityBuilding& building = _city.getBuildings()[buildingIndex];
        GameObject*         pView    = spawnPrefab( manager, _tilePrefab, "FrontierBuilding" );
        MeshComponent*      pMesh    = pView != nullptr ? pView->getComponent<MeshComponent>() : nullptr;
        if ( pMesh == nullptr )
            return pView != nullptr ? pView->getHandle() : GameObjectHandle{};
        const float32 size = static_cast<float32>( def._size );
        pMesh->setLocalPosition( float3{ static_cast<float32>( building._origin._x ) + size * 0.5f, 0.5f, static_cast<float32>( building._origin._y ) + size * 0.5f } );
        pMesh->setLocalScale( float3{ size * 0.96f, 1.0f, size * 0.96f } );
        _tintCache.apply( *pMesh, _buildingColor );
        return pView->getHandle();
    }

    GameObjectHandle FrontierCityGameModeComponent::spawnRoadView( GameObjectManager& manager, size_t tileIndex )
    {
        const int32    x     = static_cast<int32>( tileIndex ) % _city.getWidth();
        const int32    y     = static_cast<int32>( tileIndex ) / _city.getWidth();
        GameObject*    pView = spawnPrefab( manager, _tilePrefab, "FrontierRoad" );
        MeshComponent* pMesh = pView != nullptr ? pView->getComponent<MeshComponent>() : nullptr;
        if ( pMesh == nullptr )
            return pView != nullptr ? pView->getHandle() : GameObjectHandle{};
        pMesh->setLocalPosition( float3{ static_cast<float32>( x ) + 0.5f, 0.05f, static_cast<float32>( y ) + 0.5f } );
        _tintCache.apply( *pMesh, _roadColor );
        return pView->getHandle();
    }
} // namespace sw

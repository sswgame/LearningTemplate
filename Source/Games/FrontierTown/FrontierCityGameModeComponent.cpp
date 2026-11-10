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
        , _listTileView{}
        , _bLaidOut{ SW_FALSE }
        , _bViewsDirty{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    FrontierCityGameModeComponent::~FrontierCityGameModeComponent() = default;

    void FrontierCityGameModeComponent::serializeState( StateIO& io )
    {
        SessionStateComponent* pState = io.isReading() ? SessionStateComponent::findOnOwner( *this ) : nullptr;
        if ( io.check( pState != nullptr ) == false )
            return;
        io.header( kStateTag, kStateVersion );
        uint8 bLaidOut = _bLaidOut;
        io.value( bLaidOut );
        if ( io.isOk() == false )
            return;
        CitySimulation restoredCity;
        if ( io.isReading() && io.check( initializeCity( restoredCity, *pState ) ) == false )
            return;
        CitySimulation& city = io.isWriting() ? _city : restoredCity;
        io.section( CitySimulation::kStateTag, CitySimulation::kStateVersion, [&city]( StateIO& bodyIO )
        { bodyIO.value( city ); } );
        io.end();
        if ( io.isWriting() || io.isOk() == false )
            return;
        _city        = std::move( restoredCity );
        _bLaidOut    = bLaidOut == SW_FALSE ? SW_FALSE : SW_TRUE;
        _bViewsDirty = SW_TRUE;
    }

    bool FrontierCityGameModeComponent::initializeCity( CitySimulation& outCity, SessionStateComponent& state )
    {
        const CityCatalog* pCatalog = game::findSubsystem<const CityCatalog>();
        if ( pCatalog == nullptr )
            return false;
        if ( outCity.initialize( pCatalog, FrontierTownData::kLandSize, FrontierTownData::kLandSize, CitySettings{}, state.makeRefs() ) == false )
            return false;
        // 땅은 놓기 전에 빌린다 — 이미 놓인 도로 · 건물은 땅을 얻지 않는다(CitySimulation::bindLand).
        outCity.bindLand( &state.getLand(), int2{ 0, 0 } );
        return true;
    }

    bool FrontierCityGameModeComponent::startGame()
    {
        // 이 오브젝트의 첫 디렉터가 판을 연다. 시작 돈은 새 판일 때만(복원한 판 위에 덧쌓이지 않게).
        SessionStateInitResult initResult = SessionStateInitResult::Refused;
        SessionStateComponent* pState     = SessionStateComponent::openOnOwner( *this, FrontierTownData::makeStateSettings(), initResult );
        if ( pState == nullptr )
            return false;
        if ( initResult == SessionStateInitResult::Fresh )
        {
            pState->getWallet().add( CitySettings{}._currency, _startingDeben );
            pState->getWallet().add( RTSSettings{}._mineralCurrency, _startingMinerals );
        }
        if ( initializeCity( _city, *pState ) == false )
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
        const CityPlaceResult result = _city.placeBuilding( hashed_string( _landmarkBuilding ), _landmarkX, _landmarkY );
        if ( result != CityPlaceResult::Ok )
            SW_LOG_ERROR_ID( "Frontier.LandmarkRefused", "[Frontier] landmark '%#' at (%#, %#) was refused (%#) - check the scene layout and city.xml", _landmarkBuilding, _landmarkX, _landmarkY,
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
        if ( pState->getClock().setTimeScale( bFastForward ? _fastTimeScale : 1.0f ) == false )
            return;
        _city.update( deltaTime );
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
        destroySpawned( manager, _listTileView );
        spawnTiles( manager );
        _bViewsDirty = SW_FALSE;
    }

    void FrontierCityGameModeComponent::onViewsDespawned()
    {
        _listTileView.clear();
    }

    void FrontierCityGameModeComponent::spawnTiles( GameObjectManager& manager )
    {
        // 건물은 크기만큼 늘린 판 하나, 도로는 칸마다 판 하나 — 위에서 보는 직교 카메라라 높이는 얕다.
        for ( const CityBuilding& building : _city.getBuildings() )
        {
            if ( building._bAlive == SW_FALSE || building._pDef == nullptr )
                continue;
            GameObject*    pView = spawnPrefab( manager, _tilePrefab, "FrontierBuilding" );
            MeshComponent* pMesh = pView != nullptr ? pView->getComponent<MeshComponent>() : nullptr;
            if ( pMesh == nullptr )
                continue;
            const float32 size = static_cast<float32>( building._pDef->_size );
            pMesh->setLocalPosition( float3{ static_cast<float32>( building._origin._x ) + size * 0.5f, 0.5f, static_cast<float32>( building._origin._y ) + size * 0.5f } );
            pMesh->setLocalScale( float3{ size * 0.96f, 1.0f, size * 0.96f } );
            _tintCache.apply( *pMesh, _buildingColor );
            _listTileView.push_back( pView->getHandle() );
        }
        for ( int32 y = 0; y < _city.getHeight(); ++y )
        {
            for ( int32 x = 0; x < _city.getWidth(); ++x )
            {
                const CityTile* pTile = _city.findTile( x, y );
                if ( pTile == nullptr || pTile->_bRoad == SW_FALSE )
                    continue;
                GameObject*    pView = spawnPrefab( manager, _tilePrefab, "FrontierRoad" );
                MeshComponent* pMesh = pView != nullptr ? pView->getComponent<MeshComponent>() : nullptr;
                if ( pMesh == nullptr )
                    continue;
                pMesh->setLocalPosition( float3{ static_cast<float32>( x ) + 0.5f, 0.05f, static_cast<float32>( y ) + 0.5f } );
                _tintCache.apply( *pMesh, _roadColor );
                _listTileView.push_back( pView->getHandle() );
            }
        }
    }
} // namespace sw

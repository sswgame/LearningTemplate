#include "pch.h"

#include "GameFramework/Kits/Genre/Strategy/CityBuilder/Component/CityGameModeComponent.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Runtime/Gameplay/GameState/GameStateComponent.h"
#include "GameFramework/Runtime/Gameplay/GameState/PlayerComponentUtil.h"
#include "GameFramework/Runtime/Gameplay/Inventory/WalletComponent.h"

namespace sw
{
    CityGameModeComponent::CityGameModeComponent()
        : _landSize{ 0 }
        , _secondsPerDay{ 0.0f }
        , _startHour{ 0.0f }
        , _landmarkBuilding{}
        , _landmarkX{ 0 }
        , _landmarkY{ 0 }
        , _roadFromX{ 0 }
        , _roadFromY{ 0 }
        , _roadToX{ 0 }
        , _roadToY{ 0 }
        , _fastTimeScale{ 1.0f }
        , _bLaidOut{ SW_FALSE }
        , _bFastForward{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    CityGameModeComponent::~CityGameModeComponent() = default;

    void CityGameModeComponent::serializeGameState( StateIO& io )
    {
        GameStateComponent* pState  = io.isReading() ? GameStateComponent::findOnOwner( *this ) : nullptr;
        WalletComponent*    pWallet = io.isReading() ? findOnPlayerState<WalletComponent>() : nullptr;
        if ( io.isReading() && io.check( pState != nullptr && pWallet != nullptr ) == false )
            return;
        io.header( kStateFormat ); // 배치를 마쳤는지는 상태 칸이다(`GameModeComponent::serializeState`)
        if ( io.isOk() == false )
            return;
        CitySimulation restoredCity;
        if ( io.isReading() && io.check( initializeCity( restoredCity, *pState, *pWallet ) ) == false )
            return;
        CitySimulation& city = io.isWriting() ? getMutableCity() : restoredCity;
        io.section( CitySimulation::kStateFormat, [&city]( StateIO& bodyIO )
        { bodyIO.value( city ); } );
        io.end();
        if ( io.isWriting() || io.isOk() == false )
            return;
        getMutableCity() = std::move( restoredCity );
        markLayoutChanged();
    }

    bool CityGameModeComponent::initializeCity( CitySimulation& outCity, GameStateComponent& state, WalletComponent& wallet ) const
    {
        const CityCatalog* const pCatalog = findCatalog();
        if ( pCatalog == nullptr || outCity.initialize( pCatalog, _landSize, _landSize, wallet.getWallet() ) == false )
            return false;
        // 땅은 놓기 전에 빌린다 — 이미 놓인 도로 · 건물은 땅을 얻지 않는다(CitySimulation::bindLand).
        outCity.bindLand( &state.getLand(), int2{ 0, 0 } );
        return true;
    }

    bool CityGameModeComponent::startGame()
    {
        if ( acquireCatalog() == false )
            return false;
        // 이 오브젝트의 첫 디렉터가 판을 연다. 시작 돈은 새 판일 때만(복원한 판 위에 덧쌓이지 않게).
        GameStateSettings settings;
        settings._clock._secondsPerDay = _secondsPerDay;
        settings._clock._startHour     = _startHour;
        settings._landWidth            = _landSize;
        settings._landHeight           = _landSize;
        GameStateComponent* pState     = GameStateComponent::openOnOwner( *this, settings );
        if ( pState == nullptr )
            return false;
        const GameObject* const pPlayerState = requirePlayerState();
        WalletComponent* const  pWallet      = pPlayerState != nullptr ? PlayerComponentUtil::requireOn<WalletComponent>( *pPlayerState ) : nullptr;
        if ( pWallet == nullptr )
            return false; // 까닭은 찾기가 남겼다
        if ( pWallet->isFreshGame() )
            pWallet->getWallet().add( findCatalog()->getRules()._currency, getStartingMoney() );
        if ( initializeCity( getMutableCity(), *pState, *pWallet ) == false )
            return false; // 범위 밖 도시 설정은 도시가 오류를 남겼다
        _bFastForward = SW_FALSE;
        return beginCity();
    }

    void CityGameModeComponent::onGameStarted()
    {
        SW_LOG_INFO_ID( "City.TownOpen", "[City] the town is open - hold fast forward to speed up the clock; the city shares one land and one clock with the other game modes on this object, and the player's wallet" );
    }

    bool CityGameModeComponent::submitFastForward( bool bHeld )
    {
        if ( isStarted() == false )
            return false;
        _bFastForward = bHeld ? SW_TRUE : SW_FALSE;
        return true;
    }

    bool CityGameModeComponent::submitTimeInput( bool bPause, bool bFaster, bool bSlower )
    {
        (void)bPause;
        (void)bFaster;
        (void)bSlower;
        return false;
    }

    const Wallet* CityGameModeComponent::findCityWallet() const
    {
        const GameObject* const      pPlayerState = findPlayerState();
        const WalletComponent* const pWallet      = pPlayerState != nullptr ? pPlayerState->getComponent<WalletComponent>() : nullptr;
        return pWallet != nullptr ? &pWallet->getWallet() : nullptr;
    }

    void CityGameModeComponent::layOutTown()
    {
        _bLaidOut                    = SW_TRUE;
        const CityPlaceResult result = getMutableCity().placeBuilding( _landmarkBuilding.key(), _landmarkX, _landmarkY );
        if ( result != CityPlaceResult::Ok )
            SW_LOG_ERROR_ID( "City.LandmarkRefused", "[City] landmark '%#' at (%#, %#) was refused (%#) - check the scene layout and the city catalog", _landmarkBuilding.c_str(), _landmarkX, _landmarkY,
                             static_cast<int32>( result ) );
        const int32 roadCount = getMutableCity().placeRoadLine( int2{ _roadFromX, _roadFromY }, int2{ _roadToX, _roadToY } );
        if ( roadCount == 0 )
            SW_LOG_ERROR_ID( "City.RoadLaidNoTile", "[City] the road (%#, %#) -> (%#, %#) laid no tile - check the scene layout", _roadFromX, _roadFromY, _roadToX, _roadToY );
        markLayoutChanged();
    }

    void CityGameModeComponent::collectMinigameResult( vector<MinigameResultValue>& outListValue ) const
    {
        const Wallet* const pCityWallet = findCityWallet();
        if ( pCityWallet != nullptr )
            addMoneyResult( outListValue, pCityWallet->getBalance( getCity().getCurrency() ) - getStartingMoney() );
    }

    void CityGameModeComponent::updateGame( float32 deltaTime )
    {
        GameStateComponent* pState = GameStateComponent::findOnOwner( *this );
        if ( pState == nullptr )
            return;
        // 첫 틱에 놓는다 — 땅을 나누는 다른 디렉터는 시작할 때 땅을 칠했으므로 땅 리비전으로 다시 칠해야 보인다(RTSWorld::bindLand).
        if ( _bLaidOut == SW_FALSE )
            layOutTown();
        const bool bFastForward = isAutoPlayOn() || _bFastForward == SW_TRUE;
        if ( getMutableSpeed().setTimeScale( bFastForward ? _fastTimeScale : 1.0f ) == false || pState->getClock().setTimeScale( getSpeed().getTimeScale() ) == false )
            return;
        (void)getMutableCity().update( deltaTime, getSpeed().getTimeScale() ); // 시계와 같은 배율 — 시계만 밀면 빨리 감는 동안 결산만 잦고 생산 · 일꾼은 1 배속이다(감사 K-2 · K-10, 유니티 Time.timeScale 자리)
        for ( const WorldClockEvent& clockEvent : pState->getClockEvents() )
        {
            if ( clockEvent._kind == WorldClockEvent::Kind::DayChanged )
                getMutableCity().settleMonth( false );
        }
        collectStepEvents();
    }

    void CityGameModeComponent::onFlush( GameObjectManager& manager, bool bRespawnViews )
    {
        // 모습은 클라이언트 모듈(`CityViewComponent`)이 세운다.
        (void)manager;
        (void)bRespawnViews;
    }
} // namespace sw

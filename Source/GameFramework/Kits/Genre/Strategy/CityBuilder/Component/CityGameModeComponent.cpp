#include "pch.h"

#include "GameFramework/Kits/Genre/Strategy/CityBuilder/Component/CityGameModeComponent.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Runtime/Gameplay/GameState/GameStateComponent.h"
#include "GameFramework/Runtime/Gameplay/GameState/PlayerComponentUtil.h"
#include "GameFramework/Runtime/Gameplay/Inventory/WalletComponent.h"

namespace sw
{
    CityGameModeComponent::CityGameModeComponent()
        : _cityDataPath{}
        , _landSize{ 0 }
        , _secondsPerDay{ 0.0f }
        , _startHour{ 0.0f }
        , _startingMoney{ 0 }
        , _landmarkBuilding{}
        , _landmarkX{ 0 }
        , _landmarkY{ 0 }
        , _roadFromX{ 0 }
        , _roadFromY{ 0 }
        , _roadToX{ 0 }
        , _roadToY{ 0 }
        , _fastTimeScale{ 1.0f }
        , _pCatalog{}
        , _city{}
        , _speed{}
        , _layoutRevision{ 0 }
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
        io.header( kStateTag, kStateVersion ); // 배치를 마쳤는지는 상태 칸이다(`GameModeComponent::serializeState`)
        if ( io.isOk() == false )
            return;
        CitySimulation restoredCity;
        if ( io.isReading() && io.check( initializeCity( restoredCity, *pState, *pWallet ) ) == false )
            return;
        CitySimulation& city = io.isWriting() ? _city : restoredCity;
        io.section( CitySimulation::kStateTag, CitySimulation::kStateVersion, [&city]( StateIO& bodyIO )
        { bodyIO.value( city ); } );
        io.end();
        if ( io.isWriting() || io.isOk() == false )
            return;
        _city = std::move( restoredCity );
        ++_layoutRevision;
    }

    bool CityGameModeComponent::initializeCity( CitySimulation& outCity, GameStateComponent& state, WalletComponent& wallet ) const
    {
        const CityCatalog* const pCatalog = _pCatalog.get();
        if ( pCatalog == nullptr || outCity.initialize( pCatalog, _landSize, _landSize, wallet.getWallet() ) == false )
            return false;
        // 땅은 놓기 전에 빌린다 — 이미 놓인 도로 · 건물은 땅을 얻지 않는다(CitySimulation::bindLand).
        outCity.bindLand( &state.getLand(), int2{ 0, 0 } );
        return true;
    }

    bool CityGameModeComponent::startGame()
    {
        if ( acquireRequired( _cityDataPath, _pCatalog ) == false )
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
            pWallet->getWallet().add( _pCatalog->getRules()._currency, _startingMoney );
        if ( initializeCity( _city, *pState, *pWallet ) == false )
            return false; // 범위 밖 도시 설정은 도시가 오류를 남겼다
        _bFastForward = SW_FALSE;
        ++_layoutRevision;
        return true;
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

    void CityGameModeComponent::layOutTown()
    {
        _bLaidOut                    = SW_TRUE;
        const CityPlaceResult result = _city.placeBuilding( _landmarkBuilding.key(), _landmarkX, _landmarkY );
        if ( result != CityPlaceResult::Ok )
            SW_LOG_ERROR_ID( "City.LandmarkRefused", "[City] landmark '%#' at (%#, %#) was refused (%#) - check the scene layout and the city catalog", _landmarkBuilding.c_str(), _landmarkX, _landmarkY,
                             static_cast<int32>( result ) );
        const int32 roadCount = _city.placeRoadLine( int2{ _roadFromX, _roadFromY }, int2{ _roadToX, _roadToY } );
        if ( roadCount == 0 )
            SW_LOG_ERROR_ID( "City.RoadLaidNoTile", "[City] the road (%#, %#) -> (%#, %#) laid no tile - check the scene layout", _roadFromX, _roadFromY, _roadToX, _roadToY );
        ++_layoutRevision;
    }

    void CityGameModeComponent::collectMinigameResult( vector<MinigameResultValue>& outListValue ) const
    {
        const GameObject* const      pPlayerState = findPlayerState();
        const WalletComponent* const pWallet      = pPlayerState != nullptr ? pPlayerState->getComponent<WalletComponent>() : nullptr;
        if ( pWallet != nullptr )
            addMoneyResult( outListValue, pWallet->getWallet().getBalance( _pCatalog->getRules()._currency ) - _startingMoney );
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
        if ( _speed.setTimeScale( bFastForward ? _fastTimeScale : 1.0f ) == false || pState->getClock().setTimeScale( _speed.getTimeScale() ) == false )
            return;
        (void)_city.update( deltaTime, _speed.getTimeScale() ); // 시계와 같은 배율 — 시계만 밀면 빨리 감는 동안 결산만 잦고 생산 · 일꾼은 1 배속이다(감사 K-2 · K-10, 유니티 Time.timeScale 자리)
        for ( const WorldClockEvent& clockEvent : pState->getClockEvents() )
        {
            if ( clockEvent._kind == WorldClockEvent::Kind::DayChanged )
                _city.settleMonth( false );
        }
    }

    void CityGameModeComponent::onFlush( GameObjectManager& manager, bool bRespawnViews )
    {
        // 모습은 클라이언트 모듈(`CityViewComponent`)이 세운다.
        (void)manager;
        (void)bRespawnViews;
    }
} // namespace sw

#include "pch.h"

#include "GameFramework/Kits/Genre/Casual/KartRacing/Component/KartGameModeComponent.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Foundation/WorldOrigin.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/FormatDef.h"
#include "Engine/Serialization/Format/StateIO.h"

namespace sw
{
    namespace
    {
        struct KartGameModeComponentInternal
        {
            static constexpr FormatDef kStateFormat{ "KRGM" };

            static constexpr const utf8* kWinResult  = "win";
            static constexpr const utf8* kLoseResult = "lose";
            static constexpr int32       kFirstPlace = 1;

            // 사운드 이벤트 이름 — 클립 · 범위 · 상한은 게임의 사운드 이벤트 데이터(audioevents.xml)가 정한다.
            static constexpr const utf8* kEventGo            = "KartRacingGo";
            static constexpr const utf8* kEventLap           = "KartRacingLap";
            static constexpr const utf8* kEventFinalLap      = "KartRacingFinalLap";
            static constexpr const utf8* kEventItemBox       = "KartRacingItemBox";
            static constexpr const utf8* kEventItemUsed      = "KartRacingItemUsed";
            static constexpr const utf8* kEventHit           = "KartRacingHit";
            static constexpr const utf8* kEventBlocked       = "KartRacingBlocked";
            static constexpr const utf8* kEventBoost         = "KartRacingBoost";
            static constexpr const utf8* kEventWrongWay      = "KartRacingWrongWay";
            static constexpr const utf8* kEventWon           = "KartRacingWon";
            static constexpr const utf8* kEventLost          = "KartRacingLost";
            static constexpr const utf8* kEventLapLost       = "KartRacingLapLost";
            static constexpr const utf8* kEventBestLap       = "KartRacingBestLap";
            static constexpr const utf8* kEventPlaceChanged  = "KartRacingPlaceChanged";
            static constexpr const utf8* kEventDrift         = "KartRacingDrift";
            static constexpr const utf8* kEventVehicleLanded = "KartRacingLanded";

            /** @brief 플레이어 차의 알림 @p kind 에 낼 소리입니다. 소리가 없는 알림이면 nullptr 입니다(결승은 순위가, 거꾸로 달림은 시작인지가 정한다). */
            static const utf8* findSound( KartRaceEvent::Kind kind )
            {
                switch ( kind )
                {
                    case KartRaceEvent::Kind::LapCompleted:
                    {
                        return kEventLap;
                    }
                    case KartRaceEvent::Kind::FinalLap:
                    {
                        return kEventFinalLap;
                    }
                    case KartRaceEvent::Kind::ItemBoxTaken:
                    {
                        return kEventItemBox;
                    }
                    case KartRaceEvent::Kind::ItemUsed:
                    {
                        return kEventItemUsed;
                    }
                    case KartRaceEvent::Kind::Hit:
                    {
                        return kEventHit;
                    }
                    case KartRaceEvent::Kind::ShieldBlocked:
                    {
                        return kEventBlocked;
                    }
                    case KartRaceEvent::Kind::BoostPad:
                    {
                        return kEventBoost;
                    }
                    case KartRaceEvent::Kind::CheckpointMissed:
                    case KartRaceEvent::Kind::LapRevoked:
                    {
                        return kEventLapLost;
                    }
                    case KartRaceEvent::Kind::NewBestLap:
                    {
                        return kEventBestLap;
                    }
                    case KartRaceEvent::Kind::PlaceChanged:
                    {
                        return kEventPlaceChanged;
                    }
                    case KartRaceEvent::Kind::DriftCharged:
                    {
                        return kEventDrift;
                    }
                    case KartRaceEvent::Kind::Landed:
                    {
                        return kEventVehicleLanded;
                    }
                    case KartRaceEvent::Kind::MiniTurbo:
                    {
                        return kEventBoost;
                    }
                    case KartRaceEvent::Kind::RaceStarted: // 경기 전체의 알림이라 차의 소리가 아니다
                    case KartRaceEvent::Kind::RaceEnded:
                    case KartRaceEvent::Kind::Finished: // 순위 · 시작인지가 정한다(`queuePlayerSound`)
                    case KartRaceEvent::Kind::WrongWay:
                    {
                        break;
                    }
                }
                return nullptr;
            }
        };
    } // namespace

    SW_AUTOMATION_GETTER_WATCH( kartRacingPhase, "KartRacing.Phase", "Race phase (0 waiting, 1 countdown, 2 racing, 3 ended)", KartGameModeComponent, &KartGameModeComponent::getPhase );
    SW_AUTOMATION_GETTER_WATCH( kartRacingFinishPlace, "KartRacing.PlayerFinishPlace", "Final place of the player kart (0 = not decided)", KartGameModeComponent,
                                &KartGameModeComponent::getPlayerFinishPlace );
    SW_AUTOMATION_GETTER_WATCH( kartRacingSpeed, "KartRacing.PlayerSpeed", "Forward speed of the player kart (m/s)", KartGameModeComponent, &KartGameModeComponent::getPlayerSpeed );
    SW_AUTOMATION_GETTER_WATCH( kartRacingFinished, "KartRacing.FinishedCount", "Karts with a final place", KartGameModeComponent, &KartGameModeComponent::getFinishedCount );

    KartGameModeComponent::KartGameModeComponent()
        : _trackID{}
        , _bItems{ true }
        , _itemCatalog{}
        , _vehicleSettings{}
        , _botCount{ 3 }
        , _countdownSeconds{ 3.0f }
        , _finishTimeoutSeconds{ 10.0f }
        , _resultHoldSeconds{ 3.0f }
        , _seed{ GameRandom::kDefaultSeed }
        , _firstReward{ 0 }
        , _rewardStep{ 0 }
        , _pTrackCatalog{}
        , _pItemCatalog{}
        , _pProjectileCatalog{}
        , _pVehicle{}
        , _track{}
        , _race{}
        , _listEvent{}
        , _resultHold{}
    {
    }

    KartGameModeComponent::~KartGameModeComponent() = default;

    bool KartGameModeComponent::openRace( KartRace& outRace )
    {
        const KartTrackDef* const pDef = _pTrackCatalog->find( _trackID );
        if ( pDef == nullptr || _track.initialize( *pDef ) == false )
        {
            SW_LOG_ERROR_ID( "KartRacing.TrackUnusable", "Kart race game mode: track '%#' is not in the catalog or has no usable centre line - the race does not open", _trackID.c_str() );
            return false;
        }
        KartRaceSettings settings;
        settings._countdownTime = _countdownSeconds;
        settings._finishTimeout = _finishTimeoutSeconds;
        settings._seed          = _seed;
        settings._bItems        = _bItems ? SW_TRUE : SW_FALSE;
        if ( outRace.initialize( settings, _track, _pItemCatalog.get(), _pProjectileCatalog.get() ) == false )
            return false; // 까닭은 경기가 남겼다
        const bool bAutoPlay = isAutoPlayOn();
        for ( int32 racer = 0; racer <= _botCount; ++racer )
        {
            const expected<int32> added = outRace.addRacer( *_pVehicle, racer != kPlayerRacer || bAutoPlay );
            if ( added.has_value() == false )
            {
                SW_LOG_ERROR_ID( "KartRacing.RacerRefused", "Kart race game mode: kart %# could not enter the race (%#)", racer, added.error().message() );
                return false;
            }
        }
        return true;
    }

    bool KartGameModeComponent::startGame()
    {
        if ( acquireBoundCatalog( _pTrackCatalog ) == false || acquireRequired( _vehicleSettings, _pVehicle ) == false )
            return false;
        if ( _bItems && ( acquireRequired( _itemCatalog, _pItemCatalog ) == false || acquireBoundCatalog( _pProjectileCatalog ) == false ) )
            return false;
        if ( openRace( _race ) == false )
            return false;
        _race.start();
        _resultHold.clear();
        return true;
    }

    void KartGameModeComponent::onGameStarted()
    {
        SW_LOG_INFO_ID( "KartRacing.Started", "[KartRacing] track '%#', %# laps, %# karts - the player kart drives by commands (throttle, steer, drift, item)", _trackID.c_str(), _track.getLapCount(),
                        _race.getRacerCount() );
    }

    bool KartGameModeComponent::isAcceptingInput() const
    {
        const KartRacer* const pPlayer = _race.findRacer( kPlayerRacer );
        return isStarted() && pPlayer != nullptr && pPlayer->_bAI == SW_FALSE && pPlayer->_finishPlace == 0;
    }

    bool KartGameModeComponent::submitDriverInput( const KartRacerInput& input )
    {
        if ( isAcceptingInput() == false )
            return false;
        _race.setInput( kPlayerRacer, input );
        return true;
    }

    AbsolutePosition KartGameModeComponent::getRacerPosition( int32 racer ) const
    {
        const KartRacer* const pRacer = _race.findRacer( racer );
        return AbsolutePosition{ pRacer != nullptr ? pRacer->_motor.getPosition() : float3{} };
    }

    int32 KartGameModeComponent::getPlayerPlace() const
    {
        const KartRacer* const pPlayer = _race.findRacer( kPlayerRacer );
        return pPlayer != nullptr ? pPlayer->_place : 0;
    }

    int32 KartGameModeComponent::getPlayerFinishPlace() const
    {
        const KartRacer* const pPlayer = _race.findRacer( kPlayerRacer );
        return pPlayer != nullptr ? pPlayer->_finishPlace : 0;
    }

    float32 KartGameModeComponent::getPlayerSpeed() const
    {
        const KartRacer* const pPlayer = _race.findRacer( kPlayerRacer );
        return pPlayer != nullptr ? pPlayer->_motor.getForwardSpeed() : 0.0f;
    }

    int32 KartGameModeComponent::getFinishedCount() const
    {
        int32 finishedCount = 0;
        for ( int32 racer = 0; racer < _race.getRacerCount(); ++racer )
        {
            finishedCount += _race.findRacer( racer )->_finishPlace > 0 ? 1 : 0;
        }
        return finishedCount;
    }

    bool KartGameModeComponent::readCameraFocus( GameObjectHandle& outFocusObject, float3& outFocusPosition ) const
    {
        const GameObjectManager* const pManager = getObjectManager();
        outFocusObject                          = GameObjectHandle{};
        if ( isStarted() == false || pManager == nullptr )
            return false;
        outFocusPosition = pManager->getWorldOrigin().toLocal( getRacerPosition( kPlayerRacer ) );
        return true;
    }

    void KartGameModeComponent::queuePlayerSound( const KartRaceEvent& event )
    {
        using Internal     = KartGameModeComponentInternal;
        const utf8* pSound = Internal::findSound( event._kind );
        if ( event._kind == KartRaceEvent::Kind::Finished )
            pSound = event._value == Internal::kFirstPlace ? Internal::kEventWon : Internal::kEventLost;
        else if ( event._kind == KartRaceEvent::Kind::WrongWay && event._value != 0 )
            pSound = Internal::kEventWrongWay;
        if ( pSound != nullptr )
            getSoundQueue().queueEvent( pSound );
    }

    void KartGameModeComponent::reportEvents()
    {
        _listEvent.clear();
        _race.pollEvents( _listEvent );
        for ( const KartRaceEvent& event : _listEvent )
        {
            if ( event._racer == optional<int32>{ kPlayerRacer } )
                queuePlayerSound( event );
            if ( event._kind == KartRaceEvent::Kind::RaceStarted )
            {
                getSoundQueue().queueEvent( KartGameModeComponentInternal::kEventGo );
                SW_LOG_INFO_ID( "KartRacing.Go", "[KartRacing] go" );
            }
            else if ( event._kind == KartRaceEvent::Kind::LapCompleted && event._racer.has_value() )
                SW_LOG_INFO_ID( "KartRacing.Lap", "[KartRacing] kart %# finished lap %# in %# s", *event._racer, event._value, event._time );
            else if ( event._kind == KartRaceEvent::Kind::Finished && event._racer.has_value() )
                SW_LOG_INFO_ID( "KartRacing.Finished", "[KartRacing] kart %# took place %# in %# s", *event._racer, event._value, event._time );
            else if ( event._kind == KartRaceEvent::Kind::RaceEnded )
                SW_LOG_INFO_ID( "KartRacing.Ended", "[KartRacing] race over - the player took place %#", getPlayerFinishPlace() );
        }
    }

    void KartGameModeComponent::updateGame( float32 deltaTime )
    {
        if ( deltaTime <= 0.0f )
            return;
        const bool bWasEnded = _race.getPhase() == MatchPhase::Ended;
        (void)_race.update( deltaTime ); // 걸음 수는 쓰지 않는다
        reportEvents();
        if ( _race.getPhase() != MatchPhase::Ended )
            return;
        if ( bWasEnded == false )
        {
            _resultHold.start( _resultHoldSeconds ); // 끝난 걸음 — 결과를 보이기 시작한다
            return;
        }
        if ( _resultHold.update( deltaTime ) == false ) // 다 된 걸음에 한 번만 true 다
            return;
        const bool bWon = getPlayerFinishPlace() == KartGameModeComponentInternal::kFirstPlace;
        (void)finishMinigame( hashed_string( bWon ? KartGameModeComponentInternal::kWinResult : KartGameModeComponentInternal::kLoseResult ) ); // 흐름이 없으면 베이스가 알린다
    }

    void KartGameModeComponent::collectMinigameResult( vector<MinigameResultValue>& outListValue ) const
    {
        const int32 placesBehind = getPlayerFinishPlace() - KartGameModeComponentInternal::kFirstPlace;
        if ( placesBehind >= 0 ) // 순위가 정해지지 않았으면 음수다
            addMoneyResult( outListValue, _firstReward - _rewardStep * placesBehind );
    }

    void KartGameModeComponent::onFlush( GameObjectManager& manager, bool bRespawnViews )
    {
        // 디렉터는 세울 오브젝트가 없다 — 모습은 클라이언트 모듈(`KartRaceViewComponent`)이 읽어 보인다.
        (void)manager;
        (void)bRespawnViews;
    }

    void KartGameModeComponent::serializeGameState( StateIO& io )
    {
        io.header( KartGameModeComponentInternal::kStateFormat );
        if ( io.isOk() == false )
            return;
        // 경기는 같은 트랙 · 같은 차 수로 연 뒤에 읽는다(키트 계약).
        KartRace  restoredRace;
        Countdown resultHold = _resultHold;
        if ( io.isReading() && io.check( _pTrackCatalog != nullptr && _pVehicle != nullptr && openRace( restoredRace ) ) == false )
            return;
        io.value( io.isWriting() ? _race : restoredRace );
        io.value( resultHold );
        io.end();
        if ( io.isWriting() || io.isOk() == false )
            return;
        _race       = std::move( restoredRace );
        _resultHold = resultHold;
    }
} // namespace sw

namespace sw
{
} // namespace sw

#include "pch.h"

#include "GameFramework/Kits/Genre/Casual/PartyArena/Component/PartyGameModeComponent.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Foundation/WorldOrigin.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/FormatDef.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Kits/Genre/Casual/PartyArena/TrampolineBot.h"
#include "GameFramework/Runtime/Foundation/Random/GameRandom.h"

namespace sw
{
    namespace
    {
        struct PartyGameModeComponentInternal
        {
            static constexpr FormatDef kStateFormat{ "PTGM" };

            static constexpr const utf8* kWinResult  = "win";
            static constexpr const utf8* kLoseResult = "lose";

            // 사운드 이벤트 이름 — 클립 · 범위 · 상한은 게임의 사운드 이벤트 데이터(audioevents.xml)가 정한다.
            static constexpr const utf8* kEventBounce      = "PartyArenaBounce";
            static constexpr const utf8* kEventAttack      = "PartyArenaAttack";
            static constexpr const utf8* kEventHit         = "PartyArenaHit";
            static constexpr const utf8* kEventBlocked     = "PartyArenaBlocked";
            static constexpr const utf8* kEventPound       = "PartyArenaPound";
            static constexpr const utf8* kEventRingOut     = "PartyArenaRingOut";
            static constexpr const utf8* kEventItem        = "PartyArenaItem";
            static constexpr const utf8* kEventRound       = "PartyArenaRound";
            static constexpr const utf8* kEventItemSpawned = "PartyArenaItemSpawned";
            static constexpr const utf8* kEventItemExpired = "PartyArenaItemExpired";
            static constexpr const utf8* kEventWon         = "PartyArenaWon";
            static constexpr const utf8* kEventLost        = "PartyArenaLost";
            static constexpr const utf8* kEventComboBroken = "PartyArenaComboBroken";
            static constexpr const utf8* kEventRespawn     = "PartyArenaRespawn";

            /** @brief 무대의 알림 @p kind 가 그 사람(`_player`)의 자리에서 내는 소리입니다. 소리가 없는 알림이면 nullptr 입니다. */
            static const utf8* findSound( TrampolineEvent::Kind kind )
            {
                switch ( kind )
                {
                    case TrampolineEvent::Kind::Bounced:
                    {
                        return kEventBounce;
                    }
                    case TrampolineEvent::Kind::Attacked:
                    {
                        return kEventAttack;
                    }
                    case TrampolineEvent::Kind::Hit:
                    {
                        return kEventHit;
                    }
                    case TrampolineEvent::Kind::ShieldBlocked:
                    {
                        return kEventBlocked;
                    }
                    case TrampolineEvent::Kind::GroundPound:
                    {
                        return kEventPound;
                    }
                    case TrampolineEvent::Kind::RingOut:
                    {
                        return kEventRingOut;
                    }
                    case TrampolineEvent::Kind::ItemPicked:
                    {
                        return kEventItem;
                    }
                    case TrampolineEvent::Kind::ComboBroken:
                    {
                        return kEventComboBroken;
                    }
                    case TrampolineEvent::Kind::Respawned:
                    {
                        return kEventRespawn;
                    }
                }
                return nullptr;
            }
        };
    } // namespace

    SW_AUTOMATION_GETTER_WATCH( partyArenaPhase, "PartyArena.Phase", "Party phase (0 round, 1 intermission, 2 finished)", PartyGameModeComponent, &PartyGameModeComponent::getPhase );
    SW_AUTOMATION_GETTER_WATCH( partyArenaRound, "PartyArena.Round", "Round number (0 = first)", PartyGameModeComponent, &PartyGameModeComponent::getRound );
    SW_AUTOMATION_GETTER_WATCH( partyArenaTotal, "PartyArena.PlayerTotal", "Placement points of the local player", PartyGameModeComponent, &PartyGameModeComponent::getPlayerTotal );
    SW_AUTOMATION_GETTER_WATCH( partyArenaRingOuts, "PartyArena.PlayerRingOuts", "Times the local player fell off the arena", PartyGameModeComponent, &PartyGameModeComponent::getPlayerRingOutCount );

    PartyGameModeComponent::PartyGameModeComponent()
        : _items{}
        , _botCount{ 2 }
        , _roundLimit{ 5 }
        , _intermissionSeconds{ 2.0f }
        , _resultHoldSeconds{ 3.0f }
        , _seed{ GameRandom::kDefaultSeed }
        , _winReward{ 0 }
        , _arenaSettings{}
        , _pCatalog{}
        , _pItems{}
        , _series{}
        , _arena{}
        , _listArenaEvent{}
        , _listItemEvent{}
        , _listSeriesEvent{}
        , _phaseTimer{}
        , _phase{ PartyGamePhase::Round }
        , _winner{ kNoWinner }
        , _playerRingOutCount{ 0 }
    {
    }

    PartyGameModeComponent::~PartyGameModeComponent() = default;

    bool PartyGameModeComponent::openSeries( PartyRoundSeries& outSeries ) const
    {
        // 공유 카탈로그는 읽기 전용이라 진행 상태를 가진 시리즈를 카탈로그 값으로 새로 세운다.
        for ( const PartyRoundDef& round : _pCatalog->getRounds() )
        {
            outSeries.addRound( round );
        }
        outSeries.setPlacementPoints( _pCatalog->getPlacementPoints() );
        if ( outSeries.setWinScore( _pCatalog->getWinScore() ) && outSeries.start( _botCount + 1 ) )
            return true;
        SW_LOG_ERROR_ID( "PartyArena.SeriesUnusable", "Party game mode: the series catalog has no round or no usable win score - the series does not open" );
        return false;
    }

    bool PartyGameModeComponent::openRound( const PartyRoundSeries& series, TrampolineArena& outArena ) const
    {
        const PartyRoundDef& round = *series.getCurrentRound(); // 연 시리즈는 라운드가 있다(`openSeries`)
        if ( _pItems != nullptr )
            outArena.getItemSpawner() = *_pItems;
        // 라운드마다 다른 아이템 순서가 나오게 씨앗을 라운드 번호만큼 민다.
        return outArena.initialize( _arenaSettings, series.getPlayerCount(), round._timeLimit, round._scoreLimit, _seed + static_cast<uint32>( series.getRoundNumber() ) );
    }

    bool PartyGameModeComponent::startGame()
    {
        if ( acquireBoundCatalog( _pCatalog ) == false )
            return false;
        if ( _items.empty() == false && acquireRequired( _items, _pItems ) == false )
            return false;
        PartyRoundSeries series;
        TrampolineArena  arena;
        if ( openSeries( series ) == false || openRound( series, arena ) == false )
            return false;
        _series = std::move( series );
        _arena  = std::move( arena );
        _arena.start();
        _phaseTimer.clear();
        return true;
    }

    void PartyGameModeComponent::onGameStarted()
    {
        SW_LOG_INFO_ID( "PartyArena.Started", "[PartyArena] %# players, first to %# points, %# rounds at most - the player input arrives as commands (move, jump, attack, pound)",
                        _series.getPlayerCount(), _series.getWinScore(), _roundLimit );
    }

    bool PartyGameModeComponent::isAcceptingInput() const
    {
        return isStarted() && _phase == PartyGamePhase::Round && isAutoPlayOn() == false;
    }

    bool PartyGameModeComponent::submitPlayerInput( const TrampolineInput& input )
    {
        if ( isAcceptingInput() == false )
            return false;
        _arena.setInput( kLocalPlayer, input );
        return true;
    }

    AbsolutePosition PartyGameModeComponent::getPlayerPosition( int32 player ) const
    {
        const TrampolinePlayer* const pPlayer = _arena.findPlayer( player );
        return AbsolutePosition{ pPlayer != nullptr ? pPlayer->_position : float3{} };
    }

    float32 PartyGameModeComponent::getPlayerX() const
    {
        return getPlayerPosition( kLocalPlayer )._value._x;
    }

    int32 PartyGameModeComponent::getPlayerRoundScore() const
    {
        const MatchTeam* const pTeam = _arena.getMatch().findTeam( kLocalPlayer ); // 사람마다 팀 하나(`TrampolineArena::initialize`)
        return pTeam != nullptr ? pTeam->_score : 0;
    }

    int32 PartyGameModeComponent::getPlayerTotal() const
    {
        return isStarted() ? _series.getTotal( kLocalPlayer ) : 0;
    }

    void PartyGameModeComponent::reportEvents()
    {
        _listArenaEvent.clear();
        _arena.pollEvents( _listArenaEvent );
        const GameObjectManager* const pManager = getObjectManager();
        for ( const TrampolineEvent& event : _listArenaEvent )
        {
            const utf8* const pSound = PartyGameModeComponentInternal::findSound( event._kind );
            if ( pSound != nullptr && pManager != nullptr )
                getSoundQueue().queueEventAt( pSound, pManager->getWorldOrigin().toLocal( getPlayerPosition( event._player ) ) );
            if ( event._kind != TrampolineEvent::Kind::RingOut )
                continue;
            _playerRingOutCount += event._player == kLocalPlayer ? 1 : 0;
            SW_LOG_INFO_ID( "PartyArena.RingOut", "[PartyArena] player %# fell, point to %#", event._player, event._other.has_value() ? *event._other : kNoWinner );
        }
        _listItemEvent.clear();
        _arena.getItemSpawner().pollEvents( _listItemEvent );
        for ( const PartyItemEvent& event : _listItemEvent )
        {
            const utf8* const pSound = event._kind == PartyItemEvent::Kind::Spawned ? PartyGameModeComponentInternal::kEventItemSpawned : PartyGameModeComponentInternal::kEventItemExpired;
            if ( pManager != nullptr )
                getSoundQueue().queueEventAt( pSound, pManager->getWorldOrigin().toLocal( AbsolutePosition{ event._position } ) );
        }
        _listSeriesEvent.clear();
        _series.pollEvents( _listSeriesEvent );
        for ( const PartySeriesEvent& event : _listSeriesEvent )
        {
            if ( event._kind == PartySeriesEvent::Kind::RoundStarted )
            {
                getSoundQueue().queueEvent( PartyGameModeComponentInternal::kEventRound );
                SW_LOG_INFO_ID( "PartyArena.Round", "[PartyArena] round %#", event._value );
            }
            else if ( event._kind == PartySeriesEvent::Kind::RoundRanked && event._player.has_value() )
                SW_LOG_INFO_ID( "PartyArena.Ranked", "[PartyArena] player %# took place %# for %# points", *event._player, event._value, event._points );
        }
    }

    int32 PartyGameModeComponent::findLeader() const
    {
        int32 leader = kNoWinner;
        int32 best   = 0;
        for ( int32 player = 0; player < _series.getPlayerCount(); ++player )
        {
            const int32 total = _series.getTotal( player );
            if ( player > 0 && total == best )
                leader = kNoWinner;
            else if ( player == 0 || total > best )
            {
                leader = player;
                best   = total;
            }
        }
        return leader;
    }

    void PartyGameModeComponent::finishSeries( int32 winner )
    {
        _winner = winner;
        _phase  = PartyGamePhase::Finished;
        _phaseTimer.start( _resultHoldSeconds );
        getSoundQueue().queueEvent( _winner == kLocalPlayer ? PartyGameModeComponentInternal::kEventWon : PartyGameModeComponentInternal::kEventLost );
        SW_LOG_INFO_ID( "PartyArena.Finished", "[PartyArena] the series is over - winner %#", _winner );
    }

    void PartyGameModeComponent::settleRound()
    {
        const int32   playedRound = _series.getRoundNumber();
        vector<int32> listScore;
        _arena.computeRoundScores( listScore );
        if ( _series.reportRound( listScore ) == false )
        {
            SW_LOG_ERROR_ID( "PartyArena.RoundRefused", "Party game mode: round %# was refused by the series - the match ends without a winner", playedRound );
            finishSeries( kNoWinner );
            return;
        }
        if ( _series.isFinished() )
        {
            finishSeries( _series.getWinner() );
            return;
        }
        if ( playedRound + 1 >= _roundLimit )
        {
            finishSeries( findLeader() );
            return;
        }
        _phase = PartyGamePhase::Intermission;
        _phaseTimer.start( _intermissionSeconds );
    }

    void PartyGameModeComponent::updateGame( float32 deltaTime )
    {
        if ( deltaTime <= 0.0f )
            return;
        switch ( _phase )
        {
            case PartyGamePhase::Round:
            {
                for ( int32 player = isAutoPlayOn() ? kLocalPlayer : kLocalPlayer + 1; player < _arena.getPlayerCount(); ++player )
                {
                    _arena.setInput( player, TrampolineBot::computeInput( _arena, player ) );
                }
                (void)_arena.update( deltaTime ); // 걸음 수는 쓰지 않는다
                reportEvents();
                if ( _arena.isRoundOver() )
                    settleRound();
                return;
            }
            case PartyGamePhase::Intermission:
            {
                if ( _phaseTimer.update( deltaTime ) == false )
                    return;
                TrampolineArena arena;
                if ( openRound( _series, arena ) == false ) // 첫 라운드를 같은 설정으로 열었으므로 라운드 데이터가 틀린 것이다 — 까닭은 무대가 남겼다
                {
                    finishSeries( kNoWinner );
                    return;
                }
                _arena = std::move( arena );
                _arena.start();
                _phase = PartyGamePhase::Round;
                reportEvents();
                return;
            }
            case PartyGamePhase::Finished:
            {
                if ( _phaseTimer.update( deltaTime ) == false ) // 다 된 걸음에 한 번만 true 다
                    return;
                (void)finishMinigame( hashed_string( _winner == kLocalPlayer ? PartyGameModeComponentInternal::kWinResult : PartyGameModeComponentInternal::kLoseResult ) ); // 흐름이 없으면 베이스가 알린다
                return;
            }
        }
    }

    void PartyGameModeComponent::collectMinigameResult( vector<MinigameResultValue>& outListValue ) const
    {
        if ( _phase == PartyGamePhase::Finished && _winner == kLocalPlayer )
            addMoneyResult( outListValue, _winReward );
    }

    void PartyGameModeComponent::onFlush( GameObjectManager& manager, bool bRespawnViews )
    {
        // 디렉터는 세울 오브젝트가 없다 — 모습은 클라이언트 모듈(`PartyArenaViewComponent`)이 읽어 보인다.
        (void)manager;
        (void)bRespawnViews;
    }

    void PartyGameModeComponent::serializeGameState( StateIO& io )
    {
        io.header( PartyGameModeComponentInternal::kStateFormat );
        if ( io.isOk() == false )
            return;
        // 시리즈는 같은 카탈로그 · 같은 사람 수로, 무대는 읽은 시리즈의 라운드로 연 뒤에 읽는다(키트 계약).
        PartyRoundSeries restoredSeries;
        TrampolineArena  restoredArena;
        Countdown        phaseTimer = _phaseTimer;
        if ( io.isReading() && io.check( _pCatalog != nullptr && openSeries( restoredSeries ) ) == false )
            return;
        io.value( io.isWriting() ? _series : restoredSeries );
        if ( io.isReading() && io.check( io.isOk() && openRound( restoredSeries, restoredArena ) ) == false )
            return;
        io.value( io.isWriting() ? _arena : restoredArena );
        io.value( phaseTimer );
        io.end();
        if ( io.isWriting() || io.isOk() == false )
            return;
        _series     = std::move( restoredSeries );
        _arena      = std::move( restoredArena );
        _phaseTimer = phaseTimer;
    }
} // namespace sw

namespace sw
{
} // namespace sw

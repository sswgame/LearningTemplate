#include "pch.h"

#include "GameFramework/Kits/Genre/Casual/PartyArena/PartyRoundSeries.h"

#include "Core/Container/StringUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Xml/XmlDocument.h"

#include "GameFramework/Base/Foundation/Data/GameDataXml.h"

namespace sw
{
    SW_LOG_CALLER( "PartyRoundSeries" );

    namespace
    {
        struct PartyRoundSeriesInternal
        {
            /** @brief 파티 기본 — 순위 점수 3 · 2 · 1 · 0, 먼저 5 점, 함께 닿아 총점까지 같으면 서든 데스, 라운드 시간 · 대기는 묶음이 세지 않는다. */
            static RoundSeriesSettings makeDefaultSettings()
            {
                RoundSeriesSettings settings;
                settings._listPlacementPoint = { 3, 2, 1, 0 };
                settings._winScore           = 5;
                settings._tieRule            = RoundSeriesTieRule::SuddenDeath;
                return settings;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    PartyRoundSeries::PartyRoundSeries()
        : _listRound{}
        , _seriesSettings{ PartyRoundSeriesInternal::makeDefaultSettings() }
        , _series{}
        , _eventBuffer{}
    {
    }

    bool PartyRoundSeries::start( int32 playerCount )
    {
        if ( playerCount < 2 || _listRound.empty() )
            return false;
        _series.initialize( _seriesSettings );
        if ( _series.start( playerCount ) == false )
            return false;
        _eventBuffer.clear();
        pushEvent( PartySeriesEvent::Kind::RoundStarted, -1, 0, 0 );
        return true;
    }

    bool PartyRoundSeries::reportRound( const vector<int32>& listRoundScore )
    {
        const int32 playerCount = getPlayerCount();
        if ( _series.getPhase() != RoundSeriesPhase::RoundActive || listRoundScore.size() != static_cast<size_t>( playerCount ) )
            return false;

        // 순위 알림은 라운드를 넘기기 전에 — 끝난 라운드의 id 를 싣는다.
        for ( int32 player = 0; player < playerCount; ++player )
        {
            const int32 rank = RoundSeries::computeRank( listRoundScore, player );
            pushEvent( PartySeriesEvent::Kind::RoundRanked, player, rank, _series.getPlacementPoint( rank ) );
        }
        // 위에서 같은 조건을 봤다 — 거절되지 않는다. 대기가 0 이라 끝나지 않으면 바로 다음 라운드다.
        const RoundSeriesOutcome outcome = _series.reportRound( listRoundScore );
        if ( outcome == RoundSeriesOutcome::Finished )
        {
            pushEvent( PartySeriesEvent::Kind::SeriesWon, getWinner(), getTotal( getWinner() ), 0 );
            return true;
        }
        pushEvent( PartySeriesEvent::Kind::RoundStarted, -1, getRoundNumber(), 0 );
        return true;
    }

    const PartyRoundDef* PartyRoundSeries::getCurrentRound() const
    {
        if ( _listRound.empty() )
            return nullptr;
        return &_listRound[static_cast<size_t>( getRoundNumber() ) % _listRound.size()];
    }

    void PartyRoundSeries::drainEvents( vector<PartySeriesEvent>& outListEvent )
    {
        _eventBuffer.drainTo( outListEvent );
    }

    uint32 PartyRoundSeries::loadRoot( const XmlNode& root, string_view sourceName )
    {
        setWinScore( root.getAttributeInt( "winScore", _seriesSettings._winScore ) );
        const string_view points = root.getAttributeText( "placementPoints" );
        if ( points.empty() == false )
        {
            vector<int32>& listPlacementPoint = _seriesSettings._listPlacementPoint;
            listPlacementPoint.clear();
            GameDataXml::forEachToken( points, ",; ",
                                       [&]( string_view token )
            {
                int32 value = 0;
                if ( StringUtil::parseInt( token, value ) )
                    listPlacementPoint.push_back( MathUtil::max( 0, value ) );
            } );
        }
        uint32 loadedCount = 0;
        for ( XmlNode node = root.findChild( "Round" ); node; node = node.findNextSibling( "Round" ) )
        {
            const utf8* pID = GameDataXml::findRequiredID( node, sourceName );
            if ( pID == nullptr )
                continue;
            PartyRoundDef round;
            round._id         = hashed_string( pID );
            round._timeLimit  = MathUtil::max( 0.0f, node.getAttributeFloat( "time", 0.0f ) );
            round._scoreLimit = MathUtil::max( 0, node.getAttributeInt( "scoreLimit", 0 ) );
            _listRound.push_back( round );
            ++loadedCount;
        }
        if ( loadedCount == 0 )
            SW_LOG_WARNING( "%#: no <Round>", sourceName );
        return loadedCount;
    }

    void PartyRoundSeries::pushEvent( PartySeriesEvent::Kind kind, int32 player, int32 value, int32 points )
    {
        PartySeriesEvent event;
        event._kind                 = kind;
        event._player               = player;
        event._value                = value;
        event._points               = points;
        const PartyRoundDef* pRound = getCurrentRound();
        event._roundID              = pRound != nullptr ? pRound->_id : hashed_string{};
        _eventBuffer.push( event );
    }
} // namespace sw

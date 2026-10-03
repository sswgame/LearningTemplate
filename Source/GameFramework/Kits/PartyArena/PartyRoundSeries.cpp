#include "pch.h"

#include "GameFramework/Kits/PartyArena/PartyRoundSeries.h"

#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Utility/Xml/XmlDocument.h"

#include "GameFramework/Data/GameDataXml.h"

namespace sw
{
    SW_LOG_CALLER( "PartyRoundSeries" );

    PartyRoundSeries::PartyRoundSeries()
        : _listRound{}
        , _listPlacementPoint{ 3, 2, 1, 0 }
        , _listTotal{}
        , _listEvent{}
        , _winScore{ 5 }
        , _roundNumber{ 0 }
        , _winner{ -1 }
    {
    }

    bool PartyRoundSeries::loadFromResource( string_view path )
    {
        XmlDocument doc;
        XmlNode     root;
        string      sourceName;
        return GameDataXml::loadRoot( doc, path, "PartySeries", root, sourceName ) && loadRoot( root, sourceName ) > 0;
    }

    bool PartyRoundSeries::loadFromXmlText( string_view xmlText, string_view sourceName )
    {
        XmlDocument doc;
        XmlNode     root;
        return GameDataXml::parseRoot( doc, xmlText, sourceName, "PartySeries", root ) && loadRoot( root, sourceName ) > 0;
    }

    bool PartyRoundSeries::start( int32 playerCount )
    {
        if ( playerCount < 2 || _listRound.empty() )
            return false;
        _listTotal.assign( static_cast<size_t>( playerCount ), 0 );
        _listEvent.clear();
        _roundNumber = 0;
        _winner      = -1;
        pushEvent( PartySeriesEvent::Kind::RoundStarted, -1, 0, 0 );
        return true;
    }

    bool PartyRoundSeries::reportRound( const vector<int32>& listRoundScore )
    {
        if ( isFinished() || _listTotal.empty() || listRoundScore.size() != _listTotal.size() )
            return false;

        // 순위 = 1 + 나보다 라운드 점수가 높은 사람 수(같은 점수는 같은 순위).
        const int32 playerCount = getPlayerCount();
        for ( int32 player = 0; player < playerCount; ++player )
        {
            int32 rank = 1;
            for ( int32 other = 0; other < playerCount; ++other )
                rank += listRoundScore[static_cast<size_t>( other )] > listRoundScore[static_cast<size_t>( player )] ? 1 : 0;
            const size_t placement = static_cast<size_t>( rank - 1 );
            const int32  points    = placement < _listPlacementPoint.size() ? _listPlacementPoint[placement] : 0;
            _listTotal[static_cast<size_t>( player )] += points;
            pushEvent( PartySeriesEvent::Kind::RoundRanked, player, rank, points );
        }

        // 목표에 닿은 사람 중 총점이 가장 높은 한 명 — 같으면 아직 아무도 아니다.
        int32 best      = -1;
        int32 bestTotal = 0;
        bool  bTied     = false;
        for ( int32 player = 0; player < playerCount; ++player )
        {
            const int32 total = _listTotal[static_cast<size_t>( player )];
            if ( total < _winScore )
                continue;
            if ( best < 0 || total > bestTotal )
            {
                best      = player;
                bestTotal = total;
                bTied     = false;
            }
            else if ( total == bestTotal )
            {
                bTied = true;
            }
        }
        if ( best >= 0 && bTied == false )
        {
            _winner = best;
            pushEvent( PartySeriesEvent::Kind::SeriesWon, best, bestTotal, 0 );
            return true;
        }
        ++_roundNumber;
        pushEvent( PartySeriesEvent::Kind::RoundStarted, -1, _roundNumber, 0 );
        return true;
    }

    const PartyRoundDef* PartyRoundSeries::getCurrentRound() const
    {
        if ( _listRound.empty() )
            return nullptr;
        return &_listRound[static_cast<size_t>( _roundNumber ) % _listRound.size()];
    }

    int32 PartyRoundSeries::getTotal( int32 player ) const
    {
        return player >= 0 && player < getPlayerCount() ? _listTotal[static_cast<size_t>( player )] : 0;
    }

    void PartyRoundSeries::drainEvents( vector<PartySeriesEvent>& outListEvent )
    {
        outListEvent.insert( outListEvent.end(), _listEvent.begin(), _listEvent.end() );
        _listEvent.clear();
    }

    uint32 PartyRoundSeries::loadRoot( const XmlNode& root, string_view sourceName )
    {
        setWinScore( root.getAttributeInt( "winScore", _winScore ) );
        const string_view points = root.getAttributeText( "placementPoints" );
        if ( points.empty() == false )
        {
            _listPlacementPoint.clear();
            GameDataXml::forEachToken( points, ",; ",
                                       [&]( string_view token )
            {
                int32 value = 0;
                if ( StringUtil::parseInt( token, value ) )
                    _listPlacementPoint.push_back( MathUtil::max( 0, value ) );
            } );
        }
        uint32 loadedCount = 0;
        for ( XmlNode node = root.findChild( "Round" ); node; node = node.findNextSibling( "Round" ) )
        {
            const utf8* pId = GameDataXml::findRequiredId( node, sourceName );
            if ( pId == nullptr )
                continue;
            PartyRoundDef round;
            round._id         = hashed_string( pId );
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
        event._roundId              = pRound != nullptr ? pRound->_id : hashed_string{};
        _listEvent.push_back( event );
    }
} // namespace sw

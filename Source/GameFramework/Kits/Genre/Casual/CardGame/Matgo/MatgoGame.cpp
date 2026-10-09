#include "pch.h"

#include "GameFramework/Kits/Genre/Casual/CardGame/Matgo/MatgoGame.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Foundation/Utility/StateArchiveUtil.h"

namespace sw
{
    namespace
    {
        struct MatgoGameInternal
        {
            static constexpr int32 kDealAttemptCount = 16;
            static constexpr int32 kRibbonCount      = 4;

            /** @brief 같은 월 바닥 패 중 먼저 먹을 값입니다(광 > 열끗 > 띠 > 쌍피 > 피). */
            static int32 computeCapturePriority( const Card& card )
            {
                const HwatuCardInfo& info = HwatuDeck::getInfo( card._id );
                return ( 3 - static_cast<int32>( info._kind ) ) * 4 + info._piValue;
            }

            static void collectSameMonth( const CardPile& floor, uint8 month, vector<uint16>& outListCardId )
            {
                outListCardId.clear();
                for ( const Card& card : floor.getCards() )
                {
                    if ( HwatuDeck::getMonth( card ) == month )
                        outListCardId.push_back( card._id );
                }
            }

            static bool hasFourOfMonth( const CardPile& floor )
            {
                int32 arrCount[HwatuDeck::kMonthCount + 1]{};
                for ( const Card& card : floor.getCards() )
                {
                    if ( ++arrCount[HwatuDeck::getMonth( card )] >= 4 )
                        return true;
                }
                return false;
            }

            static int32 computeCountScore( int32 count, int32 threshold ) { return count >= threshold ? count - threshold + 1 : 0; }
        };
    } // namespace
} // namespace sw

namespace sw
{
    MatgoGame::MatgoGame()
        : _listPlayer{}
        , _eventBuffer{}
        , _listSettlement{}
        , _floor{}
        , _drawPile{}
        , _settings{}
        , _currentPlayer{ 0 }
        , _winner{ -1 }
        , _phase{ MatgoPhase::Finished }
    {
    }

    void MatgoGame::initialize( const MatgoSettings& settings, uint32 seed )
    {
        GameRandom  random( seed );
        MatgoLayout layout;
        for ( int32 attempt = 0; attempt < MatgoGameInternal::kDealAttemptCount; ++attempt )
        {
            CardPile deck;
            HwatuDeck::makeDeck( deck );
            deck.shuffle( random );
            layout._listHand.clear();
            layout._listHand.resize( static_cast<size_t>( MathUtil::max( 1, settings._playerCount ) ) );
            layout._floor.clear();
            (void)deck.deal( layout._listHand, settings._handSize );
            (void)deck.drawInto( layout._floor, settings._floorSize );
            layout._drawPile = deck;
            if ( MatgoGameInternal::hasFourOfMonth( layout._floor ) == false )
                break; // 바닥에 같은 월 넷이면 다시 섞는다
        }
        initializeFromLayout( settings, layout );
    }

    void MatgoGame::initializeFromLayout( const MatgoSettings& settings, const MatgoLayout& layout )
    {
        _settings = settings;
        _listPlayer.clear();
        _listPlayer.resize( static_cast<size_t>( MathUtil::max( 1, settings._playerCount ) ) );
        for ( size_t player = 0; player < _listPlayer.size() && player < layout._listHand.size(); ++player )
        {
            _listPlayer[player]._hand = layout._listHand[player];
        }
        for ( size_t player = 0; player < _listPlayer.size() && player < layout._listCaptured.size(); ++player )
        {
            _listPlayer[player]._captured = layout._listCaptured[player];
        }
        _floor         = layout._floor;
        _drawPile      = layout._drawPile;
        _currentPlayer = MathUtil::clamp( layout._firstPlayer, 0, getPlayerCount() - 1 );
        _winner        = -1;
        _phase         = MatgoPhase::Play;
        _listSettlement.clear();
        _listSettlement.resize( _listPlayer.size(), 0 );
        _eventBuffer.clear();
    }

    bool MatgoGame::playCard( int32 player, uint16 cardId, uint16 targetId )
    {
        if ( _phase != MatgoPhase::Play || player != _currentPlayer )
            return false;
        Card played;
        if ( _listPlayer[static_cast<size_t>( player )]._hand.takeById( cardId, played ) == false )
            return false;
        _eventBuffer.push( MatgoEvent{ player, 0, played._id, MatgoEvent::Kind::Played } );

        const uint8    month = HwatuDeck::getMonth( played );
        vector<uint16> listSameMonth;
        MatgoGameInternal::collectSameMonth( _floor, month, listSameMonth );
        const int32 matchCount = static_cast<int32>( listSameMonth.size() );

        Card       flipped;
        const bool bFlipped = _drawPile.draw( flipped );
        if ( bFlipped )
            _eventBuffer.push( MatgoEvent{ player, 0, flipped._id, MatgoEvent::Kind::Flipped } );

        if ( bFlipped && HwatuDeck::getMonth( flipped ) == month )
        {
            if ( matchCount == 1 )
            {
                // 뻑 — 셋이 바닥에 남는다.
                _floor.push( played );
                _floor.push( flipped );
                _eventBuffer.push( MatgoEvent{ player, 0, played._id, MatgoEvent::Kind::Ppeok } );
            }
            else
            {
                // 쪽(바닥에 없었다) · 따닥(바닥에 둘) — 다 먹고 피를 뺏는다.
                for ( const uint16 floorCardId : listSameMonth )
                {
                    captureFloorCard( player, floorCardId );
                }
                captureCard( player, played );
                captureCard( player, flipped );
                _eventBuffer.push( MatgoEvent{ player, 0, played._id, matchCount == 0 ? MatgoEvent::Kind::Jjok : MatgoEvent::Kind::Ttadak } );
                stealPi( player );
            }
        }
        else
        {
            resolveCard( player, played, targetId );
            if ( bFlipped )
                resolveCard( player, flipped, Card::kNoCard );
        }
        finishTurn( player );
        return true;
    }

    void MatgoGame::resolveCard( int32 player, const Card& card, uint16 targetId )
    {
        vector<uint16> listSameMonth;
        MatgoGameInternal::collectSameMonth( _floor, HwatuDeck::getMonth( card ), listSameMonth );
        const int32 matchCount = static_cast<int32>( listSameMonth.size() );
        if ( matchCount == 0 )
        {
            _floor.push( card );
            return;
        }
        if ( matchCount == 2 )
        {
            uint16 chosenId     = listSameMonth[0];
            int32  bestPriority = -1;
            for ( const uint16 floorCardId : listSameMonth )
            {
                const int32 priority = MatgoGameInternal::computeCapturePriority( _floor.getAt( _floor.findIndexById( floorCardId ) ) );
                if ( floorCardId == targetId )
                {
                    chosenId = floorCardId;
                    break;
                }
                if ( priority > bestPriority )
                {
                    bestPriority = priority;
                    chosenId     = floorCardId;
                }
            }
            captureFloorCard( player, chosenId );
            captureCard( player, card );
            return;
        }
        // 한 장이면 그것, 셋(뻑)이면 다 먹고 피를 뺏는다.
        for ( const uint16 floorCardId : listSameMonth )
        {
            captureFloorCard( player, floorCardId );
        }
        captureCard( player, card );
        if ( matchCount >= 3 )
        {
            _eventBuffer.push( MatgoEvent{ player, 0, card._id, MatgoEvent::Kind::PpeokEaten } );
            stealPi( player );
        }
    }

    void MatgoGame::captureCard( int32 player, const Card& card )
    {
        _listPlayer[static_cast<size_t>( player )]._captured.push( card );
        _eventBuffer.push( MatgoEvent{ player, 0, card._id, MatgoEvent::Kind::Captured } );
    }

    void MatgoGame::captureFloorCard( int32 player, uint16 cardId )
    {
        Card card;
        if ( _floor.takeById( cardId, card ) )
            captureCard( player, card );
    }

    void MatgoGame::stealPi( int32 player )
    {
        for ( int32 other = 0; other < getPlayerCount(); ++other )
        {
            if ( other == player )
                continue;
            CardPile& captured  = _listPlayer[static_cast<size_t>( other )]._captured;
            int32     bestIndex = -1;
            // 홑피를 먼저 — 없으면 쌍피.
            for ( int32 index = 0; index < captured.getCount(); ++index )
            {
                const HwatuCardInfo& info = HwatuDeck::getInfo( captured.getAt( index )._id );
                if ( info._kind != HwatuKind::Pi )
                    continue;
                if ( bestIndex < 0 || info._piValue < HwatuDeck::getInfo( captured.getAt( bestIndex )._id )._piValue )
                    bestIndex = index;
            }
            if ( bestIndex < 0 )
                continue;
            const Card stolen = captured.removeAt( bestIndex );
            _listPlayer[static_cast<size_t>( player )]._captured.push( stolen );
            _eventBuffer.push( MatgoEvent{ player, other, stolen._id, MatgoEvent::Kind::PiStolen } );
        }
    }

    bool MatgoGame::areHandsEmpty() const
    {
        for ( const MatgoPlayer& matgoPlayer : _listPlayer )
        {
            if ( matgoPlayer._hand.isEmpty() == false )
                return false;
        }
        return true;
    }

    void MatgoGame::finishTurn( int32 player )
    {
        if ( _floor.isEmpty() )
        {
            _eventBuffer.push( MatgoEvent{ player, 0, Card::kNoCard, MatgoEvent::Kind::Sweep } );
            stealPi( player );
        }
        const MatgoPlayer& matgoPlayer = _listPlayer[static_cast<size_t>( player )];
        const int32        score       = computePlayerScore( player )._total;
        const bool         bCanStop    = score >= _settings._goThreshold && score > matgoPlayer._scoreAtLastGo;
        if ( bCanStop )
        {
            if ( areHandsEmpty() )
            {
                finishGame( player ); // 마지막 패 — 고할 수 없으니 스톱
                return;
            }
            _phase = MatgoPhase::GoStopChoice;
            _eventBuffer.push( MatgoEvent{ player, score, Card::kNoCard, MatgoEvent::Kind::GoStopChoice } );
            return;
        }
        if ( areHandsEmpty() )
        {
            _phase  = MatgoPhase::Finished;
            _winner = -1;
            _eventBuffer.push( MatgoEvent{ -1, 0, Card::kNoCard, MatgoEvent::Kind::Nagari } );
            return;
        }
        _currentPlayer = ( player + 1 ) % getPlayerCount();
    }

    bool MatgoGame::declareGo( int32 player )
    {
        if ( _phase != MatgoPhase::GoStopChoice || player != _currentPlayer )
            return false;
        MatgoPlayer& matgoPlayer   = _listPlayer[static_cast<size_t>( player )];
        matgoPlayer._scoreAtLastGo = computePlayerScore( player )._total;
        ++matgoPlayer._goCount;
        _eventBuffer.push( MatgoEvent{ player, matgoPlayer._goCount, Card::kNoCard, MatgoEvent::Kind::Go } );
        _phase         = MatgoPhase::Play;
        _currentPlayer = ( player + 1 ) % getPlayerCount();
        return true;
    }

    bool MatgoGame::declareStop( int32 player )
    {
        if ( _phase != MatgoPhase::GoStopChoice || player != _currentPlayer )
            return false;
        finishGame( player );
        return true;
    }

    void MatgoGame::finishGame( int32 winner )
    {
        const HwatuScore winnerScore = computePlayerScore( winner );
        const int32      goCount     = _listPlayer[static_cast<size_t>( winner )]._goCount;
        int32            firstTotal  = 0;
        for ( int32 offset = 1; offset < getPlayerCount(); ++offset )
        {
            const int32       loser  = ( winner + offset ) % getPlayerCount();
            const MatgoPayout payout = computePayout( winnerScore, computePlayerScore( loser ), goCount, _settings );
            _listSettlement[static_cast<size_t>( loser )] -= payout._total;
            _listSettlement[static_cast<size_t>( winner )] += payout._total;
            if ( offset == 1 )
                firstTotal = payout._total;
        }
        _winner = winner;
        _phase  = MatgoPhase::Finished;
        _eventBuffer.push( MatgoEvent{ winner, firstTotal, Card::kNoCard, MatgoEvent::Kind::Stop } );
    }

    bool MatgoGame::applyAction( int32 player, const CardAction& action )
    {
        if ( action._kind > static_cast<uint8>( MatgoActionKind::Stop ) )
            return false;
        switch ( static_cast<MatgoActionKind>( action._kind ) )
        {
            case MatgoActionKind::Play:
                return playCard( player, action._cardId, action._targetId );
            case MatgoActionKind::Go:
                return declareGo( player );
            case MatgoActionKind::Stop:
                return declareStop( player );
        }
        return false;
    }

    HwatuScore MatgoGame::computeScore( const CardPile& captured, const MatgoSettings& settings )
    {
        HwatuScore score;
        bool       bRainGwang  = false;
        int32      godoriCount = 0;
        int32      arrRibbonCount[MatgoGameInternal::kRibbonCount]{};
        for ( const Card& card : captured.getCards() )
        {
            const HwatuCardInfo& info = HwatuDeck::getInfo( card._id );
            switch ( info._kind )
            {
                case HwatuKind::Gwang:
                {
                    ++score._gwangCount;
                    bRainGwang = bRainGwang || info._bRainGwang == SW_TRUE;
                    break;
                }
                case HwatuKind::Yeol:
                {
                    if ( info._bSeptemberYeol == SW_TRUE && settings._bSeptemberYeolAsDoublePi == SW_TRUE )
                    {
                        score._piCount += 2;
                        break;
                    }
                    ++score._yeolCount;
                    godoriCount += info._bGodori == SW_TRUE ? 1 : 0;
                    break;
                }
                case HwatuKind::Tti:
                {
                    ++score._ttiCount;
                    ++arrRibbonCount[static_cast<int32>( info._ribbon )];
                    break;
                }
                case HwatuKind::Pi:
                {
                    score._piCount += info._piValue;
                    break;
                }
            }
        }
        if ( score._gwangCount >= 5 )
            score._gwang = settings._gwangFiveScore;
        else if ( score._gwangCount == 4 )
            score._gwang = settings._gwangFourScore;
        else if ( score._gwangCount == 3 )
            score._gwang = bRainGwang ? settings._gwangRainThreeScore : settings._gwangThreeScore;
        score._godori = godoriCount >= 3 ? settings._godoriScore : 0;
        for ( int32 ribbon = static_cast<int32>( HwatuRibbon::Hong ); ribbon < MatgoGameInternal::kRibbonCount; ++ribbon )
        {
            score._dan += arrRibbonCount[ribbon] >= 3 ? settings._danScore : 0;
        }
        score._yeol  = MatgoGameInternal::computeCountScore( score._yeolCount, settings._yeolThreshold );
        score._tti   = MatgoGameInternal::computeCountScore( score._ttiCount, settings._ttiThreshold );
        score._pi    = MatgoGameInternal::computeCountScore( score._piCount, settings._piThreshold );
        score._total = score._gwang + score._godori + score._dan + score._yeol + score._tti + score._pi;
        return score;
    }

    MatgoPayout MatgoGame::computePayout( const HwatuScore& winner, const HwatuScore& loser, int32 goCount, const MatgoSettings& settings )
    {
        MatgoPayout payout;
        payout._baseScore    = winner._total + MathUtil::max( 0, goCount );
        payout._multiplier   = goCount >= 3 ? ( 1 << ( goCount - 2 ) ) : 1;
        const bool bPibak    = settings._bPibak == SW_TRUE && winner._pi > 0 && loser._piCount < settings._pibakBelow;
        const bool bGwangbak = settings._bGwangbak == SW_TRUE && winner._gwang > 0 && loser._gwangCount == 0;
        const bool bMeongbak = settings._bMeongbak == SW_TRUE && winner._yeolCount >= settings._meongbakYeolCount;
        payout._bPibak       = bPibak ? SW_TRUE : SW_FALSE;
        payout._bGwangbak    = bGwangbak ? SW_TRUE : SW_FALSE;
        payout._bMeongbak    = bMeongbak ? SW_TRUE : SW_FALSE;
        payout._multiplier *= ( bPibak ? 2 : 1 ) * ( bGwangbak ? 2 : 1 ) * ( bMeongbak ? 2 : 1 );
        payout._total = payout._baseScore * payout._multiplier;
        return payout;
    }

    void MatgoGame::drainEvents( vector<MatgoEvent>& outListEvent )
    {
        _eventBuffer.drainTo( outListEvent );
    }

    void MatgoGame::writeState( Archive& outArchive ) const
    {
        outArchive << static_cast<uint32>( _listPlayer.size() );
        for ( size_t player = 0; player < _listPlayer.size(); ++player )
        {
            const MatgoPlayer& entry = _listPlayer[player];
            entry._hand.writeState( outArchive );
            entry._captured.writeState( outArchive );
            outArchive << entry._goCount;
            outArchive << entry._scoreAtLastGo;
            outArchive << _listSettlement[player];
        }
        _floor.writeState( outArchive );
        _drawPile.writeState( outArchive );
        outArchive << _currentPlayer;
        outArchive << _winner;
        outArchive << static_cast<uint8>( _phase );
    }

    bool MatgoGame::readState( Archive& archive )
    {
        uint32 playerCount = 0;
        archive >> playerCount;
        if ( archive.isError() || playerCount != _listPlayer.size() )
            return false;
        // 사본에 읽고 끝까지 맞으면 바꾼다 — 설정은 사본이 그대로 든다.
        MatgoGame game = *this;
        for ( size_t player = 0; player < game._listPlayer.size(); ++player )
        {
            MatgoPlayer& entry = game._listPlayer[player];
            if ( entry._hand.readState( archive ) == false || entry._captured.readState( archive ) == false )
                return false;
            archive >> entry._goCount;
            archive >> entry._scoreAtLastGo;
            archive >> game._listSettlement[player];
        }
        if ( game._floor.readState( archive ) == false || game._drawPile.readState( archive ) == false )
            return false;
        uint8 phase = 0;
        archive >> game._currentPlayer;
        archive >> game._winner;
        archive >> phase;
        const int32 count         = static_cast<int32>( playerCount );
        const bool  bPlayerValid  = 0 <= game._currentPlayer && game._currentPlayer < count;
        const bool  bWinnerValid  = -1 <= game._winner && game._winner < count;
        const bool  bPhaseInRange = phase <= static_cast<uint8>( MatgoPhase::Finished );
        if ( archive.isError() || bPlayerValid == false || bWinnerValid == false || bPhaseInRange == false )
            return false;
        game._phase = static_cast<MatgoPhase>( phase );
        game._eventBuffer.clear();
        *this = std::move( game );
        return true;
    }
} // namespace sw

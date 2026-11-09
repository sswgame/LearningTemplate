#include "pch.h"

#include "GameFramework/Kits/Genre/Casual/CardGame/Poker/PokerTable.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Foundation/Constants.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Runtime/Foundation/GameData/ConfigRangeUtil.h"

#include <algorithm>
#include <limits>

namespace sw
{
    namespace
    {
        struct PokerTableInternal
        {
            static constexpr int32 kHoleCount  = 2;
            static constexpr int32 kBoardCount = 5;

            /** @brief 읽은 자리 값이 맞는지 봅니다. 칩은 음수가 아니고, 이번 스트리트에 낸 칩은 이번 판에 낸 칩에 들어 있으며, 폴드와 올인은 판에 든 자리만 합니다. */
            static bool isSeatValid( const PokerSeat& seat )
            {
                const bool bChipsValid = 0 <= seat._stack && 0 <= seat._committed && seat._committed <= seat._contributed && 0 <= seat._won;
                const bool bFlagsValid = seat._bInHand <= SW_TRUE && seat._bFolded <= SW_TRUE && seat._bAllIn <= SW_TRUE && seat._bActed <= SW_TRUE;
                return bChipsValid && bFlagsValid && ( seat._bInHand == SW_TRUE || ( seat._bFolded == SW_FALSE && seat._bAllIn == SW_FALSE ) );
            }

            /** @brief 읽은 더미와 홀 카드가 트럼프 52장 안에서 겹치지 않는지 봅니다. 진행 중인 판의 자리는 홀 카드 두 장을 다 가집니다. */
            [[nodiscard]] static bool areCardsValid( const vector<PokerSeat>& listSeat, const CardPile& deck, const CardPile& board, bool bHandRunning )
            {
                CardPile holes;
                for ( const PokerSeat& seat : listSeat )
                {
                    for ( const Card& card : seat._arrHole )
                    {
                        if ( card.isValid() )
                            holes.push( card );
                        else if ( bHandRunning && seat._bInHand == SW_TRUE )
                            return false;
                    }
                }
                return CardPile::arePilesValid( { &holes, &deck, &board }, &CardPile::isStandardCard );
            }

            static bool isSameSeatList( const vector<int32>& listLhsSeat, const vector<int32>& listRhsSeat )
            {
                if ( listLhsSeat.size() != listRhsSeat.size() )
                    return false;
                for ( size_t index = 0; index < listLhsSeat.size(); ++index )
                {
                    if ( listLhsSeat[index] != listRhsSeat[index] )
                        return false;
                }
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    void PokerPot::makePots( const vector<int32>& listContribution, const vector<uint8>& listFolded, vector<PokerPot>& outListPot )
    {
        outListPot.clear();
        vector<int32> listLevel;
        for ( const int32 contribution : listContribution )
        {
            if ( contribution > 0 && VectorUtil::contains( listLevel, contribution ) == false )
                listLevel.push_back( contribution );
        }
        VectorUtil::sort( listLevel );

        int32 previousLevel = 0;
        for ( const int32 level : listLevel )
        {
            PokerPot pot;
            for ( size_t seat = 0; seat < listContribution.size(); ++seat )
            {
                const int32 contribution = listContribution[seat];
                pot._amount += MathUtil::min( contribution, level ) - MathUtil::min( contribution, previousLevel );
                const bool bFolded = seat < listFolded.size() && listFolded[seat] != SW_FALSE;
                if ( bFolded == false && contribution >= level )
                    pot._listEligibleSeat.push_back( static_cast<int32>( seat ) );
            }
            previousLevel = level;
            if ( pot._amount <= 0 )
                continue;
            // 아무도 못 가져가는 몫(폴드한 사람만 낸 꼭대기)과, 가져갈 사람이 같은 이웃 팟은 아래 팟에 붙인다.
            const bool bMerge = outListPot.empty() == false &&
                                ( pot._listEligibleSeat.empty() || PokerTableInternal::isSameSeatList( pot._listEligibleSeat, outListPot.back()._listEligibleSeat ) );
            if ( bMerge )
                outListPot.back()._amount += pot._amount;
            else
                outListPot.push_back( pot );
        }
    }

    void PokerPot::distributePots( const vector<PokerPot>& listPot, const vector<uint32>& listScore, int32 firstSeat, vector<int32>& outListWon )
    {
        const int32 seatCount = static_cast<int32>( listScore.size() );
        outListWon.clear();
        outListWon.resize( listScore.size(), 0 );
        if ( seatCount == 0 )
            return;
        for ( const PokerPot& pot : listPot )
        {
            if ( pot._listEligibleSeat.empty() )
                continue;
            uint32 bestScore = 0;
            for ( const int32 seat : pot._listEligibleSeat )
            {
                bestScore = MathUtil::max( bestScore, listScore[static_cast<size_t>( seat )] );
            }
            // 이긴 자리를 버튼 왼쪽부터의 순서로 정렬한다. 나머지 칩이 그 순서로 돌아간다.
            vector<int32> listWinner;
            for ( int32 offset = 0; offset < seatCount; ++offset )
            {
                const int32 seat      = ( firstSeat + offset ) % seatCount;
                const bool  bEligible = VectorUtil::contains( pot._listEligibleSeat, seat );
                if ( bEligible && listScore[static_cast<size_t>( seat )] == bestScore )
                    listWinner.push_back( seat );
            }
            const int32 winnerCount = static_cast<int32>( listWinner.size() );
            const int32 share       = pot._amount / winnerCount;
            const int32 remainder   = pot._amount % winnerCount;
            for ( int32 index = 0; index < winnerCount; ++index )
            {
                outListWon[static_cast<size_t>( listWinner[static_cast<size_t>( index )] )] += share + ( index < remainder ? 1 : 0 );
            }
        }
    }

    PokerTable::PokerTable()
        : _listSeat{}
        , _listLastPot{}
        , _eventBuffer{}
        , _deck{}
        , _board{}
        , _settings{}
        , _button{ constant::invalid_index::kInt32 }
        , _currentSeat{ constant::invalid_index::kInt32 }
        , _currentBet{ 0 }
        , _lastRaiseSize{ 0 }
        , _street{ PokerStreet::HandOver }
    {
    }

    bool PokerTable::initialize( const PokerSettings& settings, const vector<int32>& listStack )
    {
        const string rangeError = ConfigRangeUtil::describeRangeErrors( settings );
        if ( rangeError.empty() == false )
        {
            SW_LOG_ERROR_ID( "PokerTable.PokerTableRefusedSettings", "PokerTable: refused settings - %#", rangeError.c_str() );
            return false;
        }
        for ( const int32 stack : listStack )
        {
            if ( stack < 0 )
            {
                SW_LOG_ERROR_ID( "PokerTable.PokerTableRefusedNegativeStarting", "PokerTable: refused a negative starting stack %#", stack );
                return false;
            }
        }
        _settings = settings;
        _listSeat.clear();
        _listSeat.resize( listStack.size() );
        for ( size_t seat = 0; seat < listStack.size(); ++seat )
        {
            _listSeat[seat]._stack = listStack[seat];
        }
        _button      = static_cast<int32>( listStack.size() ) - 1;
        _currentSeat = constant::invalid_index::kInt32;
        _street      = PokerStreet::HandOver;
        _eventBuffer.clear();
        _listLastPot.clear();
        return true;
    }

    bool PokerTable::startHand( GameRandom& random )
    {
        CardPile::makeStandardDeck( _deck );
        _deck.shuffle( random );
        return openHand();
    }

    bool PokerTable::startHandWithDeck( const CardPile& deck )
    {
        _deck = deck;
        return openHand();
    }

    bool PokerTable::canAct( int32 seat ) const
    {
        const PokerSeat& pokerSeat = _listSeat[static_cast<size_t>( seat )];
        return pokerSeat._bInHand == SW_TRUE && pokerSeat._bFolded == SW_FALSE && pokerSeat._bAllIn == SW_FALSE;
    }

    bool PokerTable::needsAction( int32 seat ) const
    {
        const PokerSeat& pokerSeat = _listSeat[static_cast<size_t>( seat )];
        return canAct( seat ) && ( pokerSeat._bActed == SW_FALSE || pokerSeat._committed < _currentBet );
    }

    optional<int32> PokerTable::findNextSeat( int32 fromSeat, bool bCanActOnly ) const
    {
        const int32 seatCount = getSeatCount();
        for ( int32 offset = 1; offset <= seatCount; ++offset )
        {
            const int32      seat      = ( ( fromSeat + offset ) % seatCount + seatCount ) % seatCount;
            const PokerSeat& pokerSeat = _listSeat[static_cast<size_t>( seat )];
            if ( pokerSeat._bInHand == SW_FALSE || pokerSeat._bFolded == SW_TRUE )
                continue;
            if ( bCanActOnly == false || canAct( seat ) )
                return seat;
        }
        return nullopt;
    }

    void PokerTable::commitChips( int32 seat, int32 amount )
    {
        PokerSeat&  pokerSeat = _listSeat[static_cast<size_t>( seat )];
        const int32 paid      = MathUtil::clamp( amount, 0, pokerSeat._stack );
        pokerSeat._stack -= paid;
        pokerSeat._committed += paid;
        pokerSeat._contributed += paid;
        if ( pokerSeat._stack == 0 )
            pokerSeat._bAllIn = SW_TRUE;
    }

    bool PokerTable::openHand()
    {
        int32 playerCount = 0;
        for ( PokerSeat& pokerSeat : _listSeat )
        {
            pokerSeat._bInHand     = pokerSeat._stack > 0 ? SW_TRUE : SW_FALSE;
            pokerSeat._bFolded     = SW_FALSE;
            pokerSeat._bAllIn      = SW_FALSE;
            pokerSeat._bActed      = SW_FALSE;
            pokerSeat._committed   = 0;
            pokerSeat._contributed = 0;
            pokerSeat._won         = 0;
            pokerSeat._arrHole[0]  = Card{};
            pokerSeat._arrHole[1]  = Card{};
            if ( pokerSeat._bInHand == SW_TRUE )
                ++playerCount;
        }
        if ( playerCount < 2 )
        {
            _street = PokerStreet::HandOver;
            return false;
        }
        _board.clear();
        _listLastPot.clear();
        // 아래 다음 자리 찾기는 핸드에 둘 이상이 있을 때다(위에서 셌다) — 비면 결함이라 value 가 단언한다.
        _button                    = findNextSeat( _button, false ).value();
        const int32 smallBlindSeat = playerCount == 2 ? _button : findNextSeat( _button, false ).value();
        const int32 bigBlindSeat   = findNextSeat( smallBlindSeat, false ).value();
        commitChips( smallBlindSeat, _settings._smallBlind );
        _eventBuffer.push( PokerEvent{ smallBlindSeat, _listSeat[static_cast<size_t>( smallBlindSeat )]._committed, PokerEvent::Kind::Blind } );
        commitChips( bigBlindSeat, _settings._bigBlind );
        _eventBuffer.push( PokerEvent{ bigBlindSeat, _listSeat[static_cast<size_t>( bigBlindSeat )]._committed, PokerEvent::Kind::Blind } );
        _currentBet    = MathUtil::max( _listSeat[static_cast<size_t>( smallBlindSeat )]._committed, _listSeat[static_cast<size_t>( bigBlindSeat )]._committed );
        _lastRaiseSize = _settings._bigBlind;

        // 스몰 블라인드부터 한 장씩 두 바퀴를 나눈다.
        for ( int32 holeIndex = 0; holeIndex < PokerTableInternal::kHoleCount; ++holeIndex )
        {
            int32 seat = smallBlindSeat;
            for ( int32 dealt = 0; dealt < playerCount; ++dealt )
            {
                Card card;
                if ( _deck.draw( card ) == false )
                    return false;
                _listSeat[static_cast<size_t>( seat )]._arrHole[holeIndex] = card;
                seat                                                       = findNextSeat( seat, false ).value();
            }
        }
        _street      = PokerStreet::Preflop;
        _currentSeat = constant::invalid_index::kInt32;
        afterAction( bigBlindSeat );
        return true;
    }

    bool PokerTable::act( int32 seat, PokerActionKind kind, int32 amount )
    {
        const bool bActionNotAllowed = _street == PokerStreet::HandOver || seat != _currentSeat || seat < 0 || canAct( seat ) == false;
        if ( bActionNotAllowed )
            return false;
        PokerSeat&  pokerSeat = _listSeat[static_cast<size_t>( seat )];
        const int32 toCall    = _currentBet - pokerSeat._committed;
        int32       raiseTo   = constant::invalid_index::kInt32;
        switch ( kind )
        {
            case PokerActionKind::Fold:
            {
                pokerSeat._bFolded = SW_TRUE;
                _eventBuffer.push( PokerEvent{ seat, 0, PokerEvent::Kind::Fold } );
                break;
            }
            case PokerActionKind::Check:
            {
                if ( toCall > 0 )
                    return false;
                _eventBuffer.push( PokerEvent{ seat, 0, PokerEvent::Kind::Check } );
                break;
            }
            case PokerActionKind::Call:
            {
                if ( toCall <= 0 )
                {
                    _eventBuffer.push( PokerEvent{ seat, 0, PokerEvent::Kind::Check } );
                    break;
                }
                const int32 paid = MathUtil::min( toCall, pokerSeat._stack );
                commitChips( seat, paid );
                _eventBuffer.push( PokerEvent{ seat, paid, PokerEvent::Kind::Call } );
                break;
            }
            case PokerActionKind::Raise:
            {
                const int32 needed = amount - pokerSeat._committed;
                if ( amount <= _currentBet || needed > pokerSeat._stack )
                    return false;
                const bool bAllIn = needed == pokerSeat._stack;
                if ( bAllIn == false && amount < getMinRaiseTo() )
                    return false; // 최소 증액에 미달한다(올인만 예외이다)
                raiseTo = amount;
                break;
            }
            case PokerActionKind::AllIn:
            {
                if ( pokerSeat._stack <= 0 )
                    return false;
                raiseTo = pokerSeat._committed + pokerSeat._stack;
                break;
            }
        }

        if ( raiseTo >= 0 )
        {
            commitChips( seat, raiseTo - pokerSeat._committed );
            const bool bAllIn = pokerSeat._bAllIn == SW_TRUE;
            if ( raiseTo > _currentBet )
            {
                const int32 increase = raiseTo - _currentBet;
                if ( increase >= _lastRaiseSize )
                    _lastRaiseSize = increase; // 모자란 올인은 최소 증액을 바꾸지 않는다
                _currentBet = raiseTo;
                for ( PokerSeat& other : _listSeat )
                {
                    other._bActed = SW_FALSE;
                }
            }
            _eventBuffer.push( PokerEvent{ seat, raiseTo, bAllIn ? PokerEvent::Kind::AllIn : PokerEvent::Kind::Raise } );
        }
        pokerSeat._bActed = SW_TRUE;
        afterAction( seat );
        return true;
    }

    bool PokerTable::applyAction( int32 seat, const CardAction& action )
    {
        if ( action._kind > static_cast<uint8>( PokerActionKind::AllIn ) )
            return false;
        return act( seat, static_cast<PokerActionKind>( action._kind ), action._amount );
    }

    void PokerTable::afterAction( int32 seat )
    {
        int32 liveCount = 0;
        int32 liveSeat  = constant::invalid_index::kInt32;
        for ( int32 index = 0; index < getSeatCount(); ++index )
        {
            const PokerSeat& pokerSeat = _listSeat[static_cast<size_t>( index )];
            if ( pokerSeat._bInHand == SW_TRUE && pokerSeat._bFolded == SW_FALSE )
            {
                ++liveCount;
                liveSeat = index;
            }
        }
        if ( liveCount == 1 )
        {
            finishUncontested( liveSeat );
            return;
        }
        for ( int32 offset = 1; offset <= getSeatCount(); ++offset )
        {
            const int32 nextSeat = ( seat + offset ) % getSeatCount();
            if ( needsAction( nextSeat ) )
            {
                _currentSeat = nextSeat;
                return;
            }
        }
        // 스트리트가 끝났다. 행동할 수 있는 사람이 둘 미만이면 보드를 끝까지 깐다.
        while ( _street != PokerStreet::River )
        {
            beginStreet( static_cast<PokerStreet>( static_cast<uint8>( _street ) + 1 ) );
            int32 canActCount = 0;
            for ( int32 index = 0; index < getSeatCount(); ++index )
            {
                canActCount += canAct( index ) ? 1 : 0;
            }
            if ( canActCount >= 2 )
            {
                _currentSeat = findNextSeat( _button, true ).value(); // 행동할 수 있는 자리가 둘 이상이다(위에서 셌다)
                return;
            }
        }
        finishShowdown();
    }

    void PokerTable::beginStreet( PokerStreet street )
    {
        _street = street;
        for ( PokerSeat& pokerSeat : _listSeat )
        {
            pokerSeat._committed = 0;
            pokerSeat._bActed    = SW_FALSE;
        }
        _currentBet    = 0;
        _lastRaiseSize = _settings._bigBlind;
        dealBoard( street == PokerStreet::Flop ? 3 : 1 );
        _eventBuffer.push( PokerEvent{ constant::invalid_index::kInt32, static_cast<int32>( street ), PokerEvent::Kind::Street } );
    }

    void PokerTable::dealBoard( int32 count )
    {
        for ( int32 index = 0; index < count && _board.getCount() < PokerTableInternal::kBoardCount; ++index )
        {
            Card card;
            if ( _deck.draw( card ) )
                _board.push( card );
        }
    }

    int32 PokerTable::computePotTotal() const
    {
        int32 total = 0;
        for ( const PokerSeat& pokerSeat : _listSeat )
        {
            total += pokerSeat._contributed;
        }
        return total;
    }

    void PokerTable::finishShowdown()
    {
        vector<int32>  listContribution;
        vector<uint8>  listFolded;
        vector<uint32> listScore;
        for ( int32 seat = 0; seat < getSeatCount(); ++seat )
        {
            const PokerSeat& pokerSeat = _listSeat[static_cast<size_t>( seat )];
            const bool       bLive     = pokerSeat._bInHand == SW_TRUE && pokerSeat._bFolded == SW_FALSE;
            listContribution.push_back( pokerSeat._contributed );
            listFolded.push_back( bLive ? SW_FALSE : SW_TRUE );
            uint32 score = 0;
            if ( bLive )
            {
                Card  arrCard[PokerTableInternal::kHoleCount + PokerTableInternal::kBoardCount]{};
                int32 cardCount      = 0;
                arrCard[cardCount++] = pokerSeat._arrHole[0];
                arrCard[cardCount++] = pokerSeat._arrHole[1];
                for ( int32 index = 0; index < _board.getCount(); ++index )
                {
                    arrCard[cardCount++] = _board.getAt( index );
                }
                score = PokerHandValue::evaluateBest( arrCard, cardCount )._score;
            }
            listScore.push_back( score );
        }
        PokerPot::makePots( listContribution, listFolded, _listLastPot );
        vector<int32> listWon;
        PokerPot::distributePots( _listLastPot, listScore, findNextSeat( _button, false ).value(), listWon ); // 쇼다운에는 핸드에 남은 자리가 있다
        for ( int32 seat = 0; seat < getSeatCount(); ++seat )
        {
            PokerSeat& pokerSeat = _listSeat[static_cast<size_t>( seat )];
            pokerSeat._won       = listWon[static_cast<size_t>( seat )];
            pokerSeat._stack += pokerSeat._won;
            if ( pokerSeat._won > 0 )
                _eventBuffer.push( PokerEvent{ seat, pokerSeat._won, PokerEvent::Kind::Win } );
        }
        _street      = PokerStreet::HandOver;
        _currentSeat = constant::invalid_index::kInt32;
    }

    void PokerTable::finishUncontested( int32 winnerSeat )
    {
        vector<int32> listContribution;
        vector<uint8> listFolded;
        for ( int32 seat = 0; seat < getSeatCount(); ++seat )
        {
            listContribution.push_back( _listSeat[static_cast<size_t>( seat )]._contributed );
            listFolded.push_back( seat == winnerSeat ? SW_FALSE : SW_TRUE );
        }
        PokerPot::makePots( listContribution, listFolded, _listLastPot );
        PokerSeat& winner = _listSeat[static_cast<size_t>( winnerSeat )];
        winner._won       = computePotTotal();
        winner._stack += winner._won;
        _eventBuffer.push( PokerEvent{ winnerSeat, winner._won, PokerEvent::Kind::Win } );
        _street      = PokerStreet::HandOver;
        _currentSeat = constant::invalid_index::kInt32;
    }

    void PokerTable::pollEvents( vector<PokerEvent>& outListEvent )
    {
        _eventBuffer.moveAllTo( outListEvent );
    }

    void PokerTable::serializeState( StateIO& io )
    {
        io.expect( static_cast<uint32>( _listSeat.size() ) );
        if ( io.isOk() == false )
            return;
        const int32 seatCount = static_cast<int32>( _listSeat.size() );
        int64       chipTotal = 0;
        for ( PokerSeat& seat : _listSeat )
        {
            for ( Card& card : seat._arrHole )
            {
                io.value( card._id );
                io.value( card._suit );
                io.value( card._rank );
            }
            io.value( seat._stack );
            io.value( seat._committed );
            io.value( seat._contributed );
            io.value( seat._won );
            io.value( seat._bInHand );
            io.value( seat._bFolded );
            io.value( seat._bAllIn );
            io.value( seat._bActed );
            if ( io.isReading() && io.check( PokerTableInternal::isSeatValid( seat ) ) == false )
                return;
            chipTotal += static_cast<int64>( seat._stack ) + seat._contributed + seat._won;
        }
        // 칩을 더하는 곳(`computePotTotal`, 레이즈 투)이 넘치지 않게 판 전체의 칩이 int32 안이어야 한다.
        io.check( chipTotal <= std::numeric_limits<int32>::max() );

        // 팟마다 자리 수(4) + 금액(4). 팟은 낸 금액의 단계마다 하나라 자리 수를 넘지 않는다(`makePots`).
        io.list( _listLastPot, 8, [&]( PokerPot& pot )
        {
            io.list( pot._listEligibleSeat, 4, [&]( int32& seat )
            {
                io.value( seat );
                io.check( 0 <= seat && seat < seatCount );
            } );
            io.value( pot._amount );
            // 겹친 자리는 이긴 수를 부풀려 몫을 나누어 준다. 팟은 판의 칩을 넘지 못한다.
            if ( io.isReading() )
            {
                io.check( static_cast<int64>( pot._listEligibleSeat.size() ) <= seatCount && 0 <= pot._amount && pot._amount <= chipTotal && VectorUtil::findDuplicate( pot._listEligibleSeat ) == nullptr );
            }
        } );
        io.check( static_cast<int64>( _listLastPot.size() ) <= seatCount );
        io.value( _deck );
        io.value( _board );
        io.value( _button );
        io.value( _currentSeat );
        io.value( _currentBet );
        io.value( _lastRaiseSize );
        io.enumValue( _street, PokerStreet::HandOver );
        if ( io.isWriting() || io.isOk() == false )
            return;
        io.check( constant::invalid_index::kInt32 <= _button && _button < seatCount && constant::invalid_index::kInt32 <= _currentSeat && _currentSeat < seatCount );
        io.check( 0 <= _currentBet && _currentBet <= chipTotal && 0 <= _lastRaiseSize && static_cast<int64>( _currentBet ) + _lastRaiseSize <= std::numeric_limits<int32>::max() );
        // 쇼다운은 홀 둘과 보드를 고정 크기 배열에 담는다(`finishShowdown`) — 보드가 다섯을 넘으면 스택을 넘어 쓴다.
        if ( io.check( _board.getCount() <= PokerTableInternal::kBoardCount ) == false )
            return;
        // 진행 중인 판은 차례인 자리가 있어야 이어진다. 끝난 판에는 차례가 없다.
        const bool bHandRunning = _street != PokerStreet::HandOver;
        io.check( bHandRunning ? _currentSeat >= 0 && canAct( _currentSeat ) : _currentSeat < 0 );
        io.check( PokerTableInternal::areCardsValid( _listSeat, _deck, _board, bHandRunning ) );
        _eventBuffer.clear();
    }

} // namespace sw

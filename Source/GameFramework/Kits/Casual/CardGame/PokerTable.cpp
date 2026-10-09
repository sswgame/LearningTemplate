#include "pch.h"

#include "GameFramework/Kits/Casual/CardGame/PokerTable.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Foundation/Utility/StateArchiveUtil.h"

#include <algorithm>

namespace sw
{
    namespace
    {
        struct PokerTableInternal
        {
            static constexpr int32 kHoleCount  = 2;
            static constexpr int32 kBoardCount = 5;

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
    void PokerPotUtil::makePots( const vector<int32>& listContribution, const vector<uint8>& listFolded, vector<PokerPot>& outListPot )
    {
        outListPot.clear();
        vector<int32> listLevel;
        for ( const int32 contribution : listContribution )
        {
            if ( contribution > 0 && std::find( listLevel.begin(), listLevel.end(), contribution ) == listLevel.end() )
                listLevel.push_back( contribution );
        }
        std::sort( listLevel.begin(), listLevel.end() );

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
            // 아무도 못 가져가는 몫(폴드한 사람만 낸 꼭대기)과 가져갈 사람이 같은 이웃 팟은 아래 팟에 붙인다.
            const bool bMerge = outListPot.empty() == false &&
                                ( pot._listEligibleSeat.empty() || PokerTableInternal::isSameSeatList( pot._listEligibleSeat, outListPot.back()._listEligibleSeat ) );
            if ( bMerge )
                outListPot.back()._amount += pot._amount;
            else
                outListPot.push_back( pot );
        }
    }

    void PokerPotUtil::distributePots( const vector<PokerPot>& listPot, const vector<uint32>& listScore, int32 firstSeat, vector<int32>& outListWon )
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
            // 이긴 자리를 버튼 왼쪽부터의 순서로 — 나머지 칩이 그 순서로 간다.
            vector<int32> listWinner;
            for ( int32 offset = 0; offset < seatCount; ++offset )
            {
                const int32 seat      = ( firstSeat + offset ) % seatCount;
                const bool  bEligible = std::find( pot._listEligibleSeat.begin(), pot._listEligibleSeat.end(), seat ) != pot._listEligibleSeat.end();
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
        , _button{ -1 }
        , _currentSeat{ -1 }
        , _currentBet{ 0 }
        , _lastRaiseSize{ 0 }
        , _street{ PokerStreet::HandOver }
    {
    }

    void PokerTable::initialize( const PokerSettings& settings, const vector<int32>& listStack )
    {
        _settings = settings;
        _listSeat.clear();
        _listSeat.resize( listStack.size() );
        for ( size_t seat = 0; seat < listStack.size(); ++seat )
        {
            _listSeat[seat]._stack = listStack[seat];
        }
        _button      = static_cast<int32>( listStack.size() ) - 1;
        _currentSeat = -1;
        _street      = PokerStreet::HandOver;
        _eventBuffer.clear();
        _listLastPot.clear();
    }

    bool PokerTable::startHand( GameRandom& random )
    {
        CardDeckUtil::makeStandardDeck( _deck );
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

    int32 PokerTable::findNextSeat( int32 fromSeat, bool bCanActOnly ) const
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
        return -1;
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
        _button                    = findNextSeat( _button, false );
        const int32 smallBlindSeat = playerCount == 2 ? _button : findNextSeat( _button, false );
        const int32 bigBlindSeat   = findNextSeat( smallBlindSeat, false );
        commitChips( smallBlindSeat, _settings._smallBlind );
        _eventBuffer.push( PokerEvent{ smallBlindSeat, _listSeat[static_cast<size_t>( smallBlindSeat )]._committed, PokerEvent::Kind::Blind } );
        commitChips( bigBlindSeat, _settings._bigBlind );
        _eventBuffer.push( PokerEvent{ bigBlindSeat, _listSeat[static_cast<size_t>( bigBlindSeat )]._committed, PokerEvent::Kind::Blind } );
        _currentBet    = MathUtil::max( _listSeat[static_cast<size_t>( smallBlindSeat )]._committed, _listSeat[static_cast<size_t>( bigBlindSeat )]._committed );
        _lastRaiseSize = _settings._bigBlind;

        // 스몰 블라인드부터 한 장씩 두 바퀴.
        for ( int32 holeIndex = 0; holeIndex < PokerTableInternal::kHoleCount; ++holeIndex )
        {
            int32 seat = smallBlindSeat;
            for ( int32 dealt = 0; dealt < playerCount; ++dealt )
            {
                Card card;
                if ( _deck.draw( card ) == false )
                    return false;
                _listSeat[static_cast<size_t>( seat )]._arrHole[holeIndex] = card;
                seat                                                       = findNextSeat( seat, false );
            }
        }
        _street      = PokerStreet::Preflop;
        _currentSeat = -1;
        afterAction( bigBlindSeat );
        return true;
    }

    bool PokerTable::act( int32 seat, PokerActionKind kind, int32 amount )
    {
        if ( _street == PokerStreet::HandOver || seat != _currentSeat || seat < 0 || canAct( seat ) == false )
            return false;
        PokerSeat&  pokerSeat = _listSeat[static_cast<size_t>( seat )];
        const int32 toCall    = _currentBet - pokerSeat._committed;
        int32       raiseTo   = -1;
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
                    return false; // 최소 증액 미달 — 올인만 예외
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
        int32 liveSeat  = -1;
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
                _currentSeat = findNextSeat( _button, true );
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
        _eventBuffer.push( PokerEvent{ -1, static_cast<int32>( street ), PokerEvent::Kind::Street } );
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
                score = PokerHandEvaluator::evaluateBest( arrCard, cardCount )._score;
            }
            listScore.push_back( score );
        }
        PokerPotUtil::makePots( listContribution, listFolded, _listLastPot );
        vector<int32> listWon;
        PokerPotUtil::distributePots( _listLastPot, listScore, findNextSeat( _button, false ), listWon );
        for ( int32 seat = 0; seat < getSeatCount(); ++seat )
        {
            PokerSeat& pokerSeat = _listSeat[static_cast<size_t>( seat )];
            pokerSeat._won       = listWon[static_cast<size_t>( seat )];
            pokerSeat._stack += pokerSeat._won;
            if ( pokerSeat._won > 0 )
                _eventBuffer.push( PokerEvent{ seat, pokerSeat._won, PokerEvent::Kind::Win } );
        }
        _street      = PokerStreet::HandOver;
        _currentSeat = -1;
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
        PokerPotUtil::makePots( listContribution, listFolded, _listLastPot );
        PokerSeat& winner = _listSeat[static_cast<size_t>( winnerSeat )];
        winner._won       = computePotTotal();
        winner._stack += winner._won;
        _eventBuffer.push( PokerEvent{ winnerSeat, winner._won, PokerEvent::Kind::Win } );
        _street      = PokerStreet::HandOver;
        _currentSeat = -1;
    }

    void PokerTable::drainEvents( vector<PokerEvent>& outListEvent )
    {
        _eventBuffer.drainTo( outListEvent );
    }

    void PokerTable::writeState( Archive& outArchive ) const
    {
        outArchive << static_cast<uint32>( _listSeat.size() );
        for ( const PokerSeat& seat : _listSeat )
        {
            for ( const Card& card : seat._arrHole )
            {
                outArchive << card._id;
                outArchive << card._suit;
                outArchive << card._rank;
            }
            outArchive << seat._stack;
            outArchive << seat._committed;
            outArchive << seat._contributed;
            outArchive << seat._won;
            outArchive << seat._bInHand;
            outArchive << seat._bFolded;
            outArchive << seat._bAllIn;
            outArchive << seat._bActed;
        }
        outArchive << static_cast<uint32>( _listLastPot.size() );
        for ( const PokerPot& pot : _listLastPot )
        {
            outArchive << static_cast<uint32>( pot._listEligibleSeat.size() );
            for ( const int32 seat : pot._listEligibleSeat )
            {
                outArchive << seat;
            }
            outArchive << pot._amount;
        }
        _deck.writeState( outArchive );
        _board.writeState( outArchive );
        outArchive << _button;
        outArchive << _currentSeat;
        outArchive << _currentBet;
        outArchive << _lastRaiseSize;
        outArchive << static_cast<uint8>( _street );
    }

    bool PokerTable::readState( Archive& archive )
    {
        uint32 seatCount = 0;
        archive >> seatCount;
        if ( archive.isError() || seatCount != _listSeat.size() )
            return false;
        // 사본에 읽고 끝까지 맞으면 바꾼다 — 설정은 사본이 그대로 든다.
        PokerTable  table = *this;
        const int32 count = static_cast<int32>( seatCount );
        for ( PokerSeat& seat : table._listSeat )
        {
            for ( Card& card : seat._arrHole )
            {
                archive >> card._id;
                archive >> card._suit;
                archive >> card._rank;
            }
            archive >> seat._stack;
            archive >> seat._committed;
            archive >> seat._contributed;
            archive >> seat._won;
            archive >> seat._bInHand;
            archive >> seat._bFolded;
            archive >> seat._bAllIn;
            archive >> seat._bActed;
            const bool bFlagsValid = seat._bInHand <= SW_TRUE && seat._bFolded <= SW_TRUE && seat._bAllIn <= SW_TRUE && seat._bActed <= SW_TRUE;
            if ( archive.isError() || bFlagsValid == false )
                return false;
        }

        uint32 potCount = 0;
        // 팟마다 자리 수(4) + 금액(4)
        if ( StateArchiveUtil::readCount( archive, 8, potCount ) == false )
            return false;
        table._listLastPot.assign( potCount, PokerPot{} );
        for ( PokerPot& pot : table._listLastPot )
        {
            uint32 eligibleCount = 0;
            if ( StateArchiveUtil::readCount( archive, 4, eligibleCount ) == false )
                return false;
            pot._listEligibleSeat.assign( eligibleCount, -1 );
            for ( int32& seat : pot._listEligibleSeat )
            {
                archive >> seat;
                if ( seat < 0 || count <= seat )
                    return false;
            }
            archive >> pot._amount;
        }
        if ( table._deck.readState( archive ) == false || table._board.readState( archive ) == false )
            return false;
        uint8 street = 0;
        archive >> table._button;
        archive >> table._currentSeat;
        archive >> table._currentBet;
        archive >> table._lastRaiseSize;
        archive >> street;
        const bool bButtonValid = -1 <= table._button && table._button < count;
        const bool bSeatValid   = -1 <= table._currentSeat && table._currentSeat < count;
        if ( archive.isError() || bButtonValid == false || bSeatValid == false || street > static_cast<uint8>( PokerStreet::HandOver ) )
            return false;
        table._street = static_cast<PokerStreet>( street );
        table._eventBuffer.clear();
        *this = std::move( table );
        return true;
    }
} // namespace sw

#include "pch.h"

#include "GameFramework/Kits/Casual/CardGame/KlondikeGame.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    KlondikeGame::KlondikeGame()
        : _listHistory{}
        , _state{}
        , _settings{}
    {
    }

    void KlondikeGame::initialize( const KlondikeSettings& settings, uint32 seed )
    {
        GameRandom random( seed );
        CardPile   deck;
        CardDeckUtil::makeStandardDeck( deck );
        deck.shuffle( random );
        initializeWithDeck( settings, deck );
    }

    void KlondikeGame::initializeWithDeck( const KlondikeSettings& settings, const CardPile& deck )
    {
        KlondikeState state;
        CardPile      remaining = deck;
        for ( int32 row = 0; row < KlondikeState::kColumnCount; ++row )
        {
            for ( int32 column = row; column < KlondikeState::kColumnCount; ++column )
                (void)remaining.drawInto( state._arrTableau[column], 1 );
        }
        for ( int32 column = 0; column < KlondikeState::kColumnCount; ++column )
            state._arrFaceDownCount[column] = MathUtil::max( 0, state._arrTableau[column].getCount() - 1 );
        state._stock = remaining;
        initializeFromState( settings, state );
    }

    void KlondikeGame::initializeFromState( const KlondikeSettings& settings, const KlondikeState& state )
    {
        _settings            = settings;
        _settings._drawCount = MathUtil::max( 1, settings._drawCount );
        _state               = state;
        _listHistory.clear();
    }

    bool KlondikeGame::isColumnValid( int32 column ) const
    {
        return 0 <= column && column < KlondikeState::kColumnCount;
    }

    void KlondikeGame::pushHistory()
    {
        _listHistory.push_back( _state );
        ++_state._moveCount;
    }

    void KlondikeGame::revealColumnTop( int32 column )
    {
        const int32 count = _state._arrTableau[column].getCount();
        if ( count > 0 && _state._arrFaceDownCount[column] >= count )
            _state._arrFaceDownCount[column] = count - 1;
    }

    void KlondikeGame::moveToFoundation( const Card& card )
    {
        _state._arrFoundation[card._suit].push( card );
    }

    bool KlondikeGame::canStackOnTableau( const Card& card, const CardPile& column )
    {
        if ( column.isEmpty() )
            return card._rank == StandardRank::kKing;
        const Card top = column.getTop();
        return CardDeckUtil::isRedSuit( top._suit ) != CardDeckUtil::isRedSuit( card._suit ) && card._rank + 1 == top._rank;
    }

    bool KlondikeGame::canPlaceOnFoundation( const Card& card ) const
    {
        if ( card._suit >= KlondikeState::kFoundationCount )
            return false;
        return _state._arrFoundation[card._suit].getCount() + 1 == card._rank;
    }

    bool KlondikeGame::drawStock()
    {
        if ( _state._stock.isEmpty() )
        {
            const bool bRecycleLeft = _settings._maxRecycle < 0 || _state._recycleCount < _settings._maxRecycle;
            if ( _state._waste.isEmpty() || bRecycleLeft == false )
                return false;
            pushHistory();
            // 웨이스트를 뒤집어 스톡으로 — 먼저 나왔던 패가 다시 먼저 나온다.
            (void)_state._waste.drawInto( _state._stock, _state._waste.getCount() );
            ++_state._recycleCount;
            return true;
        }
        pushHistory();
        (void)_state._stock.drawInto( _state._waste, _settings._drawCount );
        return true;
    }

    bool KlondikeGame::moveWasteToTableau( int32 column )
    {
        if ( isColumnValid( column ) == false || _state._waste.isEmpty() || canStackOnTableau( _state._waste.getTop(), _state._arrTableau[column] ) == false )
            return false;
        pushHistory();
        (void)_state._waste.drawInto( _state._arrTableau[column], 1 );
        return true;
    }

    bool KlondikeGame::moveWasteToFoundation()
    {
        if ( _state._waste.isEmpty() || canPlaceOnFoundation( _state._waste.getTop() ) == false )
            return false;
        pushHistory();
        Card card;
        (void)_state._waste.draw( card );
        moveToFoundation( card );
        return true;
    }

    bool KlondikeGame::moveTableauToFoundation( int32 column )
    {
        if ( isColumnValid( column ) == false || _state._arrTableau[column].isEmpty() || canPlaceOnFoundation( _state._arrTableau[column].getTop() ) == false )
            return false;
        pushHistory();
        Card card;
        (void)_state._arrTableau[column].draw( card );
        moveToFoundation( card );
        revealColumnTop( column );
        return true;
    }

    bool KlondikeGame::moveTableauToTableau( int32 fromColumn, int32 count, int32 toColumn )
    {
        if ( isColumnValid( fromColumn ) == false || isColumnValid( toColumn ) == false || fromColumn == toColumn || count <= 0 )
            return false;
        const CardPile& from   = _state._arrTableau[fromColumn];
        const int32     faceUp = from.getCount() - _state._arrFaceDownCount[fromColumn];
        if ( count > faceUp )
            return false; // 뒤집힌 패는 옮길 수 없다
        const int32 baseIndex = from.getCount() - count;
        // 옮기는 묶음 자체도 색 번갈이 · 하나씩 내려가야 한다(앞면이면 늘 그렇지만 상태를 그대로 받은 판을 위해 본다).
        for ( int32 index = baseIndex + 1; index < from.getCount(); ++index )
        {
            const Card& upper = from.getAt( index );
            const Card& lower = from.getAt( index - 1 );
            if ( CardDeckUtil::isRedSuit( upper._suit ) == CardDeckUtil::isRedSuit( lower._suit ) || upper._rank + 1 != lower._rank )
                return false;
        }
        if ( canStackOnTableau( from.getAt( baseIndex ), _state._arrTableau[toColumn] ) == false )
            return false;
        pushHistory();
        CardPile& source = _state._arrTableau[fromColumn];
        for ( int32 index = baseIndex; index < source.getCount(); ++index )
            _state._arrTableau[toColumn].push( source.getAt( index ) );
        while ( source.getCount() > baseIndex )
            (void)source.removeAt( source.getCount() - 1 );
        revealColumnTop( fromColumn );
        return true;
    }

    bool KlondikeGame::moveFoundationToTableau( int32 suit, int32 column )
    {
        if ( isColumnValid( column ) == false || suit < 0 || KlondikeState::kFoundationCount <= suit || _state._arrFoundation[suit].isEmpty() )
            return false;
        if ( canStackOnTableau( _state._arrFoundation[suit].getTop(), _state._arrTableau[column] ) == false )
            return false;
        pushHistory();
        (void)_state._arrFoundation[suit].drawInto( _state._arrTableau[column], 1 );
        return true;
    }

    bool KlondikeGame::undo()
    {
        if ( _listHistory.empty() )
            return false;
        _state = _listHistory.back();
        _listHistory.pop_back();
        return true;
    }

    bool KlondikeGame::isWon() const
    {
        for ( const CardPile& foundation : _state._arrFoundation )
        {
            if ( foundation.getCount() != StandardRank::kKing )
                return false;
        }
        return true;
    }

    bool KlondikeGame::canAutoComplete() const
    {
        if ( _state._stock.isEmpty() == false || _state._waste.isEmpty() == false )
            return false;
        for ( const int32 faceDownCount : _state._arrFaceDownCount )
        {
            if ( faceDownCount > 0 )
                return false;
        }
        return true;
    }

    int32 KlondikeGame::autoComplete()
    {
        if ( canAutoComplete() == false )
            return 0;
        pushHistory();
        int32 movedCount = 0;
        bool  bMoved     = true;
        while ( bMoved )
        {
            bMoved = false;
            for ( int32 column = 0; column < KlondikeState::kColumnCount; ++column )
            {
                CardPile& tableau = _state._arrTableau[column];
                if ( tableau.isEmpty() || canPlaceOnFoundation( tableau.getTop() ) == false )
                    continue;
                Card card;
                (void)tableau.draw( card );
                moveToFoundation( card );
                ++movedCount;
                bMoved = true;
            }
        }
        if ( movedCount == 0 )
            (void)undo(); // 아무것도 못 올렸으면 기록을 남기지 않는다
        return movedCount;
    }
} // namespace sw

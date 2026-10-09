#include "pch.h"

#include "GameFramework/Kits/Casual/CardGame/Klondike/KlondikeGame.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Foundation/Utility/StateArchiveUtil.h"

namespace sw
{
    namespace
    {
        struct KlondikeGameInternal
        {
            static void writeBoard( Archive& outArchive, const KlondikeState& state )
            {
                for ( int32 column = 0; column < KlondikeState::kColumnCount; ++column )
                {
                    state._arrTableau[column].writeState( outArchive );
                    outArchive << state._arrFaceDownCount[column];
                }
                for ( const CardPile& foundation : state._arrFoundation )
                {
                    foundation.writeState( outArchive );
                }
                state._stock.writeState( outArchive );
                state._waste.writeState( outArchive );
                outArchive << state._recycleCount;
                outArchive << state._moveCount;
            }

            [[nodiscard]] static bool readBoard( Archive& archive, KlondikeState& outState )
            {
                for ( int32 column = 0; column < KlondikeState::kColumnCount; ++column )
                {
                    if ( outState._arrTableau[column].readState( archive ) == false )
                        return false;
                    archive >> outState._arrFaceDownCount[column];
                    // 뒤집힌 장수는 열의 장수를 넘지 않는다
                    const bool bFaceDownValid = 0 <= outState._arrFaceDownCount[column] && outState._arrFaceDownCount[column] <= outState._arrTableau[column].getCount();
                    if ( archive.isError() || bFaceDownValid == false )
                        return false;
                }
                for ( CardPile& foundation : outState._arrFoundation )
                {
                    if ( foundation.readState( archive ) == false )
                        return false;
                }
                const bool bPilesRead = outState._stock.readState( archive ) && outState._waste.readState( archive );
                archive >> outState._recycleCount;
                archive >> outState._moveCount;
                return bPilesRead && archive.isOk();
            }
        };
    } // namespace

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
            {
                (void)remaining.drawInto( state._arrTableau[column], 1 );
            }
        }
        for ( int32 column = 0; column < KlondikeState::kColumnCount; ++column )
        {
            state._arrFaceDownCount[column] = MathUtil::max( 0, state._arrTableau[column].getCount() - 1 );
        }
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
        {
            _state._arrTableau[toColumn].push( source.getAt( index ) );
        }
        while ( source.getCount() > baseIndex )
        {
            (void)source.removeAt( source.getCount() - 1 ); // 카드는 위에서 옮겼다 — 뺀 카드는 버린다
        }
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

    void KlondikeGame::writeState( Archive& outArchive ) const
    {
        KlondikeGameInternal::writeBoard( outArchive, _state );
        outArchive << static_cast<uint32>( _listHistory.size() );
        for ( const KlondikeState& history : _listHistory )
        {
            KlondikeGameInternal::writeBoard( outArchive, history );
        }
    }

    bool KlondikeGame::readState( Archive& archive )
    {
        KlondikeState state;
        if ( KlondikeGameInternal::readBoard( archive, state ) == false )
            return false;
        uint32 historyCount = 0;
        // 판마다 열 일곱(더미 4 + 뒤집힌 수 4) + 파운데이션 넷 · 스톡 · 웨이스트(더미 4) + 되돌린 수 · 옮긴 수(8)
        if ( StateArchiveUtil::readCount( archive, 88, historyCount ) == false )
            return false;
        vector<KlondikeState> listHistory( historyCount, KlondikeState{} );
        for ( KlondikeState& history : listHistory )
        {
            if ( KlondikeGameInternal::readBoard( archive, history ) == false )
                return false;
        }
        _state       = std::move( state );
        _listHistory = std::move( listHistory );
        return true;
    }
} // namespace sw

#include "pch.h"

#include "GameFramework/Kits/Genre/Casual/CardGame/Klondike/KlondikeGame.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Runtime/Foundation/GameData/ConfigRangeUtil.h"

namespace sw
{
    namespace
    {
        struct KlondikeGameInternal
        {
            /** @brief 설정이 선언된 범위 안인지 봅니다. 밖이면 오류를 남기고 false 입니다(맞추지 않는다). */
            [[nodiscard]] static bool acceptSettings( const KlondikeSettings& settings )
            {
                const string rangeError = ConfigRangeUtil::describeRangeErrors( settings );
                if ( rangeError.empty() == false )
                {
                    SW_LOG_ERROR_ID( "KlondikeGame.KlondikeGameRefusedSettings", "KlondikeGame: refused settings - %#", rangeError.c_str() );
                    return false;
                }
                return true;
            }

            static void serializeBoard( StateIO& io, const KlondikeSettings& settings, KlondikeState& state )
            {
                for ( int32 column = 0; column < KlondikeState::kColumnCount; ++column )
                {
                    io.value( state._arrTableau[column] );
                    io.value( state._arrFaceDownCount[column] );
                    // 카드가 있는 열의 맨 위는 늘 앞면이다(`revealColumnTop`). 뒤집힌 맨 위는 옮길 수 없는데 파운데이션으로는 올라가 숨은 패를 꺼낸다
                    const int32 columnCount = state._arrTableau[column].getCount();
                    if ( io.check( 0 <= state._arrFaceDownCount[column] && state._arrFaceDownCount[column] <= MathUtil::max( 0, columnCount - 1 ) ) == false )
                        return;
                }
                for ( CardPile& foundation : state._arrFoundation )
                {
                    io.value( foundation );
                }
                io.value( state._stock );
                io.value( state._waste );
                io.value( state._recycleCount );
                io.value( state._moveCount );
                if ( io.isReading() && io.isOk() )
                {
                    io.check( isBoardValid( state, settings ) );
                }
            }

            /** @brief 읽은 판이 규칙으로 나올 수 있는 판인지 봅니다. 파운데이션은 무늬별로 에이스부터 하나씩 쌓이고, 카드는 트럼프 52장이며 겹치지 않습니다. */
            [[nodiscard]] static bool isBoardValid( const KlondikeState& state, const KlondikeSettings& settings )
            {
                vector<const CardPile*> listPile{ &state._stock, &state._waste };
                for ( const CardPile& tableau : state._arrTableau )
                {
                    listPile.push_back( &tableau );
                }
                for ( int32 suit = 0; suit < KlondikeState::kFoundationCount; ++suit )
                {
                    const CardPile& foundation = state._arrFoundation[suit];
                    listPile.push_back( &foundation );
                    for ( int32 index = 0; index < foundation.getCount(); ++index )
                    {
                        // `isWon` 은 장수만 세고 `canPlaceOnFoundation` 은 장수로 다음 숫자를 정한다 — 순서가 틀린 더미는 가짜 승리다
                        const Card& card = foundation.getAt( index );
                        if ( card._suit != suit || card._rank != index + StandardRank::kAce )
                            return false;
                    }
                }
                const bool bRecycleValid = settings.isRecycleCountValid( state._recycleCount );
                return bRecycleValid && 0 <= state._moveCount && CardPile::arePilesValid( listPile, &CardPile::isStandardCard );
            }
        };
    } // namespace

    KlondikeGame::KlondikeGame()
        : _listHistory{}
        , _state{}
        , _settings{}
    {
    }

    bool KlondikeGame::initialize( const KlondikeSettings& settings, uint32 seed )
    {
        if ( KlondikeGameInternal::acceptSettings( settings ) == false )
            return false;
        GameRandom random( seed );
        CardPile   deck;
        CardPile::makeStandardDeck( deck );
        deck.shuffle( random );
        return initializeWithDeck( settings, deck );
    }

    bool KlondikeGame::initializeWithDeck( const KlondikeSettings& settings, const CardPile& deck )
    {
        if ( KlondikeGameInternal::acceptSettings( settings ) == false )
            return false;
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
        return initializeFromState( settings, state );
    }

    bool KlondikeGame::initializeFromState( const KlondikeSettings& settings, const KlondikeState& state )
    {
        if ( KlondikeGameInternal::acceptSettings( settings ) == false )
            return false;
        _settings = settings;
        _state    = state;
        _listHistory.clear();
        return true;
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
        return CardPile::isRedSuit( top._suit ) != CardPile::isRedSuit( card._suit ) && card._rank + 1 == top._rank;
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
            const bool bRecycleLeft = _settings.canRecycle( _state._recycleCount );
            if ( _state._waste.isEmpty() || bRecycleLeft == false )
                return false;
            pushHistory();
            // 웨이스트를 뒤집어 스톡으로 되돌린다. 먼저 나왔던 패가 다시 먼저 나온다.
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
        const bool bInvalidWasteMove = isColumnValid( column ) == false || _state._waste.isEmpty() || canStackOnTableau( _state._waste.getTop(), _state._arrTableau[column] ) == false;
        if ( bInvalidWasteMove )
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
        const bool bInvalidFoundationMove = isColumnValid( column ) == false || _state._arrTableau[column].isEmpty() || canPlaceOnFoundation( _state._arrTableau[column].getTop() ) == false;
        if ( bInvalidFoundationMove )
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
        const bool bInvalidTableauMove = isColumnValid( fromColumn ) == false || isColumnValid( toColumn ) == false || fromColumn == toColumn || count <= 0;
        if ( bInvalidTableauMove )
            return false;
        const CardPile& from   = _state._arrTableau[fromColumn];
        const int32     faceUp = from.getCount() - _state._arrFaceDownCount[fromColumn];
        if ( count > faceUp )
            return false; // 뒤집힌 패는 옮길 수 없다
        const int32 baseIndex = from.getCount() - count;
        // 옮기는 카드 그룹도 색이 번갈아 가며 하나씩 내려가야 한다. 앞면이면 항상 그렇지만, 상태를 그대로 받은 판을 위해 확인한다.
        for ( int32 index = baseIndex + 1; index < from.getCount(); ++index )
        {
            const Card& upper = from.getAt( index );
            const Card& lower = from.getAt( index - 1 );
            if ( CardPile::isRedSuit( upper._suit ) == CardPile::isRedSuit( lower._suit ) || upper._rank + 1 != lower._rank )
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
            (void)source.removeAt( source.getCount() - 1 ); // 카드는 위에서 이미 옮겼으므로 뺀 카드는 버린다
        }
        revealColumnTop( fromColumn );
        return true;
    }

    bool KlondikeGame::moveFoundationToTableau( int32 suit, int32 column )
    {
        const bool bInvalidFoundationReturn = isColumnValid( column ) == false || suit < 0 || KlondikeState::kFoundationCount <= suit || _state._arrFoundation[suit].isEmpty();
        if ( bInvalidFoundationReturn )
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
            (void)undo(); // 아무것도 올리지 못했으면 기록을 남기지 않는다
        return movedCount;
    }

    void KlondikeGame::serializeState( StateIO& io )
    {
        KlondikeGameInternal::serializeBoard( io, _settings, _state );
        // 판마다 열 7개(더미 4 + 뒤집힌 수 4) + 파운데이션 4개, 스톡, 웨이스트(더미 4) + 되돌린 수, 옮긴 수(8)
        io.list( _listHistory, 88, [&]( KlondikeState& history )
        { KlondikeGameInternal::serializeBoard( io, _settings, history ); } );
    }

} // namespace sw

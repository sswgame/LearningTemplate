#include "pch.h"

#include "GameFramework/Kits/Genre/Casual/CardGame/CardDeck.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Foundation/Constants.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

#include <algorithm>

namespace sw
{
    CardPile::CardPile()
        : _listCard{}
    {
    }

    void CardPile::pushBottom( const Card& card )
    {
        _listCard.insert( _listCard.begin(), card );
    }

    bool CardPile::draw( Card& outCard )
    {
        if ( _listCard.empty() )
            return false;
        outCard = _listCard.back();
        _listCard.pop_back();
        return true;
    }

    int32 CardPile::drawInto( CardPile& target, int32 count )
    {
        int32 movedCount = 0;
        Card  card;
        while ( movedCount < count && draw( card ) )
        {
            target.push( card );
            ++movedCount;
        }
        return movedCount;
    }

    bool CardPile::takeByID( uint16 cardID, Card& outCard )
    {
        const optional<int32> index = findIndexByID( cardID );
        if ( index.has_value() == false )
            return false;
        outCard = removeAt( *index );
        return true;
    }

    Card CardPile::removeAt( int32 index )
    {
        const Card card = _listCard[static_cast<size_t>( index )];
        _listCard.erase( _listCard.begin() + index );
        return card;
    }

    optional<int32> CardPile::findIndexByID( uint16 cardID ) const
    {
        for ( size_t index = 0; index < _listCard.size(); ++index )
        {
            if ( _listCard[index]._id == cardID )
                return static_cast<int32>( index );
        }
        return nullopt;
    }

    void CardPile::shuffle( GameRandom& random )
    {
        random.shuffle( _listCard ); // 기반의 Fisher-Yates 를 쓴다
    }

    bool CardPile::deal( vector<CardPile>& inoutListHand, int32 cardsEach )
    {
        Card card;
        for ( int32 round = 0; round < cardsEach; ++round )
        {
            for ( CardPile& hand : inoutListHand )
            {
                if ( draw( card ) == false )
                    return false;
                hand.push( card );
            }
        }
        return true;
    }

    void CardPile::serializeState( StateIO& io )
    {
        // 카드마다 번호(2) + 무늬(1) + 숫자(1)
        io.list( _listCard, 4, [&]( Card& card )
        {
            io.value( card._id );
            io.value( card._suit );
            io.value( card._rank );
        } );
        if ( io.isReading() && io.isOk() )
        {
            // 더미에는 빈 카드가 들어가지 않는다. 겹친 번호는 `takeByID` 가 앞의 것만 찾아 뒤의 것이 남는다.
            io.check( arePilesValid( { this }, []( const Card& card )
            { return card.isValid(); } ) );
        }
    }

    bool CardPile::isStandardCard( const Card& card )
    {
        constexpr int32 kRankCount = StandardRank::kKing;
        if ( card._id >= StandardSuit::kCount * kRankCount )
            return false;
        // `makeStandardDeck` 의 번호 규칙 — 번호만 보면 같은 에이스를 번호만 달리해 여러 장 만들 수 있다.
        return card._suit == card._id / kRankCount && card._rank == card._id % kRankCount + StandardRank::kAce;
    }

    bool CardPile::hasDuplicateID( const vector<const CardPile*>& listPile )
    {
        vector<uint16> listID;
        for ( const CardPile* pPile : listPile )
        {
            for ( const Card& card : pPile->_listCard )
            {
                listID.push_back( card._id );
            }
        }
        VectorUtil::sort( listID ); // 꾸민 바이트의 긴 목록이 제곱 시간을 쓰지 않게 정렬해 본다
        return std::adjacent_find( listID.begin(), listID.end() ) != listID.end();
    }

    void CardPile::makeStandardDeck( CardPile& outPile, int32 jokerCount )
    {
        outPile.clear();
        uint16 cardID = 0;
        for ( int32 suit = 0; suit < StandardSuit::kCount; ++suit )
        {
            for ( int32 rank = StandardRank::kAce; rank <= StandardRank::kKing; ++rank )
            {
                outPile.push( Card{ cardID++, static_cast<uint8>( suit ), static_cast<uint8>( rank ) } );
            }
        }
        for ( int32 jokerIndex = 0; jokerIndex < jokerCount; ++jokerIndex )
        {
            outPile.push( Card{ cardID++, StandardSuit::kJoker, 0 } );
        }
    }

    void CardPile::makePileInDrawOrder( const vector<Card>& listCardInDrawOrder, CardPile& outPile )
    {
        outPile.clear();
        for ( size_t index = listCardInDrawOrder.size(); index > 0; --index )
        {
            outPile.push( listCardInDrawOrder[index - 1] );
        }
    }

    void CardAction::encodeAction( const CardAction& action, vector<uint8>& outBuffer )
    {
        outBuffer.clear();
        outBuffer.reserve( kEncodedSize );
        const uint32 amount = static_cast<uint32>( action._amount );
        outBuffer.push_back( kTag );
        outBuffer.push_back( action._kind );
        outBuffer.push_back( static_cast<uint8>( action._cardID & 0xFFu ) );
        outBuffer.push_back( static_cast<uint8>( action._cardID >> 8 ) );
        outBuffer.push_back( static_cast<uint8>( action._targetID & 0xFFu ) );
        outBuffer.push_back( static_cast<uint8>( action._targetID >> 8 ) );
        outBuffer.push_back( static_cast<uint8>( amount & 0xFFu ) );
        outBuffer.push_back( static_cast<uint8>( ( amount >> 8 ) & 0xFFu ) );
        outBuffer.push_back( static_cast<uint8>( ( amount >> 16 ) & 0xFFu ) );
        outBuffer.push_back( static_cast<uint8>( amount >> 24 ) );
    }

    bool CardAction::decodeAction( const vector<uint8>& buffer, CardAction& outAction )
    {
        if ( static_cast<int32>( buffer.size() ) != kEncodedSize || buffer[0] != kTag )
            return false;
        outAction._kind     = buffer[1];
        outAction._cardID   = static_cast<uint16>( buffer[2] | ( buffer[3] << 8 ) );
        outAction._targetID = static_cast<uint16>( buffer[4] | ( buffer[5] << 8 ) );
        const uint32 amount = static_cast<uint32>( buffer[6] ) | ( static_cast<uint32>( buffer[7] ) << 8 ) | ( static_cast<uint32>( buffer[8] ) << 16 ) |
                              ( static_cast<uint32>( buffer[9] ) << 24 );
        outAction._amount = static_cast<int32>( amount );
        return true;
    }
} // namespace sw

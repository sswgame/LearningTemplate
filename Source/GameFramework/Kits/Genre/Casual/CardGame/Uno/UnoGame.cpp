#include "pch.h"

#include "GameFramework/Kits/Genre/Casual/CardGame/Uno/UnoGame.h"

#include "Core/Foundation/Constants.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Runtime/Foundation/GameData/ConfigRangeUtil.h"

namespace sw
{
    namespace
    {
        struct UnoGameInternal
        {
            static constexpr int32 kColorCount     = 4;
            static constexpr int32 kWildCopyCount  = 4;
            static constexpr int32 kActionPoints   = 20;
            static constexpr int32 kWildPoints     = 50;
            static constexpr int32 kDeckCardCount  = 108;
            static constexpr int32 kFirstCardTries = kDeckCardCount;

            static bool isWildValue( uint8 rank ) { return rank == static_cast<uint8>( UnoValue::Wild ) || rank == static_cast<uint8>( UnoValue::WildDrawFour ); }

            /** @brief 읽은 더미들의 카드가 모두 덱(`makeDeck`)의 카드이고 겹치지 않는지 반환합니다. 색이 범위 밖이면 그 카드를 낼 때 지금 색이 범위 밖이 됩니다. */
            [[nodiscard]] static bool arePilesFromDeck( const vector<const CardPile*>& listPile )
            {
                CardPile deck;
                UnoGame::makeDeck( deck );
                const auto isDeckCard = [&deck]( const Card& card )
                { return card._id < deck.getCount() && deck.getAt( card._id ) == card; };
                return CardPile::arePilesValid( listPile, isDeckCard );
            }

            /**
             * @brief 설정이 선언된 범위 안이고 나눌 카드가 덱에 있는지 봅니다. 밖이면 오류를 남기고 false 입니다(맞추지 않는다).
             * @details 인원 × 손패 + 첫 장 한 장이 덱(108장)을 넘으면 나누기가 모자라 손패가 조용히 짧아진다.
             */
            [[nodiscard]] static bool acceptSettings( const UnoSettings& settings )
            {
                const string rangeError = ConfigRangeUtil::describeRangeErrors( settings );
                if ( rangeError.empty() == false )
                {
                    SW_LOG_ERROR_ID( "UnoGame.UnoGameRefusedSettings", "UnoGame: refused settings - %#", rangeError.c_str() );
                    return false;
                }
                if ( settings._playerCount * settings._handSize + 1 > kDeckCardCount )
                {
                    SW_LOG_ERROR_ID( "UnoGame.UnoGameRefusedSettingsPlayers", "UnoGame: refused settings - %# players x %# cards leaves no card for the first discard (deck has %# cards)", settings._playerCount, settings._handSize, kDeckCardCount );
                    return false;
                }
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    UnoGame::UnoGame()
        : _listHand{}
        , _eventBuffer{}
        , _drawPile{}
        , _discardPile{}
        , _settings{}
        , _random{}
        , _currentPlayer{ 0 }
        , _direction{ 1 }
        , _pendingDraw{ 0 }
        , _unoTarget{ constant::invalid_index::kInt32 }
        , _winner{ constant::invalid_index::kInt32 }
        , _color{ UnoColor::Red }
    {
    }

    void UnoGame::makeDeck( CardPile& outPile )
    {
        outPile.clear();
        uint16 cardID = 0;
        for ( int32 color = 0; color < UnoGameInternal::kColorCount; ++color )
        {
            outPile.push( Card{ cardID++, static_cast<uint8>( color ), static_cast<uint8>( UnoValue::Zero ) } );
            for ( int32 value = 1; value <= static_cast<int32>( UnoValue::DrawTwo ); ++value )
            {
                outPile.push( Card{ cardID++, static_cast<uint8>( color ), static_cast<uint8>( value ) } );
                outPile.push( Card{ cardID++, static_cast<uint8>( color ), static_cast<uint8>( value ) } );
            }
        }
        for ( int32 copy = 0; copy < UnoGameInternal::kWildCopyCount; ++copy )
        {
            outPile.push( Card{ cardID++, static_cast<uint8>( UnoColor::Wild ), static_cast<uint8>( UnoValue::Wild ) } );
            outPile.push( Card{ cardID++, static_cast<uint8>( UnoColor::Wild ), static_cast<uint8>( UnoValue::WildDrawFour ) } );
        }
    }

    int32 UnoGame::computeHandPoints( const CardPile& hand )
    {
        int32 points = 0;
        for ( const Card& card : hand.getCards() )
        {
            if ( card._rank <= static_cast<uint8>( UnoValue::Nine ) )
                points += card._rank;
            else if ( UnoGameInternal::isWildValue( card._rank ) )
                points += UnoGameInternal::kWildPoints;
            else
                points += UnoGameInternal::kActionPoints;
        }
        return points;
    }

    bool UnoGame::initialize( const UnoSettings& settings, uint32 seed )
    {
        if ( UnoGameInternal::acceptSettings( settings ) == false )
            return false;
        GameRandom random( seed );
        UnoLayout  layout;
        makeDeck( layout._drawPile );
        layout._drawPile.shuffle( random );
        layout._listHand.resize( static_cast<size_t>( settings._playerCount ) );
        (void)layout._drawPile.deal( layout._listHand, settings._handSize );
        // 와일드가 아닌 첫 장을 뒤집는다. 와일드는 더미 맨 아래로 보낸다.
        for ( int32 attempt = 0; attempt < UnoGameInternal::kFirstCardTries; ++attempt )
        {
            Card card;
            if ( layout._drawPile.draw( card ) == false )
                break;
            if ( UnoGameInternal::isWildValue( card._rank ) )
            {
                layout._drawPile.pushBottom( card );
                continue;
            }
            layout._discardPile.push( card );
            layout._color = static_cast<UnoColor>( card._suit );
            break;
        }
        return initializeFromLayout( settings, layout, random.getState() );
    }

    bool UnoGame::initializeFromLayout( const UnoSettings& settings, const UnoLayout& layout, uint32 seed )
    {
        if ( UnoGameInternal::acceptSettings( settings ) == false )
            return false;
        const int32 handCount = static_cast<int32>( layout._listHand.size() );
        if ( handCount != settings._playerCount )
        {
            SW_LOG_ERROR_ID( "UnoGame.UnoGameRefusedLayoutHands", "UnoGame: refused layout - %# hands for %# players", handCount, settings._playerCount );
            return false;
        }
        if ( layout._firstPlayer < 0 || layout._firstPlayer >= handCount )
        {
            SW_LOG_ERROR_ID( "UnoGame.UnoGameRefusedLayoutFirst", "UnoGame: refused layout - first player %# is outside 0..%#", layout._firstPlayer, handCount - 1 );
            return false;
        }
        _settings      = settings;
        _listHand      = layout._listHand;
        _drawPile      = layout._drawPile;
        _discardPile   = layout._discardPile;
        _random        = GameRandom( seed );
        _currentPlayer = layout._firstPlayer;
        _direction     = 1;
        _pendingDraw   = 0;
        _unoTarget     = constant::invalid_index::kInt32;
        _winner        = constant::invalid_index::kInt32;
        _color         = layout._color;
        _eventBuffer.clear();
        return true;
    }

    bool UnoGame::isPlayable( const Card& card ) const
    {
        if ( _pendingDraw > 0 )
        {
            const bool bStackTwo  = card._rank == static_cast<uint8>( UnoValue::DrawTwo ) && _settings._bStackDrawTwo == SW_TRUE;
            const bool bStackFour = card._rank == static_cast<uint8>( UnoValue::WildDrawFour ) && _settings._bStackDrawFour == SW_TRUE;
            return bStackTwo || bStackFour;
        }
        if ( UnoGameInternal::isWildValue( card._rank ) )
            return true;
        const Card top = _discardPile.getTop();
        return card._suit == static_cast<uint8>( _color ) || ( top.isValid() && card._rank == top._rank );
    }

    int32 UnoGame::findNextPlayer( int32 player, int32 steps ) const
    {
        const int32 count = getPlayerCount();
        return ( ( player + _direction * steps ) % count + count ) % count;
    }

    bool UnoGame::hasColor( int32 player, UnoColor color, uint16 exceptCardID ) const
    {
        for ( const Card& card : getHand( player ).getCards() )
        {
            if ( card._id != exceptCardID && card._suit == static_cast<uint8>( color ) )
                return true;
        }
        return false;
    }

    int32 UnoGame::drawInto( int32 player, int32 count )
    {
        CardPile& hand       = _listHand[static_cast<size_t>( player )];
        int32     drawnCount = 0;
        for ( ; drawnCount < count; ++drawnCount )
        {
            if ( _drawPile.isEmpty() )
            {
                // 버린 더미의 맨 위만 남기고 섞어 새 더미로 쓴다.
                Card top;
                if ( _discardPile.draw( top ) == false )
                    break;
                (void)_discardPile.drawInto( _drawPile, _discardPile.getCount() );
                _drawPile.shuffle( _random );
                _discardPile.push( top );
                _eventBuffer.push( UnoEvent{ constant::invalid_index::kInt32, _drawPile.getCount(), Card::kNoCard, UnoEvent::Kind::Reshuffled } );
            }
            Card card;
            if ( _drawPile.draw( card ) == false )
                break;
            hand.push( card );
        }
        return drawnCount;
    }

    bool UnoGame::playCard( int32 player, uint16 cardID, UnoColor chosenColor, bool bDeclareUno )
    {
        if ( _winner >= 0 || player != _currentPlayer )
            return false;
        CardPile&             hand      = _listHand[static_cast<size_t>( player )];
        const optional<int32> foundCard = hand.findIndexByID( cardID );
        if ( foundCard.has_value() == false )
            return false;
        const int32 index = *foundCard;
        const Card  card  = hand.getAt( index );
        if ( isPlayable( card ) == false )
            return false;
        const bool bWild = UnoGameInternal::isWildValue( card._rank );
        if ( bWild && UnoColor::Wild <= chosenColor )
            return false;
        const bool bDrawFour            = card._rank == static_cast<uint8>( UnoValue::WildDrawFour );
        const bool bIllegalWildDrawFour = bDrawFour && _pendingDraw == 0 && _settings._bStrictWildDrawFour == SW_TRUE && hasColor( player, _color, card._id );
        if ( bIllegalWildDrawFour )
            return false; // 지금 색이 있으면 +4 를 낼 수 없다

        _unoTarget = constant::invalid_index::kInt32; // 앞사람을 잡을 기회는 다음 행동에서 닫힌다
        (void)hand.removeAt( index );                 // 뺀 카드는 위에서 읽어 둔 card 이다
        _discardPile.push( card );
        _eventBuffer.push( UnoEvent{ player, 0, card._id, UnoEvent::Kind::Played } );
        _color = bWild ? chosenColor : static_cast<UnoColor>( card._suit );
        if ( bWild )
            _eventBuffer.push( UnoEvent{ player, static_cast<int32>( _color ), card._id, UnoEvent::Kind::ColorChosen } );
        if ( hand.getCount() == 1 )
        {
            if ( bDeclareUno )
                _eventBuffer.push( UnoEvent{ player, 0, Card::kNoCard, UnoEvent::Kind::UnoDeclared } );
            else
                _unoTarget = player;
        }
        if ( hand.isEmpty() )
        {
            _winner = player;
            _eventBuffer.push( UnoEvent{ player, 0, Card::kNoCard, UnoEvent::Kind::Won } );
            return true;
        }

        switch ( static_cast<UnoValue>( card._rank ) )
        {
            case UnoValue::Skip:
            {
                _eventBuffer.push( UnoEvent{ findNextPlayer( player, 1 ), 0, Card::kNoCard, UnoEvent::Kind::Skipped } );
                _currentPlayer = findNextPlayer( player, 2 );
                break;
            }
            case UnoValue::Reverse:
            {
                _direction = -_direction;
                _eventBuffer.push( UnoEvent{ player, _direction, Card::kNoCard, UnoEvent::Kind::Reversed } );
                // 둘이면 리버스는 스킵이 되어 다시 내 차례이다.
                _currentPlayer = getPlayerCount() == 2 ? player : findNextPlayer( player, 1 );
                break;
            }
            case UnoValue::DrawTwo:
            case UnoValue::WildDrawFour:
            {
                const int32 amount = bDrawFour ? 4 : 2;
                const bool  bStack = bDrawFour ? _settings._bStackDrawFour == SW_TRUE : _settings._bStackDrawTwo == SW_TRUE;
                if ( bStack )
                {
                    _pendingDraw += amount; // 다음 사람이 쌓거나 다 뽑는다
                    _currentPlayer = findNextPlayer( player, 1 );
                    break;
                }
                const int32 victim = findNextPlayer( player, 1 );
                const int32 drawn  = drawInto( victim, amount + _pendingDraw );
                _pendingDraw       = 0;
                _eventBuffer.push( UnoEvent{ victim, drawn, Card::kNoCard, UnoEvent::Kind::Drew } );
                _eventBuffer.push( UnoEvent{ victim, 0, Card::kNoCard, UnoEvent::Kind::Skipped } );
                _currentPlayer = findNextPlayer( player, 2 );
                break;
            }
            default:
            {
                _currentPlayer = findNextPlayer( player, 1 );
                break;
            }
        }
        return true;
    }

    bool UnoGame::drawCard( int32 player )
    {
        if ( _winner >= 0 || player != _currentPlayer )
            return false;
        _unoTarget        = constant::invalid_index::kInt32;
        const int32 count = _pendingDraw > 0 ? _pendingDraw : 1;
        const int32 drawn = drawInto( player, count );
        _pendingDraw      = 0;
        _eventBuffer.push( UnoEvent{ player, drawn, Card::kNoCard, UnoEvent::Kind::Drew } );
        _currentPlayer = findNextPlayer( player, 1 );
        return true;
    }

    bool UnoGame::callUno( int32 caller )
    {
        const bool bCannotCallUno = _winner >= 0 || _unoTarget < 0 || caller < 0 || getPlayerCount() <= caller;
        if ( bCannotCallUno )
            return false;
        const int32 target = _unoTarget;
        _unoTarget         = constant::invalid_index::kInt32;
        if ( getHand( target ).getCount() != 1 )
            return false;
        if ( caller == target )
        {
            _eventBuffer.push( UnoEvent{ target, 0, Card::kNoCard, UnoEvent::Kind::UnoDeclared } );
            return true;
        }
        const int32 drawn = drawInto( target, _settings._unoPenalty );
        _eventBuffer.push( UnoEvent{ target, drawn, Card::kNoCard, UnoEvent::Kind::UnoPenalty } );
        return true;
    }

    bool UnoGame::applyAction( int32 player, const CardAction& action )
    {
        if ( action._kind > static_cast<uint8>( UnoActionKind::CallUno ) )
            return false;
        switch ( static_cast<UnoActionKind>( action._kind ) )
        {
            case UnoActionKind::Play:
            {
                const UnoColor color = action._targetID < static_cast<uint16>( UnoColor::Wild ) ? static_cast<UnoColor>( action._targetID ) : UnoColor::Wild;
                return playCard( player, action._cardID, color, action._amount != 0 );
            }
            case UnoActionKind::Draw:
            {
                return drawCard( player );
            }
            case UnoActionKind::CallUno:
            {
                return callUno( player );
            }
        }
        return false;
    }

    void UnoGame::pollEvents( vector<UnoEvent>& outListEvent )
    {
        _eventBuffer.moveAllTo( outListEvent );
    }

    void UnoGame::serializeState( StateIO& io )
    {
        io.expect( static_cast<uint32>( _listHand.size() ) );
        if ( io.isOk() == false )
            return;
        for ( CardPile& hand : _listHand )
        {
            io.value( hand );
        }
        io.value( _drawPile );
        io.value( _discardPile );
        io.value( _random );
        io.value( _currentPlayer );
        io.value( _direction );
        io.value( _pendingDraw );
        io.value( _unoTarget );
        io.value( _winner );
        io.enumValue( _color, UnoColor::Wild );
        if ( io.isWriting() || io.isOk() == false )
            return;
        const int32 handCount = static_cast<int32>( _listHand.size() );
        // 쌓인 벌칙은 덱을 넘지 못한다 — 쌓는 동안에는 뽑지 않아 카드가 돌아오지 않는다. 터무니없는 값은 `_pendingDraw += amount` 를 넘친다.
        if ( io.check( 0 <= _currentPlayer && _currentPlayer < handCount && ( _direction == 1 || _direction == -1 ) && -1 <= _unoTarget && _unoTarget < handCount && -1 <= _winner && _winner < handCount && 0 <= _pendingDraw && _pendingDraw <= UnoGameInternal::kDeckCardCount ) == false )
            return;
        // 이긴 사람은 손패를 다 낸 사람이다.
        io.check( _winner < 0 || _listHand[static_cast<size_t>( _winner )].isEmpty() );
        vector<const CardPile*> listPile{ &_drawPile, &_discardPile };
        for ( const CardPile& hand : _listHand )
        {
            listPile.push_back( &hand );
        }
        io.check( UnoGameInternal::arePilesFromDeck( listPile ) );
        _eventBuffer.clear();
    }

} // namespace sw

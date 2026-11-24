#include "pch.h"

#include "GameFramework/Kits/Genre/Casual/CardGame/SheddingCard/SheddingCardGame.h"

#include "Core/Foundation/Constants.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Reflection/TypeFqn.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Runtime/Foundation/GameData/ConfigRangeUtil.h"

namespace sw
{
    namespace
    {
        struct SheddingCardGameInternal
        {
            static constexpr int32 kColorCount     = 4;
            static constexpr int32 kWildCopyCount  = 4;
            static constexpr int32 kActionPoints   = 20;
            static constexpr int32 kWildPoints     = 50;
            static constexpr int32 kDeckCardCount  = SheddingCardGame::kDeckCardCount;
            static constexpr int32 kFirstCardTries = kDeckCardCount;

            static bool isWildValue( uint8 rank ) { return rank == static_cast<uint8>( SheddingCardValue::Wild ) || rank == static_cast<uint8>( SheddingCardValue::WildDrawFour ); }

            /** @brief 읽은 더미들의 카드가 모두 덱(`makeDeck`)의 카드이고 겹치지 않는지 반환합니다. 색이 범위 밖이면 그 카드를 낼 때 지금 색이 범위 밖이 됩니다. */
            [[nodiscard]] static bool arePilesFromDeck( const vector<const CardPile*>& listPile )
            {
                CardPile deck;
                SheddingCardGame::makeDeck( deck );
                const auto isDeckCard = [&deck]( const Card& card )
                { return card._id < deck.getCount() && deck.getAt( card._id ) == card; };
                return CardPile::arePilesValid( listPile, isDeckCard );
            }

            /**
             * @brief 설정이 선언된 범위 안이고 나눌 카드가 덱에 있는지 봅니다. 밖이면 오류를 남기고 false 입니다(맞추지 않는다).
             * @details 인원 × 손패 + 첫 장 한 장이 덱(108장)을 넘으면 나누기가 모자라 손패가 조용히 짧아진다.
             */
            [[nodiscard]] static bool acceptSettings( const SheddingCardSettings& settings )
            {
                const string rangeError = ConfigRangeUtil::describeRangeErrors( settings );
                if ( rangeError.empty() == false )
                {
                    SW_LOG_ERROR_ID( "SheddingCardGame.SheddingCardGameRefusedSettings", "SheddingCardGame: refused settings - %#", rangeError.c_str() );
                    return false;
                }
                if ( settings._playerCount * settings._handSize + 1 > kDeckCardCount )
                {
                    SW_LOG_ERROR_ID( "SheddingCardGame.SheddingCardGameRefusedSettingsPlayers", "SheddingCardGame: refused settings - %# players x %# cards leaves no card for the first discard (deck has %# cards)", settings._playerCount, settings._handSize, kDeckCardCount );
                    return false;
                }
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SheddingCardGame::SheddingCardGame()
        : _listHand{}
        , _eventBuffer{}
        , _drawPile{}
        , _discardPile{}
        , _settings{}
        , _random{}
        , _shuffleKey{}
        , _dealID{ 0 }
        , _shuffleCount{ 0 }
        , _currentPlayer{ 0 }
        , _direction{ 1 }
        , _pendingDraw{ 0 }
        , _lastCardTarget{ constant::invalid_index::kInt32 }
        , _winner{ constant::invalid_index::kInt32 }
        , _color{ SheddingCardColor::Red }
    {
    }

    void SheddingCardGame::makeDeck( CardPile& outPile )
    {
        outPile.clear();
        uint16 cardID = 0;
        for ( int32 color = 0; color < SheddingCardGameInternal::kColorCount; ++color )
        {
            outPile.push( Card{ cardID++, static_cast<uint8>( color ), static_cast<uint8>( SheddingCardValue::Zero ) } );
            for ( int32 value = 1; value <= static_cast<int32>( SheddingCardValue::DrawTwo ); ++value )
            {
                outPile.push( Card{ cardID++, static_cast<uint8>( color ), static_cast<uint8>( value ) } );
                outPile.push( Card{ cardID++, static_cast<uint8>( color ), static_cast<uint8>( value ) } );
            }
        }
        for ( int32 copy = 0; copy < SheddingCardGameInternal::kWildCopyCount; ++copy )
        {
            outPile.push( Card{ cardID++, static_cast<uint8>( SheddingCardColor::Wild ), static_cast<uint8>( SheddingCardValue::Wild ) } );
            outPile.push( Card{ cardID++, static_cast<uint8>( SheddingCardColor::Wild ), static_cast<uint8>( SheddingCardValue::WildDrawFour ) } );
        }
    }

    int32 SheddingCardGame::computeHandPoints( const CardPile& hand )
    {
        int32 points = 0;
        for ( const Card& card : hand.getCards() )
        {
            if ( card._rank <= static_cast<uint8>( SheddingCardValue::Nine ) )
                points += card._rank;
            else if ( SheddingCardGameInternal::isWildValue( card._rank ) )
                points += SheddingCardGameInternal::kWildPoints;
            else
                points += SheddingCardGameInternal::kActionPoints;
        }
        return points;
    }

    bool SheddingCardGame::initialize( const SheddingCardSettings& settings, uint32 seed )
    {
        if ( SheddingCardGameInternal::acceptSettings( settings ) == false )
            return false;
        GameRandom random( seed );
        CardPile   deck;
        makeDeck( deck );
        deck.shuffle( random );
        SheddingCardLayout layout;
        makeDealtLayout( settings, std::move( deck ), layout );
        return initializeFromLayout( settings, layout, random.getState() );
    }

    bool SheddingCardGame::initializeSecret( const SheddingCardSettings& settings, const SipHashKey& shuffleKey, uint64 dealID )
    {
        if ( SheddingCardGameInternal::acceptSettings( settings ) == false )
            return false;
        CardPile deck;
        makeDeck( deck );
        deck.shuffleSecret( shuffleKey, dealID, 0 );
        SheddingCardLayout layout;
        makeDealtLayout( settings, std::move( deck ), layout );
        if ( initializeFromLayout( settings, layout ) == false )
            return false;
        _shuffleKey   = shuffleKey;
        _dealID       = dealID;
        _shuffleCount = 1;
        return true;
    }

    void SheddingCardGame::makeDealtLayout( const SheddingCardSettings& settings, CardPile&& deck, SheddingCardLayout& outLayout )
    {
        outLayout           = SheddingCardLayout{};
        outLayout._drawPile = std::move( deck );
        outLayout._listHand.resize( static_cast<size_t>( settings._playerCount ) );
        (void)outLayout._drawPile.deal( outLayout._listHand, settings._handSize );
        // 와일드가 아닌 첫 장을 뒤집는다. 와일드는 더미 맨 아래로 보낸다.
        for ( int32 attempt = 0; attempt < SheddingCardGameInternal::kFirstCardTries; ++attempt )
        {
            Card card;
            if ( outLayout._drawPile.draw( card ) == false )
                break;
            if ( SheddingCardGameInternal::isWildValue( card._rank ) )
            {
                outLayout._drawPile.pushBottom( card );
                continue;
            }
            outLayout._discardPile.push( card );
            outLayout._color = static_cast<SheddingCardColor>( card._suit );
            break;
        }
    }

    bool SheddingCardGame::initializeFromLayout( const SheddingCardSettings& settings, const SheddingCardLayout& layout, uint32 seed )
    {
        if ( SheddingCardGameInternal::acceptSettings( settings ) == false )
            return false;
        const int32 handCount = static_cast<int32>( layout._listHand.size() );
        if ( handCount != settings._playerCount )
        {
            SW_LOG_ERROR_ID( "SheddingCardGame.SheddingCardGameRefusedLayoutHands", "SheddingCardGame: refused layout - %# hands for %# players", handCount, settings._playerCount );
            return false;
        }
        if ( layout._firstPlayer < 0 || layout._firstPlayer >= handCount )
        {
            SW_LOG_ERROR_ID( "SheddingCardGame.SheddingCardGameRefusedLayoutFirst", "SheddingCardGame: refused layout - first player %# is outside 0..%#", layout._firstPlayer, handCount - 1 );
            return false;
        }
        _settings       = settings;
        _listHand       = layout._listHand;
        _drawPile       = layout._drawPile;
        _discardPile    = layout._discardPile;
        _random         = GameRandom( seed );
        _shuffleKey     = nullopt;
        _dealID         = 0;
        _shuffleCount   = 0;
        _currentPlayer  = layout._firstPlayer;
        _direction      = 1;
        _pendingDraw    = 0;
        _lastCardTarget = constant::invalid_index::kInt32;
        _winner         = constant::invalid_index::kInt32;
        _color          = layout._color;
        _eventBuffer.clear();
        return true;
    }

    bool SheddingCardGame::isPlayable( const Card& card ) const
    {
        if ( _pendingDraw > 0 )
        {
            const bool bStackTwo  = card._rank == static_cast<uint8>( SheddingCardValue::DrawTwo ) && _settings._bStackDrawTwo == SW_TRUE;
            const bool bStackFour = card._rank == static_cast<uint8>( SheddingCardValue::WildDrawFour ) && _settings._bStackDrawFour == SW_TRUE;
            return bStackTwo || bStackFour;
        }
        if ( SheddingCardGameInternal::isWildValue( card._rank ) )
            return true;
        const Card top = _discardPile.getTop();
        return card._suit == static_cast<uint8>( _color ) || ( top.isValid() && card._rank == top._rank );
    }

    int32 SheddingCardGame::findNextPlayer( int32 player, int32 steps ) const
    {
        const int32 count = getPlayerCount();
        return ( ( player + _direction * steps ) % count + count ) % count;
    }

    bool SheddingCardGame::hasColor( int32 player, SheddingCardColor color, uint16 exceptCardID ) const
    {
        for ( const Card& card : getHand( player ).getCards() )
        {
            if ( card._id != exceptCardID && card._suit == static_cast<uint8>( color ) )
                return true;
        }
        return false;
    }

    int32 SheddingCardGame::drawInto( int32 player, int32 count )
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
                if ( _shuffleKey.has_value() )
                    _drawPile.shuffleSecret( *_shuffleKey, _dealID, _shuffleCount++ );
                else
                    _drawPile.shuffle( _random );
                _discardPile.push( top );
                _eventBuffer.push( SheddingCardEvent{ nullopt, _drawPile.getCount(), Card::kNoCard, SheddingCardEvent::Kind::Reshuffled } );
            }
            Card card;
            if ( _drawPile.draw( card ) == false )
                break;
            hand.push( card );
        }
        return drawnCount;
    }

    bool SheddingCardGame::playCard( int32 player, uint16 cardID, SheddingCardColor chosenColor, bool bDeclareLastCard )
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
        const bool bWild = SheddingCardGameInternal::isWildValue( card._rank );
        if ( bWild && SheddingCardColor::Wild <= chosenColor )
            return false;
        const bool bDrawFour            = card._rank == static_cast<uint8>( SheddingCardValue::WildDrawFour );
        const bool bIllegalWildDrawFour = bDrawFour && _pendingDraw == 0 && _settings._bStrictWildDrawFour == SW_TRUE && hasColor( player, _color, card._id );
        if ( bIllegalWildDrawFour )
            return false; // 지금 색이 있으면 +4 를 낼 수 없다

        _lastCardTarget = constant::invalid_index::kInt32; // 앞사람을 잡을 기회는 다음 행동에서 닫힌다
        (void)hand.removeAt( index );                      // 뺀 카드는 위에서 읽어 둔 card 이다
        _discardPile.push( card );
        _eventBuffer.push( SheddingCardEvent{ player, 0, card._id, SheddingCardEvent::Kind::Played } );
        _color = bWild ? chosenColor : static_cast<SheddingCardColor>( card._suit );
        if ( bWild )
            _eventBuffer.push( SheddingCardEvent{ player, static_cast<int32>( _color ), card._id, SheddingCardEvent::Kind::ColorChosen } );
        if ( hand.getCount() == 1 )
        {
            if ( bDeclareLastCard )
                _eventBuffer.push( SheddingCardEvent{ player, 0, Card::kNoCard, SheddingCardEvent::Kind::LastCardDeclared } );
            else
                _lastCardTarget = player;
        }
        if ( hand.isEmpty() )
        {
            _winner = player;
            _eventBuffer.push( SheddingCardEvent{ player, 0, Card::kNoCard, SheddingCardEvent::Kind::Won } );
            return true;
        }

        switch ( static_cast<SheddingCardValue>( card._rank ) )
        {
            case SheddingCardValue::Skip:
            {
                _eventBuffer.push( SheddingCardEvent{ findNextPlayer( player, 1 ), 0, Card::kNoCard, SheddingCardEvent::Kind::Skipped } );
                _currentPlayer = findNextPlayer( player, 2 );
                break;
            }
            case SheddingCardValue::Reverse:
            {
                _direction = -_direction;
                _eventBuffer.push( SheddingCardEvent{ player, _direction, Card::kNoCard, SheddingCardEvent::Kind::Reversed } );
                // 둘이면 리버스는 스킵이 되어 다시 내 차례이다.
                _currentPlayer = getPlayerCount() == 2 ? player : findNextPlayer( player, 1 );
                break;
            }
            case SheddingCardValue::DrawTwo:
            case SheddingCardValue::WildDrawFour:
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
                _eventBuffer.push( SheddingCardEvent{ victim, drawn, Card::kNoCard, SheddingCardEvent::Kind::Drew } );
                _eventBuffer.push( SheddingCardEvent{ victim, 0, Card::kNoCard, SheddingCardEvent::Kind::Skipped } );
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

    bool SheddingCardGame::drawCard( int32 player )
    {
        if ( _winner >= 0 || player != _currentPlayer )
            return false;
        _lastCardTarget   = constant::invalid_index::kInt32;
        const int32 count = _pendingDraw > 0 ? _pendingDraw : 1;
        const int32 drawn = drawInto( player, count );
        _pendingDraw      = 0;
        _eventBuffer.push( SheddingCardEvent{ player, drawn, Card::kNoCard, SheddingCardEvent::Kind::Drew } );
        _currentPlayer = findNextPlayer( player, 1 );
        return true;
    }

    bool SheddingCardGame::callLastCard( int32 caller )
    {
        const bool bCannotCallLastCard = _winner >= 0 || _lastCardTarget < 0 || caller < 0 || getPlayerCount() <= caller;
        if ( bCannotCallLastCard )
            return false;
        const int32 target = _lastCardTarget;
        _lastCardTarget    = constant::invalid_index::kInt32;
        if ( getHand( target ).getCount() != 1 )
            return false;
        if ( caller == target )
        {
            _eventBuffer.push( SheddingCardEvent{ target, 0, Card::kNoCard, SheddingCardEvent::Kind::LastCardDeclared } );
            return true;
        }
        const int32 drawn = drawInto( target, _settings._lastCardPenalty );
        _eventBuffer.push( SheddingCardEvent{ target, drawn, Card::kNoCard, SheddingCardEvent::Kind::LastCardPenalty } );
        return true;
    }

    bool SheddingCardGame::applyAction( int32 player, const CardAction& action )
    {
        if ( action._kind > static_cast<uint8>( SheddingCardActionKind::CallLastCard ) )
            return false;
        switch ( static_cast<SheddingCardActionKind>( action._kind ) )
        {
            case SheddingCardActionKind::Play:
            {
                const SheddingCardColor color = action._targetID < static_cast<uint16>( SheddingCardColor::Wild ) ? static_cast<SheddingCardColor>( action._targetID ) : SheddingCardColor::Wild;
                return playCard( player, action._cardID, color, action._amount != 0 );
            }
            case SheddingCardActionKind::Draw:
            {
                return drawCard( player );
            }
            case SheddingCardActionKind::CallLastCard:
            {
                return callLastCard( player );
            }
        }
        return false;
    }

    void SheddingCardGame::pollEvents( vector<SheddingCardEvent>& outListEvent )
    {
        _eventBuffer.moveAllTo( outListEvent );
    }

    void SheddingCardGame::serializeState( StateIO& io )
    {
        io.reflected( *this ); // 차례 · 방향 · 쌓인 벌칙 · 선언 누락 대상 · 이긴 사람 · 색 — 바닥은 칸의 `PROPERTY( Min · Max )`
        io.expect( static_cast<uint32>( _listHand.size() ) );
        if ( io.isOk() == false )
            return;
        for ( CardPile& hand : _listHand )
        {
            io.value( hand ); // 리플렉션 밖 값 — 더미는 자기 `serializeState` 가 빈 카드 · 겹침을 본다, 난수
        }
        io.value( _drawPile );
        io.value( _discardPile );
        io.value( _random );
        if ( io.isWriting() || io.isOk() == false )
            return;
        const int32 handCount = static_cast<int32>( _listHand.size() );
        // 쌓인 벌칙은 덱을 넘지 못한다 — 쌓는 동안에는 뽑지 않아 카드가 돌아오지 않는다. 터무니없는 값은 `_pendingDraw += amount` 를 넘친다.
        if ( io.check( _currentPlayer < handCount && _direction != 0 && _lastCardTarget < handCount && _winner < handCount && _pendingDraw <= SheddingCardGameInternal::kDeckCardCount ) == false ) // 칸 사이 · 설정 상한
            return;
        // 이긴 사람은 손패를 다 낸 사람이다.
        io.check( _winner < 0 || _listHand[static_cast<size_t>( _winner )].isEmpty() );
        vector<const CardPile*> listPile{ &_drawPile, &_discardPile };
        for ( const CardPile& hand : _listHand )
        {
            listPile.push_back( &hand );
        }
        io.check( SheddingCardGameInternal::arePilesFromDeck( listPile ) );
        _eventBuffer.clear();
    }

} // namespace sw

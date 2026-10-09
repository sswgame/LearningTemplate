#include "pch.h"

#include "GameFramework/Kits/Genre/Casual/CardGame/Uno/UnoGame.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Foundation/Utility/StateArchiveUtil.h"

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
            static constexpr int32 kFirstCardTries = 108;

            static bool isWildValue( uint8 rank ) { return rank == static_cast<uint8>( UnoValue::Wild ) || rank == static_cast<uint8>( UnoValue::WildDrawFour ); }
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
        , _unoTarget{ -1 }
        , _winner{ -1 }
        , _color{ UnoColor::Red }
    {
    }

    void UnoGame::makeDeck( CardPile& outPile )
    {
        outPile.clear();
        uint16 cardId = 0;
        for ( int32 color = 0; color < UnoGameInternal::kColorCount; ++color )
        {
            outPile.push( Card{ cardId++, static_cast<uint8>( color ), static_cast<uint8>( UnoValue::Zero ) } );
            for ( int32 value = 1; value <= static_cast<int32>( UnoValue::DrawTwo ); ++value )
            {
                outPile.push( Card{ cardId++, static_cast<uint8>( color ), static_cast<uint8>( value ) } );
                outPile.push( Card{ cardId++, static_cast<uint8>( color ), static_cast<uint8>( value ) } );
            }
        }
        for ( int32 copy = 0; copy < UnoGameInternal::kWildCopyCount; ++copy )
        {
            outPile.push( Card{ cardId++, static_cast<uint8>( UnoColor::Wild ), static_cast<uint8>( UnoValue::Wild ) } );
            outPile.push( Card{ cardId++, static_cast<uint8>( UnoColor::Wild ), static_cast<uint8>( UnoValue::WildDrawFour ) } );
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

    void UnoGame::initialize( const UnoSettings& settings, uint32 seed )
    {
        GameRandom random( seed );
        UnoLayout  layout;
        makeDeck( layout._drawPile );
        layout._drawPile.shuffle( random );
        layout._listHand.resize( static_cast<size_t>( MathUtil::max( 2, settings._playerCount ) ) );
        (void)layout._drawPile.deal( layout._listHand, settings._handSize );
        // 와일드가 아닌 첫 장을 뒤집는다 — 와일드는 더미 맨 아래로.
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
        initializeFromLayout( settings, layout, random.getState() );
    }

    void UnoGame::initializeFromLayout( const UnoSettings& settings, const UnoLayout& layout, uint32 seed )
    {
        _settings = settings;
        _listHand = layout._listHand;
        if ( _listHand.size() < 2 )
            _listHand.resize( 2 );
        _drawPile      = layout._drawPile;
        _discardPile   = layout._discardPile;
        _random        = GameRandom( seed );
        _currentPlayer = MathUtil::clamp( layout._firstPlayer, 0, getPlayerCount() - 1 );
        _direction     = 1;
        _pendingDraw   = 0;
        _unoTarget     = -1;
        _winner        = -1;
        _color         = layout._color;
        _eventBuffer.clear();
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

    bool UnoGame::hasColor( int32 player, UnoColor color, uint16 exceptCardId ) const
    {
        for ( const Card& card : getHand( player ).getCards() )
        {
            if ( card._id != exceptCardId && card._suit == static_cast<uint8>( color ) )
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
                // 버린 더미의 맨 위만 남기고 섞어 새 더미로.
                Card top;
                if ( _discardPile.draw( top ) == false )
                    break;
                (void)_discardPile.drawInto( _drawPile, _discardPile.getCount() );
                _drawPile.shuffle( _random );
                _discardPile.push( top );
                _eventBuffer.push( UnoEvent{ -1, _drawPile.getCount(), Card::kNoCard, UnoEvent::Kind::Reshuffled } );
            }
            Card card;
            if ( _drawPile.draw( card ) == false )
                break;
            hand.push( card );
        }
        return drawnCount;
    }

    bool UnoGame::playCard( int32 player, uint16 cardId, UnoColor chosenColor, bool bDeclareUno )
    {
        if ( _winner >= 0 || player != _currentPlayer )
            return false;
        CardPile&   hand  = _listHand[static_cast<size_t>( player )];
        const int32 index = hand.findIndexById( cardId );
        if ( index < 0 )
            return false;
        const Card card = hand.getAt( index );
        if ( isPlayable( card ) == false )
            return false;
        const bool bWild = UnoGameInternal::isWildValue( card._rank );
        if ( bWild && UnoColor::Wild <= chosenColor )
            return false;
        const bool bDrawFour = card._rank == static_cast<uint8>( UnoValue::WildDrawFour );
        if ( bDrawFour && _pendingDraw == 0 && _settings._bStrictWildDrawFour == SW_TRUE && hasColor( player, _color, card._id ) )
            return false; // 지금 색이 있으면 +4 를 낼 수 없다

        _unoTarget = -1;              // 앞사람을 잡을 기회는 다음 행동에서 닫힌다
        (void)hand.removeAt( index ); // 뺀 카드는 위에서 읽어 둔 card 다
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
                // 둘이면 리버스는 스킵 — 다시 내 차례.
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
        _unoTarget        = -1;
        const int32 count = _pendingDraw > 0 ? _pendingDraw : 1;
        const int32 drawn = drawInto( player, count );
        _pendingDraw      = 0;
        _eventBuffer.push( UnoEvent{ player, drawn, Card::kNoCard, UnoEvent::Kind::Drew } );
        _currentPlayer = findNextPlayer( player, 1 );
        return true;
    }

    bool UnoGame::callUno( int32 caller )
    {
        if ( _winner >= 0 || _unoTarget < 0 || caller < 0 || getPlayerCount() <= caller )
            return false;
        const int32 target = _unoTarget;
        _unoTarget         = -1;
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
                const UnoColor color = action._targetId < static_cast<uint16>( UnoColor::Wild ) ? static_cast<UnoColor>( action._targetId ) : UnoColor::Wild;
                return playCard( player, action._cardId, color, action._amount != 0 );
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

    void UnoGame::drainEvents( vector<UnoEvent>& outListEvent )
    {
        _eventBuffer.drainTo( outListEvent );
    }

    void UnoGame::writeState( Archive& outArchive ) const
    {
        outArchive << static_cast<uint32>( _listHand.size() );
        for ( const CardPile& hand : _listHand )
        {
            hand.writeState( outArchive );
        }
        _drawPile.writeState( outArchive );
        _discardPile.writeState( outArchive );
        StateArchiveUtil::writeRandom( outArchive, _random );
        outArchive << _currentPlayer;
        outArchive << _direction;
        outArchive << _pendingDraw;
        outArchive << _unoTarget;
        outArchive << _winner;
        outArchive << static_cast<uint8>( _color );
    }

    bool UnoGame::readState( Archive& archive )
    {
        uint32 handCount = 0;
        archive >> handCount;
        if ( archive.isError() || handCount != _listHand.size() )
            return false;
        // 사본에 읽고 끝까지 맞으면 바꾼다 — 설정은 사본이 그대로 든다.
        UnoGame game = *this;
        for ( CardPile& hand : game._listHand )
        {
            if ( hand.readState( archive ) == false )
                return false;
        }
        const bool bPilesRead = game._drawPile.readState( archive ) && game._discardPile.readState( archive );
        if ( bPilesRead == false || StateArchiveUtil::readRandom( archive, game._random ) == false )
            return false;
        uint8 color = 0;
        archive >> game._currentPlayer;
        archive >> game._direction;
        archive >> game._pendingDraw;
        archive >> game._unoTarget;
        archive >> game._winner;
        archive >> color;
        const int32 count           = static_cast<int32>( handCount );
        const bool  bPlayerValid    = 0 <= game._currentPlayer && game._currentPlayer < count;
        const bool  bDirectionValid = game._direction == 1 || game._direction == -1;
        const bool  bTargetValid    = -1 <= game._unoTarget && game._unoTarget < count && -1 <= game._winner && game._winner < count;
        const bool  bColorValid     = color <= static_cast<uint8>( UnoColor::Wild );
        if ( archive.isError() || bPlayerValid == false || bDirectionValid == false || bTargetValid == false || bColorValid == false || game._pendingDraw < 0 )
            return false;
        game._color = static_cast<UnoColor>( color );
        game._eventBuffer.clear();
        *this = std::move( game );
        return true;
    }
} // namespace sw

#include "pch.h"

#include "GameFramework/Kits/Genre/Casual/CardGame/DeckBattle/DeckBattle.h"

#include "Core/Container/StringUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Base/Foundation/Data/GameDataXML.h"
#include "GameFramework/Base/Foundation/Utility/StateArchiveUtil.h"

namespace sw
{
    SW_LOG_CALLER( "DeckBattle" );

    namespace
    {
        struct DeckBattleInternal
        {
            [[nodiscard]] static bool parseEffectKind( string_view text, DeckBattleEffectKind& outKind )
            {
                if ( StringUtil::equals( text, "Damage", true ) )
                    outKind = DeckBattleEffectKind::Damage;
                else if ( StringUtil::equals( text, "Block", true ) )
                    outKind = DeckBattleEffectKind::Block;
                else if ( StringUtil::equals( text, "Draw", true ) )
                    outKind = DeckBattleEffectKind::Draw;
                else if ( StringUtil::equals( text, "Energy", true ) )
                    outKind = DeckBattleEffectKind::Energy;
                else
                    return false;
                return true;
            }

            /** @brief 더미의 카드 번호가 모두 덱(@p cardCount 장) 안에 있는지 반환합니다. */
            static bool arePileIDsInDeck( const CardPile& pile, size_t cardCount )
            {
                for ( const Card& card : pile.getCards() )
                {
                    if ( card._id >= cardCount )
                        return false;
                }
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    DeckBattleCatalog::DeckBattleCatalog()
    {
    }

    uint32 DeckBattleCatalog::loadRoot( const XMLNode& root, string_view sourceName )
    {
        // 칸의 기본값은 Def 의 초기값, 범위는 `PROPERTY( Min · Max )` 다. 틀린 카드는 넣지 않고 오류다(readFields 가 남긴다).
        uint32 loadedCount = 0;
        for ( XMLNode node = root.findChild(); node; node = node.findNextSibling() )
        {
            if ( StringUtil::equals( node.getName(), "Card", true ) == false )
            {
                SW_LOG_ERROR( "%#: unknown element <%#> - skipped", sourceName, node.getName() );
                continue;
            }
            DeckBattleCardDef def;
            bool              bValid = GameDataXML::readFields( node, def, sourceName, { "effects" } );
            XMLReadUtil::forEachNamedNumber( node.getAttributeText( "effects" ), ",; ", 0, XMLReadUtil::MissingValue::Reject,
                                             [&]( string_view token, string_view kindName, int32 amount, bool bAmount )
            {
                DeckBattleEffect effect;
                const bool       bKind = DeckBattleInternal::parseEffectKind( kindName, effect._kind );
                effect._amount         = amount;
                if ( bKind && bAmount )
                    def._listEffect.push_back( effect );
                else
                {
                    SW_LOG_ERROR( "%#: card '%#' has an unreadable effect '%#'", sourceName, def._id.c_str(), token );
                    bValid = false;
                }
            } );
            if ( bValid && add( def ) >= 0 )
                ++loadedCount;
        }
        return loadedCount;
    }

    DeckBattle::DeckBattle()
        : _listDefIndex{}
        , _drawPile{}
        , _hand{}
        , _discardPile{}
        , _exhaustPile{}
        , _enemy{}
        , _settings{}
        , _random{}
        , _pCatalog{ nullptr }
        , _playerHp{ 0 }
        , _playerBlock{ 0 }
        , _energy{ 0 }
        , _turn{ 0 }
        , _phase{ DeckBattlePhase::Lost }
    {
    }

    bool DeckBattle::initialize( const DeckBattleCatalog* pCatalog, const DeckBattleSettings& settings, const vector<hashed_string>& listDeckCardID,
                                 const DeckBattleEnemy& enemy, uint32 seed )
    {
        _pCatalog = pCatalog;
        _settings = settings;
        _enemy    = enemy;
        _random   = GameRandom( seed );
        _listDefIndex.clear();
        _drawPile.clear();
        _hand.clear();
        _discardPile.clear();
        _exhaustPile.clear();
        _playerHp    = settings._playerHp;
        _playerBlock = 0;
        _turn        = 0;
        if ( pCatalog == nullptr )
            return false;
        for ( const hashed_string& cardID : listDeckCardID )
        {
            const int32 defIndex = pCatalog->findIndex( cardID );
            if ( defIndex < 0 )
                continue;
            _drawPile.push( Card{ static_cast<uint16>( _listDefIndex.size() ), 0, 0 } );
            _listDefIndex.push_back( defIndex );
        }
        if ( _drawPile.isEmpty() )
            return false;
        _drawPile.shuffle( _random );
        _phase = DeckBattlePhase::PlayerTurn;
        startTurn();
        return true;
    }

    const DeckBattleCardDef* DeckBattle::getCardDef( const Card& card ) const
    {
        if ( _pCatalog == nullptr || card._id >= _listDefIndex.size() )
            return nullptr;
        return &_pCatalog->getAt( static_cast<size_t>( _listDefIndex[card._id] ) );
    }

    void DeckBattle::startTurn()
    {
        ++_turn;
        _playerBlock = 0;
        _energy      = _settings._energyPerTurn;
        drawCards( _settings._drawPerTurn );
    }

    void DeckBattle::drawCards( int32 count )
    {
        for ( int32 drawn = 0; drawn < count; ++drawn )
        {
            if ( _drawPile.isEmpty() )
            {
                // 버린 더미를 섞어 다시 뽑을 더미로 쓴다.
                (void)_discardPile.drawInto( _drawPile, _discardPile.getCount() );
                _drawPile.shuffle( _random );
            }
            Card card;
            if ( _drawPile.draw( card ) == false )
                return;
            if ( _hand.getCount() >= _settings._handLimit )
                _discardPile.push( card );
            else
                _hand.push( card );
        }
    }

    void DeckBattle::applyEffect( const DeckBattleEffect& effect )
    {
        switch ( effect._kind )
        {
            case DeckBattleEffectKind::Damage:
            {
                const int32 absorbed = MathUtil::min( _enemy._block, effect._amount );
                _enemy._block -= absorbed;
                _enemy._hp = MathUtil::max( 0, _enemy._hp - ( effect._amount - absorbed ) );
                break;
            }
            case DeckBattleEffectKind::Block:
            {
                _playerBlock += effect._amount;
                break;
            }
            case DeckBattleEffectKind::Draw:
            {
                drawCards( effect._amount );
                break;
            }
            case DeckBattleEffectKind::Energy:
            {
                _energy += effect._amount;
                break;
            }
        }
    }

    bool DeckBattle::playCard( int32 handIndex )
    {
        const bool bCannotPlayCard = _phase != DeckBattlePhase::PlayerTurn || handIndex < 0 || _hand.getCount() <= handIndex;
        if ( bCannotPlayCard )
            return false;
        const DeckBattleCardDef* pDef = getCardDef( _hand.getAt( handIndex ) );
        if ( pDef == nullptr || _energy < pDef->_cost )
            return false;
        _energy -= pDef->_cost;
        // 효과(뽑기)가 손을 바꾸기 전에 낸 패를 손에서 뺀다.
        const Card card = _hand.removeAt( handIndex );
        for ( const DeckBattleEffect& effect : pDef->_listEffect )
        {
            applyEffect( effect );
        }
        if ( pDef->_bExhaust )
            _exhaustPile.push( card );
        else
            _discardPile.push( card );
        if ( _enemy._hp <= 0 )
            _phase = DeckBattlePhase::Won;
        return true;
    }

    void DeckBattle::endTurn()
    {
        if ( _phase != DeckBattlePhase::PlayerTurn )
            return;
        (void)_hand.drawInto( _discardPile, _hand.getCount() );
        // 적 차례에서는 방어가 적 차례 시작에 사라진다.
        _enemy._block = 0;
        if ( _enemy._listIntent.empty() == false )
        {
            const DeckBattleEffect& intent = _enemy._listIntent[static_cast<size_t>( _enemy._intentIndex ) % _enemy._listIntent.size()];
            ++_enemy._intentIndex;
            if ( intent._kind == DeckBattleEffectKind::Damage )
            {
                const int32 absorbed = MathUtil::min( _playerBlock, intent._amount );
                _playerBlock -= absorbed;
                _playerHp = MathUtil::max( 0, _playerHp - ( intent._amount - absorbed ) );
            }
            else if ( intent._kind == DeckBattleEffectKind::Block )
            {
                _enemy._block += intent._amount;
            }
        }
        if ( _playerHp <= 0 )
        {
            _phase = DeckBattlePhase::Lost;
            return;
        }
        startTurn();
    }

    void DeckBattle::writeState( Archive& outArchive ) const
    {
        outArchive << static_cast<uint32>( _listDefIndex.size() );
        for ( const int32 defIndex : _listDefIndex )
        {
            StateArchiveUtil::writeName( outArchive, _pCatalog->getAt( static_cast<size_t>( defIndex ) )._id );
        }
        _drawPile.writeState( outArchive );
        _hand.writeState( outArchive );
        _discardPile.writeState( outArchive );
        _exhaustPile.writeState( outArchive );
        outArchive << static_cast<uint32>( _enemy._listIntent.size() );
        for ( const DeckBattleEffect& intent : _enemy._listIntent )
        {
            outArchive << static_cast<uint8>( intent._kind );
            outArchive << intent._amount;
        }
        outArchive << _enemy._hp;
        outArchive << _enemy._block;
        outArchive << _enemy._intentIndex;
        StateArchiveUtil::writeRandom( outArchive, _random );
        outArchive << _playerHp;
        outArchive << _playerBlock;
        outArchive << _energy;
        outArchive << _turn;
        outArchive << static_cast<uint8>( _phase );
    }

    bool DeckBattle::readState( Archive& archive )
    {
        if ( _pCatalog == nullptr )
            return false;
        // 사본에 읽고 끝까지 맞으면 그때 바꾼다. 카탈로그와 설정은 사본도 그대로 가지고 있다.
        DeckBattle battle   = *this;
        uint32     defCount = 0;
        // 카드마다 정의 id 이름(4) 이상
        if ( StateArchiveUtil::readCount( archive, 4, defCount ) == false )
            return false;
        battle._listDefIndex.assign( defCount, -1 );
        for ( int32& defIndex : battle._listDefIndex )
        {
            hashed_string defID;
            if ( StateArchiveUtil::readName( archive, defID ) == false )
                return false;
            defIndex = _pCatalog->findIndex( defID );
            if ( defIndex < 0 )
                return false;
        }
        const bool bPilesRead = battle._drawPile.readState( archive ) && battle._hand.readState( archive ) && battle._discardPile.readState( archive ) &&
                                battle._exhaustPile.readState( archive );
        if ( bPilesRead == false )
            return false;

        uint32 intentCount = 0;
        // 의도마다 종류(1) + 양(4)
        if ( StateArchiveUtil::readCount( archive, 5, intentCount ) == false )
            return false;
        battle._enemy._listIntent.assign( intentCount, DeckBattleEffect{} );
        for ( DeckBattleEffect& intent : battle._enemy._listIntent )
        {
            uint8 kind = 0;
            archive >> kind;
            archive >> intent._amount;
            if ( kind > static_cast<uint8>( DeckBattleEffectKind::Energy ) )
                return false;
            intent._kind = static_cast<DeckBattleEffectKind>( kind );
        }
        archive >> battle._enemy._hp;
        archive >> battle._enemy._block;
        archive >> battle._enemy._intentIndex;
        if ( StateArchiveUtil::readRandom( archive, battle._random ) == false )
            return false;
        uint8 phase = 0;
        archive >> battle._playerHp;
        archive >> battle._playerBlock;
        archive >> battle._energy;
        archive >> battle._turn;
        archive >> phase;
        const bool bInvalidBattleState = archive.isError() || phase > static_cast<uint8>( DeckBattlePhase::Lost ) || battle._enemy._intentIndex < 0;
        if ( bInvalidBattleState )
            return false;
        battle._phase = static_cast<DeckBattlePhase>( phase );

        // 더미의 카드 번호가 덱 안이어야 한다(`getCardDef` 가 카탈로그 index 를 찾는다).
        const size_t cardCount = battle._listDefIndex.size();
        const bool   bIDsValid = DeckBattleInternal::arePileIDsInDeck( battle._drawPile, cardCount ) && DeckBattleInternal::arePileIDsInDeck( battle._hand, cardCount ) &&
                               DeckBattleInternal::arePileIDsInDeck( battle._discardPile, cardCount ) &&
                               DeckBattleInternal::arePileIDsInDeck( battle._exhaustPile, cardCount );
        if ( bIDsValid == false )
            return false;
        *this = std::move( battle );
        return true;
    }
} // namespace sw

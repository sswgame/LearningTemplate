#include "pch.h"

#include "GameFramework/Kits/Casual/CardGame/DeckBattle.h"

#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Utility/Xml/XmlDocument.h"

#include "GameFramework/Data/GameDataXml.h"

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
        };
    } // namespace
} // namespace sw

namespace sw
{
    DeckBattleCatalog::DeckBattleCatalog()
        : _catalog{}
    {
    }

    bool DeckBattleCatalog::loadFromResource( string_view path )
    {
        return GameDataXml::loadFile( *this, &DeckBattleCatalog::loadRoot, path, "DeckBattleCatalog" );
    }

    bool DeckBattleCatalog::loadFromXmlText( string_view xmlText, string_view sourceName )
    {
        return GameDataXml::loadText( *this, &DeckBattleCatalog::loadRoot, xmlText, sourceName, "DeckBattleCatalog" );
    }

    uint32 DeckBattleCatalog::loadRoot( const XmlNode& root, string_view sourceName )
    {
        uint32 loadedCount = 0;
        for ( XmlNode node = root.findChild( "Card" ); node; node = node.findNextSibling( "Card" ) )
        {
            const utf8* pId = GameDataXml::findRequiredId( node, sourceName );
            if ( pId == nullptr )
                continue;
            DeckBattleCardDef def;
            def._id           = hashed_string( pId );
            const utf8* pName = node.findAttribute( "name" );
            def._name         = pName != nullptr ? pName : pId;
            def._cost         = MathUtil::max( 0, node.getAttributeInt( "cost", 1 ) );
            def._bExhaust     = node.getAttributeBool( "exhaust", false ) ? SW_TRUE : SW_FALSE;
            GameDataXml::forEachToken( node.getAttributeText( "effects" ), ",; ", [&]( string_view token )
            {
                const size_t     colon = token.find( ':' );
                DeckBattleEffect effect;
                const bool       bKind   = DeckBattleInternal::parseEffectKind( token.substr( 0, colon ), effect._kind );
                const bool       bAmount = colon != string_view::npos && StringUtil::parseInt( token.substr( colon + 1 ), effect._amount );
                if ( bKind && bAmount )
                    def._listEffect.push_back( effect );
                else
                    SW_LOG_WARNING( "%#: card '%#' has an unreadable effect '%#'", sourceName, pId, token );
            } );
            if ( _catalog.add( def ) >= 0 )
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

    bool DeckBattle::initialize( const DeckBattleCatalog* pCatalog, const DeckBattleSettings& settings, const vector<hashed_string>& listDeckCardId,
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
        for ( const hashed_string& cardId : listDeckCardId )
        {
            const int32 defIndex = pCatalog->findIndex( cardId );
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
        return &_pCatalog->getAt( _listDefIndex[card._id] );
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
                // 버린 더미를 섞어 다시 뽑을 더미로.
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
        if ( _phase != DeckBattlePhase::PlayerTurn || handIndex < 0 || _hand.getCount() <= handIndex )
            return false;
        const DeckBattleCardDef* pDef = getCardDef( _hand.getAt( handIndex ) );
        if ( pDef == nullptr || _energy < pDef->_cost )
            return false;
        _energy -= pDef->_cost;
        // 효과(뽑기)가 손을 바꾸기 전에 낸 패를 손에서 뺀다.
        const Card card = _hand.removeAt( handIndex );
        for ( const DeckBattleEffect& effect : pDef->_listEffect )
            applyEffect( effect );
        if ( pDef->_bExhaust == SW_TRUE )
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
        // 적 차례 — 방어는 적 차례 시작에 사라진다.
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
} // namespace sw

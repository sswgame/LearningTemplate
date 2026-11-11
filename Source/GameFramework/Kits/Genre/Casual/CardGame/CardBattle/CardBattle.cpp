#include "pch.h"

#include "GameFramework/Kits/Genre/Casual/CardGame/CardBattle/CardBattle.h"

#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Module/ModuleSharedAssetCache.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"
#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Runtime/Foundation/GameData/DepletingValue.h"
#include "GameFramework/Runtime/Foundation/GameData/GameDataXML.h"
#include "GameFramework/Runtime/Foundation/GameData/XMLCatalogTable.h"

namespace sw
{
    namespace
    {
        struct CardBattleInternal
        {
            [[nodiscard]] static bool parseEffectKind( string_view text, CardBattleEffectKind& outKind )
            {
                if ( StringUtil::equals( text, "Damage" ) )
                    outKind = CardBattleEffectKind::Damage;
                else if ( StringUtil::equals( text, "Block" ) )
                    outKind = CardBattleEffectKind::Block;
                else if ( StringUtil::equals( text, "Draw" ) )
                    outKind = CardBattleEffectKind::Draw;
                else if ( StringUtil::equals( text, "Energy" ) )
                    outKind = CardBattleEffectKind::Energy;
                else
                    return false;
                return true;
            }

            /** @brief 더미 넷이 덱(@p cardCount 장)을 빠짐없이 한 장씩 나눠 가지는지 반환합니다. 전투 중에 카드는 더미 사이를 옮겨 다닐 뿐 늘거나 줄지 않습니다. */
            [[nodiscard]] static bool arePilesExactlyDeck( const vector<const CardPile*>& listPile, size_t cardCount )
            {
                size_t heldCount = 0;
                for ( const CardPile* pPile : listPile )
                {
                    heldCount += static_cast<size_t>( pPile->getCount() );
                }
                // 카드 번호는 `getCardDef` 가 찾는 덱 index 이고 무늬와 숫자는 쓰지 않는다(`initialize` 가 0 으로 둔다).
                const auto isDeckCard = [cardCount]( const Card& card )
                { return card._id < cardCount && card._suit == 0 && card._rank == 0; };
                return heldCount == cardCount && CardPile::arePilesValid( listPile, isDeckCard );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    CardBattleCatalog::CardBattleCatalog()
    {
    }

    uint32 CardBattleCatalog::loadRoot( const XMLNode& root, string_view sourceName )
    {
        // 칸의 기본값은 Def 의 초기값, 범위는 `PROPERTY( Min · Max )` 다. 틀린 카드는 넣지 않고 오류다(readProperties 가 남긴다).
        using Element                          = XMLCatalogElement<CardBattleCatalog>;
        static constexpr Element kArrElement[] = {
            Element::of<&CardBattleCatalog::loadCard>( "Card" ),
        };
        return XMLCatalogReadResult::readInDocumentOrder( *this, root, sourceName, kArrElement )._loadedCount;
    }

    bool CardBattleCatalog::loadCard( const XMLNode& node, string_view sourceName )
    {
        CardBattleCardDef def;
        bool              bValid = XMLFields::readProperties( node, def, sourceName, { "effects" } );
        XMLReadUtil::forEachNamedNumber( node.getAttributeText( "effects" ), ",; ", 0, XMLReadUtil::MissingValue::Reject,
                                         [&]( string_view token, string_view kindName, int32 amount, bool bAmount )
        {
            CardBattleEffect effect;
            const bool       bKind = CardBattleInternal::parseEffectKind( kindName, effect._kind );
            effect._amount         = amount;
            if ( bKind && bAmount )
                def._listEffect.push_back( effect );
            else
            {
                SW_LOG_ERROR_ID( "CardBattle.CardUnreadableEffect", "%#: card '%#' has an unreadable effect '%#'", sourceName, def._id.c_str(), token );
                bValid = false;
            }
        } );
        return bValid && add( def ) >= 0;
    }

    CardBattle::CardBattle()
        : _listDefIndex{}
        , _drawPile{}
        , _hand{}
        , _discardPile{}
        , _exhaustPile{}
        , _enemy{}
        , _settings{}
        , _random{}
        , _pCatalog{ nullptr }
        , _playerHP{ 0 }
        , _playerBlock{ 0 }
        , _energy{ 0 }
        , _turn{ 0 }
        , _phase{ CardBattlePhase::Lost }
    {
    }

    bool CardBattle::initialize( const CardBattleCatalog* pCatalog, const CardBattleSettings& settings, const vector<hashed_string>& listDeckCardID,
                                 const CardBattleEnemy& enemy, uint32 seed )
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
        _playerHP    = settings._playerHP;
        _playerBlock = 0;
        _turn        = 0;
        if ( pCatalog == nullptr )
            return false;
        for ( const hashed_string& cardID : listDeckCardID )
        {
            const optional<int32> defIndex = pCatalog->findIndex( cardID );
            if ( defIndex.has_value() == false )
                continue;
            _drawPile.push( Card{ static_cast<uint16>( _listDefIndex.size() ), 0, 0 } );
            _listDefIndex.push_back( *defIndex );
        }
        if ( _drawPile.isEmpty() )
            return false;
        _drawPile.shuffle( _random );
        _phase = CardBattlePhase::PlayerTurn;
        startTurn();
        return true;
    }

    const CardBattleCardDef* CardBattle::getCardDef( const Card& card ) const
    {
        if ( _pCatalog == nullptr || card._id >= _listDefIndex.size() )
            return nullptr;
        return &_pCatalog->getAt( static_cast<size_t>( _listDefIndex[card._id] ) );
    }

    void CardBattle::startTurn()
    {
        ++_turn;
        _playerBlock = 0;
        _energy      = _settings._energyPerTurn;
        drawCards( _settings._drawPerTurn );
    }

    void CardBattle::drawCards( int32 count )
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

    void CardBattle::applyEffect( const CardBattleEffect& effect )
    {
        switch ( effect._kind )
        {
            case CardBattleEffectKind::Damage:
            {
                const int32 absorbed = MathUtil::min( _enemy._block, effect._amount );
                _enemy._block -= absorbed;
                DepletingValue::take( _enemy._hp, effect._amount - absorbed );
                break;
            }
            case CardBattleEffectKind::Block:
            {
                _playerBlock += effect._amount;
                break;
            }
            case CardBattleEffectKind::Draw:
            {
                drawCards( effect._amount );
                break;
            }
            case CardBattleEffectKind::Energy:
            {
                _energy += effect._amount;
                break;
            }
        }
    }

    bool CardBattle::playCard( int32 handIndex )
    {
        const bool bCannotPlayCard = _phase != CardBattlePhase::PlayerTurn || handIndex < 0 || _hand.getCount() <= handIndex;
        if ( bCannotPlayCard )
            return false;
        const CardBattleCardDef* pDef = getCardDef( _hand.getAt( handIndex ) );
        if ( pDef == nullptr || _energy < pDef->_cost )
            return false;
        _energy -= pDef->_cost;
        // 효과(뽑기)가 손을 바꾸기 전에 낸 패를 손에서 뺀다.
        const Card card = _hand.removeAt( handIndex );
        for ( const CardBattleEffect& effect : pDef->_listEffect )
        {
            applyEffect( effect );
        }
        if ( pDef->_bExhaust )
            _exhaustPile.push( card );
        else
            _discardPile.push( card );
        if ( _enemy._hp <= 0 )
            _phase = CardBattlePhase::Won;
        return true;
    }

    void CardBattle::endTurn()
    {
        if ( _phase != CardBattlePhase::PlayerTurn )
            return;
        (void)_hand.drawInto( _discardPile, _hand.getCount() );
        // 적 차례에서는 방어가 적 차례 시작에 사라진다.
        _enemy._block = 0;
        if ( _enemy._listIntent.empty() == false )
        {
            const CardBattleEffect& intent = _enemy._listIntent[static_cast<size_t>( _enemy._intentIndex ) % _enemy._listIntent.size()];
            ++_enemy._intentIndex;
            if ( intent._kind == CardBattleEffectKind::Damage )
            {
                const int32 absorbed = MathUtil::min( _playerBlock, intent._amount );
                _playerBlock -= absorbed;
                DepletingValue::take( _playerHP, intent._amount - absorbed );
            }
            else if ( intent._kind == CardBattleEffectKind::Block )
            {
                _enemy._block += intent._amount;
            }
        }
        if ( _playerHP <= 0 )
        {
            _phase = CardBattlePhase::Lost;
            return;
        }
        startTurn();
    }

    void CardBattle::serializeState( StateIO& io )
    {
        if ( io.check( _pCatalog != nullptr ) == false )
            return;
        // 카드마다 정의 id 이름(4) 이상
        io.list( _listDefIndex, 4, [&]( int32& defIndex )
        {
            hashed_string defID = io.isWriting() ? _pCatalog->getAt( static_cast<size_t>( defIndex ) )._id : hashed_string{};
            io.value( defID );
            if ( io.isReading() )
            {
                const optional<int32> found = _pCatalog->findIndex( defID );
                if ( io.check( found.has_value() ) )
                    defIndex = *found;
            }
        } );
        io.value( _drawPile );
        io.value( _hand );
        io.value( _discardPile );
        io.value( _exhaustPile );
        // 의도마다 종류(1) + 양(4)
        io.list( _enemy._listIntent, 5, [&]( CardBattleEffect& intent )
        {
            io.enumValue( intent._kind, CardBattleEffectKind::Energy );
            io.value( intent._amount );
            io.check( 0 <= intent._amount ); // 음수 공격은 `endTurn` 의 빼기가 더하기로 뒤집혀 체력과 방어를 채운다
        } );
        io.value( _enemy._hp );
        io.value( _enemy._block );
        io.value( _enemy._intentIndex );
        io.value( _random );
        io.value( _playerHP );
        io.value( _playerBlock );
        io.value( _energy );
        io.value( _turn );
        io.enumValue( _phase, CardBattlePhase::Lost );
        if ( io.isWriting() || io.isOk() == false )
            return;
        io.check( 0 <= _enemy._hp && 0 <= _enemy._block && 0 <= _enemy._intentIndex );
        io.check( 0 <= _playerHP && _playerHP <= _settings._playerHP && 0 <= _playerBlock && 0 <= _energy && 0 <= _turn );
        // 끝난 판은 끝난 이유가 값에 남는다(이긴 적 없는데 이겼다고 적은 바이트를 받지 않는다).
        io.check( ( _phase != CardBattlePhase::Won || _enemy._hp == 0 ) && ( _phase != CardBattlePhase::Lost || _playerHP == 0 ) );
        io.check( _hand.getCount() <= _settings._handLimit && CardBattleInternal::arePilesExactlyDeck( { &_drawPile, &_hand, &_discardPile, &_exhaustPile }, _listDefIndex.size() ) );
    }

} // namespace sw

namespace sw
{
    /** 쓰는 쪽은 공유 캐시의 한 벌을 받는다(`SharedAssetCache<CardBattleCatalog>::acquire`). 경로 규칙 없이 접미사로 가르는 종류라 카탈로그 편집기에는 나오지 않는다. */
    SW_MODULE_SHARED_ASSET_CACHE( CardBattleCatalog, CardBattleCatalog, SW_KIT_API, CardBattleCatalog::kAssetType.withLoader( AssetTypeLoader::of<CardBattleCatalog>( MemoryTag::Game ) ) );
} // namespace sw

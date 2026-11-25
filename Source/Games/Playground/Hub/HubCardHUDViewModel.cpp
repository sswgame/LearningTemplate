#include "pch.h"

#include "Games/Playground/Hub/HubCardHUDViewModel.h"

#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"

#include "Engine/Localization/LocText.h"

#include "Games/Playground/Hub/HubCardTableComponent.h"

namespace sw
{
    namespace
    {
        struct HubCardHUDInternal
        {
            /** @brief 색의 표 키입니다(`SheddingCardColor` 차례 — 와일드는 색이 없다). */
            static constexpr const utf8* kArrColorKey[] = { SW_LOCKEYREF( "HubCard", "Red" ), SW_LOCKEYREF( "HubCard", "Yellow" ), SW_LOCKEYREF( "HubCard", "Green" ), SW_LOCKEYREF( "HubCard", "Blue" ) };
            /** @brief 숫자가 아닌 값의 표 키입니다(`SheddingCardValue::Skip` 부터 차례). */
            static constexpr const utf8* kArrValueKey[] = { SW_LOCKEYREF( "HubCard", "Skip" ), SW_LOCKEYREF( "HubCard", "Reverse" ), SW_LOCKEYREF( "HubCard", "DrawTwo" ), SW_LOCKEYREF( "HubCard", "Wild" ), SW_LOCKEYREF( "HubCard", "WildDrawFour" ) };
            static constexpr uint32      kFirstKeyValue = static_cast<uint32>( SheddingCardValue::Skip );

            /** @brief 카드 하나의 화면 글입니다(그릴 때 지금 언어로 풀린다). */
            static string makeCardText( const CardPile& deck, uint16 cardID )
            {
                const Card& card = deck.getAt( cardID ); // 번호는 받을 때 덱 안인지 봤다(`readSeatView`)
                if ( card._suit >= SW_COUNT_OF( kArrColorKey ) )
                    return string( kArrValueKey[card._rank - kFirstKeyValue] ); // 와일드
                LocalizedMessage message = SW_LOCALIZED_MESSAGE( "HubCard", "Card" ).addKey( "color", kArrColorKey[card._suit] );
                if ( card._rank < kFirstKeyValue )
                    return message.addInteger( "value", card._rank ).str();
                return message.addKey( "value", kArrValueKey[card._rank - kFirstKeyValue] ).str();
            }

            static WidgetVisibility toVisibility( bool bShown ) { return bShown ? WidgetVisibility::HitTestInvisible : WidgetVisibility::Collapsed; }
        };
    } // namespace
} // namespace sw

namespace sw
{
    HubCardHUDViewModel::HubCardHUDViewModel()
        : UIViewModel{}
        , _hand{}
        , _topCard{}
        , _opponentCount{ 0 }
        , _tableVisibility{ WidgetVisibility::Collapsed }
        , _turnVisibility{ WidgetVisibility::Collapsed }
    {
    }

    HubCardHUDViewModel::~HubCardHUDViewModel() = default;

    const TypeInfo* HubCardHUDViewModel::getTypeInfo() const
    {
        return StaticType();
    }

    void HubCardHUDViewModel::setTable( bool bSeated, const HubCardSeatView& view, bool bMyTurn )
    {
        using Internal = HubCardHUDInternal;
        (void)setField( _tableVisibility, Internal::toVisibility( bSeated ), "_tableVisibility" );
        (void)setField( _turnVisibility, Internal::toVisibility( bMyTurn ), "_turnVisibility" );
        if ( bSeated == false )
            return;
        CardPile deck;
        SheddingCardGame::makeDeck( deck );
        vector<string> listHand;
        listHand.reserve( view._listHandCardID.size() );
        for ( const uint16 cardID : view._listHandCardID )
        {
            listHand.push_back( Internal::makeCardText( deck, cardID ) );
        }
        (void)setField( _hand, LocalizedMessage::join( " ", listHand ), "_hand" );
        (void)setField( _topCard, Internal::makeCardText( deck, view._topCardID ), "_topCard" );
        (void)setField( _opponentCount, view._opponentCardCount, "_opponentCount" );
    }
} // namespace sw

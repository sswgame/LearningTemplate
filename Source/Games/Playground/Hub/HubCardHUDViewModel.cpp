#include "pch.h"

#include "Games/Playground/Hub/HubCardHUDViewModel.h"

#include "Core/Foundation/Constants.h"
#include "Core/String/StringBuilder.h"

#include "Games/Playground/Hub/HubCardTableComponent.h"

namespace sw
{
    namespace
    {
        struct HubCardHUDInternal
        {
            static constexpr const utf8* kArrColor[] = { "R", "Y", "G", "B", "" };
            static constexpr const utf8* kArrValue[] = { "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "S", "V", "+2", "W", "W+4" }; // 값 차례(`SheddingCardValue`)

            static void appendCard( const CardPile& deck, uint16 cardID, StringBuilder<constant::buffer::kMax1024>& builder )
            {
                const Card& card = deck.getAt( cardID ); // 번호는 받을 때 덱 안인지 봤다(`readSeatView`)
                builder.append( kArrColor[card._suit] );
                builder.append( kArrValue[card._rank] );
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
        StringBuilder<constant::buffer::kMax1024> hand;
        for ( const uint16 cardID : view._listHandCardID )
        {
            if ( hand.size() > 0 )
                hand.append( " " );
            Internal::appendCard( deck, cardID, hand );
        }
        StringBuilder<constant::buffer::kMax1024> top;
        Internal::appendCard( deck, view._topCardID, top );
        (void)setField( _hand, string( hand.view() ), "_hand" );
        (void)setField( _topCard, string( top.view() ), "_topCard" );
        (void)setField( _opponentCount, view._opponentCardCount, "_opponentCount" );
    }
} // namespace sw

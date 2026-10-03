#include "pch.h"

#include "GameFramework/Kits/CardGame/HwatuDeck.h"

namespace sw
{
    namespace
    {
        struct HwatuDeckInternal
        {
            static constexpr int32 kCardsPerMonth = 4;

            static HwatuCardInfo makeInfo( int32 month, HwatuKind kind, HwatuRibbon ribbon = HwatuRibbon::None, uint8 piValue = 0 )
            {
                HwatuCardInfo info;
                info._month   = static_cast<uint8>( month );
                info._kind    = kind;
                info._ribbon  = ribbon;
                info._piValue = kind == HwatuKind::Pi ? ( piValue > 0 ? piValue : 1 ) : 0;
                return info;
            }

            struct Table
            {
                HwatuCardInfo _arrInfo[HwatuDeck::kCardCount]{};

                Table()
                {
                    // 월마다 앞의 둘은 특별한 장(광 · 열끗 · 띠), 뒤는 피. 12 월만 넷이 다르다.
                    for ( int32 month = 1; month <= HwatuDeck::kMonthCount; ++month )
                    {
                        set( month, 2, makeInfo( month, HwatuKind::Pi ) );
                        set( month, 3, makeInfo( month, HwatuKind::Pi ) );
                    }
                    set( 1, 0, makeInfo( 1, HwatuKind::Gwang ) );
                    set( 1, 1, makeInfo( 1, HwatuKind::Tti, HwatuRibbon::Hong ) );
                    set( 2, 0, makeGodori( 2 ) );
                    set( 2, 1, makeInfo( 2, HwatuKind::Tti, HwatuRibbon::Hong ) );
                    set( 3, 0, makeInfo( 3, HwatuKind::Gwang ) );
                    set( 3, 1, makeInfo( 3, HwatuKind::Tti, HwatuRibbon::Hong ) );
                    set( 4, 0, makeGodori( 4 ) );
                    set( 4, 1, makeInfo( 4, HwatuKind::Tti, HwatuRibbon::Cho ) );
                    set( 5, 0, makeInfo( 5, HwatuKind::Yeol ) );
                    set( 5, 1, makeInfo( 5, HwatuKind::Tti, HwatuRibbon::Cho ) );
                    set( 6, 0, makeInfo( 6, HwatuKind::Yeol ) );
                    set( 6, 1, makeInfo( 6, HwatuKind::Tti, HwatuRibbon::Cheong ) );
                    set( 7, 0, makeInfo( 7, HwatuKind::Yeol ) );
                    set( 7, 1, makeInfo( 7, HwatuKind::Tti, HwatuRibbon::Cho ) );
                    set( 8, 0, makeInfo( 8, HwatuKind::Gwang ) );
                    set( 8, 1, makeGodori( 8 ) );
                    HwatuCardInfo septemberYeol   = makeInfo( 9, HwatuKind::Yeol );
                    septemberYeol._bSeptemberYeol = SW_TRUE;
                    set( 9, 0, septemberYeol );
                    set( 9, 1, makeInfo( 9, HwatuKind::Tti, HwatuRibbon::Cheong ) );
                    set( 10, 0, makeInfo( 10, HwatuKind::Yeol ) );
                    set( 10, 1, makeInfo( 10, HwatuKind::Tti, HwatuRibbon::Cheong ) );
                    set( 11, 0, makeInfo( 11, HwatuKind::Gwang ) );
                    set( 11, 1, makeInfo( 11, HwatuKind::Pi, HwatuRibbon::None, 2 ) );
                    HwatuCardInfo rainGwang = makeInfo( 12, HwatuKind::Gwang );
                    rainGwang._bRainGwang   = SW_TRUE;
                    set( 12, 0, rainGwang );
                    set( 12, 1, makeInfo( 12, HwatuKind::Yeol ) );
                    set( 12, 2, makeInfo( 12, HwatuKind::Tti, HwatuRibbon::None ) );
                    set( 12, 3, makeInfo( 12, HwatuKind::Pi, HwatuRibbon::None, 2 ) );
                }

                void set( int32 month, int32 indexInMonth, const HwatuCardInfo& info ) { _arrInfo[( month - 1 ) * kCardsPerMonth + indexInMonth] = info; }

                static HwatuCardInfo makeGodori( int32 month )
                {
                    HwatuCardInfo info = makeInfo( month, HwatuKind::Yeol );
                    info._bGodori      = SW_TRUE;
                    return info;
                }
            };

            static const Table& getTable()
            {
                static const Table s_table;
                return s_table;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    void HwatuDeck::makeDeck( CardPile& outPile )
    {
        outPile.clear();
        for ( int32 month = 1; month <= kMonthCount; ++month )
        {
            for ( int32 indexInMonth = 0; indexInMonth < HwatuDeckInternal::kCardsPerMonth; ++indexInMonth )
                outPile.push( makeCard( month, indexInMonth ) );
        }
    }

    const HwatuCardInfo& HwatuDeck::getInfo( uint16 cardId )
    {
        static const HwatuCardInfo s_emptyInfo{};
        if ( cardId >= kCardCount )
            return s_emptyInfo;
        return HwatuDeckInternal::getTable()._arrInfo[cardId];
    }

    Card HwatuDeck::makeCard( int32 month, int32 indexInMonth )
    {
        const uint16         cardId = static_cast<uint16>( ( month - 1 ) * HwatuDeckInternal::kCardsPerMonth + indexInMonth );
        const HwatuCardInfo& info   = getInfo( cardId );
        return Card{ cardId, static_cast<uint8>( info._kind ), info._month };
    }
} // namespace sw

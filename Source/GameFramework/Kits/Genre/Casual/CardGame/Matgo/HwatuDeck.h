/**
 * @file HwatuDeck.h
 * @brief 화투 48장을 정의합니다. 월 12개에 4장씩이며, 종류(광, 열끗, 띠, 피), 띠 색(홍단, 청단, 초단), 쌍피, 고도리 새, 비광을 구분합니다.
 * @details 카드 번호는 (월 - 1) x 4 + 월 안의 순서(0..3)이고, `Card::_rank` 는 월, `Card::_suit` 는 종류(`HwatuKind`)이다.
 *          월별 구성은 다음과 같다. 1 송학(광, 홍단, 피, 피), 2 매조(고도리, 홍단, 피, 피), 3 벚꽃(광, 홍단, 피, 피), 4 흑싸리(고도리, 초단, 피, 피),
 *          5 난초(열끗, 초단, 피, 피), 6 모란(열끗, 청단, 피, 피), 7 홍싸리(열끗, 초단, 피, 피), 8 공산(광, 고도리, 피, 피),
 *          9 국진(열끗(쌍피로 쓸 수 있음), 청단, 피, 피), 10 단풍(열끗, 청단, 피, 피), 11 오동(광, 쌍피, 피, 피), 12 비(비광, 열끗, 띠, 쌍피).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/Casual/CardGame/CardDeck.h"

namespace sw
{
    /** @brief 화투의 종류입니다. */
    enum class HwatuKind : uint8
    {
        Gwang = 0, ///< 광
        Yeol,      ///< 열끗(동물)
        Tti,       ///< 띠
        Pi         ///< 피(쌍피 포함)
    };

    /** @brief 띠 색(단)입니다. */
    enum class HwatuRibbon : uint8
    {
        None = 0, ///< 비띠이다. 단에 들지 않는다
        Hong,     ///< 홍단(1, 2, 3월)
        Cheong,   ///< 청단(6, 9, 10월)
        Cho       ///< 초단(4, 5, 7월)
    };

    /** @brief 화투 한 장의 속성입니다. */
    struct HwatuCardInfo
    {
        uint8       _month{ 0 };
        uint8       _piValue{ 0 }; ///< 피 장수로 세는 값이다(피 1, 쌍피 2, 그 밖 0)
        HwatuKind   _kind{ HwatuKind::Pi };
        HwatuRibbon _ribbon{ HwatuRibbon::None };
        uint8       _bGodori{ SW_FALSE };        ///< 고도리 새이다(2, 4, 8월 열끗)
        uint8       _bRainGwang{ SW_FALSE };     ///< 비광이다(12월 광)
        uint8       _bSeptemberYeol{ SW_FALSE }; ///< 국진 열끗이다. 설정에 따라 쌍피로 센다
    };
} // namespace sw

namespace sw
{
    /** @brief 화투 덱입니다. 상태가 없습니다. */
    struct SW_KIT_API HwatuDeck
    {
        static constexpr int32 kCardCount  = 48;
        static constexpr int32 kMonthCount = 12;

        /** @brief 48장을 번호 순으로 채웁니다(섞지 않습니다). */
        static void makeDeck( CardPile& outPile );
        /** @brief 카드 번호의 속성을 반환합니다. 범위 밖이면 빈 속성(월 0)을 반환합니다. */
        static const HwatuCardInfo& getInfo( uint16 cardID );
        static Card                 makeCard( int32 month, int32 indexInMonth );
        static uint8                getMonth( const Card& card ) { return getInfo( card._id )._month; }
    };
} // namespace sw

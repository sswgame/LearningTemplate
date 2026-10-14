/**
 * @file PokerHand.h
 * @brief 포커 족보를 판정합니다. 5장의 족보와 키커를 계산하고, 7장(홀 카드 2장 + 보드 5장) 중 가장 센 5장을 고릅니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/Casual/CardGame/CardDeck.h"

namespace sw
{
    /** @brief 족보입니다. 아래로 갈수록 약하고 위로 갈수록 셉니다. */
    enum class PokerHandRank : uint8
    {
        HighCard = 0,
        OnePair,
        TwoPair,
        ThreeOfAKind,
        Straight, ///< A-2-3-4-5(휠)를 포함한다. 휠의 높은 카드는 5 이다
        Flush,
        FullHouse,
        FourOfAKind,
        StraightFlush,
        RoyalFlush
    };

    /**
     * @struct PokerHandValue
     * @brief 판정 결과입니다. `_score` 하나로 크기를 비교합니다(족보 << 20 | 비교 숫자 5개 x 4비트).
     * @details 비교 숫자는 먼저 비교할 것부터 담는다. 풀하우스는 트리플과 페어, 투페어는 높은 페어, 낮은 페어, 키커 순이다. 에이스는 14 이다.
     */
    struct PokerHandValue
    {
        Card          _arrBest[5]{};     ///< 고른 5장이다
        uint32        _score{ 0 };       ///< 클수록 센 패이고, 같으면 무승부이다
        uint8         _arrTieBreak[5]{}; ///< 비교 숫자이다(2..14, 0 은 없음)
        PokerHandRank _rank{ PokerHandRank::HighCard };
    };
} // namespace sw

namespace sw
{
    /** @brief 족보를 판정합니다. 상태가 없습니다. */
    struct SW_KIT_API PokerHandEvaluator
    {
        static constexpr uint8 kAceHigh = 14;

        /** @brief 트럼프 숫자(에이스 = 1)를 포커 숫자(에이스 = 14)로 바꿉니다. */
        static uint8 toPokerRank( uint8 rank ) { return rank == StandardRank::kAce ? kAceHigh : rank; }
        /** @brief 정확히 5장의 족보를 판정합니다. */
        static PokerHandValue evaluateFive( const Card* pCard );
        /** @brief @p count 장(5..7) 중 가장 센 5장을 판정합니다. 5장보다 적으면 점수 0 인 빈 결과를 반환합니다. */
        static PokerHandValue evaluateBest( const Card* pCard, int32 count );
        /** @brief 음수이면 @p lhs 가 약하고, 0 이면 무승부이고, 양수이면 @p lhs 가 센 것입니다. */
        static int32 compareHands( const PokerHandValue& lhs, const PokerHandValue& rhs );
    };
} // namespace sw

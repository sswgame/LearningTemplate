/**
 * @file PokerHand.h
 * @brief 포커 족보 판정 — 5 장의 족보 · 키커와, 7 장(홀 카드 2 + 보드 5) 중 가장 센 5 장입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Casual/CardGame/CardDeck.h"

namespace sw
{
    /** @brief 족보입니다(위로 갈수록 셉니다). */
    enum class PokerHandRank : uint8
    {
        HighCard = 0,
        OnePair,
        TwoPair,
        ThreeOfAKind,
        Straight, ///< A-2-3-4-5(휠) 포함 — 휠의 높은 카드는 5
        Flush,
        FullHouse,
        FourOfAKind,
        StraightFlush,
        RoyalFlush
    };

    /**
     * @struct PokerHandValue
     * @brief 판정 결과입니다. `_score` 하나로 크기를 비교합니다(족보 << 20 | 비교 숫자 다섯 개 × 4 비트).
     * @details 비교 숫자는 "먼저 비교할 것부터" 입니다 — 풀하우스는 트리플 · 페어, 투페어는 높은 페어 · 낮은 페어 · 키커. 에이스는 14 입니다.
     */
    struct PokerHandValue
    {
        Card          _arrBest[5]{};     ///< 고른 5 장
        uint32        _score{ 0 };       ///< 클수록 센 패 · 같으면 무승부
        uint8         _arrTieBreak[5]{}; ///< 비교 숫자(2..14, 0 = 없음)
        PokerHandRank _rank{ PokerHandRank::HighCard };
    };
} // namespace sw

namespace sw
{
    /** @brief 족보 판정입니다. 상태가 없습니다. */
    struct SW_GF_API PokerHandEvaluator
    {
        static constexpr uint8 kAceHigh = 14;

        /** @brief 트럼프 숫자(에이스 = 1) → 포커 숫자(에이스 = 14)입니다. */
        static uint8 toPokerRank( uint8 rank ) { return rank == StandardRank::kAce ? kAceHigh : rank; }
        /** @brief 정확히 5 장의 족보입니다. */
        static PokerHandValue evaluateFive( const Card* pCard );
        /** @brief @p count 장(5..7) 중 가장 센 5 장입니다. 5 장보다 적으면 점수 0 인 빈 결과입니다. */
        static PokerHandValue evaluateBest( const Card* pCard, int32 count );
        /** @brief 음수 = @p lhs 가 약하다, 0 = 무승부, 양수 = @p lhs 가 세다. */
        static int32 compareHands( const PokerHandValue& lhs, const PokerHandValue& rhs );
    };
} // namespace sw

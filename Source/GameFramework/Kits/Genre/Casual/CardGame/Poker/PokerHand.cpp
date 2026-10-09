#include "pch.h"

#include "GameFramework/Kits/Genre/Casual/CardGame/Poker/PokerHand.h"

namespace sw
{
    namespace
    {
        struct PokerHandInternal
        {
            static constexpr int32 kHandSize = 5;

            /** @brief 숫자별 장수를 (장수 내림, 숫자 내림)으로 늘어놓습니다. 서로 다른 숫자의 개수입니다. */
            static int32 makeGroups( const uint8* pRank, uint8* pOutGroupRank, uint8* pOutGroupCount )
            {
                uint8 arrCount[PokerHandEvaluator::kAceHigh + 1]{};
                for ( int32 index = 0; index < kHandSize; ++index )
                {
                    ++arrCount[pRank[index]];
                }
                int32 groupCount = 0;
                for ( int32 count = 4; count >= 1; --count )
                {
                    for ( int32 rank = PokerHandEvaluator::kAceHigh; rank >= 2; --rank )
                    {
                        if ( arrCount[rank] != count )
                            continue;
                        pOutGroupRank[groupCount]  = static_cast<uint8>( rank );
                        pOutGroupCount[groupCount] = static_cast<uint8>( count );
                        ++groupCount;
                    }
                }
                return groupCount;
            }

            /** @brief 스트레이트의 높은 카드입니다(휠 = 5). 아니면 0 입니다. @p pRank 는 서로 다른 숫자 5 개(내림차순). */
            static uint8 findStraightHigh( const uint8* pSortedRank )
            {
                if ( pSortedRank[0] - pSortedRank[4] == 4 )
                    return pSortedRank[0];
                const bool bWheel = pSortedRank[0] == PokerHandEvaluator::kAceHigh && pSortedRank[1] == 5 && pSortedRank[4] == 2;
                return bWheel ? 5 : 0;
            }

            static uint32 makeScore( PokerHandRank rank, const uint8* pTieBreak )
            {
                uint32 score = static_cast<uint32>( rank ) << 20;
                for ( int32 index = 0; index < kHandSize; ++index )
                {
                    score |= static_cast<uint32>( pTieBreak[index] ) << ( 16 - index * 4 );
                }
                return score;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    PokerHandValue PokerHandEvaluator::evaluateFive( const Card* pCard )
    {
        PokerHandValue value;
        uint8          arrRank[PokerHandInternal::kHandSize]{};
        bool           bFlush = true;
        for ( int32 index = 0; index < PokerHandInternal::kHandSize; ++index )
        {
            value._arrBest[index] = pCard[index];
            arrRank[index]        = toPokerRank( pCard[index]._rank );
            if ( pCard[index]._suit != pCard[0]._suit )
                bFlush = false;
        }
        uint8       arrGroupRank[PokerHandInternal::kHandSize]{};
        uint8       arrGroupCount[PokerHandInternal::kHandSize]{};
        const int32 groupCount = PokerHandInternal::makeGroups( arrRank, arrGroupRank, arrGroupCount );

        uint8 straightHigh = 0;
        if ( groupCount == PokerHandInternal::kHandSize )
            straightHigh = PokerHandInternal::findStraightHigh( arrGroupRank );

        if ( straightHigh > 0 )
        {
            value._rank           = bFlush ? ( straightHigh == kAceHigh ? PokerHandRank::RoyalFlush : PokerHandRank::StraightFlush ) : PokerHandRank::Straight;
            value._arrTieBreak[0] = straightHigh;
        }
        else
        {
            for ( int32 index = 0; index < groupCount; ++index )
            {
                value._arrTieBreak[index] = arrGroupRank[index];
            }
            if ( arrGroupCount[0] == 4 )
                value._rank = PokerHandRank::FourOfAKind;
            else if ( arrGroupCount[0] == 3 && arrGroupCount[1] == 2 )
                value._rank = PokerHandRank::FullHouse;
            else if ( bFlush )
                value._rank = PokerHandRank::Flush;
            else if ( arrGroupCount[0] == 3 )
                value._rank = PokerHandRank::ThreeOfAKind;
            else if ( arrGroupCount[0] == 2 && arrGroupCount[1] == 2 )
                value._rank = PokerHandRank::TwoPair;
            else if ( arrGroupCount[0] == 2 )
                value._rank = PokerHandRank::OnePair;
            else
                value._rank = PokerHandRank::HighCard;
        }
        value._score = PokerHandInternal::makeScore( value._rank, value._arrTieBreak );
        return value;
    }

    PokerHandValue PokerHandEvaluator::evaluateBest( const Card* pCard, int32 count )
    {
        PokerHandValue best;
        if ( count < PokerHandInternal::kHandSize )
            return best;
        // 7 장이면 21 가지 — 고를 5 장을 비트로 센다.
        const uint32 maskEnd = 1u << count;
        bool         bFound  = false;
        for ( uint32 mask = 0; mask < maskEnd; ++mask )
        {
            int32 bitCount = 0;
            for ( int32 bit = 0; bit < count; ++bit )
            {
                bitCount += static_cast<int32>( ( mask >> bit ) & 1u );
            }
            if ( bitCount != PokerHandInternal::kHandSize )
                continue;
            Card  arrPick[PokerHandInternal::kHandSize]{};
            int32 pickCount = 0;
            for ( int32 bit = 0; bit < count; ++bit )
            {
                if ( ( mask >> bit ) & 1u )
                    arrPick[pickCount++] = pCard[bit];
            }
            const PokerHandValue value = evaluateFive( arrPick );
            if ( bFound == false || value._score > best._score )
            {
                best   = value;
                bFound = true;
            }
        }
        return best;
    }

    int32 PokerHandEvaluator::compareHands( const PokerHandValue& lhs, const PokerHandValue& rhs )
    {
        if ( lhs._score == rhs._score )
            return 0;
        return lhs._score > rhs._score ? 1 : -1;
    }
} // namespace sw

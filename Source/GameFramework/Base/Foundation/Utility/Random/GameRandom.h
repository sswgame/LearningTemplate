/**
 * @file GameRandom.h
 * @brief 게임 규칙의 결정적 난수(Core `DeterministicRandom32`)에 가중치 고르기 · 섞기 도우미를 더한 것과 좌표 해시를 정의합니다.
 * @details 테스트, 리플레이, 절차 생성이 같은 결과를 되풀이하게 한다. 수열 · 씨앗 규칙(0 거절)은 `DeterministicRandom32` 하나다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/DeterministicRandom.h"

namespace sw
{
    /**
     * @class GameRandom
     * @brief 결정적 난수입니다. 장르를 가리지 않아서 슈터의 탄 퍼짐, 테마파크의 손님 성향, 농장의 날씨가 같은 것을 씁니다.
     * @details 수열(`nextUint` · `nextFloat` · `nextRange` · `nextInt` · `nextChance`)은 `DeterministicRandom32` 이고 여기는 그 위의 도우미만 둔다.
     */
    class GameRandom : public DeterministicRandom32
    {
    public:
        using DeterministicRandom32::DeterministicRandom32;
        GameRandom() = default;

        /**
         * @brief 가중치로 하나를 고릅니다. `nextFloat` 를 한 번 씁니다. @p getWeight( 원소 ) 가 가중치이고 음수는 0 으로 칩니다. 합이 0 이하이면 -1 입니다.
         * @code
         *     const int32 index = random.pickWeightedIndex( listItem, []( const ItemDef& def ) { return def._weight; } );
         * @endcode
         */
        template <typename TList, typename TGetWeight>
        int32 pickWeightedIndex( const TList& list, TGetWeight&& getWeight )
        {
            float32 total = 0.0f;
            for ( const auto& element : list )
            {
                const float32 weight = static_cast<float32>( getWeight( element ) );
                total += weight > 0.0f ? weight : 0.0f;
            }
            if ( total <= 0.0f )
                return -1;
            float32 roll  = nextFloat() * total;
            int32   index = 0;
            int32   last  = -1;
            for ( const auto& element : list )
            {
                const float32 weight = static_cast<float32>( getWeight( element ) );
                if ( weight > 0.0f )
                {
                    if ( roll < weight )
                        return index;
                    roll -= weight;
                    last = index;
                }
                ++index;
            }
            return last; // 부동소수 끝자락에서는 마지막 양수 원소를 고른다.
        }

        /**
         * @brief 정수 가중치로 하나를 고릅니다. `nextInt( 0, 합 - 1 )` 를 한 번 씁니다. 음수는 0 으로 치고, 합이 0 이하이면 -1 입니다.
         * @details 정수 가중치(조우표, 드롭표)는 이것을 쓴다.
         *          정수 합과 정수 비교로 고르므로 부동소수 누적 오차가 없고, 가중치 합이 커도 비율이 정확하다.
         *          손으로 고르던 조우표와 난수 흐름도 같다.
         */
        template <typename TList, typename TGetWeight>
        int32 pickWeightedIndexInt( const TList& list, TGetWeight&& getWeight )
        {
            int32 total = 0;
            for ( const auto& element : list )
            {
                const int32 weight = static_cast<int32>( getWeight( element ) );
                total += weight > 0 ? weight : 0;
            }
            if ( total <= 0 )
                return -1;
            int32 roll  = nextInt( 0, total - 1 );
            int32 index = 0;
            for ( const auto& element : list )
            {
                const int32 weight = static_cast<int32>( getWeight( element ) );
                if ( weight > 0 )
                {
                    if ( roll < weight )
                        return index;
                    roll -= weight;
                }
                ++index;
            }
            return -1;
        }

        /** @brief Fisher-Yates 로 섞습니다. 뒤에서부터 [0, i] 의 한 위치와 바꾸며, `nextInt` 를 원소 수 - 1 번 부릅니다. */
        template <typename TList>
        void shuffle( TList& inoutList )
        {
            for ( int32 index = static_cast<int32>( inoutList.size() ) - 1; index > 0; --index )
            {
                const int32 swapIndex = nextInt( 0, index );
                if ( swapIndex == index )
                    continue;
                auto temp                                   = inoutList[static_cast<size_t>( index )];
                inoutList[static_cast<size_t>( index )]     = inoutList[static_cast<size_t>( swapIndex )];
                inoutList[static_cast<size_t>( swapIndex )] = temp;
            }
        }
    };
} // namespace sw

namespace sw
{
    /**
     * @struct GameHash
     * @brief 좌표를 정수 해시로 바꿉니다(lowbias32 섞기). 상태가 없어서 어느 셀부터 물어도 같은 값이 나오므로, 절차 생성(지형, 나무, 광석)에 씁니다.
     */
    struct GameHash
    {
        /** @brief 32 비트 섞기(lowbias32)입니다. */
        static constexpr uint32 mix32( uint32 value )
        {
            value ^= value >> 16;
            value *= 0x7feb352du;
            value ^= value >> 15;
            value *= 0x846ca68bu;
            value ^= value >> 16;
            return value;
        }

        static constexpr uint32 hashCoord( int32 x, int32 z, uint32 seed )
        {
            return mix32( static_cast<uint32>( x ) * 0x9e3779b1u ^ mix32( static_cast<uint32>( z ) * 0x85ebca77u ^ seed ) );
        }

        /** @brief 3 차원 셀의 해시입니다. y 를 섞어 x 에 접은 뒤 2 차원 해시로 넘깁니다. */
        static constexpr uint32 hashCoord( int32 x, int32 y, int32 z, uint32 seed )
        {
            return hashCoord( static_cast<int32>( static_cast<uint32>( x ) ^ mix32( static_cast<uint32>( y ) * 0xc2b2ae35u ^ seed ) ), z, seed );
        }

        /** @brief 해시의 아래 24 비트를 [0, 1] 실수로 바꿉니다. 확률 비교에 씁니다. */
        static constexpr float32 toUnitFloat( uint32 hash ) { return static_cast<float32>( hash & 0xffffffu ) / static_cast<float32>( 0xffffffu ); }
    };
} // namespace sw

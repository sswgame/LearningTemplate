/**
 * @file GameRandom.h
 * @brief 씨앗이 같으면 같은 수열을 내는 난수(xorshift32)와 좌표 해시 — 시험 · 리플레이 · 절차 생성이 되풀이되게 합니다.
 */
#pragma once
#include "Core/Common/HashUtil.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /**
     * @class GameRandom
     * @brief 결정적 난수입니다. 장르를 가리지 않습니다 — 탄 퍼짐(슈터) · 손님 성향(테마파크) · 날씨(농장)가 같은 것을 씁니다.
     * @details 표준 `<random>` 은 분포 구현이 표준 라이브러리마다 달라 같은 씨앗이 플랫폼마다 다른 수를 냅니다. 여기는 정수 연산만 씁니다.
     */
    class SW_GF_API GameRandom
    {
    public:
        static constexpr uint32 kDefaultSeed = HashUtil::kGoldenRatio32;

        explicit GameRandom( uint32 seed = kDefaultSeed );

        /** @brief 씨앗을 다시 둡니다. 0 은 xorshift 의 고정점이라 기본 씨앗으로 바꿉니다. */
        void   setSeed( uint32 seed );
        uint32 getState() const { return _state; }
        uint32 nextUint();
        /** @brief [0, 1) 의 실수입니다(위 24 비트). */
        float32 nextFloat();
        /** @brief [@p minValue, @p maxValue) 의 실수입니다. */
        float32 nextRange( float32 minValue, float32 maxValue );
        /** @brief [@p minValue, @p maxValue] 의 정수입니다(양 끝 포함). */
        int32 nextInt( int32 minValue, int32 maxValue );
        /** @brief 확률 @p probability 로 true 입니다. */
        bool nextChance( float32 probability ) { return nextFloat() < probability; }

        /**
         * @brief 가중치로 하나를 고릅니다(`nextFloat` 한 번). @p getWeight( 원소 ) 가 가중치이고 음수는 0 으로 칩니다. 합이 0 이하이면 −1 입니다.
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
            return last; // 부동소수 끝자락 — 마지막 양수 원소
        }

        /**
         * @brief 정수 가중치로 하나를 고릅니다(`nextInt( 0, 합 − 1 )` 한 번). 음수는 0, 합이 0 이하이면 −1 입니다.
         * @details 정수 가중치(조우표 · 드롭표)는 이것을 쓴다 — 정수 합 · 정수 비교로 걸어 부동소수 누적 오차가 없고(가중치 합이 커도 비율이 정확하다),
         *          손으로 걷던 조우표와 난수 흐름이 같다.
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

        /** @brief Fisher-Yates 로 섞습니다(뒤에서부터 [0, i] 의 한 자리와 바꾼다 — 원소 수 − 1 번 `nextInt`). */
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

    private:
        uint32 _state;
    };
} // namespace sw

namespace sw
{
    /**
     * @struct GameHash
     * @brief 좌표 → 정수 해시입니다(lowbias32 섞기). 상태가 없어 어느 칸부터 물어도 같은 값이라 절차 생성(지형 · 나무 · 광석)에 씁니다.
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

        /** @brief 3 차원 칸의 해시입니다 — y 를 섞어 x 에 접은 뒤 2 차원 해시로 넘긴다. */
        static constexpr uint32 hashCoord( int32 x, int32 y, int32 z, uint32 seed )
        {
            return hashCoord( static_cast<int32>( static_cast<uint32>( x ) ^ mix32( static_cast<uint32>( y ) * 0xc2b2ae35u ^ seed ) ), z, seed );
        }

        /** @brief 해시의 아래 24 비트를 [0, 1] 실수로 — 확률 비교에 씁니다. */
        static constexpr float32 toUnitFloat( uint32 hash ) { return static_cast<float32>( hash & 0xffffffu ) / static_cast<float32>( 0xffffffu ); }
    };
} // namespace sw

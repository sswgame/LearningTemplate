/**
 * @file GameRandom.h
 * @brief 씨앗이 같으면 같은 수열을 내는 난수(xorshift32)와 좌표 해시 — 시험 · 리플레이 · 절차 생성이 되풀이되게 합니다.
 */
#pragma once
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
        static constexpr uint32 kDefaultSeed = 0x9E3779B9u;

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

    private:
        uint32 _state;
    };

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

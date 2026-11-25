/**
 * @file TickInterval.h
 * @brief 컴포넌트 틱의 주기입니다 — 형식이 선언한 주기(`Component::kTickInterval`) · 서브틱 주기(`Component::registerSubTick`)가 이번 프레임에 몇 번 찼는지 셉니다.
 * @details 언리얼 `FTickFunction::TickInterval` 자리다. 언리얼은 틱 함수마다 남은 시간을 들고 식은 목록에서 깨우지만, 여기서는 남은 시간을 들지 않는다 —
 *          씬이 지나온 시간(`SceneTickScheduler`)과 컴포넌트 id 의 위상에서 바로 센다. 그래서 저장 · 상태 해시 · 워커 사이에 옮길 칸이 없고, 같은 시간 · 같은 id 면 어느 실행 ·
 *          어느 모드(파도 · 태스크 · 메인 · 스테이지)에서나 같은 프레임에 돈다. 위상은 id 로 흩어 같은 주기의 컴포넌트들이 한 프레임에 몰리지 않는다.
 *          꺼져 있거나 멈춘 동안 찬 주기는 세지 않는다(다시 켜진 뒤 몰아 돌지 않는다).
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/MathUtil.h"

namespace sw
{
    /** @struct TickFrameTime @brief 틱 한 프레임의 시간입니다 — 프레임 델타와 씬이 지나온 시간의 이번 프레임 구간(`_startSeconds`, `_endSeconds`]. */
    struct TickFrameTime
    {
        float64 _startSeconds{ 0.0 };
        float64 _endSeconds{ 0.0 };
        float32 _deltaTime{ 0.0f };
    };

    /** @struct TickTurn @brief 틱 하나가 이번 프레임에 돌 차례입니다. `_intervalCount` 가 0 이면 차례가 아니다(돌지 않는다). */
    struct TickTurn
    {
        float32 _deltaTime{ 0.0f };  ///< 틱에 넘길 시간 — 주기가 있으면 찬 주기 수만큼의 시간이다
        uint32  _intervalCount{ 0 }; ///< 이번 프레임에 찬 주기 수(주기가 없으면 1)
    };

    /** @struct TickIntervalUtil @brief 틱 주기의 셈입니다(파일 머리말). */
    struct TickIntervalUtil
    {
        static constexpr uint32  kPhaseBitCount    = 6;                     ///< 위상 칸 번호의 비트 수
        static constexpr uint32  kPhaseCount       = 1u << kPhaseBitCount;  ///< 주기를 나누는 위상 칸 수. 60 fps 에서 1 초 주기까지 한 프레임에 한 칸꼴이다
        static constexpr uint64  kGoldenRatioScale = 0x9E3779B97F4A7C15ull; ///< 2^64 / 황금비 — 곱의 위 비트가 잇따른 id 를 고르게 흩는다
        static constexpr uint32  kKeyBitCount      = 64;
        static constexpr float64 kSlotCenter       = 0.5; ///< 칸의 가운데

        /**
         * @brief @p tickKey(컴포넌트 id + 서브틱 id)의 위상(주기에 대한 비율, 0 과 1 사이)입니다.
         * @details 잇따른 id 를 황금비로 흩어 어느 개수든 고르게 나뉜다. 칸의 가운데를 쓰므로 프레임 시간이 주기의 정수배여도(고정 스텝) 반올림이 칸 경계를 넘지 않는다.
         */
        static constexpr float64 computePhase( uint64 tickKey )
        {
            const uint64 slot = ( tickKey * kGoldenRatioScale ) >> ( kKeyBitCount - kPhaseBitCount );
            return ( static_cast<float64>( slot ) + kSlotCenter ) / static_cast<float64>( kPhaseCount );
        }

        /** @brief 주기 @p interval 초인 틱이 이번 프레임에 돌 차례인지, 돌면 얼마만큼인지 셉니다. 주기가 0 이면 늘 한 번(프레임 델타)입니다. */
        static TickTurn computeTurn( float32 interval, uint64 tickKey, const TickFrameTime& time )
        {
            if ( interval <= 0.0f )
                return TickTurn{ time._deltaTime, 1 };
            const float64 period      = static_cast<float64>( interval );
            const float64 phase       = computePhase( tickKey );
            const float64 startIndex  = MathUtil::floor( time._startSeconds / period + phase );
            const float64 endIndex    = MathUtil::floor( time._endSeconds / period + phase );
            const uint32  filledCount = endIndex > startIndex ? static_cast<uint32>( endIndex - startIndex ) : 0u;
            return TickTurn{ interval * static_cast<float32>( filledCount ), filledCount };
        }

        /** @brief 초를 주기 @p interval 의 횟수로 바꿉니다. 반올림하며 최소 @p minCount 입니다(주기로 세는 지연 · 간격). */
        [[nodiscard]] static int32 secondsToCount( float32 interval, float32 seconds, int32 minCount )
        {
            return MathUtil::max( minCount, static_cast<int32>( MathUtil::round( seconds / interval ) ) );
        }
    };

    /**
     * @struct TickBatchCall
     * @brief 일괄 틱 한 번의 조건입니다 — 한 목록의 원소가 모두 같다. `_subTickID` 가 0 이면 주 틱(`onTick`), 아니면 그 서브틱(`onSubTick`)입니다.
     */
    struct TickBatchCall
    {
        TickFrameTime _time{};
        uint32        _subTickID{ 0 };
        float32       _interval{ 0.0f }; ///< 주기(초). 0 이면 매 프레임

        /** @brief 컴포넌트 @p componentID 의 이번 프레임 차례입니다(`TickIntervalUtil::computeTurn`). */
        TickTurn computeTurn( uint64 componentID ) const { return TickIntervalUtil::computeTurn( _interval, componentID + _subTickID, _time ); }
    };
} // namespace sw

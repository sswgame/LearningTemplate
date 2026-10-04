/**
 * @file Countdown.h
 * @brief 게임 시간 값 타입 — 끝나면 알리는 남은 시간(`Countdown`)과 초당 비율을 정수 발생으로 바꾸는 누적기(`RateAccumulator`)입니다.
 */
#pragma once
#include "Core/Common/Types.h"

namespace sw
{
    /**
     * @struct Countdown
     * @brief 쿨다운 · 지속 시간 · 반복 간격 하나입니다. `tick` 이 시간을 흘리고 끝난 걸음에 true 를 돌려줍니다.
     * @details 끝난 걸음에는 0 을 지나친 몫(늦음)이 음수로 남고 다음 `tick` 이 0 으로 지웁니다. 반복하는 쪽(연사 · 스폰 · 생각 간격)은 끝난 걸음 안에서
     *          `restart` 로 다시 걸어 그 늦음을 다음 간격에서 뺍니다 — 그래야 발생 빈도가 프레임률 · 고정 걸음 크기와 무관하게 설계값(1 / 간격)이 됩니다.
     *          잇는 몫은 한 간격까지라 걸음이 간격보다 길면 걸음마다 한 번으로 떨어지고, 멈춘 프레임 뒤에 몰아 내지 않습니다(CS 의 "한 틱 안의 늦음만
     *          잇는다", Lyra 도 발사는 프레임당 한 번). 끝난 걸음에 다시 걸지 않으면(쉬는 중) 늦음은 사라집니다 — 쉰 시간을 다음 발로 잇지 않습니다.
     *          상태 바이트는 `_remaining` 을 그대로 적습니다(음수 늦음까지 담아야 되감기 · 재시뮬레이션이 같은 발생 시각을 낸다).
     * @code
     *     _fireCooldown.tick( deltaTime );
     *     if ( _fireCooldown.isActive() == false && bTriggerHeld )
     *     {
     *         fire();
     *         _fireCooldown.restart( _fireInterval ); // 늦음을 잇는다
     *     }
     *     if ( _stun.tick( deltaTime ) )
     *         pushEvent( StunEnded );                  // 끝난 걸음에 한 번
     * @endcode
     */
    struct Countdown
    {
        float32 _remaining{ 0.0f }; /**< 남은 시간(s). 음수는 이번 걸음에 끝난 뒤 지난 시간(늦음) — 다음 `tick` 이 0 으로 지운다 */

        constexpr Countdown() = default;
        constexpr explicit Countdown( float32 duration )
            : _remaining{ duration }
        {
        }

        /** @brief 새로 겁니다 — 늦음을 잇지 않습니다(맞은 뒤 무적 · 기절 · 첫 발). */
        constexpr void start( float32 duration ) { _remaining = duration; }

        /**
         * @brief 반복 간격으로 다시 겁니다 — 이번 걸음에 끝나며 지나친 몫을 @p interval 에서 뺍니다(한 간격까지). 아직 남아 있거나 쉬던 중이면 `start` 와 같습니다.
         * @details 결과는 늘 0 이상이라 한 번 걸면 한 번입니다 — 걸음이 간격보다 길어도 몰아 내지 않습니다.
         */
        constexpr void restart( float32 interval )
        {
            const float32 lateness = _remaining < 0.0f ? ( _remaining > -interval ? _remaining : -interval ) : 0.0f;
            _remaining             = interval + lateness;
        }

        /** @brief 남은 시간을 @p duration 까지 늘립니다 — 이미 더 길게 남았으면 그대로입니다(무적 · 부스트 겹치기). */
        constexpr void extendTo( float32 duration )
        {
            if ( _remaining < duration )
                _remaining = duration;
        }

        /** @brief 끕니다(남은 시간 · 늦음 모두 0). */
        constexpr void clear() { _remaining = 0.0f; }

        /**
         * @brief @p deltaTime 만큼 흘립니다. 남아 있던 시간이 이번에 0 이하가 됐으면 true(끝난 걸음 — 한 번만)입니다.
         * @details 이미 끝나 있었으면 늦음을 지우고 false 입니다. @p deltaTime 이 0 이하면 아무것도 하지 않습니다(일시정지 · 0 걸음).
         */
        constexpr bool tick( float32 deltaTime )
        {
            if ( deltaTime <= 0.0f )
                return false;
            if ( _remaining > 0.0f )
            {
                _remaining -= deltaTime;
                return _remaining <= 0.0f;
            }
            _remaining = 0.0f;
            return false;
        }

        /** @brief 아직 남았는가(쿨다운 중 · 지속 중)입니다. */
        constexpr bool isActive() const { return _remaining > 0.0f; }
        /** @brief 남은 시간(0 이상)입니다 — 화면 · 비율에 씁니다. */
        constexpr float32 getRemaining() const { return _remaining > 0.0f ? _remaining : 0.0f; }
    };
} // namespace sw

namespace sw
{
    /**
     * @struct RateAccumulator
     * @brief 초당(또는 걸음당) 분수 비율을 쌓아 정수 발생으로 바꿉니다 — 손님 도착 · 분당 운영비처럼 한 걸음에 1 보다 작은 몫이 오는 곳입니다.
     * @details 남은 분수는 다음 걸음으로 넘어가 긴 시간 동안의 합이 비율 × 시간과 같습니다(프레임률 · 걸음 크기와 무관).
     * @code
     *     _arrival.add( arrivalPerSecond * deltaTime );
     *     while ( _arrival.takeOne() )
     *         admitGuest();
     * @endcode
     */
    struct RateAccumulator
    {
        float32 _fraction{ 0.0f }; /**< 아직 정수가 되지 않은 몫 */

        constexpr RateAccumulator() = default;

        /** @brief @p amount(비율 × 시간)를 쌓습니다. */
        constexpr void add( float32 amount ) { _fraction += amount; }

        /** @brief 쌓인 몫이 1 이상이면 하나를 빼고 true 입니다. */
        constexpr bool takeOne()
        {
            if ( _fraction < 1.0f )
                return false;
            _fraction -= 1.0f;
            return true;
        }

        /** @brief 쌓인 몫의 정수 부분을 모두 빼서 돌려줍니다(음수 몫은 0 쪽으로 자른다). */
        constexpr int32 takeWhole()
        {
            const int32 whole = static_cast<int32>( _fraction );
            _fraction -= static_cast<float32>( whole );
            return whole;
        }

        constexpr void    reset() { _fraction = 0.0f; }
        constexpr float32 getFraction() const { return _fraction; }
    };
} // namespace sw

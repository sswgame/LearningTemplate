/**
 * @file Countdown.h
 * @brief 게임 시간 값 타입을 정의합니다.
 * @details 끝나면 알리는 남은 시간(`Countdown`)과, 초당 비율을 정수 발생으로 바꾸는 누적기(`RateAccumulator`)가 있다.
 */
#pragma once
#include "Core/Common/Types.h"

namespace sw
{
    /**
     * @struct Countdown
     * @brief 쿨다운, 지속 시간, 반복 간격 하나를 나타냅니다. `tick` 이 시간을 흘리고, 끝난 걸음에 true 를 반환합니다.
     * @details 끝난 걸음에는 0 을 지나친 몫(늦음)이 음수로 남고, 다음 `tick` 이 0 으로 지운다.
     *          연사, 스폰, 생각 간격처럼 반복하는 쪽은 끝난 걸음 안에서 `restart` 로 다시 걸어 그 늦음을 다음 간격에서 뺀다.
     *          그래야 발생 빈도가 프레임률과 고정 걸음 크기에 상관없이 설계값(1 / 간격)이 된다.
     *          이어 주는 몫은 한 간격까지이다. 걸음이 간격보다 길면 걸음마다 한 번으로 떨어지고, 멈춘 프레임 뒤에 몰아서 내지 않는다.
     *          CS 는 한 틱 안의 늦음만 이어 주고, Lyra 도 발사는 프레임당 한 번이다.
     *          끝난 걸음에 다시 걸지 않으면(쉬는 중) 늦음은 사라진다. 쉰 시간을 다음 발로 이어 주지 않는다.
     *          상태 바이트는 `_remaining` 을 그대로 적는다. 음수 늦음까지 담아야 되감기와 재시뮬레이션이 같은 발생 시각을 낸다.
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

        /** @brief 새로 겁니다. 늦음을 잇지 않으며, 맞은 뒤의 무적, 기절, 첫 발에 씁니다. */
        constexpr void start( float32 duration ) { _remaining = duration; }

        /**
         * @brief 반복 간격으로 다시 겁니다. 이번 걸음에 끝나며 지나친 몫을 @p interval 에서 빼며, 한 간격까지만 뺍니다. 아직 남아 있거나 쉬던 중이면 `start` 와 같습니다.
         * @details 결과는 늘 0 이상이라 한 번 걸면 한 번만 끝난다. 걸음이 간격보다 길어도 몰아서 내지 않는다.
         */
        constexpr void restart( float32 interval )
        {
            const float32 lateness = _remaining < 0.0f ? ( _remaining > -interval ? _remaining : -interval ) : 0.0f;
            _remaining             = interval + lateness;
        }

        /**
         * @brief 반복 간격 하나를 한 걸음 돌립니다.
         * @details `tick` 한 뒤 @p bWant(누르고 있음, 사정거리 안)이고 끝나 있으면 `restart( interval )` 하고 true 를 반환한다. 이번 걸음에 한 번만 true 이다.
         *          "시간을 흘리고, 원하고 끝났으면 늦음을 이어 다시 건다"를 한 줄로 합친 것이다.
         *          손으로 `x -= dt; if ( want && x <= 0 ) x = interval;` 을 쓰면 지나친 몫을 버려서 빈도가 걸음 크기에 매인다.
         *          0.1 초 걸음에 0.25 초 간격이면 0.3 초마다 발생한다.
         */
        constexpr bool updateRepeat( float32 deltaTime, float32 interval, bool bWant )
        {
            (void)update( deltaTime ); // 끝난 걸음인지는 아래 isActive 로 본다. 원하지 않는 걸음에도 시간은 흐른다.
            if ( bWant == false || isActive() )
                return false;
            restart( interval );
            return true;
        }
        /** @brief 남은 시간을 @p duration 까지 늘립니다. 이미 더 길게 남았으면 그대로이며, 무적이나 부스트가 겹칠 때 씁니다. */
        constexpr void extendTo( float32 duration )
        {
            if ( _remaining < duration )
                _remaining = duration;
        }

        /** @brief 끕니다. 남은 시간과 늦음이 모두 0 이 됩니다. */
        constexpr void clear() { _remaining = 0.0f; }

        /**
         * @brief @p deltaTime 만큼 흘립니다. 남아 있던 시간이 이번에 0 이하가 됐으면 true 이며, 끝난 걸음에 한 번만 true 입니다.
         * @details 이미 끝나 있었으면 늦음을 지우고 false 를 반환한다. @p deltaTime 이 0 이하면(일시정지, 0 걸음) 아무것도 하지 않는다.
         */
        constexpr bool update( float32 deltaTime )
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

        /** @brief 아직 남아 있으면(쿨다운 중이거나 지속 중이면) true 입니다. */
        constexpr bool isActive() const { return _remaining > 0.0f; }
        /** @brief 남은 시간(0 이상)입니다. 화면 표시와 비율 계산에 씁니다. */
        constexpr float32 getRemaining() const { return _remaining > 0.0f ? _remaining : 0.0f; }
    };
} // namespace sw

namespace sw
{
    /**
     * @struct RateAccumulator
     * @brief 초당(또는 걸음당) 분수 비율을 쌓아 정수 발생으로 바꿉니다.
     * @details 손님 도착이나 분당 운영비처럼 한 걸음에 1 보다 작은 몫이 오는 곳에 쓴다.
     *          남은 분수는 다음 걸음으로 넘어가므로, 긴 시간 동안의 합은 프레임률과 걸음 크기에 상관없이 비율 × 시간과 같다.
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

        /** @brief 쌓인 몫이 1 이상이면 하나를 빼고 true 를 반환합니다. */
        constexpr bool takeOne()
        {
            if ( _fraction < 1.0f )
                return false;
            _fraction -= 1.0f;
            return true;
        }

        /** @brief 쌓인 몫의 정수 부분을 모두 빼서 반환합니다. 음수 몫은 0 쪽으로 자릅니다. */
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

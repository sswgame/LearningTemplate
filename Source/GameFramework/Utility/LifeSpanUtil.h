/**
 * @file LifeSpanUtil.h
 * @brief 수명이 다하면 오브젝트를 지우는 컴포넌트(이펙트 페이드 · 데미지 숫자 · 투사체)가 함께 쓰는 수명 계산입니다.
 */
#pragma once
#include "Core/Common/Types.h"

namespace sw
{
    /**
     * @struct LifeSpanUtil
     * @brief 흐른 시간(올라가는 값)과 수명으로 "다했나" · 흐림을 셉니다. 수명이 0 이하이면 끝이 없습니다(씬에 놓은 견본).
     * @details 흐른 시간은 컴포넌트가 저장하는 PROPERTY 입니다 — 상태를 다시 읽은 컴포넌트(플레이 중 되돌리기 · 핫 리로드)는 `onBeginPlay` 에서 그것을
     *          0 으로 돌리지 않고 남은 수명을 이어 갑니다(언리얼 `AActor::SetLifeSpan` 의 자리지만, 저기는 타이머라 상태를 넘지 않는다).
     *          끝나는 경계는 `Countdown::tick` 과 같은 "수명 이상" 입니다.
     * @code
     *     if ( LifeSpanUtil::advance( _currentLife, _lifeTime, deltaTime ) )
     *         pOwner->destroy();
     *     _alpha = LifeSpanUtil::computeFade( _currentLife, _lifeTime );
     * @endcode
     */
    struct LifeSpanUtil
    {
        /** @brief @p inoutElapsed 에 @p deltaTime 을 더하고, 수명이 있고 다했으면 true 입니다(다한 뒤로도 계속 true). */
        static constexpr bool advance( float32& inoutElapsed, float32 lifeTime, float32 deltaTime )
        {
            inoutElapsed += deltaTime;
            return hasExpired( inoutElapsed, lifeTime );
        }

        /** @brief 수명이 있고 흐른 시간이 수명 이상인가입니다. */
        static constexpr bool hasExpired( float32 elapsed, float32 lifeTime ) { return lifeTime > 0.0f && elapsed >= lifeTime; }

        /** @brief 흐림(1 → 0)입니다. 수명이 없으면 1, 다한 뒤는 0 입니다. */
        static constexpr float32 computeFade( float32 elapsed, float32 lifeTime )
        {
            if ( lifeTime <= 0.0f )
                return 1.0f;
            const float32 fade = 1.0f - elapsed / lifeTime;
            if ( fade < 0.0f )
                return 0.0f;
            return fade > 1.0f ? 1.0f : fade;
        }
    };
} // namespace sw

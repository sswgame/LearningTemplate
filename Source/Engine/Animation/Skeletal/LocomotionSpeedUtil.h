/**
 * @file LocomotionSpeedUtil.h
 * @brief 이동 클립의 발 속도를 측정하고, 실제 이동 속도에 맞는 재생 속도를 구합니다(발 미끄러짐 줄이기).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/String/hashed_string.h"

namespace sw
{
    class AnimationClip;
    class Skeleton;

    /**
     * @struct LocomotionSpeedUtil
     * @brief 제자리 이동 클립(루트가 움직이지 않는 걷기와 달리기)의 발 속도와, 그 속도로 맞춘 재생 속도입니다.
     * @details 제자리 클립에서 땅을 디딘 발은 모델 공간에서 이동 속도만큼 뒤로 미끄러진다. 그래서 표본마다 더 낮은 발을 디딘 발로 보고,
     *          같은 발이 이어서 디딘 동안의 수평 이동 거리를 시간으로 나누면 클립이 "원래 걷는 속도"가 된다. 루트 모션 트랙이 있으면 루트를 시작
     *          위치에 고정하고 잰다. 언리얼의 재생 속도 매칭(Play Rate Matching), 유니티의 Root Motion 속도 기준과 같은 값이다.
     *          측정은 클립마다 한 번 하고 결과를 보관해 쓴다. 표본 수만큼 포즈를 디코드하므로 프레임마다 부르지 않는다.
     */
    struct SW_API LocomotionSpeedUtil
    {
        /** @brief 측정에 쓰는 기본 표본 수입니다. 30 Hz 1 초 클립의 키 수와 같습니다. */
        static constexpr uint32 kDefaultSampleCount = 31;

        /**
         * @brief 클립의 발 속도(m/s, 수평)를 측정합니다. 발 본이 없거나 클립 길이가 0 이거나 디딘 구간이 없으면 0 입니다.
         * @param sampleCount 클립 전체에 고르게 둘 표본 수입니다(2 이상).
         */
        static float32 computeFootSpeed( const AnimationClip& clip, const Skeleton& skeleton, const hashed_string& leftFoot, const hashed_string& rightFoot,
                                         uint32 sampleCount );
        /**
         * @brief 이동 속도에 발이 맞는 재생 속도입니다. `moveSpeed / clipFootSpeed` 를 [@p minRate, @p maxRate] 로 자릅니다.
         * @details 클립 발 속도가 0 에 가까우면(제자리 동작, 측정 실패) 1 입니다. 범위를 넘는 몫은 미끄러짐으로 남는다. 보폭을 바꾸는 스트라이드 워핑이 그 몫을 맡는다.
         */
        static float32 computePlayRate( float32 moveSpeed, float32 clipFootSpeed, float32 minRate, float32 maxRate );
    };
} // namespace sw

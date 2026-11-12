/**
 * @file TimeRollover.h
 * @brief 누적 시간(float64 초)을 주기로 접어 float32 칸에 싣습니다 — 셰이더 상수처럼 칸이 float 인 곳으로 보내는 시간의 정밀도 손실(감사 C-12)을 막는다.
 * @details float32 초는 10 시간에 한 눈금이 3.9 ms 라 시간으로 도는 애니메이션이 끊긴다. 주기 안의 값만 보내면 눈금은 주기가 정한다(3600 초면 0.24 ms).
 *          대가는 주기마다 한 번 시간이 0 으로 돌아가는 것이다 — Godot 의 `rendering/limits/time/time_rollover_secs`(기본 3600)와 같은 자리이고,
 *          언리얼 · 유니티는 접지 않고 float 을 그대로 보낸다(긴 실행에서 같은 손실이 난다).
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/MathUtil.h"

namespace sw
{
    /**
     * @struct TimeRolloverUtil
     * @brief 주기로 접은 시간입니다. 주기는 부르는 쪽이 설정에서 읽는다(`EngineConfig::_shaderTimeRolloverSeconds`).
     */
    struct TimeRolloverUtil
    {
        /**
         * @brief @p seconds 를 [0, @p rolloverSeconds) 로 접은 float32 초입니다.
         * @param rolloverSeconds 0 보다 커야 한다 — 설정의 `Min` 이 로드에서 막는다(0 을 짐작해 접지 않은 값을 보내지 않는다).
         */
        [[nodiscard]] static float32 fold( float64 seconds, float64 rolloverSeconds ) noexcept
        {
            SW_ASSERT( rolloverSeconds > 0.0 );
            const float64 folded = MathUtil::fmod( seconds, rolloverSeconds );
            return static_cast<float32>( folded < 0.0 ? folded + rolloverSeconds : folded );
        }
    };
} // namespace sw

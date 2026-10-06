/**
 * @file ShooterAnimParameter.h
 * @brief Shooter3D 애니 그래프(knight · skeleton animgraph)의 매개변수 이름과 이동 코드입니다. 플레이어 몸과 적이 같은 그래프 계약을 씁니다.
 */
#pragma once
#include "Core/Common/Types.h"

namespace sw
{
    /**
     * @struct ShooterAnimParameter
     * @brief 애니 그래프 데이터의 매개변수 · `Move` 값입니다. 그래프 XML 의 전이 조건과 같은 값이어야 한다(데이터 계약).
     */
    struct ShooterAnimParameter
    {
        static constexpr string_view kMove = "Move"; ///< 이동 코드(float) — 아래 kMove* 값
        static constexpr string_view kHit  = "Hit";  ///< 맞음(트리거)
        static constexpr string_view kDead = "Dead"; ///< 쓰러짐(bool)

        static constexpr float32 kMoveIdle     = 0.0f;
        static constexpr float32 kMoveWalk     = 1.0f;
        static constexpr float32 kMoveRun      = 2.0f;
        static constexpr float32 kMoveBackward = 3.0f;
        static constexpr float32 kMoveLeft     = 4.0f;
        static constexpr float32 kMoveRight    = 5.0f;
        static constexpr float32 kMoveAir      = 6.0f;
    };
} // namespace sw

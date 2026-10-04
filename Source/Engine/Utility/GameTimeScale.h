/**
 * @file GameTimeScale.h
 * @brief 게임 시간 배율(슬로 모션 · 빨리 감기)입니다. 호스트의 프레임 시간이 이 값을 곱해 게임 업데이트 · 씬 틱 · 고정 스텝으로 흐릅니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

namespace sw
{
    /**
     * @struct GameTimeScale
     * @brief 게임 시간 배율입니다. 값은 전역 변수 `gv_timeScale` 하나에 있습니다(에디터 툴바 · 전역 변수 패널 · 콘솔 `timescale` 이 같은 값을 씁니다).
     * @details 0 이면 게임 시간이 멈춥니다(에디터 UI 와 에디터 카메라는 자기 시간으로 계속 돕니다). 범위 밖은 `kMinScale` · `kMaxScale` 로 자릅니다.
     */
    struct SW_API GameTimeScale
    {
        static constexpr float32 kMinScale = 0.0f;
        static constexpr float32 kMaxScale = 16.0f;

        /** @brief 지금 배율입니다(범위로 자른 값). */
        static float32 get();
        /** @brief 배율을 정합니다(범위로 자릅니다). */
        static void set( float32 scale );
    };
} // namespace sw

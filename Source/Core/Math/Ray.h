/**
 * @file Ray.h
 * @brief 광선(시작점 + 단위 방향)입니다. 게임 질의 · 월드 UI 피킹 · 에디터 피킹이 함께 쓰는 수학 타입입니다.
 * @details 게임프레임워크 · 월드 UI · 에디터가 각자 같은 모양의 광선 타입을 두던 것을 하나로 모았다(유니티 `Ray`, 언리얼 `FRay` 처럼 수학 층에 하나).
 */
#pragma once
#include "Core/Math/Math.h"

namespace sw
{
    /** @brief 광선 하나입니다. 방향은 정규화되어 있어야 합니다. */
    struct Ray
    {
        float3 _origin{};
        float3 _direction{ 0.0f, 0.0f, 1.0f };
    };
} // namespace sw

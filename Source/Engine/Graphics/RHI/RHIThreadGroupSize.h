/**
 * @file RHIThreadGroupSize.h
 * @brief 컴퓨트 셰이더의 축마다 스레드 수입니다.
 */
#pragma once
#include "Core/Foundation/Types.h"

namespace sw
{
    /**
     * @struct RHIThreadGroupSize
     * @brief 축마다의 스레드 수입니다. 셰이더 리플렉션이 채우고 컴퓨트 PSO 가 들고 있다 — C++ 에 같은 수를 다시 적지 않는다(유니티 `GetKernelThreadGroupSizes`).
     */
    struct RHIThreadGroupSize
    {
        uint32 _x{ 0 };
        uint32 _y{ 0 };
        uint32 _z{ 0 };

        /** @brief 컴퓨트 셰이더의 크기면 true 입니다(다른 스테이지는 0). */
        bool isValid() const { return _x > 0 && _y > 0 && _z > 0; }
    };
} // namespace sw

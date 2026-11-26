/**
 * @file RHITextureAllocationInfo.h
 * @brief 텍스처를 힙에 놓을 때의 크기 · 정렬입니다.
 */
#pragma once
#include "Core/Foundation/Types.h"

namespace sw
{
    /** @brief 텍스처를 힙에 놓을 때의 크기 · 정렬입니다(`IRHIResourceFactory::getTextureAllocationInfo`). 능력이 없으면 둘 다 0 이다. */
    struct RHITextureAllocationInfo
    {
        uint64 _sizeBytes{ 0 };
        uint64 _alignment{ 0 };
    };
} // namespace sw

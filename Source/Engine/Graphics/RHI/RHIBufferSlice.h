/**
 * @file RHIBufferSlice.h
 * @brief 상수 버퍼 페이지 안의 한 구간(버퍼 핸들 · 오프셋 · 크기)입니다.
 */
#pragma once
#include "Engine/Graphics/RHI/RHIHandles.h"

namespace sw
{
    /**
     * @brief 상수 버퍼 페이지(`IRHIResourceFactory::createConstantBufferPage`) 안의 한 구간입니다(감사 R-4).
     * @details 오프셋은 256 B(`constant::render::kConstantBufferAlignment`)의 배수이고 크기는 그 구간의 바이트다 — 만드는 쪽은 `ConstantBufferPagePool` 하나다.
     *          비행 중 프레임 복제는 페이지가 갖는다(백엔드가 프레임 칸마다 같은 오프셋을 쓴다). 언리얼 `FRHIBufferRange` · 유니티 SRP Batcher 의 오프셋 자리다.
     */
    struct RHIBufferSlice
    {
        RHIBufferHandle _buffer{ 0 };
        uint32          _offset{ 0 };
        uint32          _size{ 0 };
    };
} // namespace sw

/**
 * @file RHIBufferSize.h
 * @brief 32비트 API 에 넘길 버퍼 크기의 계산 — 넘치면 만들지 않는다
 * @details `elementSize * elementCount` 를 uint32 로 곱하면 넘쳐서 **조용히 작은 버퍼**가 만들어지고, 셰이더는 원래 개수만큼
 *          쓰므로 그 밖으로 나간다. DX12 는 `Width` 가 UINT64 라 넓히는 것으로 끝났지만 DX11 · GL · Vulkan 은 API 가 32비트
 *          크기를 받으므로 담기지 않으면 거절해야 한다 — 그 검사가 세 백엔드에 같은 열두 줄로 복사돼 있었다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

namespace sw
{
    /**
     * @class RHIBufferSize
     * @brief 원소 크기 × 개수를 64비트로 곱해 32비트에 담기는지 봅니다.
     */
    class SW_API RHIBufferSize
    {
    public:
        /**
         * @brief 원소 크기 × 개수가 32비트에 담기면 true, 아니면 오류를 찍고 false(outTotalBytes 는 0).
         * @details 0 × n 은 0 으로 true 다 — 빈 버퍼를 거절할지는 호출자가 정한다.
         */
        static bool computeStructuredBytes( uint32 elementSize, uint32 elementCount, uint32& outTotalBytes );
    };
} // namespace sw

/**
 * @file RHIBufferDesc.h
 * @brief 범용 버퍼 서술체와 버퍼 부분 갱신 조각입니다.
 */
#pragma once
#include "Engine/Graphics/RHI/RHIEnums.h"

namespace sw
{
    /**
     * @struct RHIBufferCopyRegion
     * @brief 버퍼 부분 갱신의 한 조각입니다. 원본 블롭 안의 위치, 목적 버퍼 안의 위치, 크기를 가집니다.
     * @details 여러 조각을 한 번의 호출로 넘기려고 만든 구조체다. 조각마다 따로 부르면 백엔드가
     *          스테이징 확보와 큐 제출을 그만큼 반복한다(DX12 에서 호출당 약 3.3µs).
     */
    struct RHIBufferCopyRegion
    {
        uint32 _srcOffset{ 0 }; ///< 넘긴 원본 포인터 기준 바이트 오프셋
        uint32 _dstOffset{ 0 }; ///< 목적 버퍼 안의 바이트 오프셋
        uint32 _sizeBytes{ 0 }; ///< 옮길 바이트 수
    };

    /**
     * @struct RHIBufferDesc
     * @brief 범용 버퍼를 만들 때 쓰는 서술체입니다.
     */
    struct RHIBufferDesc
    {
        uint32         _sizeBytes{ 0 };
        uint32         _elementSize{ 0 }; ///< 구조 버퍼의 stride 입니다. raw 버퍼와 바이트 버퍼는 0 입니다.
        uint32         _elementCount{ 0 };
        RHIBufferUsage _usage        = RHIBufferUsage::None;
        const void*    _pInitialData = nullptr;
    };
} // namespace sw

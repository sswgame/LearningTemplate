/**
 * @file RHIHandles.h
 * @brief RHI 자원 핸들(버퍼, 텍스처, PSO, 렌더 패스), bindless 디스크립터 인덱스, 해제 · 네이티브 문맥 콜백 별칭입니다.
 * @details 정수 별칭만 담는다. 핸들을 들고 다니기만 하는 헤더(머티리얼, 캔버스)가 서술체 전체(`RHIDescriptors.h`)를 끌지 않게 따로 둔다.
 */
#pragma once
#include "Core/Foundation/Defines.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    /** @brief GPU 버퍼 리소스의 64비트 핸들입니다. */
    using RHIBufferHandle = uint64;

    /** @brief GPU 텍스처 리소스의 64비트 핸들입니다. */
    using RHITextureHandle = uint64;

    /** @brief bindless 리소스 인덱스입니다. 디스크립터 힙이나 세트 안의 바인딩 인덱스를 가리킵니다. */
    using RHIDescriptorIndex = uint32;

    /** @brief 유효하지 않은 디스크립터 인덱스입니다. */
    constexpr RHIDescriptorIndex kInvalidDescriptorIndex = invalid_index::kUint32;

    template <typename T>
    class Delegate;

    /** @brief GPU 가 다 쓴 뒤에 부르는 자원 해제 콜백입니다(`RHIReleaseQueue`, `IRHIDevice::enqueueGPURelease`). */
    using RHIResourceReleaseDelegate = Delegate<void()>;
    /** @brief 네이티브 컨텍스트를 그것을 지키는 잠금 안에서 받는 콜백입니다(`IRHIDevice::withNativeContext`). 인자는 콜백 안에서만 씁니다. */
    using RHINativeContextVisitor = Delegate<void( void* pContext )>;

    /** @brief 파이프라인 상태 객체(PSO)의 64비트 핸들입니다. */
    using RHIPipelineStateHandle = uint64;

    /** @brief 렌더 패스 객체의 64비트 핸들입니다. */
    using RHIRenderPassHandle = uint64;
} // namespace sw

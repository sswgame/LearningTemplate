/**
 * @file RHIHandles.h
 * @brief RHI 자원 핸들(버퍼, 텍스처, PSO, 렌더 패스), bindless 디스크립터 인덱스, 해제 · 네이티브 문맥 콜백 별칭입니다.
 * @details 정수 별칭만 담는다. 핸들을 들고 다니기만 하는 헤더(머티리얼, 캔버스)가 서술체 전체(`RHIDescriptors.h`)를 끌지 않게 따로 둔다.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Foundation/RHIDescriptorIndex.h"

namespace sw
{
    /** @brief GPU 버퍼 리소스의 64비트 핸들입니다. */
    using RHIBufferHandle = uint64;

    /** @brief GPU 텍스처 리소스의 64비트 핸들입니다. */
    using RHITextureHandle = uint64;

    /**
     * @brief 상수 버퍼 페이지(`IRHIResourceFactory::createConstantBufferPage`) 안의 한 구간입니다(설계: DesignBacklog "자원 구간 할당", 감사 R-4).
     * @details 오프셋은 256 B(`constant::render::kConstantBufferAlignment`)의 배수이고 크기는 그 구간의 바이트다 — 만드는 쪽은 `ConstantBufferPagePool` 하나다.
     *          비행 중 프레임 복제는 페이지가 갖는다(백엔드가 프레임 칸마다 같은 오프셋을 쓴다). 언리얼 `FRHIBufferRange` · 유니티 SRP Batcher 의 오프셋 자리다.
     */
    struct RHIBufferSlice
    {
        RHIBufferHandle _buffer{ 0 };
        uint32          _offset{ 0 };
        uint32          _size{ 0 };
    };

    /** @brief 텍스처를 겹쳐 놓는 힙의 64비트 핸들입니다(`IRHIResourceFactory::createTransientHeap`). 0 은 없음이다. */
    using RHITransientHeapHandle = uint64;

    /** @brief 텍스처를 힙에 놓을 때의 크기 · 정렬입니다(`IRHIResourceFactory::getTextureAllocationInfo`). 능력이 없으면 둘 다 0 이다. */
    struct RHITextureAllocationInfo
    {
        uint64 _sizeBytes{ 0 };
        uint64 _alignment{ 0 };
    };

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

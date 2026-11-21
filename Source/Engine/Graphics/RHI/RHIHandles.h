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

    /** @brief GPU 텍스처 리소스의 64비트 핸들입니다. 0 은 무효다 — 텍스처를 받는 RHI 호출(렌더 패스 컬러 타깃 · 전이 · 블릿)은 0 을 오류로 알리고 버린다. */
    using RHITextureHandle = uint64;

    /**
     * @brief 스왑체인 백버퍼를 가리키는 전용 텍스처 핸들입니다 — 렌더 패스 컬러 타깃 · 전이 · 블릿이 받는다. 어느 핸들 표도 이 값을 내지 않는다(칸 번호 · 세대 모두 최댓값).
     * @details 예전에는 0 이 "없음" 과 "백버퍼" 를 겸해, 선언하지 않은 그래프 접근 · 만들지 못한 첨부의 0 이 화면 그리기로 읽혔다. 언리얼은 뷰포트 백버퍼를
     *          `FRHIViewport::GetBackBuffer` 의 텍스처 객체로, Godot 은 화면 프레임버퍼를 따로 받는다 — 여기서는 핸들 값 하나로 따로 둔다.
     *          어느 스왑체인인지는 `RHIRenderPassBeginInfo::_pSwapChain` 이 정한다(nullptr 이면 메인 창).
     */
    inline constexpr RHITextureHandle kRHIBackBufferTexture = ~RHITextureHandle{ 0 };

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
    /**
     * @enum RHIFrameCompletion
     * @brief GPU 완료 통지의 결과입니다(`IRHIDevice::notifyFrameCompletion`).
     * @details 해제(놓기)와 통지를 한 콜백이 겸하면 기다리지 못한 비우기(종료 · 잃은 장치 · 웹 `waitIdle`)가 끝나지 않은 일을 끝났다고 알린다 — 그래서 통지는 결과를 받는다.
     */
    enum class RHIFrameCompletion : uint8
    {
        Completed, ///< GPU 가 그 프레임을 끝냈다 — 되읽기 · 구간 재사용을 해도 된다
        Abandoned, ///< 끝났는지 모른 채 큐를 비웠다(잃은 장치 · 시간 초과 · 종료 · 웹 `waitIdle`) — 결과를 믿지 않는다
    };
    /** @brief GPU 가 그 프레임을 끝냈는지(또는 포기했는지) 알리는 콜백입니다(`IRHIDevice::notifyFrameCompletion`). */
    using RHIFrameCompletionDelegate = Delegate<void( RHIFrameCompletion )>;
    /** @brief 네이티브 컨텍스트를 그것을 지키는 잠금 안에서 받는 콜백입니다(`IRHIDevice::withNativeContext`). 인자는 콜백 안에서만 씁니다. */
    using RHINativeContextVisitor = Delegate<void( void* pContext )>;

    /** @brief 파이프라인 상태 객체(PSO)의 64비트 핸들입니다. */
    using RHIPipelineStateHandle = uint64;

    /** @brief 렌더 패스 객체의 64비트 핸들입니다. */
    using RHIRenderPassHandle = uint64;
} // namespace sw

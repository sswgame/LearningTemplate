/**
 * @file RenderThread.h
 * @brief 전용 렌더 스레드, 또는 같은 스레드(inline)에서 패킷 실행
 * @details start() → 워커가 그래픽스 컨텍스트를 소유, GT는 submit()만
 *          bind()만 (start 없음) → submit()이 호출 스레드에서 executeInline
 */
#pragma once
#include "Core/Common/StdHeaders.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Memory/LinearAllocator.h"

#include "Engine/EngineMinimal.h"
#include "Engine/Graphics/Renderer/Frame/RenderFramePacket.h"

namespace sw
{
    extern SW_API bool gv_useRenderThread;

    class FrameRenderer;
    class IRHIDevice;
    class Scene;

    /**
     * @class RenderThread
     * @details RHI draw/present는 워커(start) 또는 submit() inline.
     *          패킷을 실행하는 스레드에서 bindGraphicsContext를 호출합니다.
     */
    class SW_API RenderThread
    {
    public:
        /** @brief 워커 없이 시작합니다. bind/start로 붙입니다. */
        RenderThread();
        /** @brief 스레드를 멈추고 조인합니다. */
        ~RenderThread();

        /** @brief 복사를 금지합니다. */
        RenderThread( const RenderThread& ) = delete;
        /** @brief 대입을 금지합니다. */
        RenderThread& operator=( const RenderThread& ) = delete;

        /**
         * @brief 워커 없이 디바이스/렌더러를 붙입니다 (GT inline).
         * @details submit()은 호출 스레드에서 executePacket 합니다.
         */
        bool bind( IRHIDevice* pDevice, FrameRenderer* pFrameRenderer );
        /**
         * @brief 전용 워커를 띄웁니다. bindGraphicsContext는 그 스레드에서 실행됩니다.
         */
        bool start( IRHIDevice* pDevice, FrameRenderer* pFrameRenderer );
        /** @brief 워커를 멈춥니다. */
        void stop();

        /** @brief 워커가 있으면 큐에 넣고, 없으면 executeInline. */
        void submit( RenderFramePacket&& packet );
        /** @brief RT 큐를 비웁니다 (inline이면 no-op). 붙어 있으면 device waitIdle. */
        void waitIdle();
        /** @brief 호출 스레드에서 패킷 하나를 처리합니다. */
        void executeInline( RenderFramePacket& packet );

        /** @brief 현재 제출 대기 중인 프레임 패킷의 임시 메모리를 할당합니다. */
        void* allocateFrameMemory( size_t size, size_t alignment = alignof( std::max_align_t ) );

        /** @brief 씬 렌더 후 Present 전 훅 (execute와 같은 스레드). */
        void setPresentHook( PresentHookDelegate hook ) { _presentHook = std::move( hook ); }
        /** @brief Present 완료 후 훅 (멀티 뷰포트 / 플랫폼 윈도우 처리용). */
        void setPostPresentHook( PresentHookDelegate hook ) { _postPresentHook = std::move( hook ); }

        /** @brief 워커가 돌아가면 true. */
        bool isRunning() const { return _bRunning.load( std::memory_order_acquire ); }
        /** @brief 디바이스가 붙어 있으면 true. */
        bool isBound() const { return _pDevice != nullptr; }

    private:
        /** @brief 렌더 스레드 루프: 패킷을 꺼내 executePacket. */
        void threadMain();
        /** @brief 패킷을 실행하고, 중단되더라도 postPresent 훅 통지를 보장합니다. */
        void executePacket( RenderFramePacket& packet );
        /**
         * @brief 씬 렌더 ~ present 훅 ~ Present 까지의 프레임 본문입니다.
         * @return 프레임을 실제로 그렸으면 true, 유효하지 않은 패킷/리소스로 건너뛰었으면 false.
         */
        bool executeFrameBody( RenderFramePacket& packet );
        /** @brief 현재 스레드에 그래픽스 컨텍스트가 있는지 확인합니다. */
        bool ensureContextOnCurrentThread();

    private:
        IRHIDevice*         _pDevice;
        FrameRenderer*      _pFrameRenderer;
        PresentHookDelegate _presentHook;
        PresentHookDelegate _postPresentHook;
        std::thread         _thread;
        atomic<bool>        _bRunning;
        atomic<bool>        _bStop;
        atomic<bool>        _bContextBound; ///< bindGraphicsContext succeeded on current executor
        /// @brief 직전 프레임에 디바이스로 밀어넣은 제출 정책 — 바뀔 때만 로그를 남기기 위한 것.
        bool   _bLastImmediateSubmit{ false };
        uint8  _bScreenshotTaken{ 0 };       ///< -gv_screenshot 은 한 장만 찍는다
        uint32 _screenshotFrameCounter{ 0 }; ///< 씬이 채워질 때까지 몇 프레임 기다린다

        static constexpr uint32 _s_kRingCapacity{ constant::kRenderFrameQueueDepth };
        // sw::array 대신 std::array 사용 (Game Thread와 Render Thread 간의 동시 접근 시 DataRaceDetector 오탐 방지)
        std::array<RenderFramePacket, _s_kRingCapacity> _arrRingBuffer;
        LinearAllocator                                 _arrFrameAllocators[_s_kRingCapacity];
        atomic<uint32>                                  _head; ///< 생산자(GT)가 씀
        atomic<uint32>                                  _tail; ///< Written by consumer (RT)

        mutex                       _mutex;
        std::condition_variable_any _cvProduce;
        std::condition_variable_any _cvConsume;
        std::condition_variable_any _cvIdle;
    };
} // namespace sw

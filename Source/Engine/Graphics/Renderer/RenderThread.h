/**
 * @file RenderThread.h
 * @brief 패킷을 전용 렌더 스레드에서, 또는 부르는 스레드에서 바로(inline) 실행합니다.
 * @details start() 로 띄우면 워커가 그래픽스 컨텍스트를 갖고 GT 는 submit() 만 합니다.
 *          bind() 만 하면(start 없음) submit() 이 부르는 스레드에서 executeInline 합니다.
 */
#pragma once
#include "Core/Common/StdHeaders.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"

#include "Engine/EngineMinimal.h"
#include "Engine/Graphics/Renderer/Frame/RenderFramePacket.h"

namespace sw
{
    /** @brief `-gv_useRenderThread`: 전용 렌더 스레드를 쓸지입니다(false 면 게임 스레드가 바로 제출한다). Engine 안에서만 읽습니다. */
    SW_EXTERN_GLOBAL_VARIABLE_BOOL( gv_useRenderThread );

    class FrameRenderer;
    class IRHIDevice;
    class Scene;

    /**
     * @class RenderThread
     * @details RHI 그리기 · Present 는 워커(start)에서, 또는 submit() 안에서 바로 실행합니다.
     *          패킷을 실행하는 스레드에서 bindGraphicsContext 를 부릅니다.
     */
    class SW_API RenderThread
    {
    public:
        /** @brief 워커 없이 만듭니다. bind · start 로 붙입니다. */
        RenderThread();
        /** @brief 스레드를 멈추고 조인합니다. */
        ~RenderThread();

        /** @brief 복사를 금지합니다. */
        RenderThread( const RenderThread& ) = delete;
        /** @brief 대입을 금지합니다. */
        RenderThread& operator=( const RenderThread& ) = delete;

        /**
         * @brief 워커 없이 디바이스 · 렌더러를 붙입니다(GT 에서 바로 실행).
         * @details submit() 이 부르는 스레드에서 executePacket 합니다.
         */
        bool bind( IRHIDevice* pDevice, FrameRenderer* pFrameRenderer );
        /**
         * @brief 전용 워커를 띄웁니다. bindGraphicsContext 는 그 스레드에서 실행됩니다.
         */
        bool start( IRHIDevice* pDevice, FrameRenderer* pFrameRenderer );
        /** @brief 워커를 멈춥니다. */
        void stop();

        /**
         * @brief 워커가 있으면 링에 넣고, 없으면 executeInline 합니다.
         * @details 패킷은 **바꿔치기**로 들어갑니다. 부르는 쪽의 패킷은 링 자리에 있던 지난 패킷(저장소 포함)을 돌려받습니다.
         *          그래서 부르는 쪽이 패킷 하나를 스크래치로 들고 매 프레임 다시 채우면 프레임당 할당이 없습니다. 옮겨 넣기(move)
         *          였을 때는 링 자리의 저장소가 프레임마다 버려졌습니다.
         */
        void submit( RenderFramePacket& packet );
        /** @brief 워커가 돌고 있으면 RT 큐가 빌 때까지 기다리고 디바이스 waitIdle 까지 합니다(워커가 없으면 할 일 없음). */
        void waitIdle();
        /** @brief 호출 스레드에서 패킷 하나를 처리합니다. */
        void executeInline( RenderFramePacket& packet );

        /** @brief 씬 렌더 뒤 · Present 전에 부를 훅을 정합니다(패킷을 실행하는 스레드에서 불립니다). */
        void setPresentHook( PresentHookDelegate hook ) { _presentHook = std::move( hook ); }
        /** @brief Present 뒤에 부를 훅을 정합니다(멀티 뷰포트 · 플랫폼 창 처리용). */
        void setPostPresentHook( PresentHookDelegate hook ) { _postPresentHook = std::move( hook ); }

        /** @brief 워커가 돌고 있으면 true 를 반환합니다. */
        bool isRunning() const { return _bRunning.load( std::memory_order_acquire ); }
        /** @brief 디바이스가 붙어 있으면 true 를 반환합니다. */
        bool isBound() const { return _pDevice != nullptr; }

    private:
        /** @brief 렌더 스레드 루프입니다. 패킷을 꺼내 executePacket 합니다. */
        void threadMain();
        /** @brief 패킷을 실행하고, 중단되더라도 postPresent 훅 통지를 보장합니다. */
        void executePacket( RenderFramePacket& packet );
        /**
         * @brief 씬 렌더부터 present 훅, Present 까지의 프레임 본문입니다.
         * @return 프레임을 실제로 그렸으면 true, 유효하지 않은 패킷 · 리소스라 건너뛰었으면 false.
         */
        bool executeFrameBody( RenderFramePacket& packet );
        /** @brief 지금 스레드에 그래픽스 컨텍스트가 있는지 확인합니다(없으면 붙입니다). */
        bool ensureContextOnCurrentThread();

    private:
        IRHIDevice*         _pDevice;
        FrameRenderer*      _pFrameRenderer;
        PresentHookDelegate _presentHook;
        PresentHookDelegate _postPresentHook;
        std::thread         _thread;
        atomic<bool>        _bRunning;
        atomic<bool>        _bStop;
        atomic<bool>        _bContextBound; ///< 지금 실행 스레드에서 bindGraphicsContext 가 성공했는지
        /// @brief 직전 프레임에 디바이스로 밀어 넣은 제출 정책입니다. 바뀔 때만 로그를 남기려고 둡니다.
        bool   _bLastImmediateSubmit;
        uint8  _bScreenshotTaken;       ///< -gv_screenshot 은 한 장만 찍는다
        uint32 _screenshotFrameCounter; ///< 씬이 채워질 때까지 몇 프레임 기다린다

        static constexpr uint32 _s_kRingCapacity{ constant::kRenderFrameQueueDepth };
        // 게임 스레드와 렌더 스레드가 이 링을 **동시에** 만진다. 그래서 레이스 탐지기가 붙은
        // sw::array 를 쓰지 않는다. 이유는 Core/Container/array.h 머리말에 있다.
        std::array<RenderFramePacket, _s_kRingCapacity> _arrRingBuffer;
        atomic<uint32>                                  _head; ///< 생산자(GT)가 씀
        atomic<uint32>                                  _tail; ///< 소비자(RT)가 씀

        mutex                       _mutex;
        std::condition_variable_any _cvProduce;
        std::condition_variable_any _cvConsume;
        std::condition_variable_any _cvIdle;
    };
} // namespace sw

#include "pch.h"

#include "Engine/Graphics/Renderer/RenderThread.h"

#include "Core/Concurrency/mutex.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"

#include "Engine/Graphics/RHI/IRHICommandContext.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHISwapChain.h"
#include "Engine/Graphics/Renderer/Frame/FrameRenderer.h"

namespace sw
{
    SW_LOG_CALLER( "RenderThread" );

    // 커맨드 리스트를 프레임 끝에 모아 제출할지(기본), 잘릴 때마다 바로 제출할지.
    // 정의는 RHI.cpp — 여기서는 프레임마다 디바이스로 밀어넣기만 한다.
    SW_EXTERN_GLOBAL_VARIABLE_BOOL( gv_rhiImmediateSubmit );

    SW_GLOBAL_VARIABLE_BOOL( gv_useRenderThread, true, "전용 RenderThread 사용 (false = 게임 스레드 인라인 submit)" );

    RenderThread::RenderThread()
        : _pDevice{ nullptr }
        , _pFrameRenderer{ nullptr }
        , _presentHook{}
        , _thread{}
        , _bRunning{ false }
        , _bStop{ false }
        , _bContextBound{ false }
        , _arrRingBuffer{}
        , _arrFrameAllocators{}
        , _head{ 0 }
        , _tail{ 0 }
    {
    }

    RenderThread::~RenderThread()
    {
        stop();
    }

    bool RenderThread::bind( IRHIDevice* pDevice, FrameRenderer* pFrameRenderer )
    {
        if ( pDevice == nullptr )
            return false;

        if ( _bRunning.load( std::memory_order_relaxed ) )
        {
            SW_LOG_WARNING( "bind() ignored while worker is running." );
            return false;
        }

        _pDevice        = pDevice;
        _pFrameRenderer = pFrameRenderer;
        _bContextBound  = false;
        SW_LOG_INFO( "Bound for inline submit (no dedicated worker). Backend=%#", pDevice->getBackendName() );
        return true;
    }

    bool RenderThread::start( IRHIDevice* pDevice, FrameRenderer* pFrameRenderer )
    {
        if ( _bRunning.load( std::memory_order_relaxed ) )
            return true;

        if ( bind( pDevice, pFrameRenderer ) == false )
            return false;

        if ( pDevice != nullptr )
            pDevice->unbindGraphicsContext();

        _bStop    = false;
        _bRunning = true;
        _thread   = std::thread( &RenderThread::threadMain, this );
        SW_LOG_TRACE( "Dedicated worker started" );
        return true;
    }

    void RenderThread::stop()
    {
        if ( _bRunning.exchange( false, std::memory_order_acq_rel ) == false )
        {
            _pDevice        = nullptr;
            _pFrameRenderer = nullptr;
            _bContextBound  = false;
            return;
        }
        _bStop.store( true, std::memory_order_release );
        _cvProduce.notify_all();
        _cvConsume.notify_all();
        _cvIdle.notify_all();
        if ( _thread.joinable() )
        {
            if ( std::this_thread::get_id() != _thread.get_id() )
                _thread.join();
        }
        _pDevice        = nullptr;
        _pFrameRenderer = nullptr;
        _bContextBound  = false;
        SW_LOG_TRACE( "Stopped" );
    }

    void RenderThread::submit( RenderFramePacket&& packet )
    {
        if ( _pDevice == nullptr )
        {
            SW_LOG_WARNING( "submit() with no bound device — packet dropped." );
            return;
        }

        // 런타임에 gv_useRenderThread 상태가 동적으로 변경되었을 때 스레드 모드를 핫스왑합니다.
        const bool bShouldRunWorker = gv_useRenderThread;
        if ( _bRunning.load( std::memory_order_acquire ) != bShouldRunWorker )
        {
            IRHIDevice*    pSavedDevice        = _pDevice;
            FrameRenderer* pSavedFrameRenderer = _pFrameRenderer;

            if ( bShouldRunWorker )
            {
                if ( _bContextBound )
                {
                    _pDevice->unbindGraphicsContext();
                    _bContextBound = false;
                }
                start( pSavedDevice, pSavedFrameRenderer );
            }
            else
            {
                waitIdle();
                stop();
                bind( pSavedDevice, pSavedFrameRenderer );
            }
        }

        if ( _bRunning.load( std::memory_order_acquire ) == false )
        {
            executeInline( packet );
            return;
        }

        uint32 currentHead = _head.load( std::memory_order_relaxed );
        uint32 nextHead    = ( currentHead + 1 ) % _s_kRingCapacity;

        {
            std::unique_lock<mutex> lock{ _mutex };
            _cvProduce.wait( lock, [this, nextHead]()
            { return _bStop.load( std::memory_order_relaxed ) || nextHead != _tail.load( std::memory_order_acquire ); } );

            if ( _bStop.load( std::memory_order_relaxed ) )
                return;

            _arrRingBuffer[currentHead] = std::move( packet );
            _head.store( nextHead, std::memory_order_release );
        }
        _cvConsume.notify_one();
    }

    void RenderThread::waitIdle()
    {
        if ( _bRunning.load( std::memory_order_acquire ) == false )
            return;

        if ( std::this_thread::get_id() != _thread.get_id() )
        {
            std::unique_lock<mutex> lock{ _mutex };
            _cvIdle.wait( lock, [this]()
            { return _bStop.load( std::memory_order_relaxed ) || _tail.load( std::memory_order_acquire ) == _head.load( std::memory_order_acquire ); } );
        }

        if ( _pDevice != nullptr )
            _pDevice->waitIdle();
    }

    void RenderThread::executeInline( RenderFramePacket& packet )
    {
        // Game-thread / no-worker path: own context on this thread for the frame.
        ensureContextOnCurrentThread();
        executePacket( packet );
        _arrFrameAllocators[0].reset();
    }

    void* RenderThread::allocateFrameMemory( size_t size, size_t alignment )
    {
        // Game Thread calls this to allocate data for the _head frame
        uint32 currentHead = _head.load( std::memory_order_relaxed );
        if ( _bRunning.load( std::memory_order_relaxed ) )
        {
            uint32 nextHead = ( currentHead + 1 ) % _s_kRingCapacity;
            if ( nextHead == _tail.load( std::memory_order_acquire ) )
            {
                std::unique_lock<mutex> lock{ _mutex };
                _cvProduce.wait( lock, [this, nextHead]()
                { return _bStop.load( std::memory_order_relaxed ) || nextHead != _tail.load( std::memory_order_acquire ); } );
            }
        }
        return _arrFrameAllocators[currentHead].allocate( size, alignment );
    }

    void RenderThread::threadMain()
    {
        _bContextBound = false;

        for ( ;; )
        {
            uint32 currentTail = _tail.load( std::memory_order_relaxed );

            {
                std::unique_lock<mutex> lock{ _mutex };
                _cvConsume.wait( lock, [this, currentTail]()
                { return _bStop.load( std::memory_order_relaxed ) || currentTail != _head.load( std::memory_order_acquire ); } );
            }

            if ( _bStop.load( std::memory_order_relaxed ) && currentTail == _head.load( std::memory_order_acquire ) )
                break;

            RenderFramePacket packet = std::move( _arrRingBuffer[currentTail] );
            executePacket( packet );
            _arrFrameAllocators[currentTail].reset();

            {
                std::scoped_lock<mutex> lock{ _mutex };
                _tail.store( ( currentTail + 1 ) % _s_kRingCapacity, std::memory_order_release );
            }
            _cvProduce.notify_one();
            _cvIdle.notify_all();
        }

        if ( _pDevice != nullptr )
        {
            _pDevice->unbindGraphicsContext();
            _bContextBound = false;
        }

        for ( uint32 ringSlotIndex = 0; ringSlotIndex < _s_kRingCapacity; ++ringSlotIndex )
        {
            _arrFrameAllocators[ringSlotIndex].clear();
        }
    }

    void RenderThread::executePacket( RenderFramePacket& packet )
    {
        // 훅에 넘길 디바이스가 아예 없으면 할 수 있는 게 없다.
        if ( _pDevice == nullptr )
            return;

        std::ignore = executeFrameBody( packet );

        // 프레임 본문이 중간에 중단되더라도 postPresent 통지는 반드시 보낸다.
        // 에디터는 이 신호로 draw 스냅샷 in-flight 상태를 해제하므로, 빠뜨리면
        // 다음 updateUI 가 waitForDrawSnapshotIdle 에서 영구 대기한다.
        if ( _postPresentHook.isBound() )
            _postPresentHook( *_pDevice, packet );

        if ( _pDevice->requiresExclusiveContextThread() && _bContextBound.load( std::memory_order_relaxed ) )
        {
            _pDevice->unbindGraphicsContext();
            _bContextBound = false;
        }
    }

    bool RenderThread::executeFrameBody( RenderFramePacket& packet )
    {
        if ( packet._bValid == 0 )
            return false;

        ensureContextOnCurrentThread();

        const bool          bOffscreen   = packet._gameRenderTarget != 0;
        IRHICommandContext* pFrameStream = _pDevice->getFrameStreamContext();
        if ( _pDevice->getSwapChain() == nullptr )
        {
            SW_LOG_ERROR( "getSwapChain() is null; skipping packet" );
            return false;
        }
        if ( bOffscreen && pFrameStream == nullptr )
        {
            SW_LOG_ERROR( "getFrameStreamContext() is null; skipping offscreen packet" );
            return false;
        }

        // 프레임 수명주기는 경로와 무관하게 항상 여기서 한 번 연다. 예전엔 오프스크린 경로에서만
        // beginFrame 을 그래프 뒤로 미뤄뒀는데, 그건 "백버퍼를 바인딩할 다른 수단이 없어서" 위치로
        // 대신하던 것이었다(docs/05_RHI_FrameContract.md 의 R2). 그 역할은 아래 명시적 백버퍼
        // 렌더패스가 대신한다 — 이 둘은 반드시 같이 있어야 한다.
        // 제출 정책은 매 프레임 갱신한다 — 런타임에 gv 를 토글해도 다음 프레임부터 바로 먹힌다.
        // 즉시 모드는 제출 횟수가 늘어 성능이 크게 떨어지므로, 모르고 그 상태로 재는 일이 없도록
        // 바뀐 순간에 한 번 남긴다.
        if ( gv_rhiImmediateSubmit != _bLastImmediateSubmit )
        {
            _bLastImmediateSubmit = gv_rhiImmediateSubmit;
            SW_LOG_INFO( "RHI submit mode: %#", _bLastImmediateSubmit ? "immediate (debug)" : "batched at endFrame" );
        }
        _pDevice->setImmediateSubmit( gv_rhiImmediateSubmit );
        _pDevice->getSwapChain()->beginFrame( packet._clearColor );

        // 게임뷰 RT 확립. 예전엔 beginOffscreenPass 였는데, 그건 "렌더타깃 바인딩"과 "백엔드마다
        // 다른 스트림 분리"가 섞인 API 였다(Vulkan 만 별도 커맨드버퍼 + 블로킹 제출).
        // 렌더타깃 바인딩은 beginRenderPass 로 충분하다 — docs/05_RHI_FrameContract.md S3.
        if ( bOffscreen )
        {
            RHIRenderPassBeginInfo gameViewPass{};
            gameViewPass._bBindColor        = 1;
            gameViewPass._colorTargetCount  = 1;
            gameViewPass._arrColorTarget[0] = packet._gameRenderTarget;
            gameViewPass._arrLoadOp[0]      = RHIRenderPassLoadOp::Clear;
            gameViewPass._arrClearColor[0]  = packet._clearColor;
            pFrameStream->beginRenderPass( gameViewPass );
        }

        if ( _pFrameRenderer != nullptr && _pFrameRenderer->isReady() )
            _pFrameRenderer->executePacket( _pDevice, packet );

        // 에디터가 게임뷰 텍스처를 샘플링한다 — 읽기 상태로 전환(열려 있는 렌더패스도 여기서 닫힌다).
        if ( bOffscreen )
            pFrameStream->prepareTextureForShaderRead( packet._gameRenderTarget );

        // UI(presentHook)는 백버퍼에 그린다. 그래프가 오프스크린/백버퍼 어디에 그렸든, 여기서 타깃을
        // 명시적으로 백버퍼로 되돌린다. 그래프가 백버퍼에 그린 경우도 있으므로 Load 여야 한다.
        if ( pFrameStream != nullptr )
        {
            RHIRenderPassBeginInfo backbufferPass{};
            backbufferPass._bBindColor        = 1;
            backbufferPass._colorTargetCount  = 1;
            backbufferPass._arrColorTarget[0] = 0; // 0 = 백버퍼
            // 오프스크린 경로에서는 그래프가 게임 RT 에만 그렸으므로 백버퍼는 아직 아무도 안 건드렸다
            // → 여기서 클리어한다. 백버퍼 경로에서는 그래프가 이미 그렸으므로 보존해야 한다.
            backbufferPass._arrLoadOp[0]     = bOffscreen ? RHIRenderPassLoadOp::Clear : RHIRenderPassLoadOp::Load;
            backbufferPass._arrClearColor[0] = packet._clearColor;
            pFrameStream->beginRenderPass( backbufferPass );
        }

        if ( _presentHook.isBound() )
            _presentHook( *_pDevice, packet );

        if ( _pDevice->getSwapChain() != nullptr )
            _pDevice->getSwapChain()->endFrame( true );

        return true;
    }

    bool RenderThread::ensureContextOnCurrentThread()
    {
        if ( _pDevice == nullptr )
            return false;
        if ( _bContextBound.load( std::memory_order_relaxed ) )
            return true;

        if ( _pDevice->bindGraphicsContext() == false )
        {
            SW_LOG_ERROR( "bindGraphicsContext failed on executor thread (%#)",
                          _pDevice->getBackendName() );
            return false;
        }
        _bContextBound = true;
        return true;
    }
} // namespace sw

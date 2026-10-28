#include "pch.h"

#include "Engine/Renderer/RenderThread.h"

#include "Core/Concurrency/ThreadName.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Concurrency/thread.h"
#include "Core/File/FileUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/LogOnce.h"
#include "Core/Memory/Memory.h"
#include "Core/Process/ThreadCrashStack.h"
#include "Core/String/StringBuilder.h"
#include "Core/Task/TaskManager.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/Debug/RenderTargetRegistry.h"
#include "Engine/Graphics/RHI/IRHICommandContext.h"
#include "Engine/Graphics/RHI/IRHIContextThreadBinding.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Graphics/RHI/Support/RHITransferUtil.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Profiling/ProfilerBackend.h"
#include "Engine/Renderer/Frame/FrameRenderer.h"
#include "Engine/Renderer/Frame/FrameRendererUtil.h"
#include "Engine/Renderer/ScreenshotMailbox.h"

namespace sw
{
    SW_LOG_CALLER( "RenderThread" );

    namespace
    {
        struct RenderThreadInternal
        {
            /** @brief Present 캡처를 켠 뒤 쓰기 전에 더 그릴 프레임 수입니다. 켠 프레임의 캡처 텍스처는 아직 비어 있을 수 있습니다. */
            static constexpr uint32 kPresentCaptureWaitFrames = 1;
            /** @brief `-gv_screenshotSettledFrames` 가 화면이 자리 잡기를 기다리는 벽시계 시한(초)입니다. Debug 빌드가 큰 씬을 읽고 셰이더를 처음 만드는 시간보다 넉넉하다. */
            static constexpr float64 kSettleTimeoutSeconds = 120.0;

            /**
             * @brief 스크린샷 우편함 서비스입니다. 렌더러를 짓지 않는 호스트(전용 서버)와 서비스 범위가 서지 않은 곳에는 없어 nullptr 입니다.
             * @details 없으면 요청을 받을 곳이 없다 — 정적 창구는 요청을 오류로 알리고 버리며, 읽는 창구는 "아직 아무것도 처리하지 않았다" 로 답한다.
             */
            static ScreenshotMailbox* findMailbox() { return engine::findSubsystem<ScreenshotMailbox>(); }

            /** @brief 컨텍스트가 스레드에 묶이는 디바이스(DX11, GL)면 이 스레드에서 컨텍스트를 뗍니다. 그 능력이 없는 디바이스는 할 일이 없습니다. */
            static void unbindGraphicsContext( const IRHIDevice* pDevice )
            {
                IRHIContextThreadBinding* pBinding = pDevice != nullptr ? pDevice->findContextThreadBinding() : nullptr;
                if ( pBinding != nullptr )
                    pBinding->unbindGraphicsContext();
            }
        };
    } // namespace

    /**
     * @brief `-gv_screenshot=<파일경로>` 입니다. 화면에 나간 이미지(Present 결과)를 PPM 으로 한 장 덤프합니다(백엔드별 시각 검증용).
     * @details Win32 PrintWindow 캡처는 DX11, GL, Vulkan 에서 빈 화면이 자주 나온다. 스왑체인이 GDI 로
     *          합성되지 않기 때문이다. 그래서 GPU 에서 직접 읽는다(readbackTexture2DAndWait).
     *          PPM 은 인코더가 필요 없어 의존성이 늘지 않는다. `-gv_profileFrames` 와 같이 쓰면 찍고 종료한다.
     * @note 세 변수 모두 이 파일이 유일한 소비자라 여기서 정의한다. 다른 파일에서 `extern` 으로 가져다 쓰면 타입이 어긋나도 링커까지 가야 걸린다.
     */
    SW_TEST_GLOBAL_VARIABLE_SHIPPED( sw::string, gv_screenshot, "", "화면에 나간 그림(Present 결과)을 PPM 으로 덤프할 경로 (비면 사용 안 함)" );

    /** @brief `-gv_screenshotAttachment=<이름>` 입니다. 덤프할 트랜지언트 첨부 이름이며, 비면 Present 캡처(없으면 Present 가 읽는 첨부)를 저장합니다. */
    SW_TEST_GLOBAL_VARIABLE( sw::string, gv_screenshotAttachment, "", "Present 결과 대신 덤프할 트랜지언트 이름 (비면 Present 결과)" );

    /**
     * @brief `-gv_screenshotFrame=<N>` 입니다. 몇 번째 프레임에서 찍을지 정합니다(기본 10이고, 10보다 작으면 10으로 봅니다).
     * @details 시간에 따라 움직이는 것(GPU 인스턴스 회전 등)을 검증하려면 **서로 다른 시각**의 장면이
     *          필요하다. 주의: `-gv_profileFrames` 는 찍는 시각을 바꾸지 않는다. 같은 프레임 번호끼리 비교하면
     *          "움직이지 않는다" 는 잘못된 결론이 나온다.
     */
    SW_TEST_GLOBAL_VARIABLE( int32, gv_screenshotFrame, 10, "스크린샷을 찍을 프레임 번호 (기본 10)" );

    /**
     * @brief `-gv_screenshotSettledFrames=<N>` 입니다. 1 이상이면 `-gv_screenshotFrame` 대신, 화면이 자리 잡은 패킷부터 N 프레임을 이어 센 뒤 찍습니다.
     * @details 자리 잡음은 활성 씬이 플레이를 시작했고 로딩 화면 · 화면 페이드 · 에셋 읽기가 끝난 것이다(`RenderFramePacket::_bReadyForCapture`).
     *          로딩 화면의 최소 표시 시간과 페이드는 실제 시간이라, 프레임이 빠른 기계에서는 고정 프레임 번호가 로딩 화면을 찍는다(골든 이미지).
     *          N 프레임을 다 세기 전에 자리 잡음이 풀리면(새 에셋이 디더로 나타나는 중) 처음부터 다시 센다. `kSettleTimeoutSeconds` 안에 다 세지 못하면 오류로 적고 찍지 않는다
     *          (`-gv_screenshotQuit` 은 14 로 끝난다). 언리얼 자동화 스크린샷이 로드 · 스트리밍이 끝나기를 기다린 뒤 지연을 두고 찍는 것과 같다.
     */
    SW_TEST_GLOBAL_VARIABLE_SHIPPED( int32, gv_screenshotSettledFrames, 0, "1 이상이면 화면이 자리 잡은(로딩 화면 · 페이드 · 에셋 읽기 끝) 뒤 이 프레임 수만큼 지나 찍는다 (0=gv_screenshotFrame)" );

    /**
     * @brief `-gv_screenshotCount=<N>` 과 `-gv_screenshotInterval=<K>` 입니다. 첫 장(`-gv_screenshotFrame`)부터 K 프레임마다 N 장을 찍습니다(움직임과 튐 확인용).
     * @details 두 장 이상이면 파일 이름의 확장자 앞에 `_000`, `_001` 등이 붙는다. 장마다 GPU 되읽기라 그 프레임은 느려진다.
     */
    SW_TEST_GLOBAL_VARIABLE( int32, gv_screenshotCount, 1, "연속으로 찍을 스크린샷 수 (기본 1)" );
    SW_TEST_GLOBAL_VARIABLE( int32, gv_screenshotInterval, 1, "연속 스크린샷 사이 프레임 수 (기본 1)" );

    /**
     * @brief `-gv_screenshotQuit` 입니다. `-gv_screenshot` · `-gv_screenshotEditor` 로 요청한 사진을 모두 찍으면 앱이 스스로 끝납니다(종료 코드 0, 파일을 쓰지 못했으면 14).
     * @details 기본은 끄기입니다. `-gv_profileFrames` 와 함께 쓰는 실행(사진을 찍으며 프로파일을 측정한다)이 사진 뒤에 일찍 끝나지 않게 하려는 것이고,
     *          표준 입력의 quit 으로 끝내던 호출자도 그대로 돕니다. 사진 인자를 주지 않으면 끝낼 근거가 없어 아무 일도 하지 않습니다.
     */
    SW_TEST_GLOBAL_VARIABLE_SHIPPED( bool, gv_screenshotQuit, false, "요청한 스크린샷을 모두 찍으면 앱이 스스로 끝난다 (0, 파일을 쓰지 못했으면 14)" );

    // 커맨드 리스트를 프레임 끝에 모아 한 번에 제출할지(기본), 잘릴 때마다 바로 제출할지 정한다. 이 파일이 프레임마다 디바이스로 전달한다.
    // 두 모드 모두 기록 순서가 곧 실행 순서다. 즉시 모드도 [세그먼트][리스트] 순서를 지켜 제출하고
    // 제출 '시점'만 달라진다. 즉시 모드는 제출 횟수가 늘어 오버헤드가 크지만, GPU 오류(DEVICE_HUNG,
    // 검증 레이어)가 어느 제출에서 났는지 좁히기 쉬워 디버깅에 쓴다.
    SW_GLOBAL_VARIABLE( bool, gv_rhiImmediateSubmit, false,
                        "RHI 커맨드 리스트를 프레임 끝에 모아 제출하지 않고 즉시 제출 (디버깅용, 오버헤드 큼)" );

    // 전용 렌더 스레드를 쓸지 정한다(false 면 게임 스레드가 바로 제출한다). 읽는 곳은 이 파일뿐이다(`attach` 와 `submit`).
    SW_GLOBAL_VARIABLE( bool, gv_useRenderThread, true, "전용 RenderThread 사용 (false = 게임 스레드 인라인 submit)" );

    /** @brief 받은 렌더 타깃 읽기 하나입니다. 티켓을 직접 쥐고 소멸할 때 버립니다 — `PendingReadback` 은 쥔 동안 같은 스레드의 `...AndWait`(스크린샷)를 단언으로 막아 쓰지 않는다. */
    struct RenderThread::PendingRenderTargetRead
    {
        IRHIResourceFactory* _pFactory{ nullptr };
        RHITransferTicket    _ticket{};   ///< 제출했으면 유효하다(`Busy` 로 아직 못 냈으면 무효)
        RenderTargetReadback _readback{}; ///< 요청의 텍스처 · 번호와, 제출할 때 받은 행 배치
        bool                 _bSubmitted{ false };

        PendingRenderTargetRead() = default;
        ~PendingRenderTargetRead() { discardTicket(); }
        PendingRenderTargetRead( const PendingRenderTargetRead& )            = delete;
        PendingRenderTargetRead& operator=( const PendingRenderTargetRead& ) = delete;

        void discardTicket()
        {
            if ( _pFactory != nullptr && _ticket.isValid() )
                _pFactory->discardTextureReadback( _ticket );
            _ticket = RHITransferTicket{};
        }
    };

    RenderThread::RenderThread()
        : _pDevice{ nullptr }
        , _pFrameRenderer{ nullptr }
        , _presentHook{}
        , _thread{}
        , _bRunning{ false }
        , _bStop{ false }
        , _bContextBound{ false }
        , _bLastImmediateSubmit{ false }
        , _bScreenshotTaken{ SW_FALSE }
        , _bScreenshotWriteFailed{ SW_FALSE }
        , _screenshotFrameCounter{ 0 }
        , _screenshotShotCount{ 0 }
        , _screenshotWaitStartMicroseconds{ 0 }
        , _budgetFrameCounter{ 0 }
        , _completedScenarioScreenshotCount{ 0 }
        , _bScenarioCaptureEnabled{ false }
        , _requestedScreenshotPath{}
        , _requestedScreenshotTexture{ 0 }
        , _requestedScreenshotFormat{ RHIFormat::R8G8B8A8_UNORM }
        , _requestedScreenshotWaitFrames{ 0 }
        , _pPendingTargetRead{}
#if SW_DEV_COMMANDS_ENABLED
        , _pendingDeviceLossWaitMilliseconds{ 0 }
#endif
        , _arrRingBuffer{}
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

    bool RenderThread::attach( IRHIDevice* pDevice, FrameRenderer* pFrameRenderer )
    {
        return gv_useRenderThread ? start( pDevice, pFrameRenderer ) : bind( pDevice, pFrameRenderer );
    }

    bool RenderThread::start( IRHIDevice* pDevice, FrameRenderer* pFrameRenderer )
    {
        if ( _bRunning.load( std::memory_order_relaxed ) )
            return true;

        if ( bind( pDevice, pFrameRenderer ) == false )
            return false;

        RenderThreadInternal::unbindGraphicsContext( pDevice );

        _bStop    = false;
        _bRunning = true;
        _thread   = sw::thread( &RenderThread::threadMain, this );
        // 다른 스레드가 장치 대기를 부르면 이 스레드가 받은 작업을 먼저 끝내게 한다(`IRHIDevice::waitIdle`).
        if ( pDevice != nullptr )
            pDevice->setRenderThreadDrain( &RenderThread::drainPacketsThunk, this, _thread.get_id() );
        SW_LOG_TRACE( "Dedicated worker started" );
        return true;
    }

    void RenderThread::stop()
    {
        if ( _bRunning.exchange( false, std::memory_order_acq_rel ) == false )
        {
            discardPendingRenderTargetRead(); // 인라인 제출은 호출한 스레드가 읽기를 이었다 — 티켓이 팩토리보다 오래 살지 않게 한다
            _pDevice        = nullptr;
            _pFrameRenderer = nullptr;
            _bContextBound  = false;
            return;
        }
        {
            // 멈춤 표시는 **대기 쪽과 같은 락 안에서** 설정한다. 렌더 스레드는 `_mutex` 를 잡은 채 조건(_bStop, 새 패킷)을 보고 잠드는데, 락 밖에서
            // 설정하고 알리면 "조건을 거짓으로 본 뒤, 실제로 잠들기 전" 틈에 알림이 끼어 사라지고, 렌더 스레드는 영영 자고 아래 join 이 멈춘다
            // (백엔드 교체와 gv_useRenderThread 토글에서 가장 잘 드러났다).
            ScopedLock<mutex> lock{ _mutex };
            _bStop.store( true, std::memory_order_release );
        }
        _cvProduce.notify_all();
        _cvConsume.notify_all();
        _cvIdle.notify_all();
        if ( _thread.joinable() )
        {
            if ( std::this_thread::get_id() != _thread.get_id() )
                _thread.join();
        }
        discardPendingRenderTargetRead(); // 스레드가 끝난 뒤, 디바이스를 놓기 전에
        if ( _pDevice != nullptr )
            _pDevice->setRenderThreadDrain( nullptr, nullptr, std::thread::id{} );
        _pDevice        = nullptr;
        _pFrameRenderer = nullptr;
        _bContextBound  = false;
        SW_LOG_TRACE( "Stopped" );
    }

    void RenderThread::submit( RenderFramePacket& packet )
    {
        if ( _pDevice == nullptr )
        {
            SW_LOG_WARNING_ONCE( "submit() with no bound device — packet dropped." );
            return;
        }

        // 런타임에 gv_useRenderThread 가 바뀌면 스레드 모드를 그 자리에서 바꾼다.
        const bool bShouldRunWorker = gv_useRenderThread;
        if ( _bRunning.load( std::memory_order_acquire ) != bShouldRunWorker )
        {
            IRHIDevice*    pSavedDevice        = _pDevice;
            FrameRenderer* pSavedFrameRenderer = _pFrameRenderer;

            if ( bShouldRunWorker )
            {
                if ( _bContextBound )
                {
                    RenderThreadInternal::unbindGraphicsContext( _pDevice );
                    _bContextBound = false;
                }
                start( pSavedDevice, pSavedFrameRenderer );
            }
            else
            {
                waitIdle();
                stop();
                if ( bind( pSavedDevice, pSavedFrameRenderer ) == false )
                    SW_LOG_ERROR( "Render thread could not go back to inline submit - no device is bound" );
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
            UniqueLock<mutex> lock{ _mutex };
            _cvProduce.wait( lock, [this, nextHead]() SW_REQUIRES_LOCK( _mutex )
            { return _bStop.load( std::memory_order_relaxed ) || nextHead != _tail.load( std::memory_order_acquire ); } );

            if ( _bStop.load( std::memory_order_relaxed ) )
                return;
        }
        // 끝날 때까지 다른 스레드가 해제하는 자원 핸들은 미뤄진다(`IRHIDevice::releaseHandle`. 이 프레임을 병렬로 기록하는 동안 테이블을 바꾸지 않게 한다).
        // 넘긴 수는 패킷이 렌더 스레드에 보이기(head) 전에 올린다 — 끝낸 수가 넘긴 수를 앞지르지 않게. 장치 호출이라 잠금 밖에서 한다(잠금은 잎 잠금, 결정 5-3).
        // 생산자는 이 스레드 하나라 놓은 사이에 슬롯이 다시 차지 않고(소비자는 tail 만 민다), 멈춤(`stop`)도 이 스레드가 부른다.
        _pDevice->notifyRenderFrameQueued();
        {
            ScopedLock<mutex> lock{ _mutex };
            // 맞바꾸기다. 호출하는 쪽은 이 슬롯에 있던 지난 패킷의 저장소를 받아 다음 프레임에 그대로 쓴다.
            std::swap( _arrRingBuffer[currentHead], packet );
            _head.store( nextHead, std::memory_order_release );
        }
        _cvConsume.notify_one();
    }

    void RenderThread::drainPacketsThunk( void* pContext )
    {
        static_cast<RenderThread*>( pContext )->drainPackets();
    }

    void RenderThread::drainPackets()
    {
        if ( _bRunning.load( std::memory_order_acquire ) == false || std::this_thread::get_id() == _thread.get_id() )
            return;
        UniqueLock<mutex> lock{ _mutex };
        _cvIdle.wait( lock, [this]() SW_REQUIRES_LOCK( _mutex )
        { return _bStop.load( std::memory_order_relaxed ) || _tail.load( std::memory_order_acquire ) == _head.load( std::memory_order_acquire ); } );
    }

    void RenderThread::waitIdle()
    {
        if ( _bRunning.load( std::memory_order_acquire ) == false )
            return;

        if ( std::this_thread::get_id() != _thread.get_id() )
        {
            UniqueLock<mutex> lock{ _mutex };
            _cvIdle.wait( lock, [this]() SW_REQUIRES_LOCK( _mutex )
            { return _bStop.load( std::memory_order_relaxed ) || _tail.load( std::memory_order_acquire ) == _head.load( std::memory_order_acquire ); } );
        }

        if ( _pDevice != nullptr )
            _pDevice->waitIdle();
    }

    void RenderThread::executeInline( RenderFramePacket& packet )
    {
        // 게임 스레드(워커 없음) 경로다. 이 프레임 동안 이 스레드가 그래픽스 컨텍스트를 가진다.
        // 못 잡으면 기록 단계가 다시 확인해 그 프레임을 기록하지 않는다(실패 로그는 디바이스가 남긴다).
        (void)ensureContextOnCurrentThread();
        executePacket( packet );
    }

    void RenderThread::threadMain()
    {
        SW_MEMORY_SCOPE( RenderCPU );
        // 이 스레드에서 스택이 넘쳐도 크래시 리포트가 남게 한다(ThreadCrashStack::initializeCurrentThread 설명 참고).
        ThreadCrashStack::initializeCurrentThread();
        ThreadName::setCurrentThreadName( "RenderThread" );
        _bContextBound = false;

        for ( ;; )
        {
            uint32 currentTail = _tail.load( std::memory_order_relaxed );

            {
                UniqueLock<mutex> lock{ _mutex };
                _cvConsume.wait( lock, [this, currentTail]() SW_REQUIRES_LOCK( _mutex )
                { return _bStop.load( std::memory_order_relaxed ) || currentTail != _head.load( std::memory_order_acquire ); } );
            }

            if ( _bStop.load( std::memory_order_relaxed ) && currentTail == _head.load( std::memory_order_acquire ) )
            {
                // 이 스레드가 태스크를 기다리며 받은 도우미 슬롯을 반환한다. 렌더 스레드는 백엔드 교체와 토글마다 새로 만들어진다.
                if ( engine::areEngineServicesBound() )
                    engine::getSubsystem<TaskManager>().releaseCurrentThreadHelperSlot();
                break;
            }

            // 링 슬롯에서 그대로 처리한다. 옮겨 오면 링 슬롯의 저장소가 비어 GT 가 다음에 다시 할당한다. 생산자는
            // tail 이 앞으로 갈 때까지 이 슬롯을 덮어쓰지 않는다.
            executePacket( _arrRingBuffer[currentTail] );
            // 기록과 제출이 끝났다(병렬 기록 밖). 이 프레임까지 기다리던 핸들을 해제한다 — 뒤 패킷이 읽을 수 있는 것은 남는다.
            if ( _pDevice != nullptr )
                _pDevice->notifyRenderFrameRetired();

            {
                ScopedLock<mutex> lock{ _mutex };
                _tail.store( ( currentTail + 1 ) % _s_kRingCapacity, std::memory_order_release );
            }
            _cvProduce.notify_one();
            _cvIdle.notify_all();
        }

        if ( _pDevice != nullptr )
        {
            RenderThreadInternal::unbindGraphicsContext( _pDevice );
            _bContextBound = false;
        }
    }

    void RenderThread::executePacket( RenderFramePacket& packet )
    {
        // 훅에 넘길 디바이스가 아예 없으면 할 수 있는 일이 없다.
        if ( _pDevice == nullptr )
            return;

        std::ignore = executeFrameBody( packet );

        // 프레임 본문이 중간에 멈추더라도 postPresent 통지는 반드시 보낸다.
        // 에디터는 이 신호로 그리기 스냅샷의 "처리 중" 상태를 해제하므로, 빠뜨리면
        // 다음 updateUI 가 waitForDrawSnapshotIdle 에서 끝없이 기다린다.
        if ( _postPresentHook.isBound() )
            _postPresentHook( *_pDevice, packet );

        IRHIContextThreadBinding* pContextBinding = _pDevice->findContextThreadBinding();
        if ( pContextBinding != nullptr && _bContextBound.load( std::memory_order_relaxed ) )
        {
            pContextBinding->unbindGraphicsContext();
            _bContextBound = false;
        }
    }

    bool RenderThread::executeFrameBody( RenderFramePacket& packet )
    {
        if ( packet._bValid == 0 )
            return false;

        // 스크린샷 실행에서만 Present 결과를 텍스처로 받아 둔다. 전체 화면 복사가 한 번 더 들어간다.
        // **프레임마다 맞춘다.** bind() 시점에는 커맨드라인이 아직 전역 변수에 반영되기 전일 수 있다.
        takeScreenshotRequest();
        if ( _pFrameRenderer != nullptr )
        {
            const bool bRequestedPresent = _requestedScreenshotPath.empty() == false && _requestedScreenshotTexture == 0;
            _pFrameRenderer->getCapture().setPresentEnabled( gv_screenshot.empty() == false || _bScenarioCaptureEnabled.load( std::memory_order_acquire ) || bRequestedPresent );
        }

        // 렌더 스레드 전체다. `GT.Packet.submit` 이 크면 GT 가 여기를 기다린다는 뜻이다.
        //
        // **무엇을 기다리는지는 아래 스코프들이 알려 준다.** `RT.BeginFrame`(GPU 백프레셔),
        // `RT.ExecutePacket`(기록), `RT.PresentHook`(에디터 UI), `RT.Present`(제출)가 있다.
        // 주의: "RT.Frame - RT.Present = 기록 시간" 이 아니다. GPU 대기의 대부분은 Present 가 아니라 `beginFrame` 에
        // 있다(이번 프레임 얼로케이터가 풀릴 때까지 펜스를 기다린다). 그 대기는 **씬 복잡도가 아니라 GPU 와 프레임
        // 페이싱**이 정한다(Release 와 DX12 벤치에서 큐브 2000개를 200개로 줄여도 거의 그대로다). 기록 경로를 CPU 에서
        // 줄여도 프레임은 줄지 않는다.
        SW_PROFILE_SCOPE( "RT.Frame" );

        // 컨텍스트를 잡지 못한 프레임은 기록하지 않는다(GL 호출이 버려진다). postPresent 통지는 executePacket 이 그래도 보낸다.
        if ( ensureContextOnCurrentThread() == false )
            return false;

        const bool          bOffscreen   = packet._outputRenderTarget != 0;
        IRHICommandContext* pFrameStream = _pDevice->getFrameStreamContext();
        if ( bOffscreen && pFrameStream == nullptr )
        {
            SW_LOG_ERROR_ONCE( "getFrameStreamContext() is null; skipping offscreen packet" );
            return false;
        }

        // 프레임 수명주기는 경로와 무관하게 항상 여기서, 어떤 기록보다 먼저 한 번 연다. `beginFrame` 은 렌더 타깃을
        // 바인딩하지 않는다. 백버퍼를 잡는 것은 아래 명시적 백버퍼 렌더 패스(핸들 0) 하나뿐이다. 이 둘은 반드시
        // 같이 있어야 한다(백버퍼 렌더 패스를 빼면 UI 가 직전 타깃과 상태에 그려지고 DX12 는 DEVICE_HUNG 이 난다).
        // 제출 정책은 매 프레임 갱신한다. 런타임에 gv 를 토글해도 다음 프레임부터 바로 적용된다.
        // 즉시 모드는 제출 횟수가 늘어 성능이 크게 떨어지므로, 모르고 그 상태로 측정하는 일이 없도록
        // 바뀐 순간에 한 번 로그를 남긴다.
        if ( gv_rhiImmediateSubmit != _bLastImmediateSubmit )
        {
            _bLastImmediateSubmit = gv_rhiImmediateSubmit;
            SW_LOG_INFO( "RHI submit mode: %#", _bLastImmediateSubmit ? "immediate (debug)" : "batched at endFrame" );
        }
        _pDevice->setImmediateSubmit( gv_rhiImmediateSubmit );
        {
            // **여기가 GPU 백프레셔다.** 백엔드의 `beginFrame` 은 이번 프레임이 쓸 커맨드
            // 얼로케이터와 프레임 리소스가 풀릴 때까지 펜스를 기다린다.
            SW_PROFILE_SCOPE( "RT.BeginFrame" );
#if SW_DEV_COMMANDS_ENABLED
            // 개발 명령 `rhi.removeDevice <ms>` 가 건 드라이버 대기 + 장치 제거를 펜스 대기 자리에서 소비한다(실제 TDR 이 막히는 곳).
            consumeDeviceLossRequest();
#endif
            _pDevice->beginFrame();
        }

        // 게임 뷰 렌더 타깃을 잡는다. 오프스크린도 프레임 스트림과 같은 스트림, 같은 제출이고, 순서는 큐 순서와
        // 아래 prepareTextureForShaderRead 의 배리어가 보장한다(별도 스트림이나 블로킹 제출을 두지 않는다).
        if ( bOffscreen )
        {
            RHIRenderPassBeginInfo gameViewPass{};
            gameViewPass._bBindColor        = SW_TRUE;
            gameViewPass._colorTargetCount  = 1;
            gameViewPass._arrColorTarget[0] = packet._outputRenderTarget;
            gameViewPass._arrLoadOp[0]      = RHIRenderPassLoadOp::Clear;
            gameViewPass._arrClearColor[0]  = packet._clearColor;
            pFrameStream->beginRenderPass( gameViewPass );
        }

        // 그래프가 이 프레임을 실제로 그렸는가. 그리지 않은 프레임(렌더러 준비 전, 주 시점이 없는 패킷)은 백버퍼를 아무도 건드리지 않았다.
        bool bGraphDrew = false;
        if ( _pFrameRenderer != nullptr && _pFrameRenderer->isReady() )
        {
            // 그래프 실행 바깥의 준비 작업(업로드 큐 정리 등)도 여기에 포함된다. 안쪽의
            // `RT.Graph.executeParallel` 만으로는 그 차이가 테이블에서 사라진다.
            SW_PROFILE_SCOPE( "RT.ExecutePacket" );
            bGraphDrew = _pFrameRenderer->executePacket( _pDevice, packet );
        }

        // 에디터가 게임 뷰 텍스처를 샘플링한다. 읽기 상태로 바꾼다(열려 있는 렌더 패스도 여기서 닫힌다).
        if ( bOffscreen )
            pFrameStream->prepareTextureForShaderRead( packet._outputRenderTarget );

        drawScenePreviews( packet, pFrameStream );

        // UI(presentHook)는 백버퍼에 그린다. 그래프가 오프스크린이나 백버퍼 어디에 그렸든, 여기서 타깃을
        // 명시적으로 백버퍼로 되돌린다. 그래프가 백버퍼에 그린 경우도 있으므로 Load 여야 한다.
        if ( pFrameStream != nullptr )
        {
            RHIRenderPassBeginInfo backbufferPass{};
            backbufferPass._bBindColor        = SW_TRUE;
            backbufferPass._colorTargetCount  = 1;
            backbufferPass._arrColorTarget[0] = 0; // 0 = 백버퍼
            // 오프스크린 경로에서는 그래프가 게임 RT 에만 그렸으므로 백버퍼는 아직 아무도 건드리지 않았다.
            // 그래서 여기서 클리어한다. 백버퍼 경로에서는 그래프가 이미 그렸으므로 보존해야 한다.
            // 그래프가 그리지 않은 프레임도 백버퍼는 받은 그대로(Vulkan 은 레이아웃 UNDEFINED)라 Load 로 열 수 없다 — 클리어한다.
            backbufferPass._arrLoadOp[0]     = ( bOffscreen || bGraphDrew == false ) ? RHIRenderPassLoadOp::Clear : RHIRenderPassLoadOp::Load;
            backbufferPass._arrClearColor[0] = packet._clearColor;
            pFrameStream->beginRenderPass( backbufferPass );
        }

        if ( _presentHook.isBound() )
        {
            // 에디터 UI 가 이 훅으로 백버퍼에 그린다. 에디터를 켜면 이것이 프레임의 큰 비중을 차지한다.
            SW_PROFILE_SCOPE( "RT.PresentHook" );
            _presentHook( *_pDevice, packet );
        }

        {
            // 제출과 Present 다. GPU 가 밀리면 여기서 기다린다.
            SW_PROFILE_SCOPE( "RT.Present" );
            // VSync 는 **디바이스가 채택한 값**이다. 여기 true 를 고정해 두면 EngineConfig 의
            // `_window._bVSync` 와 CLI `-vsync` 가 둘 다 무시되고 프레임이 모니터 주사율에 묶인다.
            _pDevice->endFrame( _pDevice->isVSyncEnabled() );
        }
        // 외부 프로파일러(Tracy)의 렌더 스레드 프레임 경계다. 주 프레임(게임 스레드)과 주기가 달라 이름을 따로 둔다.
        IProfilerBackend* pProfilerBackend = ProfilerBackend::getActiveBackend();
        if ( pProfilerBackend != nullptr )
            pProfilerBackend->markFrame( "Render" );

        {
            // 드라이버의 GPU 메모리 사용량과 예산을 장부에 기록한다. 첫 프레임에도 묻는다. 보고가 "모름" 으로 시작하지 않게 하려는 것이다.
            // 프레임을 기록하는 이 스레드에서 묻는다(GL 은 컨텍스트를 잡고 묻는다. 다른 스레드면 이 프레임을 기다린다).
            const bool bRefreshBudget = ( _budgetFrameCounter % _s_kBudgetRefreshFrames ) == 0;
            ++_budgetFrameCounter;
            if ( bRefreshBudget )
            {
                SW_PROFILE_SCOPE( "RT.RefreshGpuMemoryBudget" );
                _pDevice->refreshGPUMemoryBudget();
            }
        }

        takeCommandLineScreenshotIfDue( packet );

        // 자동화 시나리오의 `<Screenshot>` 이다. 이 패킷(그 게임 프레임)이 그린 이미지를 찍는다. gv_screenshot 의 대기 프레임 규칙은 적용하지 않는다(시나리오가 시작 조건 뒤에 찍는다).
        if ( packet._screenshotPath.empty() == false )
        {
            if ( _pFrameRenderer == nullptr || writePresentedImage( packet._screenshotPath ) == false )
                SW_LOG_WARNING( "Scenario screenshot was not written: %#", packet._screenshotPath.c_str() );
            _completedScenarioScreenshotCount.fetch_add( 1, std::memory_order_acq_rel );
        }

        // 실행 중 요청(`requestScreenshot`: 개발 명령 screenshot, 에디터 버튼)을 처리한다.
        writeRequestedScreenshot();
        // 에디터 렌더 타깃 창의 CPU 읽기 요청이다. 스크린샷과 같은 위치에서 처리하며, endFrame 뒤라 이 프레임이 그린 내용이 들어 있다.
        readRequestedRenderTarget();

        return true;
    }

    void RenderThread::drawScenePreviews( RenderFramePacket& packet, IRHICommandContext* pFrameStream )
    {
        // 미리보기 씬(에디터 툴 창)은 씬마다 자기 렌더러로 자기 타깃에 그린다. 타깃의 열기 · 읽기 전환은 게임 뷰와 같은 규칙이다.
        const bool bCanDrawPreviews = _pFrameRenderer != nullptr && _pFrameRenderer->isReady() && pFrameStream != nullptr;
        if ( bCanDrawPreviews )
        {
            SW_PROFILE_SCOPE( "RT.ScenePreviews" );
            for ( uint32 previewIndex = 0; previewIndex < packet._scenePreviewCount; ++previewIndex )
            {
                RenderFramePacket&     preview = *packet._listScenePreview[previewIndex];
                RHIRenderPassBeginInfo previewPass{};
                previewPass._bBindColor        = SW_TRUE;
                previewPass._colorTargetCount  = 1;
                previewPass._arrColorTarget[0] = preview._outputRenderTarget;
                previewPass._arrLoadOp[0]      = RHIRenderPassLoadOp::Clear;
                previewPass._arrClearColor[0]  = preview._clearColor;
                pFrameStream->beginRenderPass( previewPass );
                (void)_pFrameRenderer->executeScenePreviewPacket( _pDevice, preview ); // 실패는 렌더러가 로그로 남기고, 타깃은 클리어 색으로 남는다
                pFrameStream->prepareTextureForShaderRead( preview._outputRenderTarget );
            }
            _pFrameRenderer->retireScenePreviews( packet );
        }
    }

    bool RenderThread::needsCaptureReadiness()
    {
        return gv_screenshot.empty() == false && gv_screenshotSettledFrames > 0;
    }

    void RenderThread::takeCommandLineScreenshotIfDue( const RenderFramePacket& packet )
    {
        // -gv_screenshot=<path>: 한 장(또는 -gv_screenshotCount 장)을 찍는다.
        //  - **endFrame 뒤여야 한다.** 그 전에는 커맨드 리스트가 기록만 되었고 아직 큐에 나가지 않아,
        //    읽어 보면 클리어 색만 나온다.
        //  - **몇 프레임 기다려야 한다.** 첫 프레임에는 GPUScene 업로드가 아직이라 그릴 게 없다.
        const bool bScreenshotPending = gv_screenshot.empty() == false && _bScreenshotTaken == SW_FALSE && _pFrameRenderer != nullptr;
        if ( bScreenshotPending )
        {
            // 최소 몇 프레임은 기다려야 한다. 첫 프레임에는 GPUScene 업로드가 아직이라 그릴 것이 없다.
            // 그 위로는 -gv_screenshotFrame 이 정한다(시간에 따라 움직이는 장면을 비교할 때 필요하다). -gv_screenshotSettledFrames 면 자리 잡은 패킷부터 센다.
            constexpr uint32 kScreenshotMinWarmupFrames = 10;
            const bool       bCountFromSettled          = gv_screenshotSettledFrames > 0;
            const uint32     targetFrame                = bCountFromSettled                                                         ? static_cast<uint32>( gv_screenshotSettledFrames )
                                                        : ( gv_screenshotFrame > static_cast<int32>( kScreenshotMinWarmupFrames ) ) ? static_cast<uint32>( gv_screenshotFrame )
                                                                                                                                    : kScreenshotMinWarmupFrames;
            const uint32     shotCount                  = gv_screenshotCount > 1 ? static_cast<uint32>( gv_screenshotCount ) : 1u;
            const uint32     interval                   = gv_screenshotInterval > 1 ? static_cast<uint32>( gv_screenshotInterval ) : 1u;
            if ( _screenshotWaitStartMicroseconds == 0 )
            {
                _screenshotWaitStartMicroseconds = MonotonicClock::nowMicroseconds();
                expectCapture( CaptureKind::GameView ); // 에디터 사진의 알림(첫 UI 프레임)과 어느 쪽이 먼저든, 첫 사진(10 프레임 뒤)보다 앞선다
            }
            // 첫 장 전에 자리 잡음이 풀리면 처음부터 다시 센다 — N 프레임 내내 자리 잡은 그림을 찍는다. 시한을 넘기면 찍지 않고 오류로 끝낸다.
            const bool bWaitingForSettle = bCountFromSettled && _screenshotShotCount == 0u && _screenshotFrameCounter < targetFrame;
            if ( bWaitingForSettle && packet._bReadyForCapture == SW_FALSE )
            {
                _screenshotFrameCounter     = 0;
                const float64 waitedSeconds = static_cast<float64>( MonotonicClock::nowMicroseconds() - _screenshotWaitStartMicroseconds ) * 1.0e-6;
                if ( waitedSeconds > RenderThreadInternal::kSettleTimeoutSeconds )
                {
                    SW_LOG_ERROR( "Screenshot was not taken: the screen did not settle within %# s (loading screen, screen fade or asset loads never finished) - %#",
                                  static_cast<uint32>( RenderThreadInternal::kSettleTimeoutSeconds ), gv_screenshot.c_str() );
                    _bScreenshotTaken       = SW_TRUE;
                    _bScreenshotWriteFailed = SW_TRUE;
                    reportCaptureFinished( CaptureKind::GameView, false );
                }
                return;
            }
            const uint32 frame    = ++_screenshotFrameCounter;
            const bool   bShotDue = frame >= targetFrame && ( frame - targetFrame ) % interval == 0u;
            if ( bShotDue )
            {
                // 연속 촬영이면 장 번호를 확장자 앞에 붙인다.
                string path{ gv_screenshot };
                if ( shotCount > 1u )
                {
                    StringBuilder<constant::kMaxBuffer16> suffix;
                    suffix.appendFormat( "_%03u", _screenshotShotCount );
                    path = FileUtil::removeExtension( path ) + suffix.c_str() + FileUtil::getExtension( path );
                }
                ++_screenshotShotCount;
                _bScreenshotTaken = _screenshotShotCount >= shotCount ? SW_TRUE : SW_FALSE;
                // 중간 단계를 보고 싶다고 첨부 이름을 지정했으면 그 첨부를, 아니면 화면에 나간 이미지를 덤프한다.
                // 실패는 경로와 함께 알린다(시나리오 경로와 같다). 읽기와 쓰기 실패 로그만으로는 어느 촬영이 빠졌는지 알 수 없다.
                const string_view attachment{ gv_screenshotAttachment };
                const bool        bWritten = attachment.empty() == false ? _pFrameRenderer->getCapture().dumpTransientToFile( attachment, path ) : writePresentedImage( path );
                if ( bWritten == false )
                {
                    _bScreenshotWriteFailed = SW_TRUE;
                    SW_LOG_WARNING( "Screenshot was not written: %#", path.c_str() );
                }
                if ( _bScreenshotTaken == SW_TRUE )
                    reportCaptureFinished( CaptureKind::GameView, _bScreenshotWriteFailed == SW_FALSE );
            }
        }
    }

    bool RenderThread::writePresentedImage( const string& path )
    {
        // 기본은 **Present 결과 캡처**, 곧 화면에 나간 이미지다. 캡처가 없으면 Present 가 읽는 첨부로 대체한다.
        // 첨부 이름을 리터럴로 고정하지 않는다. 그 이름이 없는 파이프라인(디퍼드)에서는 한 장도 찍히지 않는다.
        if ( _pFrameRenderer->getCapture().dumpPresentToFile( path ) )
            return true;
        // 캡처가 없으면(오프스크린 출력 등) Present 가 **읽는** 첨부를 찍는다. 그 이미지에는 Present 패스가 한 일(톤맵 등)이 들어 있지 않다.
        string_view fallback = _pFrameRenderer->getPresentedAttachmentName();
        if ( fallback.empty() )
            fallback = string_view{ FrameRendererUtil::Attachment::kSceneColor };
        return _pFrameRenderer->getCapture().dumpTransientToFile( fallback, path );
    }

    void RenderThread::requestScreenshot( string_view filePath, RHITextureHandle sourceTexture, RHIFormat sourceFormat )
    {
        ScreenshotMailbox* const pMailbox = RenderThreadInternal::findMailbox();
        if ( pMailbox == nullptr )
        {
            SW_LOG_ERROR( "Screenshot '%#' was not requested - this host has no screenshot mailbox (no renderer)", filePath );
            return;
        }
        pMailbox->put( filePath, sourceTexture, sourceFormat );
    }

#if SW_DEV_COMMANDS_ENABLED
    void RenderThread::requestDeviceLossAfterDriverWait( uint32 waitMilliseconds )
    {
        SW_ASSERT( 1u <= waitMilliseconds && waitMilliseconds <= kMaxDeviceLossDriverWaitMilliseconds );
        _pendingDeviceLossWaitMilliseconds.store( waitMilliseconds, std::memory_order_relaxed );
    }

    void RenderThread::consumeDeviceLossRequest()
    {
        const uint32 waitMilliseconds = _pendingDeviceLossWaitMilliseconds.exchange( 0u, std::memory_order_relaxed );
        if ( waitMilliseconds == 0u )
            return;
        SW_LOG_WARNING( "Holding the render thread %# ms before removing the device (rhi.removeDevice <ms>) - the driver wait a real TDR spends before the first device-lost result",
                        waitMilliseconds );
        MonotonicClock::sleepUntilNanoseconds( MonotonicClock::nowNanoseconds() + static_cast<int64>( waitMilliseconds ) * constant::kNanosecondsPerMillisecond );
        if ( _pDevice->debugRemoveDevice() == false )
            SW_LOG_ERROR( "rhi.removeDevice <ms>: this backend cannot remove its device; only the wait ran" );
    }
#endif

    uint32 RenderThread::getCompletedScreenshotSerial()
    {
        const ScreenshotMailbox* const pMailbox = RenderThreadInternal::findMailbox();
        return pMailbox != nullptr ? pMailbox->getCompletedSerial() : 0u; // 우편함이 없으면 처리한 요청이 없다
    }

    void RenderThread::expectCapture( CaptureKind kind )
    {
        ScreenshotMailbox* const pMailbox = RenderThreadInternal::findMailbox();
        if ( pMailbox == nullptr )
        {
            SW_LOG_ERROR( "A capture was announced but this host has no screenshot mailbox (no renderer)" );
            return;
        }
        pMailbox->expectCapture( static_cast<uint32>( kind ) );
    }

    void RenderThread::reportCaptureFinished( CaptureKind kind, bool bWritten )
    {
        ScreenshotMailbox* const pMailbox = RenderThreadInternal::findMailbox();
        if ( pMailbox == nullptr )
        {
            SW_LOG_ERROR( "A capture finished but this host has no screenshot mailbox (no renderer)" );
            return;
        }
        pMailbox->reportCaptureFinished( static_cast<uint32>( kind ), bWritten );
    }

    int32 RenderThread::getCaptureExitCode()
    {
        if ( gv_screenshotQuit == false )
            return kNoCaptureExitCode;
        const ScreenshotMailbox* const pMailbox = RenderThreadInternal::findMailbox();
        bool                           bFailed  = false;
        if ( pMailbox == nullptr || pMailbox->findAllCapturesFinished( bFailed ) == false )
            return kNoCaptureExitCode; // 우편함이 없으면 요청한 사진이 하나도 없다
        return bFailed ? kCaptureWriteFailedExitCode : 0;
    }

    string RenderThread::getLastScreenshotPath()
    {
        const ScreenshotMailbox* const pMailbox = RenderThreadInternal::findMailbox();
        return pMailbox != nullptr ? pMailbox->getLastPath() : string{}; // 우편함이 없으면 쓴 사진이 없다
    }

    void RenderThread::takeScreenshotRequest()
    {
        if ( _requestedScreenshotPath.empty() == false )
            return;
        ScreenshotMailbox* const pMailbox = RenderThreadInternal::findMailbox();
        ScreenshotRequest        request;
        if ( pMailbox == nullptr || pMailbox->take( request ) == false )
            return;
        _requestedScreenshotPath.swap( request._path );
        _requestedScreenshotTexture = request._texture;
        _requestedScreenshotFormat  = request._format;
        // 렌더 타깃은 이번 프레임에 그려지므로 기다리지 않는다. Present 캡처는 켠 뒤 한 프레임을 더 그린다.
        _requestedScreenshotWaitFrames = _requestedScreenshotTexture == 0 ? RenderThreadInternal::kPresentCaptureWaitFrames : 0;
    }

    void RenderThread::writeRequestedScreenshot()
    {
        if ( _requestedScreenshotPath.empty() || _pFrameRenderer == nullptr )
            return;
        if ( _requestedScreenshotWaitFrames > 0 )
        {
            --_requestedScreenshotWaitFrames;
            return;
        }

        (void)FileUtil::ensureParentDirectoryExists( _requestedScreenshotPath ); // 실패하면 아래 쓰기가 경로와 함께 알린다.
        const bool bWritten = _requestedScreenshotTexture == 0
                                ? writePresentedImage( _requestedScreenshotPath )
                                : _pFrameRenderer->getCapture().dumpTextureToFile( _requestedScreenshotTexture, _requestedScreenshotFormat, _requestedScreenshotPath );
        if ( bWritten )
            SW_LOG_INFO( "Screenshot saved: %#", _requestedScreenshotPath.c_str() );
        else
            SW_LOG_WARNING( "Screenshot was not written: %#", _requestedScreenshotPath.c_str() );

        ScreenshotMailbox* const pMailbox = RenderThreadInternal::findMailbox();
        if ( pMailbox != nullptr )
            pMailbox->reportCompleted( bWritten, _requestedScreenshotPath ); // 우편함이 먼저 내려갔으면 알릴 곳이 없다
        _requestedScreenshotPath.clear();
        _requestedScreenshotTexture = 0;
    }

    void RenderThread::discardPendingRenderTargetRead()
    {
        _pPendingTargetRead.reset();
    }

    void RenderThread::readRequestedRenderTarget()
    {
        RenderTargetRegistry* pRegistry             = engine::findSubsystem<RenderTargetRegistry>();
        const bool            bNoRenderTargetSource = pRegistry == nullptr || _pDevice == nullptr || _pDevice->getResourceFactory() == nullptr;
        if ( bNoRenderTargetSource )
            return;
        IRHIResourceFactory& factory = *_pDevice->getResourceFactory();

        // 읽는 중이 아니면 새 요청을 받는다. 읽는 중이면 새 요청은 레지스트리에 둔 채 이 읽기부터 끝낸다.
        if ( _pPendingTargetRead == nullptr )
        {
            RHITextureHandle texture = 0;
            uint32           serial  = 0;
            if ( pRegistry->takeReadbackRequest( texture, serial ) == false )
                return;
            _pPendingTargetRead                     = make_unique<PendingRenderTargetRead>();
            _pPendingTargetRead->_pFactory          = &factory;
            _pPendingTargetRead->_readback._texture = texture;
            _pPendingTargetRead->_readback._serial  = serial;
        }

        SW_PROFILE_SCOPE( "RT.ReadRenderTarget" );
        PendingRenderTargetRead& pending = *_pPendingTargetRead;
        // 실패는 조용히 돌려 UI 가 새 핸들로 다시 요청하게 한다.
        const auto publishFailure = [&]()
        {
            RenderTargetReadback readback = std::move( pending._readback );
            readback._byte.clear();
            readback._bSucceeded = SW_FALSE;
            _pPendingTargetRead.reset(); // 티켓이 남았으면 버린다
            pRegistry->publishReadback( std::move( readback ) );
        };

        if ( pending._bSubmitted == false )
        {
            // 요청한 뒤 트랜지언트가 다시 만들어졌으면(창 크기 변경) 핸들이 죽었다 — 읽지 않는다.
            if ( pRegistry->isPublished( pending._readback._texture ) == false )
            {
                publishFailure();
                return;
            }
            const RHITransferSubmit submit = factory.submitTextureReadback( pending._readback._texture, 0, 0, pending._readback._layout );
            if ( submit._result == RHISubmitResult::Busy )
                return; // 스테이징이 찼다 — 같은 요청을 다음 프레임에 다시 낸다
            if ( submit.isAccepted() == false )
            {
                publishFailure();
                return;
            }
            pending._ticket     = submit._ticket;
            pending._bSubmitted = true;
            return; // 끝남은 이후 프레임이 폴링한다(요청한 프레임에는 기다리지 않는다)
        }

        const RHITransferStatus status = factory.pollTransfer( pending._ticket );
        if ( status == RHITransferStatus::Pending )
            return;
        bool bTaken = false;
        if ( status == RHITransferStatus::Complete )
        {
            bTaken = factory.takeTextureReadback( pending._ticket, pending._readback._byte );
            if ( bTaken )
                pending._ticket = RHITransferTicket{}; // 가져갔다 — 소멸자가 버리지 않는다
        }
        if ( bTaken == false )
        {
            publishFailure();
            return;
        }
        RenderTargetReadback readback = std::move( pending._readback );
        readback._bSucceeded          = SW_TRUE;
        _pPendingTargetRead.reset();
        pRegistry->publishReadback( std::move( readback ) );
    }

    bool RenderThread::ensureContextOnCurrentThread()
    {
        if ( _pDevice == nullptr )
            return false;
        if ( _bContextBound.load( std::memory_order_relaxed ) )
            return true;

        // 실패 로그는 디바이스가 남긴다(가진 스레드와 시간까지 포함한다). 같은 실패를 두 줄로 남기지 않는다.
        // 컨텍스트가 스레드에 묶이지 않는 디바이스(DX12, Vulkan)는 붙일 것이 없어 바로 붙은 것으로 본다.
        IRHIContextThreadBinding* pContextBinding = _pDevice->findContextThreadBinding();
        if ( pContextBinding != nullptr && pContextBinding->bindGraphicsContext() == false )
            return false;
        _bContextBound = true;
        return true;
    }
} // namespace sw

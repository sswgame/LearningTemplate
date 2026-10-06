#include "pch.h"

#include "Engine/Graphics/Renderer/RenderThread.h"

#include "Core/Concurrency/ThreadName.h"
#include "Core/Concurrency/mutex.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Memory/MemoryProfiler.h"
#include "Core/Process/CrashHandler.h"
#include "Core/String/StringBuilder.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/RHI/IRHICommandContext.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/Renderer/Frame/FrameRenderer.h"
#include "Engine/Graphics/Renderer/Frame/FrameRendererUtil.h"
#include "Engine/Utility/Profiling/FrameProfiler.h"
#include "Engine/Utility/Profiling/ProfilerBackend.h"

namespace sw
{
    SW_LOG_CALLER( "RenderThread" );

    /**
     * @brief `-gv_screenshot=<파일경로>` 입니다. 화면에 나간 그림(Present 결과)을 PPM 으로 한 장 덤프합니다(백엔드별 시각 검증용).
     * @details Win32 PrintWindow 캡처는 DX11 · GL · Vulkan 에서 빈 화면이 자주 나옵니다. 스왑체인이 GDI 로
     *          합성되지 않기 때문입니다. 그래서 GPU 에서 직접 읽습니다(readbackTexture2D).
     *          PPM 은 인코더가 필요 없어 의존성이 늘지 않습니다. `-gv_profileFrames` 와 같이 쓰면 찍고 종료합니다.
     * @note 셋 모두 이 파일이 유일한 소비자라 여기서 정의합니다. 다른 파일에서 `extern` 으로 끌어 쓰면 타입이 어긋나도 링커까지 가야 걸립니다.
     */
    SW_TEST_GLOBAL_VARIABLE_SHIPPED( sw::string, gv_screenshot, "", "화면에 나간 그림(Present 결과)을 PPM 으로 덤프할 경로 (비면 사용 안 함)" );

    /** @brief `-gv_screenshotAttachment=<이름>` 입니다. 덤프할 트랜지언트 첨부 이름이며, 비면 Present 캡처(없으면 Present 가 읽는 첨부)를 찍습니다. */
    SW_TEST_GLOBAL_VARIABLE_SHIPPED( sw::string, gv_screenshotAttachment, "", "Present 결과 대신 덤프할 트랜지언트 이름 (비면 Present 결과)" );

    /**
     * @brief `-gv_screenshotFrame=<N>` 입니다. 몇 번째 프레임에서 찍을지 정합니다(기본 10, 10 보다 작으면 10).
     * @details 시간에 따라 움직이는 것(GPU 인스턴스 회전 등)을 검증하려면 **서로 다른 시각**의 장면이
     *          필요합니다. 주의: `-gv_profileFrames` 는 찍는 시각을 바꾸지 않습니다 — 같은 프레임 번호끼리 비교하면
     *          "움직이지 않는다" 는 잘못된 결론이 나옵니다.
     */
    SW_TEST_GLOBAL_VARIABLE_SHIPPED( int32, gv_screenshotFrame, 10, "스크린샷을 찍을 프레임 번호 (기본 10)" );

    /**
     * @brief `-gv_screenshotCount=<N>` · `-gv_screenshotInterval=<K>` — 첫 장(`-gv_screenshotFrame`)부터 K 프레임마다 N 장을 찍습니다(움직임 · 튐 확인).
     * @details 두 장 이상이면 파일 이름의 확장자 앞에 `_000` · `_001` … 이 붙습니다. 장마다 GPU 되읽기라 그 프레임은 느려집니다.
     */
    SW_TEST_GLOBAL_VARIABLE_SHIPPED( int32, gv_screenshotCount, 1, "연속으로 찍을 스크린샷 수 (기본 1)" );
    SW_TEST_GLOBAL_VARIABLE_SHIPPED( int32, gv_screenshotInterval, 1, "연속 스크린샷 사이 프레임 수 (기본 1)" );

    // 커맨드 리스트를 프레임 끝에 모아 한 번에 제출할지(기본), 잘릴 때마다 바로 제출할지. 이 파일이 프레임마다 디바이스로 밀어 넣는다.
    // 두 모드 모두 기록 순서 = 실행 순서다. 즉시 모드도 [세그먼트][리스트] 순서를 지켜 제출하고
    // 제출 '시점'만 달라진다. 즉시 모드는 제출 횟수가 늘어 오버헤드가 크지만, GPU 오류(DEVICE_HUNG,
    // 검증 레이어)가 어느 제출에서 났는지 좁히기 쉬워 디버깅에 쓴다.
    SW_GLOBAL_VARIABLE( bool, gv_rhiImmediateSubmit, false,
                        "RHI 커맨드 리스트를 프레임 끝에 모아 제출하지 않고 즉시 제출 (디버깅용, 오버헤드 큼)" );

    // 전용 렌더 스레드를 쓸지(false 면 게임 스레드가 바로 제출한다). 읽는 곳은 이 파일뿐이다(`attach` · `submit`).
    SW_GLOBAL_VARIABLE( bool, gv_useRenderThread, true, "전용 RenderThread 사용 (false = 게임 스레드 인라인 submit)" );

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
        , _screenshotFrameCounter{ 0 }
        , _screenshotShotCount{ 0 }
        , _budgetFrameCounter{ 0 }
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

        if ( pDevice != nullptr )
            pDevice->unbindGraphicsContext();

        _bStop    = false;
        _bRunning = true;
        _thread   = std::thread( &RenderThread::threadMain, this );
        // 다른 스레드가 장치 대기를 부르면 이 스레드가 받은 일을 먼저 끝내게 한다(`IRHIDevice::waitIdle`).
        if ( pDevice != nullptr )
            pDevice->setRenderThreadDrain( &RenderThread::drainPacketsThunk, this, _thread.get_id() );
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
        {
            // 멈춤 표시는 **대기 쪽과 같은 락 안에서** 세운다. 렌더 스레드는 `_mutex` 를 쥔 채 조건(_bStop · 새 패킷)을 보고 잠드는데, 락 밖에서
            // 세우고 알리면 "조건을 거짓으로 본 뒤 · 실제로 잠들기 전" 틈에 알림이 끼어 사라지고, 렌더 스레드는 영영 자고 아래 join 이 멈춘다
            // (백엔드 교체 · gv_useRenderThread 토글에서 가장 잘 드러났다).
            std::scoped_lock<mutex> lock{ _mutex };
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
            SW_LOG_WARNING( "submit() with no bound device — packet dropped." );
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

            // 바꿔치기다. 부르는 쪽은 이 자리에 있던 지난 패킷의 저장소를 받아 다음 프레임에 그대로 쓴다.
            // 끝날 때까지 다른 스레드가 내리는 자원 핸들은 미뤄진다(`IRHIDevice::releaseHandle` — 이 프레임을 병렬로 기록하는 동안 표를 바꾸지 않게).
            _pDevice->notifyRenderFrameQueued();
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
        std::unique_lock<mutex> lock{ _mutex };
        _cvIdle.wait( lock, [this]()
        { return _bStop.load( std::memory_order_relaxed ) || _tail.load( std::memory_order_acquire ) == _head.load( std::memory_order_acquire ); } );
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
        // 게임 스레드(워커 없음) 경로: 이 프레임 동안 이 스레드가 그래픽스 컨텍스트를 갖는다.
        ensureContextOnCurrentThread();
        executePacket( packet );
    }

    void RenderThread::threadMain()
    {
        SW_MEMORY_SCOPE( RenderCpu );
        // 이 스레드에서 스택이 넘쳐도 크래시 리포트가 남게 한다(CrashHandler::initializeCurrentThread 설명).
        CrashHandler::initializeCurrentThread();
        ThreadName::setCurrentThreadName( "RenderThread" );
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
            {
                // 이 스레드가 태스크를 기다리며 받은 도우미 슬롯을 돌려준다. 렌더 스레드는 백엔드 교체 · 토글마다 새로 만들어진다.
                if ( engine::areEngineServicesBound() )
                    engine::getTaskManager().releaseCurrentThreadHelperSlot();
                break;
            }

            // 링 자리에서 그대로 처리한다. 옮겨 오면 링 자리의 저장소가 비어 GT 가 다음에 다시 할당한다. 생산자는
            // tail 이 앞으로 갈 때까지 이 자리를 덮어쓰지 않는다.
            executePacket( _arrRingBuffer[currentTail] );
            // 기록 · 제출이 끝났다(병렬 기록 밖) — 그동안 다른 스레드가 미룬 핸들을 여기서 내린다.
            if ( _pDevice != nullptr )
            {
                _pDevice->flushDeferredHandleReleases();
                _pDevice->notifyRenderFrameRetired();
            }

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
    }

    void RenderThread::executePacket( RenderFramePacket& packet )
    {
        // 훅에 넘길 디바이스가 아예 없으면 할 수 있는 게 없다.
        if ( _pDevice == nullptr )
            return;

        std::ignore = executeFrameBody( packet );

        // 프레임 본문이 중간에 멈추더라도 postPresent 통지는 반드시 보낸다.
        // 에디터는 이 신호로 그리기 스냅샷의 "처리 중" 상태를 풀므로, 빠뜨리면
        // 다음 updateUi 가 waitForDrawSnapshotIdle 에서 끝없이 기다린다.
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

        // 스크린샷 실행에서만 Present 결과를 텍스처로 받아 둔다. 전체 화면 복사가 한 번 더 붙는다.
        // **프레임마다 맞춘다**: bind() 시점에는 커맨드라인이 아직 전역 변수에 붙기 전일 수 있다.
        if ( _pFrameRenderer != nullptr )
            _pFrameRenderer->setPresentCaptureEnabled( gv_screenshot.empty() == false );

        // 렌더 스레드 전체. `GT.Packet.submit` 이 크면 GT 가 여기를 기다린다는 뜻이다.
        //
        // **무엇을 기다리는지는 아래 스코프들이 답한다**: `RT.BeginFrame`(GPU 백프레셔) ·
        // `RT.ExecutePacket`(기록) · `RT.PresentHook`(에디터 UI) · `RT.Present`(제출).
        // 주의: "RT.Frame - RT.Present = 기록 시간" 이 아니다. GPU 대기의 대부분은 Present 가 아니라 `beginFrame` 에
        // 있다(이번 프레임 얼로케이터가 풀릴 때까지 펜스를 기다린다). 그 대기는 **씬 복잡도가 아니라 GPU · 프레임
        // 페이싱**이 정한다(Release · DX12 벤치에서 큐브 2000 → 200 으로 줄여도 거의 그대로다). 기록 경로를 CPU 에서
        // 깎아도 프레임은 줄지 않는다.
        SW_PROFILE_SCOPE( "RT.Frame" );

        // 컨텍스트를 못 잡은 프레임은 기록하지 않는다(GL 호출이 버려진다). postPresent 통지는 executePacket 이 그래도 보낸다.
        if ( ensureContextOnCurrentThread() == false )
            return false;

        const bool          bOffscreen   = packet._gameRenderTarget != 0;
        IRHICommandContext* pFrameStream = _pDevice->getFrameStreamContext();
        if ( bOffscreen && pFrameStream == nullptr )
        {
            SW_LOG_ERROR( "getFrameStreamContext() is null; skipping offscreen packet" );
            return false;
        }

        // 프레임 수명주기는 경로와 무관하게 항상 여기서, 어떤 기록보다 먼저 한 번 연다. `beginFrame` 은 렌더 타깃을
        // 바인딩하지 않는다 — 백버퍼를 잡는 것은 아래 명시적 백버퍼 렌더 패스(핸들 0) 하나뿐이다. 이 둘은 반드시
        // 같이 있어야 한다(백버퍼 렌더 패스를 빼면 UI 가 직전 타깃 · 상태에 그려지고 DX12 는 DEVICE_HUNG 이 난다).
        // 제출 정책은 매 프레임 갱신한다. 런타임에 gv 를 토글해도 다음 프레임부터 바로 먹힌다.
        // 즉시 모드는 제출 횟수가 늘어 성능이 크게 떨어지므로, 모르고 그 상태로 재는 일이 없도록
        // 바뀐 순간에 한 번 남긴다.
        if ( gv_rhiImmediateSubmit != _bLastImmediateSubmit )
        {
            _bLastImmediateSubmit = gv_rhiImmediateSubmit;
            SW_LOG_INFO( "RHI submit mode: %#", _bLastImmediateSubmit ? "immediate (debug)" : "batched at endFrame" );
        }
        _pDevice->setImmediateSubmit( gv_rhiImmediateSubmit );
        {
            // **여기가 GPU 백프레셔다.** 백엔드의 `beginFrame` 은 이번 프레임이 쓸 커맨드
            // 얼로케이터 · 프레임 리소스가 풀릴 때까지 펜스를 기다린다.
            SW_PROFILE_SCOPE( "RT.BeginFrame" );
            _pDevice->beginFrame( packet._clearColor );
        }

        // 게임뷰 렌더 타깃을 잡는다. 오프스크린도 프레임 스트림과 같은 스트림 · 같은 제출이고, 순서는 큐 순서와
        // 아래 prepareTextureForShaderRead 의 배리어가 보장한다(별도 스트림 · 블로킹 제출을 두지 않는다).
        if ( bOffscreen )
        {
            RHIRenderPassBeginInfo gameViewPass{};
            gameViewPass._bBindColor        = SW_TRUE;
            gameViewPass._colorTargetCount  = 1;
            gameViewPass._arrColorTarget[0] = packet._gameRenderTarget;
            gameViewPass._arrLoadOp[0]      = RHIRenderPassLoadOp::Clear;
            gameViewPass._arrClearColor[0]  = packet._clearColor;
            pFrameStream->beginRenderPass( gameViewPass );
        }

        if ( _pFrameRenderer != nullptr && _pFrameRenderer->isReady() )
        {
            // 그래프 실행 바깥의 준비 작업(업로드 큐 정리 등)도 여기에 든다. 안쪽의
            // `RT.Graph.executeParallel` 만으로는 그 차이가 표에서 사라진다.
            SW_PROFILE_SCOPE( "RT.ExecutePacket" );
            _pFrameRenderer->executePacket( _pDevice, packet );
        }

        // 에디터가 게임뷰 텍스처를 샘플링한다. 읽기 상태로 바꾼다(열려 있는 렌더 패스도 여기서 닫힌다).
        if ( bOffscreen )
            pFrameStream->prepareTextureForShaderRead( packet._gameRenderTarget );

        // UI(presentHook)는 백버퍼에 그린다. 그래프가 오프스크린/백버퍼 어디에 그렸든, 여기서 타깃을
        // 명시적으로 백버퍼로 되돌린다. 그래프가 백버퍼에 그린 경우도 있으므로 Load 여야 한다.
        if ( pFrameStream != nullptr )
        {
            RHIRenderPassBeginInfo backbufferPass{};
            backbufferPass._bBindColor        = SW_TRUE;
            backbufferPass._colorTargetCount  = 1;
            backbufferPass._arrColorTarget[0] = 0; // 0 = 백버퍼
            // 오프스크린 경로에서는 그래프가 게임 RT 에만 그렸으므로 백버퍼는 아직 아무도 안 건드렸다
            // → 여기서 클리어한다. 백버퍼 경로에서는 그래프가 이미 그렸으므로 보존해야 한다.
            backbufferPass._arrLoadOp[0]     = bOffscreen ? RHIRenderPassLoadOp::Clear : RHIRenderPassLoadOp::Load;
            backbufferPass._arrClearColor[0] = packet._clearColor;
            pFrameStream->beginRenderPass( backbufferPass );
        }

        if ( _presentHook.isBound() )
        {
            // 에디터 UI 가 이 훅으로 백버퍼에 그린다. 에디터를 켜면 이게 프레임의 큰 몫이다.
            SW_PROFILE_SCOPE( "RT.PresentHook" );
            _presentHook( *_pDevice, packet );
        }

        {
            // 제출과 Present. GPU 가 밀리면 여기서 기다린다.
            SW_PROFILE_SCOPE( "RT.Present" );
            // VSync 는 **디바이스가 채택한 값**이다. 여기 true 를 못박아 두면 EngineConfig 의
            // `_window._bVSync` 와 CLI `-vsync` 가 둘 다 무시되고 프레임이 모니터 주사율에 붙는다.
            _pDevice->endFrame( _pDevice->isVSyncEnabled() );
        }
        // 외부 프로파일러(Tracy)의 렌더 스레드 프레임 경계. 주 프레임(게임 스레드)과 박자가 달라 이름을 따로 둔다.
        IProfilerBackend* pProfilerBackend = ProfilerBackend::getActiveBackend();
        if ( pProfilerBackend != nullptr )
            pProfilerBackend->markFrame( "Render" );

        {
            // 드라이버의 GPU 메모리 사용량 · 예산을 장부에 적는다. 첫 프레임에도 묻는다 — 보고가 "모름" 으로 시작하지 않게.
            // 프레임을 기록하는 이 스레드에서 묻는다(GL 은 컨텍스트를 쥐고 묻는다 — 다른 스레드면 이 프레임을 기다린다).
            const bool bRefreshBudget = ( _budgetFrameCounter % _s_kBudgetRefreshFrames ) == 0;
            ++_budgetFrameCounter;
            if ( bRefreshBudget )
            {
                SW_PROFILE_SCOPE( "RT.RefreshGpuMemoryBudget" );
                _pDevice->refreshGpuMemoryBudget();
            }
        }

        // -gv_screenshot=<path> : 한 장(또는 -gv_screenshotCount 장)을 찍는다.
        //  - **endFrame 뒤여야 한다.** 그 전에는 커맨드 리스트가 기록만 됐고 아직 큐에 나가지 않아,
        //    읽어 보면 클리어 색만 나온다.
        //  - **몇 프레임 기다려야 한다.** 첫 프레임에는 GpuScene 업로드가 아직이라 그릴 게 없다.
        if ( gv_screenshot.empty() == false && _bScreenshotTaken == SW_FALSE && _pFrameRenderer != nullptr )
        {
            // 최소 몇 프레임은 기다려야 한다. 첫 프레임에는 GpuScene 업로드가 아직이라 그릴 것이 없다.
            // 그 위로는 -gv_screenshotFrame 이 정한다(시간에 따라 움직이는 장면을 비교할 때 필요하다).
            constexpr uint32 kScreenshotMinWarmupFrames = 10;
            const uint32     targetFrame                = ( gv_screenshotFrame > static_cast<int32>( kScreenshotMinWarmupFrames ) )
                                                            ? static_cast<uint32>( gv_screenshotFrame )
                                                            : kScreenshotMinWarmupFrames;
            const uint32     shotCount                  = gv_screenshotCount > 1 ? static_cast<uint32>( gv_screenshotCount ) : 1u;
            const uint32     interval                   = gv_screenshotInterval > 1 ? static_cast<uint32>( gv_screenshotInterval ) : 1u;
            const uint32     frame                      = ++_screenshotFrameCounter;
            const bool       bShotDue                   = frame >= targetFrame && ( frame - targetFrame ) % interval == 0u;
            if ( bShotDue )
            {
                // 연속 촬영이면 장 번호를 확장자 앞에 붙인다.
                string path{ gv_screenshot };
                if ( shotCount > 1u )
                {
                    const size_t                          dot = path.find_last_of( '.' );
                    StringBuilder<constant::kMaxBuffer16> suffix;
                    suffix.appendFormat( "_%03u", _screenshotShotCount );
                    path.insert( dot == string::npos ? path.size() : dot, suffix.c_str() );
                }
                ++_screenshotShotCount;
                _bScreenshotTaken = _screenshotShotCount >= shotCount ? SW_TRUE : SW_FALSE;
                // 기본은 **Present 결과 캡처**, 곧 화면에 나간 그림이다. 캡처가 없으면 Present 가 읽는 첨부로 물러난다.
                // 첨부 이름을 리터럴로 박지 말 것 — 그 이름이 없는 파이프라인(디퍼드)에서는 한 장도 안 찍힌다.
                const string_view attachment{ gv_screenshotAttachment };
                if ( attachment.empty() == false )
                {
                    // 중간 단계를 보고 싶다고 이름을 찍어 준 경우. 그 첨부를 그대로 덤프한다.
                    _pFrameRenderer->dumpTransientToPpm( attachment, path );
                }
                else if ( _pFrameRenderer->dumpPresentCaptureToPpm( path ) == false )
                {
                    // 캡처가 없으면(오프스크린 출력 등) Present 가 **읽는** 첨부를 찍는다.
                    // 그 그림에는 Present 패스가 한 일(톤맵 등)이 들어 있지 않다.
                    string_view fallback = _pFrameRenderer->getPresentedAttachmentName();
                    if ( fallback.empty() )
                        fallback = string_view{ FrameRendererUtil::Attachment::kSceneColor };
                    _pFrameRenderer->dumpTransientToPpm( fallback, path );
                }
            }
        }

        return true;
    }

    bool RenderThread::ensureContextOnCurrentThread()
    {
        if ( _pDevice == nullptr )
            return false;
        if ( _bContextBound.load( std::memory_order_relaxed ) )
            return true;

        // 실패 로그는 디바이스가 남긴다(쥔 스레드 · 시간까지) — 같은 실패를 두 줄로 남기지 않는다.
        if ( _pDevice->bindGraphicsContext() == false )
            return false;
        _bContextBound = true;
        return true;
    }
} // namespace sw

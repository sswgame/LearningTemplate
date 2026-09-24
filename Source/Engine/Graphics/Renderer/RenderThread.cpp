#include "pch.h"

#include "Engine/Graphics/Renderer/RenderThread.h"

#include "Core/Concurrency/mutex.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/RHI/IRHICommandContext.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/Renderer/Frame/FrameRenderer.h"
#include "Engine/Graphics/Renderer/Frame/FrameRendererUtil.h"
#include "Engine/Utility/Debug/FrameProfiler.h"

namespace sw
{
    SW_LOG_CALLER( "RenderThread" );

    /**
     * @brief `-gv_screenshot=<파일경로>` 입니다. 화면에 나간 그림(Present 결과)을 PPM 으로 한 장 덤프합니다(백엔드별 시각 검증용).
     * @details Win32 PrintWindow 캡처는 DX11 · GL · Vulkan 에서 빈 화면이 자주 나옵니다. 스왑체인이 GDI 로
     *          합성되지 않기 때문입니다. 그래서 GPU 에서 직접 읽습니다(readbackTexture2D).
     *          PPM 은 인코더가 필요 없어 의존성이 늘지 않습니다. `-gv_profileFrames` 와 같이 쓰면 찍고 종료합니다.
     * @note 셋 모두 이 파일이 유일한 소비자입니다. 예전에는 선언이 `EngineLoop.cpp` 에 있고 여기서 `extern` 으로
     *       끌어 썼습니다. 타입이 어긋나도 링커까지 가야 걸리는 형태라 쓰는 자리로 내렸습니다.
     */
    SW_GLOBAL_VARIABLE_STRING( gv_screenshot, "", "화면에 나간 그림(Present 결과)을 PPM 으로 덤프할 경로 (비면 사용 안 함)" );

    /** @brief `-gv_screenshotAttachment=<이름>` 입니다. 덤프할 트랜지언트 첨부 이름이며, 비면 Present 캡처(없으면 Present 가 읽는 첨부)를 찍습니다. */
    SW_GLOBAL_VARIABLE_STRING( gv_screenshotAttachment, "", "Present 결과 대신 덤프할 트랜지언트 이름 (비면 Present 결과)" );

    /**
     * @brief `-gv_screenshotFrame=<N>` 입니다. 몇 번째 프레임에서 찍을지 정합니다(기본 10, 10 보다 작으면 10).
     * @details 시간에 따라 움직이는 것(GPU 인스턴스 회전 등)을 검증하려면 **서로 다른 시각**의 장면이
     *          필요합니다. 예전에는 워밍업 10 프레임이 고정이라 `-gv_profileFrames` 를 아무리 늘려도 늘 같은
     *          시각이 찍혔고, 그것을 모르고 비교하면 "움직이지 않는다" 는 잘못된 결론이 나옵니다.
     */
    SW_GLOBAL_VARIABLE_INT( gv_screenshotFrame, 10, "스크린샷을 찍을 프레임 번호 (기본 10)" );

    // 커맨드 리스트를 프레임 끝에 모아 한 번에 제출할지(기본), 잘릴 때마다 바로 제출할지. 이 파일이 프레임마다 디바이스로 밀어 넣는다.
    // 두 모드 모두 기록 순서 = 실행 순서다. 즉시 모드도 [세그먼트][리스트] 순서를 지켜 제출하고
    // 제출 '시점'만 달라진다. 즉시 모드는 제출 횟수가 늘어 오버헤드가 크지만, GPU 오류(DEVICE_HUNG,
    // 검증 레이어)가 어느 제출에서 났는지 좁히기 쉬워 디버깅에 쓴다.
    SW_GLOBAL_VARIABLE_BOOL( gv_rhiImmediateSubmit, false,
                             "RHI 커맨드 리스트를 프레임 끝에 모아 제출하지 않고 즉시 제출 (디버깅용, 오버헤드 큼)" );

    // 전용 렌더 스레드를 쓸지(false 면 게임 스레드가 바로 제출한다). 읽는 곳은 이 파일뿐이다(`attach` · `submit`).
    SW_GLOBAL_VARIABLE_BOOL( gv_useRenderThread, true, "전용 RenderThread 사용 (false = 게임 스레드 인라인 submit)" );

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
            std::swap( _arrRingBuffer[currentHead], packet );
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
        // 게임 스레드(워커 없음) 경로: 이 프레임 동안 이 스레드가 그래픽스 컨텍스트를 갖는다.
        ensureContextOnCurrentThread();
        executePacket( packet );
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

            // 링 자리에서 그대로 처리한다. 옮겨 오면 링 자리의 저장소가 비어 GT 가 다음에 다시 할당한다. 생산자는
            // tail 이 앞으로 갈 때까지 이 자리를 덮어쓰지 않는다.
            executePacket( _arrRingBuffer[currentTail] );

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
        // 예전에는 여기에 "RT.Frame 에서 RT.Present 를 빼면 기록 시간" 이라고 적혀 있었는데 **틀렸다.**
        // GPU 대기의 대부분은 Present 가 아니라 `beginFrame` 에 있고(이번 프레임 얼로케이터가 풀릴 때까지
        // 펜스를 기다린다), 그 시간이 스코프 없이 `RT.Frame` 에만 잡혀서 통째로 "기록" 으로 오인됐다.
        //
        // 2026-09-20 실측(Release · DX12 · 벤치 큐브 2000 · 600프레임):
        //   RT.Frame 1021us = BeginFrame 516 + ExecutePacket 393 + Present 110 (합이 맞는다)
        // 큐브를 200 개로 줄여도 BeginFrame 은 455us 로 거의 안 줄었다. 이 대기는 **씬 복잡도가
        // 아니라 GPU · 프레임 페이싱**이 정한다. 기록 경로를 CPU 에서 깎아도 프레임은 안 줄어든다.
        SW_PROFILE_SCOPE( "RT.Frame" );

        ensureContextOnCurrentThread();

        const bool          bOffscreen   = packet._gameRenderTarget != 0;
        IRHICommandContext* pFrameStream = _pDevice->getFrameStreamContext();
        if ( bOffscreen && pFrameStream == nullptr )
        {
            SW_LOG_ERROR( "getFrameStreamContext() is null; skipping offscreen packet" );
            return false;
        }

        // 프레임 수명주기는 경로와 무관하게 항상 여기서 한 번 연다. 예전에는 오프스크린 경로에서만
        // beginFrame 을 그래프 뒤로 미뤄 뒀는데, 그것은 "백버퍼를 바인딩할 다른 수단이 없어서" 위치로
        // 대신하던 것이었다(docs/05_RHI_FrameContract.md 의 R2). 그 역할은 아래 명시적 백버퍼
        // 렌더 패스가 맡는다. 이 둘은 반드시 같이 있어야 한다.
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
            // 얼로케이터 · 프레임 리소스가 풀릴 때까지 펜스를 기다린다. 스코프가 없을 때는 이 시간이
            // `RT.Frame` 에만 잡혀 어디로 갔는지 표에 안 보였다. "RT.Frame 에서 RT.Present 를 빼면
            // 기록 시간" 으로 읽던 차이의 대부분이 실은 이 대기였다.
            SW_PROFILE_SCOPE( "RT.BeginFrame" );
            _pDevice->beginFrame( packet._clearColor );
        }

        // 게임뷰 렌더 타깃을 잡는다. 예전에는 beginOffscreenPass 였는데, 그것은 "렌더 타깃 바인딩" 과 "백엔드마다
        // 다른 스트림 분리" 가 섞인 API 였다(Vulkan 만 별도 커맨드 버퍼 + 블로킹 제출).
        // 렌더 타깃 바인딩은 beginRenderPass 로 충분하다(docs/05_RHI_FrameContract.md S3).
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
            // 에디터 UI 가 이 훅으로 백버퍼에 그린다. 에디터를 켜면 이게 프레임의 큰 몫인데
            // 스코프가 없어서 `RT.Frame` 안에 통째로 묻혀 있었다.
            SW_PROFILE_SCOPE( "RT.PresentHook" );
            _presentHook( *_pDevice, packet );
        }

        {
            // 제출과 Present. GPU 가 밀리면 여기서 기다린다.
            SW_PROFILE_SCOPE( "RT.Present" );
            // VSync 는 **디바이스가 채택한 값**이다. 여기 true 를 못박아 두면 EngineConfig 의
            // `_window._bVSync` 와 CLI `--VSYNC` 가 둘 다 죽는다. 실제로 죽어 있었고,
            // `_bVSync: false` 설정으로도 프레임이 모니터 주사율에 정확히 붙어 있었다.
            _pDevice->endFrame( _pDevice->isVSyncEnabled() );
        }

        // -gv_screenshot=<path> : 한 장만 찍는다.
        //  - **endFrame 뒤여야 한다.** 그 전에는 커맨드 리스트가 기록만 됐고 아직 큐에 나가지 않아,
        //    읽어 보면 클리어 색만 나온다.
        //  - **몇 프레임 기다려야 한다.** 첫 프레임에는 GpuScene 업로드가 아직이라 그릴 게 없다
        //    (그래서 처음엔 네 백엔드 중 하나만 지오메트리가 보였다).
        if ( gv_screenshot.empty() == false && _bScreenshotTaken == SW_FALSE && _pFrameRenderer != nullptr )
        {
            // 최소 몇 프레임은 기다려야 한다. 첫 프레임에는 GpuScene 업로드가 아직이라 그릴 것이 없다.
            // 그 위로는 -gv_screenshotFrame 이 정한다(시간에 따라 움직이는 장면을 비교할 때 필요하다).
            constexpr uint32 kScreenshotMinWarmupFrames = 10;
            const uint32     targetFrame                = ( gv_screenshotFrame > static_cast<int32>( kScreenshotMinWarmupFrames ) )
                                                            ? static_cast<uint32>( gv_screenshotFrame )
                                                            : kScreenshotMinWarmupFrames;
            if ( ++_screenshotFrameCounter >= targetFrame )
            {
                _bScreenshotTaken = SW_TRUE;
                // 기본은 **Present 결과 캡처**, 곧 화면에 나간 그림이다. 캡처가 없으면 Present 가 읽는 첨부로 물러난다.
                // 예전에는 `"SceneColor"` 리터럴이라 그 이름이 없는 파이프라인(디퍼드)에서는 한 장도 안 찍혔다.
                const string_view attachment{ gv_screenshotAttachment };
                if ( attachment.empty() == false )
                {
                    // 중간 단계를 보고 싶다고 이름을 찍어 준 경우. 그 첨부를 그대로 덤프한다.
                    _pFrameRenderer->dumpTransientToPpm( attachment, gv_screenshot );
                }
                else if ( _pFrameRenderer->dumpPresentCaptureToPpm( gv_screenshot ) == false )
                {
                    // 캡처가 없으면(오프스크린 출력 등) 예전 방식대로 Present 가 **읽는** 첨부를 찍는다.
                    // 그 그림에는 Present 패스가 한 일(톤맵 등)이 들어 있지 않다.
                    string_view fallback = _pFrameRenderer->getPresentedAttachmentName();
                    if ( fallback.empty() )
                        fallback = string_view{ FrameRendererUtil::Attachment::kSceneColor };
                    _pFrameRenderer->dumpTransientToPpm( fallback, gv_screenshot );
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

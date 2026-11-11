#include "pch.h"

#include "Engine/Renderer/Capture/SceneCapture.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/RHIDescriptors.h"
#include "Engine/Graphics/RHI/Transfer/RHITransferUtil.h"
#include "Engine/Graphics/Texture/Texture2D.h"
#include "Engine/Renderer/Capture/CaptureReadiness.h"
#include "Engine/Renderer/Frame/FrameRenderer.h"
#include "Engine/Renderer/Frame/OffscreenSceneRenderers.h"
#include "Engine/Renderer/RenderThread.h"
#include "Engine/Scene/Scene.h"

namespace sw
{
    namespace
    {
        struct SceneCaptureInternal
        {
            /**
             * @brief 진척 없이 다 그려진 장을 기다리는 시한(초)입니다 — 머티리얼 PSO 를 만드는 중인 장(셰이더 컴파일 중)마다 다시 센다.
             * @details 벽시계로만 재면 빈 셰이더 캐시의 첫 실행(GL · DX11 은 변형마다 수 초)이 일하는 중에 실패했다. 언리얼 썸네일 · 초상화도 셰이더 컴파일을 끝까지 기다린 뒤 찍는다.
             *          컴파일은 끝나거나 실패로 끝난다(실패한 변형은 '만드는 중' 이 아니다) — 이 시한은 씬 쪽 일(에셋)이 멈춘 경우를 잡는다.
             */
            static constexpr int64 kReadyWaitSeconds = 30;
        };
    } // namespace

    SceneCapture::SceneCapture()
        : _pRenderThread{ nullptr }
        , _pipelinePath{}
        , _outputTarget{ nullptr }
        , _scene{}
        , _job{}
        , _sceneKey{ 0 }
    {
    }

    SceneCapture::~SceneCapture()
    {
        shutdown();
    }

    bool SceneCapture::initialize( RenderThread& renderThread, string_view pipelinePath )
    {
        shutdown();
        if ( renderThread.getDevice() == nullptr || renderThread.getFrameRenderer() == nullptr || renderThread.getOffscreenRenderers() == nullptr )
        {
            SW_LOG_ERROR_ID( "SceneCapture.RenderThreadNoDevice", "The render thread has no device, renderer or off-screen renderers - nothing can be captured" );
            return false;
        }
        _pRenderThread = &renderThread;
        _scene.initialize( *renderThread.getDevice() ); // 배치 합치기는 주 화면 · 미리보기와 같은 규칙(디바이스가 정한다)
        _pipelinePath = string( pipelinePath );
        _sceneKey     = renderThread.getOffscreenRenderers()->allocateCaptureSceneKey(); // 키의 주인은 키로 렌더러를 드는 화면 밖 렌더러 표다
        return true;
    }

    void SceneCapture::shutdown()
    {
        // 출력 타깃의 GPU 자원은 렌더 스레드가 놓는다. 명령을 실행하지 못했으면(렌더 스레드가 멈췄다) 디바이스 통보가 자원을 거둔다.
        if ( _pRenderThread != nullptr && _outputTarget != nullptr )
            (void)_pRenderThread->executeAndWait( SW_DELEGATE_METHOD( RenderCommandDelegate, &SceneCapture::releaseOnRenderThread, this ) );
        _outputTarget.reset();
        _pRenderThread = nullptr;
    }

    IRHIDevice* SceneCapture::getDevice() const
    {
        return _pRenderThread != nullptr ? _pRenderThread->getDevice() : nullptr;
    }

    bool SceneCapture::capture( Scene& scene, const SceneCaptureRequest& request, SceneCaptureResult& outResult )
    {
        outResult = SceneCaptureResult{};
        if ( _pRenderThread == nullptr )
            return false;
        _job._request           = request;
        _job._bSucceeded        = true;
        _job._bRead             = false;
        _job._pendingPSOBatches = 0;
        _job._pResult           = &outResult;
        int64 deadlineMicro     = MonotonicClock::nowMicroseconds() + SceneCaptureInternal::kReadyWaitSeconds * constant::time::kMicrosecondsPerSecond;
        while ( _job._bSucceeded && _job._bRead == false )
        {
            if ( MonotonicClock::nowMicroseconds() > deadlineMicro )
            {
                SW_LOG_ERROR_ID( "SceneCapture.CaptureNeverDrewComplete", "The capture never drew a complete frame within %# s (scene work %#, or batches waiting for their material PSO) - nothing was read ('%#')",
                                 SceneCaptureInternal::kReadyWaitSeconds, CaptureReadiness::countPendingSceneJobs( scene.getObjectManager() ), _pipelinePath.c_str() );
                _job._bSucceeded = false;
                break;
            }
            _job._packet.resetForFrame();
            _scene.fillSceneView( _job._packet, scene, request._view, nullptr );
            _job._packet._sceneKey = _sceneKey;
            _job._bReadNow         = CaptureReadiness::countPendingSceneJobs( scene.getObjectManager() ) == 0;
            const bool bRan        = _pRenderThread->executeAndWait( SW_DELEGATE_METHOD( RenderCommandDelegate, &SceneCapture::renderFrameOnRenderThread, this ) );
            _job._bSucceeded       = bRan && _job._bSucceeded;
            if ( _job._pendingPSOBatches > 0 ) // 셰이더를 컴파일하는 중이다 — 진척이 있으니 시한을 다시 센다
                deadlineMicro = MonotonicClock::nowMicroseconds() + SceneCaptureInternal::kReadyWaitSeconds * constant::time::kMicrosecondsPerSecond;
        }
        _job._pResult = nullptr;
        _scene.shutdown(); // 씬의 소유(메시 · 머티리얼)를 여기서 놓는다 — 씬보다 먼저 사라지는 쪽이 놓아야 한다
        return _job._bSucceeded;
    }

    void SceneCapture::releaseOnRenderThread( IRHIDevice& device )
    {
        if ( OffscreenSceneRenderers* const pOffscreen = _pRenderThread->getOffscreenRenderers() )
            pOffscreen->retire( _sceneKey ); // 캡처가 끝났다 — 이 캡처의 씬 렌더러를 내린다(씬 키는 캡처마다 새것이다)
        _outputTarget->releaseRHI( &device );
        _outputTarget.reset();
    }

    void SceneCapture::renderFrameOnRenderThread( IRHIDevice& device )
    {
        const SceneCaptureRequest& request = _job._request;
        if ( ensureOutputTarget( device, request._view._width, request._view._height ) == false )
        {
            _job._bSucceeded = false;
            return;
        }
        _job._packet._outputRenderTarget = _outputTarget->getHandle();
        device.beginFrame();
        OffscreenSceneRenderers* const pOffscreen = _pRenderThread->getOffscreenRenderers();
        FrameRenderer* const           pRenderer  = pOffscreen != nullptr ? pOffscreen->execute( *_pRenderThread->getFrameRenderer(), &device, _job._packet, request._clearColor, _pipelinePath ) : nullptr;
        device.endFrame( false, false ); // GPU 를 기다리지 않는다 — 읽기 전송이 앞서 제출한 이 프레임 뒤에 실행된다(전송 계약). 다 그려지지 않은 장은 읽지 않으니 기다릴 까닭이 없다
        if ( pRenderer == nullptr )
        {
            SW_LOG_ERROR_ID( "SceneCapture.CaptureCouldNotDraw", "The capture could not draw the frame ('%#')", _pipelinePath.c_str() );
            _job._bSucceeded = false;
            return;
        }
        _job._pendingPSOBatches = pRenderer->getLastPendingPSOBatchCount();
        // 다 그려진 장만 읽는다 — 씬 쪽 남은 일(게임 스레드가 보고 넘겼다)과 이 렌더러가 빠뜨린 배치(`CaptureReadiness`). 아니면 다음 장을 그린다(`capture`).
        if ( _job._bReadNow == false || CaptureReadiness::isFrameComplete( *pRenderer ) == false )
            return;
        _job._bRead                = true;
        SceneCaptureResult& result = *_job._pResult;
        if ( request._attachmentName.empty() )
        {
            result._format   = constant::render::kOffscreenColorFormat;
            _job._bSucceeded = RHITransferUtil::readbackTexture2DAndWait( *device.getResourceFactory(), _outputTarget->getHandle(), 0, 0, result._bytes, result._layout );
        }
        else
        {
            _job._bSucceeded = pRenderer->getCapture().readbackTransient( request._attachmentName, result._bytes, result._layout, result._format );
        }
        if ( _job._bSucceeded == false )
            SW_LOG_ERROR_ID( "SceneCapture.CaptureCouldNotRead", "The capture could not read back '%#'", request._attachmentName.empty() ? "output" : request._attachmentName.c_str() );
    }

    bool SceneCapture::ensureOutputTarget( IRHIDevice& device, uint32 width, uint32 height )
    {
        const bool bSameSize = _outputTarget != nullptr && _outputTarget->getWidth() == width && _outputTarget->getHeight() == height;
        if ( bSameSize )
            return true;
        if ( _outputTarget != nullptr )
            _outputTarget->releaseRHI( &device ); // 크기가 바뀌었다 — 옛 타깃은 디바이스가 그 프레임 뒤로 미뤄 놓는다
        _outputTarget = make_unique<Texture2D>();
        if ( _outputTarget->createRenderTarget( &device, "SceneCaptureOutput", width, height, constant::render::kOffscreenColorFormat ) )
            return true;
        SW_LOG_ERROR_ID( "SceneCapture.CaptureOutputTargetX", "The capture output target (%#x%#) could not be created", width, height );
        _outputTarget.reset();
        return false;
    }
} // namespace sw

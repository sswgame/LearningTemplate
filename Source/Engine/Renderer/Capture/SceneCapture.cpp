#include "pch.h"

#include "Engine/Renderer/Capture/SceneCapture.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/RHIDescriptors.h"
#include "Engine/Graphics/RHI/Transfer/RHITransferUtil.h"
#include "Engine/Graphics/Texture/Texture2D.h"
#include "Engine/Renderer/Frame/FrameRenderer.h"
#include "Engine/Renderer/RenderThread.h"
#include "Engine/Renderer/Scene/GPUSceneBuilder.h"

namespace sw
{
    SW_LOG_CALLER( "SceneCapture" );

    namespace
    {
        struct SceneCaptureInternal
        {
            /** @brief 캡처 씬 키의 표시 비트입니다. 미리보기 씬 키(`SceneManager::getPreviewSceneKey` — 1 부터 센다)와 겹치지 않는 공간이다. */
            static constexpr uint32 kCaptureSceneKeyBit = 0x80000000u;

            /** @brief 캡처마다 새 씬 키를 냅니다(하위 렌더러를 캡처끼리 나누지 않는다 — 씬이 다르다). */
            static uint32 allocateSceneKey()
            {
                static atomic<uint32> s_nextKey{ 1 };
                return kCaptureSceneKeyBit | ( s_nextKey.fetch_add( 1, std::memory_order_relaxed ) & ~kCaptureSceneKeyBit );
            }

            /** @brief 요청 장 수를 그린 뒤 PSO 가 다 되기를 기다리는 시한(초)입니다. 첫 실행의 셰이더 캐시 없는 빌드(DX11 머티리얼 변형 하나에 수백 ms)보다 넉넉하다. */
            static constexpr int64 kPSOWaitSeconds = 30;
        };
    } // namespace

    SceneCapture::SceneCapture()
        : _pRenderThread{ nullptr }
        , _pipelinePath{}
        , _outputTarget{ nullptr }
        , _fill{}
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
        if ( renderThread.getDevice() == nullptr || renderThread.getFrameRenderer() == nullptr )
        {
            SW_LOG_ERROR( "The render thread has no device or renderer - nothing can be captured" );
            return false;
        }
        _pRenderThread = &renderThread;
        _pipelinePath  = string( pipelinePath );
        _sceneKey      = SceneCaptureInternal::allocateSceneKey();
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
        if ( _pRenderThread == nullptr || request._frameCount == 0 )
            return false;
        // 빌더는 이 캡처의 것이다(빌더의 재구축 판단 캐시가 씬 · 시점별이다). 배치 합치기는 주 · 미리보기 빌더와 같은 규칙(디바이스가 정한다).
        GPUSceneBuilder builder;
        builder.setMergeBatchesAcrossMaterials( getDevice()->supportsNativeBindlessSampling() );
        _job._request           = request;
        _job._bSucceeded        = true;
        _job._bRead             = false;
        int64 waitDeadlineMicro = 0;
        for ( uint32 frameIndex = 0; _job._bSucceeded && _job._bRead == false; ++frameIndex )
        {
            const bool bReadFrame = frameIndex + 1 >= request._frameCount;
            if ( bReadFrame && waitDeadlineMicro == 0 )
                waitDeadlineMicro = MonotonicClock::nowMicroseconds() + SceneCaptureInternal::kPSOWaitSeconds * constant::time::kMicrosecondsPerSecond;
            else if ( bReadFrame && MonotonicClock::nowMicroseconds() > waitDeadlineMicro )
            {
                SW_LOG_ERROR( "The capture still had batches waiting for their material PSO after %# s - the frame was not read ('%#')", SceneCaptureInternal::kPSOWaitSeconds,
                              _pipelinePath.c_str() );
                _job._bSucceeded = false;
                break;
            }
            _job._packet.resetForFrame();
            _fill.fill( _job._packet, scene, request._view, builder, nullptr );
            _job._packet._sceneKey = _sceneKey;
            _job._pResult          = bReadFrame ? &outResult : nullptr;
            const bool bRan        = _pRenderThread->executeAndWait( SW_DELEGATE_METHOD( RenderCommandDelegate, &SceneCapture::renderFrameOnRenderThread, this ) );
            _job._bSucceeded       = bRan && _job._bSucceeded;
        }
        _job._pResult = nullptr;
        builder.clear(); // 씬의 소유(메시 · 머티리얼)를 여기서 놓는다 — 씬보다 먼저 사라지는 쪽이 놓아야 한다
        return _job._bSucceeded;
    }

    void SceneCapture::releaseOnRenderThread( IRHIDevice& device )
    {
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
        FrameRenderer* const pRenderer = _pRenderThread->getFrameRenderer()->executeOffscreenPacket( &device, _job._packet, request._clearColor, _pipelinePath );
        device.endFrame( false, false );
        device.waitIdle( RHIWaitIdleReason::Capture );
        if ( pRenderer == nullptr )
        {
            SW_LOG_ERROR( "The capture could not draw the frame ('%#')", _pipelinePath.c_str() );
            _job._bSucceeded = false;
            return;
        }
        // 머티리얼 PSO 가 아직 만드는 중이라 빠진 배치가 있으면 읽지 않는다 — 다음 장을 그린다(`capture`).
        if ( _job._pResult == nullptr || pRenderer->getLastPendingPSOBatchCount() != 0 )
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
            SW_LOG_ERROR( "The capture could not read back '%#'", request._attachmentName.empty() ? "output" : request._attachmentName.c_str() );
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
        SW_LOG_ERROR( "The capture output target (%#x%#) could not be created", width, height );
        _outputTarget.reset();
        return false;
    }
} // namespace sw

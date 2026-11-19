#include "pch.h"

#include "Engine/Renderer/EngineRenderDriver.h"

#include "Core/Log/Logger.h"
#include "Core/String/StringBuilder.h"
#include "Core/Subsystem/SubsystemScope.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Config/ConfigManager.h"
#include "Engine/Config/EngineConfig.h"
#include "Engine/Foundation/DeterministicRun.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Foundation/EngineSubsystemHost.h"
#include "Engine/Foundation/EngineSubsystemRegistration.h"
#include "Engine/Foundation/QuitRequest.h"
#include "Engine/Graphics/Material/MaterialCache.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/RHI.h"
#include "Engine/Graphics/RHI/RHIBackendRegistry.h"
#include "Engine/Graphics/RHI/RHICapabilities.h"
#include "Engine/Graphics/RHI/RHIRenderResource.h"
#include "Engine/Graphics/Shader/Compile/ShaderCache.h"
#include "Engine/Graphics/Texture/TextureCache.h"
#include "Engine/Graphics/View/ScreenshotRequestQueue.h"
#include "Engine/Graphics/View/SimulationOnlyFrameQueue.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Renderer/Capture/CaptureReadiness.h"
#include "Engine/Renderer/Frame/FrameRenderer.h"
#include "Engine/Renderer/Frame/FrameRendererSubsystem.h"
#include "Engine/Renderer/Frame/OffscreenSceneRenderers.h"
#include "Engine/Renderer/Frame/Packet/RenderFramePacket.h"
#include "Engine/Renderer/Frame/Packet/RenderPacketBuilder.h"
#include "Engine/Renderer/RHISubsystem.h"
#include "Engine/Renderer/RenderThread.h"
#include "Engine/Renderer/RenderThreadSubsystem.h"
#include "Engine/Renderer/SceneRHISubsystem.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Text/FontService.h"
#include "Engine/UI/UISystem.h"
#include "Engine/Window/IWindow.h"
#include "Engine/Window/MainWindowSubsystem.h"

namespace sw
{
    namespace
    {
        struct EngineRenderDriverInternal
        {
            /** @brief 감시 값 `Display.SDRFallback` 입니다 — 마지막 화면 출력 요청이 HDR 인데 백엔드 · 모니터가 받지 않아 SDR 로 떨어졌으면 1 입니다. */
            [[nodiscard]] static bool readDisplaySDRFallback( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                EngineRenderDriverSubsystem* const pSubsystem = engine::areEngineServicesBound() ? engine::findSubsystem<EngineRenderDriverSubsystem>() : nullptr;
                if ( pSubsystem == nullptr )
                    return false;
                outValue = pSubsystem->getRenderDriver().isDisplayOutputFallenBackToSDR() ? 1.0 : 0.0;
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_WATCH( displaySDRFallback, "Display.SDRFallback", "1 when the requested HDR display output (-gv_displayOutput, display.hdr) fell back to SDR because the backend or monitor does not take it",
                         &EngineRenderDriverInternal::readDisplaySDRFallback );

    EngineRenderDriver::EngineRenderDriver( SubsystemScope& startupScope, RHISubsystem& rhiSubsystem, FrameRendererSubsystem& frameRendererSubsystem, RenderThreadSubsystem& renderThreadSubsystem )
        : _startupScope{ startupScope }
        , _rhiSubsystem{ rhiSubsystem }
        , _frameRendererSubsystem{ frameRendererSubsystem }
        , _renderThreadSubsystem{ renderThreadSubsystem }
        , _pRHI{ nullptr }
        , _pRenderThread{ nullptr }
        , _pFrameRenderer{ nullptr }
        , _pPacketBuilder{ nullptr }
        , _pSceneManager{ nullptr }
        , _pUISystem{ nullptr }
        , _pFontService{ nullptr }
        , _mainWindowResizeSubscription{}
        , _mainWindowLifecycleSubscription{}
        , _deviceLostPolicy{ RHIDeviceLostPolicy::None }
        , _deviceLostPolicyOverride{ RHIDeviceLostPolicy::None }
        , _deviceRecreateCount{ 0 }
        , _bDeviceLostHandled{ SW_FALSE }
        , _bDisplayOutputApplied{ SW_FALSE }
        , _displayRequest{}
        , _appliedDisplayOutput{}
    {
    }

    EngineRenderDriverSubsystem::EngineRenderDriverSubsystem( SubsystemScope& startupScope, RHISubsystem& rhiSubsystem, FrameRendererSubsystem& frameRendererSubsystem, RenderThreadSubsystem& renderThreadSubsystem )
        : _driver{ startupScope, rhiSubsystem, frameRendererSubsystem, renderThreadSubsystem }
    {
    }

    SubsystemHostMask EngineRenderDriverSubsystem::getSubsystemHost() const
    {
        return EngineSubsystemHost::kClient;
    }

    span<const utf8* const> EngineRenderDriverSubsystem::getSubsystemDependencies() const
    {
        static constexpr const utf8* kArrDependency[] = { RenderThreadSubsystem::kSubsystemName };
        return kArrDependency;
    }

    void EngineRenderDriver::onStartupFinished()
    {
        _pRHI           = _rhiSubsystem.findRHI();
        _pRenderThread  = _renderThreadSubsystem.findRenderThread();
        _pFrameRenderer = &_frameRendererSubsystem.getFrameRenderer();
        _pPacketBuilder = &_frameRendererSubsystem.getPacketBuilder();
        _pSceneManager  = locateSubsystem<SceneManager>( &_startupScope );
        _pUISystem      = locateSubsystem<UISystem>( &_startupScope );
        _pFontService   = locateSubsystem<FontService>( &_startupScope );
        // 렌더 스레드가 앞 패킷을 그리며 엔진 설정(`_render`)을 읽는다 — 설정을 제자리에서 덮는 길(품질 옵션 · 파일 다시 읽기)은 받은 패킷을 끝낸 뒤에 덮는다.
        if ( ConfigManager* const pConfigManager = engine::findSubsystem<ConfigManager>() )
            pConfigManager->setWriteFence( SW_DELEGATE_METHOD( ConfigWriteFenceDelegate, &EngineRenderDriver::flushSubmittedPackets, this ) );
        // 장치 분실 정책은 짐작하지 않는다 — 값이 없으면 기동을 오류로 끝낸다(폴백 없음).
        _deviceLostPolicy = engine::getSubsystem<EngineConfig>()._deviceLostPolicy;
        if ( _deviceLostPolicy == RHIDeviceLostPolicy::None )
        {
            SW_LOG_ERROR_ID( "Render.DeviceLostPolicyMissing", "EngineConfig _deviceLostPolicy is not set - add it to Config/Engine/EngineConfig.json (Quit or Recreate)" );
            engine::getSubsystem<QuitRequest>().request( ProcessExitCode::InitializationFailed );
        }
        // 스왑체인은 주 창 크기를 따른다 — RHI 가 섰으면 주 창도 있다(RHI 가 만들어 넘긴 창이거나 호스트가 먼저 넘긴 창).
        IWindow* const pMainWindow = _pRHI != nullptr ? MainWindowSubsystem::findMainWindow() : nullptr;
        if ( pMainWindow != nullptr )
        {
            _mainWindowResizeSubscription    = pMainWindow->bindResizeCallback( SW_DELEGATE_METHOD( WindowResizeDelegate, &EngineRenderDriver::onMainWindowResize, this ) );
            _mainWindowLifecycleSubscription = pMainWindow->bindLifecycleCallback( SW_DELEGATE_METHOD( WindowLifecycleDelegate, &EngineRenderDriver::onMainWindowLifecycle, this ) );
        }
    }

    void EngineRenderDriver::onShutdownBegin()
    {
        if ( ConfigManager* const pConfigManager = engine::findSubsystem<ConfigManager>() )
            pConfigManager->setWriteFence( {} ); // 렌더 스레드보다 먼저 뗀다 — 뗀 뒤의 덮기는 기다릴 렌더 스레드가 없다
        _mainWindowLifecycleSubscription.reset();
        _mainWindowResizeSubscription.reset(); // 창(MainWindow 단계가 파괴)과 RHI 보다 먼저 놓는다
    }

    RHI* EngineRenderDriver::findRHI() const
    {
        return _rhiSubsystem.findRHI();
    }

    RHIInitResult EngineRenderDriver::getRHIInitResult() const
    {
        return _rhiSubsystem.getInitResult();
    }

    void EngineRenderDriver::flushSubmittedPackets()
    {
        if ( _pRenderThread != nullptr )
            _pRenderThread->pollPackets();
    }

    void EngineRenderDriver::waitRenderThreadIdle( const RHIWaitIdleReason reason )
    {
        if ( _pRenderThread != nullptr )
            _pRenderThread->waitIdle( reason );
    }

    void EngineRenderDriver::discardViewTargets()
    {
        if ( _pRenderThread == nullptr )
            return; // 렌더 스레드가 없으면 그린 적이 없다 — 버릴 기록이 없다
        // 받은 패킷(앞 단계의 프레임)을 먼저 끝낸다 — 버린 뒤에 그것이 그려지면 새 뷰 대상에 앞 단계의 기록이 남는다.
        _pRenderThread->pollPackets();
        RenderThread* const pRenderThread = _pRenderThread;
        _pRenderThread->enqueueCommand( SW_DELEGATE_LAMBDA( RenderCommandDelegate, [pRenderThread]( IRHIDevice& )
        {
            if ( FrameRenderer* const pFrameRenderer = pRenderThread->getFrameRenderer() )
                pFrameRenderer->discardViewTargets();
            if ( OffscreenSceneRenderers* const pOffscreenRenderers = pRenderThread->getOffscreenRenderers() )
                pOffscreenRenderers->discardViewTargets();
        } ) );
    }

    void EngineRenderDriver::setScenarioCaptureEnabled( const bool bEnabled )
    {
        if ( _pRenderThread != nullptr )
            _pRenderThread->setScenarioCaptureEnabled( bEnabled );
    }

    void EngineRenderDriver::setPresentInterval( const uint32 interval )
    {
        if ( _pRenderThread != nullptr )
            _pRenderThread->setPresentInterval( interval );
    }

    uint32 EngineRenderDriver::countPendingCaptureJobs( const GameObjectManager* pManager ) const
    {
        return CaptureReadiness::countPendingJobs( pManager );
    }

    void EngineRenderDriver::overrideDeviceLostPolicy( const RHIDeviceLostPolicy policy )
    {
        SW_ASSERT( policy != RHIDeviceLostPolicy::None );
        SW_LOG_INFO_ID( "Render.DeviceLostPolicyOverride", "Device-lost policy overridden to %# (EngineConfig _deviceLostPolicy is ignored)", static_cast<int32>( policy ) );
        _deviceLostPolicyOverride = policy;
    }

    optional<RHIDeviceLostPolicy> EngineRenderDriver::decideDeviceLostResponse( const IRHIDevice& device, const RHIBackend backend, const RHIDeviceLostPolicy policy )
    {
        if ( device.isDeviceLost() == false )
            return {};
        // 다시 만드는 길(모듈 내리기 → 장치 → 모듈 세우기)은 호스트의 모듈 관리자 하나다(`EditorModuleManager::applyPendingBackendChange`, 개발 · 배포 공통).
        if ( policy == RHIDeviceLostPolicy::Recreate && RHI::canRecreateAfterDeviceLost( backend ) )
            return RHIDeviceLostPolicy::Recreate;
        return RHIDeviceLostPolicy::Quit; // `None` 이면 기동이 이미 InitializationFailed 로 끝내기를 요청했다(`onStartupFinished`) — 처음 요청의 코드가 이긴다
    }

    bool EngineRenderDriver::rebindSceneAfterDeviceRecreate( string* pOutTiming )
    {
        if ( _pRHI == nullptr || _pRHI->hasDevice() == false )
            return false;

        // 새 디바이스가 섰다. 레지스트리 전체에 "다시 올려라" 를 알린다. 어떤 캐시를 빠뜨렸는지
        // 기억할 필요가 없는 것이 이 구조의 요점이다(언리얼 FRenderResource::InitRHI 와 같은 자리).
        const Stopwatch rebindWatch;
        // 머티리얼을 다시 올리면 텍스처를 요청한다 — 캐시가 새 디바이스를 먼저 알아야 한다(SceneRHI 단계는 아래 `restartStopped` 에서야 다시 선다).
        SceneRHISubsystem::bindAssetCaches( &_pRHI->getDevice() );
        RHIRenderResource::initAllForDevice( &_pRHI->getDevice() );
        const uint64 initMicroseconds = rebindWatch.getElapsedMicroseconds();

        // 내렸던 단계를 기동과 같은 본문으로 다시 세운다: 렌더러(업로드 큐 · 배치 합치기 설정을 새 디바이스로) → 렌더 스레드 → 라이브 셰이더 →
        // 씬의 디바이스. 본문은 이미 있는 객체를 다시 쓰므로 렌더 스레드에 걸린 훅과 그것을 가리키는 포인터가 그대로다.
        const bool bRestarted = _startupScope.getCollection().restartStopped();
        if ( pOutTiming != nullptr )
        {
            StringBuilder<constant::buffer::kMax256> outTimingBuilder;
            outTimingBuilder.appendFormat( "re-init resources %#ms, restart stages %#ms", initMicroseconds / 1000, ( rebindWatch.getElapsedMicroseconds() - initMicroseconds ) / 1000 );
            *pOutTiming = string( outTimingBuilder.view() );
        }
        if ( bRestarted == false )
            SW_LOG_ERROR_ID( "Engine.BackendChange", "Backend change: %#", _startupScope.getCollection().getError().c_str() );

        Scene* pScene = _pSceneManager != nullptr ? _pSceneManager->getActiveScene() : nullptr;
        if ( pScene != nullptr )
            pScene->pickActiveGameCamera();
        return bRestarted;
    }

    void EngineRenderDriver::onMainWindowLifecycle( const WindowLifecycleEvent event )
    {
        if ( event == WindowLifecycleEvent::SurfaceLost || event == WindowLifecycleEvent::AppSuspended )
            flushSubmittedPackets();
        // 표면은 이 통보가 돌아오면 사라진다(Android) — 스왑체인 · 표면을 지금 놓고, 돌아오면 새 창으로 다시 짓는다. 멈춘 동안은 프레임을 돌지 않는다(`IWindow::isRenderPaused`).
        const bool bHasDevice = _pRHI != nullptr && _pRHI->hasDevice();
        if ( bHasDevice && event == WindowLifecycleEvent::SurfaceLost )
        {
            if ( _pRenderThread != nullptr )
                _pRenderThread->waitIdle( RHIWaitIdleReason::WindowSwapChain );
            _pRHI->getDevice().releaseSurface();
        }
        if ( bHasDevice && event == WindowLifecycleEvent::SurfaceRestored && _pRHI->getDevice().restoreSurface() == false )
            SW_LOG_ERROR_ID( "Render.SurfaceRestoreFailed", "The native surface came back but the RHI could not rebuild its swap chain - the screen stays black" );
    }

    DisplayOutputDesc EngineRenderDriver::applyDisplayOutput( const DisplayOutputDesc& requested )
    {
        if ( _pRHI == nullptr || _pRHI->hasDevice() == false )
            return requested;
        if ( _bDisplayOutputApplied != SW_FALSE && requested.isSameOutput( _displayRequest ) )
            return _appliedDisplayOutput;
        IRHIDevice& device = _pRHI->getDevice();
        // SDR 요청에 SDR 장치면 스왑체인을 건드리지 않는다(기다릴 것도 능력을 물을 것도 없다).
        const bool bTouchesSwapChain = requested._kind != DisplayOutputKind::SDR || device.getBackBufferColorSpace() != RHIColorSpace::SRGBNonLinear;
        // 렌더 스레드를 먼저 세운다 — 능력(모니터 · 표면 포맷)은 렌더 스레드의 스왑체인 재생성이 고쳐 쓰므로 그 뒤에 읽는다(창 크기 바뀜과 같은 길).
        if ( bTouchesSwapChain && _pRenderThread != nullptr )
            _pRenderThread->waitIdle( RHIWaitIdleReason::DisplayOutput );
        const uint8       supportedMask = bTouchesSwapChain ? device.getCapabilities()._displayOutputSupport : makeColorSpaceBit( RHIColorSpace::SRGBNonLinear );
        DisplayOutputDesc resolved      = DisplayOutputDesc::resolve( requested, supportedMask );
        // 같은 색 공간이면 장치가 HDR10 마스터링 값만 다시 알린다(스왑체인을 다시 만들지 않는다). 실패면 장치가 실제로 내는 색 공간을 따른다 — 렌더러 · 감시 값이 거짓 HDR 을 보지 않게.
        if ( bTouchesSwapChain && device.setBackBufferColorSpace( resolved.getColorSpace(), resolved.makeDisplayMetadata() ) == false )
            resolved._kind = DisplayOutputDesc::getKind( device.getBackBufferColorSpace() );
        _displayRequest        = requested;
        _appliedDisplayOutput  = resolved;
        _bDisplayOutputApplied = SW_TRUE;
        return resolved;
    }

    void EngineRenderDriver::onMainWindowResize( const uint32 width, const uint32 height )
    {
        if ( _pRHI == nullptr || _pRHI->hasDevice() == false )
            return;
        // 렌더 스레드가 따로 돌면 그 스레드가 beginFrame/endFrame 으로 스왑체인과 렌더 타깃을 계속 건드리고 있을 수 있다. 창 메시지 스레드가 바로
        // resize() 를 부르면 같은 자원에 대한 스레드 간 레이스가 된다. waitIdle() 로 큐를 비우고 GPU 까지 쉬게 한 뒤 크기를 바꾼다.
        if ( _pRenderThread != nullptr )
            _pRenderThread->waitIdle( RHIWaitIdleReason::Resize );
        _pRHI->getDevice().resize( width, height );
    }

    void EngineRenderDriver::submitFrame( const float32 deltaTime, const FrameViewports& views, const ViewCameraProviderDelegate& sceneViewCameraProvider, const FrameDrawMode drawMode )
    {
        // 장치를 잃었으면 검은 화면으로 계속 돌지 않는다 — 렌더 스레드가 잃은 프레임을 Present 한 다음 게임 스레드 프레임에서 정책대로 끝낸다.
        if ( _pRHI != nullptr && _pRHI->hasDevice() && _bDeviceLostHandled == SW_FALSE )
        {
            const RHIDeviceLostPolicy           policy   = _deviceLostPolicyOverride != RHIDeviceLostPolicy::None ? _deviceLostPolicyOverride : _deviceLostPolicy;
            const optional<RHIDeviceLostPolicy> response = decideDeviceLostResponse( _pRHI->getDevice(), _pRHI->getCommittedBackend(), policy );
            if ( response.has_value() )
            {
                _bDeviceLostHandled = SW_TRUE;
                if ( *response == RHIDeviceLostPolicy::Recreate )
                {
                    // 백엔드 교체와 같은 프레임 끝의 길로 다시 만든다 — 성공하면 `applyPendingBackendChange` 가 처리 표시를 내린다.
                    SW_LOG_WARNING_ID( "Render.DeviceLostRecreate", "The %# device was lost - recreating it at the end of the frame (EngineConfig _deviceLostPolicy)",
                                       RHI::getBackendTypeName( _pRHI->getCommittedBackend() ) );
                    _pRHI->scheduleDeviceRecreate();
                }
                else
                {
                    SW_LOG_ERROR_ID( "Render.DeviceLostQuit", "The %# device was lost - quitting with exit code %# (EngineConfig _deviceLostPolicy)", RHI::getBackendTypeName( _pRHI->getCommittedBackend() ),
                                     static_cast<int32>( ProcessExitCode::DeviceLost ) );
                    engine::getSubsystem<QuitRequest>().request( ProcessExitCode::DeviceLost );
                }
            }
        }
        if ( _bDeviceLostHandled != SW_FALSE )
            return; // 잃은 장치에 패킷을 더 내지 않는다 — 루프는 이 프레임 뒤 끝나거나 장치를 다시 만든다

        if ( _pRHI != nullptr && _pRHI->hasDevice() )
        {
            // 경로로 잡힌 머티리얼(메시의 저장된 참조)의 초기화는 게임 스레드가 한다(프레임당 상한, 패킷 빌더는 `Ready` 가 아닌 배치를 건너뛴다). 머티리얼은 게임 스레드
            // 객체이고 GPU 자원(상수 버퍼)은 렌더 스레드의 프록시가 만든다(언리얼 `UMaterial` / `FMaterialRenderProxy`) — 그래서 렌더 스레드를 기다리지 않는다.
            // 줄이 비어도 부른다 — 초기화를 마친 고정 머티리얼을 PSO 미리 만들기 값으로 옮기는 일도 여기서 한다.
            engine::getSubsystem<AssetManager>().getAssetCache<MaterialCache>().initializePending(
                &_pRHI->getDevice(), engine::getSubsystem<DeterministicRun>().getPolicy()._bFinishAssetJobsEachFrame == SW_TRUE ? UINT32_MAX : MaterialCache::kMaxInitsPerFrame );
            // 밉 스트리밍이 바꿔 끼운 옛 텍스처를 게임 스레드에서 해제한다 — 이미 만든 패킷이 옛 SRV 번호를 쓰는 동안은 `releaseHandle` 이 미룬다.
            engine::getSubsystem<AssetManager>().getAssetCache<TextureCache>().releaseRetiredTextures( &_pRHI->getDevice() );
        }

        // 패킷 · 씬 스냅샷은 FrameRenderer 단계가 만든다. 그 단계가 서지 않았으면(헤드리스 작업) 낼 패킷이 없다.
        if ( _pPacketBuilder == nullptr || _pPacketBuilder->isInitialized() == false )
            return;

        Scene* pActiveScene = _pSceneManager != nullptr ? _pSceneManager->getActiveScene() : nullptr;

        // 패킷 채우기의 순서는 여기서 보인다: 열기 → 관찰자 → 씬 · 뷰 → 캔버스 → 제출. 씬 · 뷰는 위의 씬 틱 · 디버그 그리기 · UI 갱신 **뒤**라
        // 이번 프레임의 게임 상태를 싣고, 캔버스는 UI 갱신이 칠한 목록을 싣는다.
        BLOCK( "RenderFramePacket 제출" )
        {
            SW_MEMORY_SCOPE( RenderCPU );
            RenderFramePacket& packet = _pPacketBuilder->beginPacket();
            // 화면 출력은 패킷이 뜬 요청을 능력에 맞춰 적용한 값으로 싣는다 — 렌더러는 장치의 색 공간과 이 값(종이 흰색 · 최대 휘도)만 본다.
            packet._settings._displayOutput = applyDisplayOutput( packet._settings._displayOutput );
            // 그리지 않는 프레임도 패킷은 똑같이 짓는다 — 짓기가 게임 상태를 건드리는 몫(LOD 뷰 · 그려진 화면비 · 컷 소비)과 GPU 장면의 고친 구간이 그리는 실행과 같아야 한다.
            packet._drawMode = drawMode;
            if ( const SimulationOnlyFrameQueue* const pSimulationOnly = engine::findSubsystem<SimulationOnlyFrameQueue>() )
                packet._simulationOnlySerial = pSimulationOnly->getLastSkippedSerial();
            // 이 프레임을 찍어 달라고 맡긴 것(`ScreenshotRequestQueue::putForNextPacket`)은 다음에 그리는 이 패킷에 싣는다. 그리지 않는 패킷은 싣지 않는다(다음에 그리는 패킷이 받는다).
            ScreenshotRequestQueue* const pMailbox = drawMode == FrameDrawMode::Draw ? engine::findSubsystem<ScreenshotRequestQueue>() : nullptr;
            if ( pMailbox != nullptr )
                (void)pMailbox->takeForPacket( packet._screenshotPath );

            _pPacketBuilder->populateScene( packet, views, sceneViewCameraProvider, pActiveScene, deltaTime, *_pRHI, _pFrameRenderer );
            // 미리보기 씬(에디터 툴 창)은 씬마다 자기 스냅샷 · 렌더 타깃으로 — 활성 씬과 섞이지 않는다.
            if ( _pSceneManager != nullptr )
                _pPacketBuilder->populateScenePreviews( packet, views, *_pSceneManager );
            _pPacketBuilder->populateCanvas( packet, views.isSceneViewMain(), _pUISystem, _pFontService, _pRHI );
            _pPacketBuilder->resolveMarkerInstanceSlots( packet, _pUISystem, pActiveScene );

            if ( _pRenderThread != nullptr )
            {
                // GT 가 여기서 기다린다면 그것은 렌더 스레드가 밀린 것이다. 링이 차면 submit 이 막는다.
                SW_PROFILE_SCOPE( "GT.Packet.submit" );
                _pRenderThread->submit( packet );
            }
        }
    }

    bool EngineRenderDriver::applyPendingBackendChange()
    {
        if ( _pRHI == nullptr )
            return false;

        const bool       bRecreateLost = _pRHI->isDeviceRecreatePending(); // 잃은 장치를 같은 백엔드로 다시 만든다(`RHIDeviceLostPolicy::Recreate`)
        const RHIBackend requested     = _pRHI->consumePendingBackendChange();
        const RHIBackend previous      = _pRHI->getCommittedBackend();
        if ( requested == previous && bRecreateLost == false )
            return true;

        SW_LOG_INFO_ID( "Engine.SoftRecreatingRHI", "Soft-recreating RHI: %# → %#",
                        RHI::getBackendTypeName( previous ),
                        RHI::getBackendTypeName( requested ) );
        // 교체의 단계마다 든 시간을 남긴다 - 무엇이 교체를 무겁게 하는지(내리기 · 디바이스 · 자원 다시 올리기 · 단계 다시 세우기) 실행마다 보인다.
        const Stopwatch swapWatch;

        BLOCK( "디바이스에 매인 단계 내리기" )
        {
            // RHI 에 (간접으로라도) 의존하는 단계(씬의 디바이스 · 라이브 셰이더 · 렌더 스레드 · 렌더러)를 기동 표의 역순으로, 기동과 같은
            // 본문으로 내린다. 씬 스냅샷 빌더가 든 머티리얼 · 인스턴스도 FrameRenderer 단계의 종료가 옛 디바이스가 살아 있을 때 놓는다.
            _startupScope.getCollection().shutdownDependents( _rhiSubsystem );

            // 옛 디바이스의 GPU 자원은 recreateDevice 안의 shutdown 이 레지스트리에 통보하며 거둔다.
            _pRHI->getDevice().waitIdle( RHIWaitIdleReason::BackendSwap );
            engine::getSubsystem<ShaderCache>().clearCache();
        }

        const uint64 shutdownMicroseconds = swapWatch.getElapsedMicroseconds();
        if ( _pRHI->recreateDevice( requested ) == false )
        {
            SW_LOG_ERROR_ID( "Engine.RecreateDeviceFailedRestoringPrevious", "recreateDevice failed; restoring previous backend %#",
                             RHI::getBackendTypeName( previous ) );
            gv_rhiBackend = previous;
            if ( _pRHI->recreateDevice( previous ) == false )
            {
                SW_LOG_ERROR_ID( "Engine.FailedRestorePreviousRHI", "Failed to restore previous RHI backend — device is gone." );
                if ( bRecreateLost )
                    engine::getSubsystem<QuitRequest>().request( ProcessExitCode::DeviceLost ); // 잃은 장치를 되살리지 못했다 — 장치 없이 돌지 않는다
                return false;
            }
            _bDeviceLostHandled    = SW_FALSE;      // 되돌린 장치는 새 장치다
            _bDisplayOutputApplied = SW_FALSE;      // 되돌린 장치도 SDR 스왑체인으로 선다
            (void)rebindSceneAfterDeviceRecreate(); // 되돌린 장치에 장면을 다시 묶는다 — 이 경로는 이미 실패로 답한다
            return false;
        }

        _bDeviceLostHandled               = SW_FALSE; // 새 장치다 — 다시 잃으면 정책을 다시 묻는다
        _bDisplayOutputApplied            = SW_FALSE; // 새 장치는 SDR 스왑체인으로 선다 — 다음 프레임이 화면 출력을 다시 적용한다
        const uint64 recreateMicroseconds = swapWatch.getElapsedMicroseconds();
        string       rebindTiming;
        if ( rebindSceneAfterDeviceRecreate( &rebindTiming ) == false )
        {
            if ( bRecreateLost )
                engine::getSubsystem<QuitRequest>().request( ProcessExitCode::DeviceLost ); // 다시 만든 장치에 장면을 묶지 못했다 — 검은 화면으로 돌지 않는다
            return false;
        }
        if ( bRecreateLost )
        {
            ++_deviceRecreateCount;
            SW_LOG_INFO_ID( "Render.DeviceRecreated", "The lost device was recreated on %# (recreate %#)", RHI::getBackendTypeName( _pRHI->getCommittedBackend() ), _deviceRecreateCount );
        }
        SW_LOG_INFO_ID( "Engine.BackendSwapTookMs", "Backend swap took %#ms (shut down %#ms, device %#ms, %#)", swapWatch.getElapsedMicroseconds() / 1000, shutdownMicroseconds / 1000,
                        ( recreateMicroseconds - shutdownMicroseconds ) / 1000, rebindTiming.c_str() );
        // 교체 뒤 디바이스에 매인 설정이 새 디바이스를 따르는지 한 줄로 남긴다.
        SW_LOG_INFO_ID( "Engine.ActiveBackendNowMerge", "Active backend is now %# (merge batches across materials %#, native bindless sampling %#)", RHI::getBackendTypeName( _pRHI->getCommittedBackend() ),
                        _pPacketBuilder->isMergeBatchesAcrossMaterials() ? 1 : 0,
                        _pRHI->getDevice().supportsNativeBindlessSampling() ? 1 : 0 );
        return true;
    }
} // namespace sw

namespace sw
{
    // 세 단계를 빌려 든다 — 공장이 찾으므로 먼저 만든다. 렌더러를 두지 않는 호스트(헤드리스 작업이 아니라 하네스)는 만들지 않는다.
    SW_ENGINE_SUBSYSTEM( EngineRenderDriverSubsystem, Startup, ( RHISubsystem, FrameRendererSubsystem, RenderThreadSubsystem ), []( engine::EngineSubsystemFactoryScope& scope, engine::EngineSubsystemBuild& build )
    {
        if ( build._pStartupSetup->_bRenderer == false )
            return;
        EngineRenderDriverSubsystem& subsystem = scope.emplaceSubsystem<EngineRenderDriverSubsystem>( scope.getScope(), scope.get<RHISubsystem>(), scope.get<FrameRendererSubsystem>(), scope.get<RenderThreadSubsystem>() );
        EngineRenderDriver&          driver    = subsystem.getRenderDriver();
        const bool                   bExposed  = scope.exposeMember<IEngineRenderDriver>( subsystem, driver );
        SW_ASSERT( bExposed );
        (void)bExposed;
        build._pStartupParts->_pRenderDriver = &driver;
    } );
} // namespace sw

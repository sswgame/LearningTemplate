#include "pch.h"

#include "Engine/Renderer/EngineRenderDriver.h"

#include "Core/Log/Logger.h"
#include "Core/String/StringBuilder.h"
#include "Core/Subsystem/SubsystemScope.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Asset/AssetManager.h"
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
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Renderer/Capture/CaptureReadiness.h"
#include "Engine/Renderer/Frame/FrameRenderer.h"
#include "Engine/Renderer/Frame/FrameRendererSubsystem.h"
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
        , _bDeviceLostHandled{ SW_FALSE }
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
        // 장치 분실 정책은 짐작하지 않는다 — 값이 없으면 기동을 오류로 끝낸다(폴백 없음).
        _deviceLostPolicy = engine::getSubsystem<EngineConfig>()._deviceLostPolicy;
        if ( _deviceLostPolicy == RHIDeviceLostPolicy::None )
        {
            SW_LOG_ERROR_ID( "Render.DeviceLostPolicyMissing", "EngineConfig _deviceLostPolicy is not set - add it to Config/Engine/EngineConfig.json (Quit)" );
            engine::getSubsystem<QuitRequest>().request( ProcessExitCode::InitializationFailed );
        }
        // 스왑체인은 주 창 크기를 따른다 — RHI 가 섰으면 주 창도 있다(RHI 가 만들어 넘긴 창이거나 호스트가 먼저 넘긴 창).
        const MainWindowSubsystem* const pMainWindow = _pRHI != nullptr ? engine::findSubsystem<MainWindowSubsystem>() : nullptr;
        if ( pMainWindow != nullptr && pMainWindow->getWindow() != nullptr )
        {
            _mainWindowResizeSubscription    = pMainWindow->getWindow()->bindResizeCallback( SW_DELEGATE_METHOD( WindowResizeDelegate, &EngineRenderDriver::onMainWindowResize, this ) );
            _mainWindowLifecycleSubscription = pMainWindow->getWindow()->bindLifecycleCallback( SW_DELEGATE_METHOD( WindowLifecycleDelegate, &EngineRenderDriver::onMainWindowLifecycle, this ) );
        }
    }

    void EngineRenderDriver::onShutdownBegin()
    {
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

    optional<ProcessExitCode> EngineRenderDriver::decideDeviceLostExit( const IRHIDevice& device, const RHIDeviceLostPolicy policy )
    {
        if ( device.isDeviceLost() == false )
            return {};
        switch ( policy )
        {
            case RHIDeviceLostPolicy::Quit:
                return ProcessExitCode::DeviceLost;
            case RHIDeviceLostPolicy::None:
                break;
        }
        return ProcessExitCode::InitializationFailed; // 정책이 없으면 기동이 이미 이 코드로 끝내기를 요청했다(`onStartupFinished`)
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

    void EngineRenderDriver::submitFrame( const float32 deltaTime, const FrameViewports& views, const ViewCameraProviderDelegate& sceneViewCameraProvider )
    {
        // 장치를 잃었으면 검은 화면으로 계속 돌지 않는다 — 렌더 스레드가 잃은 프레임을 Present 한 다음 게임 스레드 프레임에서 정책대로 끝낸다.
        if ( _pRHI != nullptr && _pRHI->hasDevice() && _bDeviceLostHandled == SW_FALSE )
        {
            const optional<ProcessExitCode> deviceLostExit = decideDeviceLostExit( _pRHI->getDevice(), _deviceLostPolicy );
            if ( deviceLostExit.has_value() )
            {
                _bDeviceLostHandled = SW_TRUE;
                SW_LOG_ERROR_ID( "Render.DeviceLostQuit", "The %# device was lost - quitting with exit code %# (EngineConfig _deviceLostPolicy)", RHI::getBackendTypeName( _pRHI->getCommittedBackend() ),
                                 static_cast<int32>( *deviceLostExit ) );
                engine::getSubsystem<QuitRequest>().request( *deviceLostExit );
            }
        }
        if ( _bDeviceLostHandled != SW_FALSE )
            return; // 잃은 장치에 패킷을 더 내지 않는다 — 루프는 이 프레임 뒤 끝난다

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
            // 이 프레임을 찍어 달라고 맡긴 것(`ScreenshotRequestQueue::putForNextPacket`)은 다음에 그리는 이 패킷에 싣는다.
            if ( ScreenshotRequestQueue* const pMailbox = engine::findSubsystem<ScreenshotRequestQueue>() )
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

        const RHIBackend requested = _pRHI->consumePendingBackendChange();
        const RHIBackend previous  = _pRHI->getCommittedBackend();
        if ( requested == previous )
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
                return false;
            }
            (void)rebindSceneAfterDeviceRecreate(); // 되돌린 장치에 장면을 다시 묶는다 — 이 경로는 이미 실패로 답한다
            return false;
        }

        const uint64 recreateMicroseconds = swapWatch.getElapsedMicroseconds();
        string       rebindTiming;
        if ( rebindSceneAfterDeviceRecreate( &rebindTiming ) == false )
            return false;
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

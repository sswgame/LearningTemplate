#include "pch.h"

#include "DevTools/Client/Shader/LiveShaderSubsystem.h"

#include "Core/Foundation/BuildConfig.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"

#include "DevTools/Client/Shader/ShaderRecompiler.h"

#include "Engine/Foundation/EngineSubsystemHost.h"
#include "Engine/Renderer/Frame/FrameRenderer.h"
#include "Engine/Renderer/Frame/FrameRendererSubsystem.h"
#include "Engine/Renderer/Frame/OffscreenSceneRenderers.h"
#include "Engine/Renderer/RenderThread.h"
#include "Engine/Renderer/RenderThreadSubsystem.h"

namespace sw
{
    /**
     * @brief `-gv_liveShaderRecompile=1` — 재컴파일기(Ctrl+F8)를 세웁니다. 기본은 Debug 만 켠다(파일 감시 · 재컴파일 큐를 측정 빌드에 두지 않는다).
     * @details 개발 빌드(Release)는 셰이더 리로드 시나리오가 `<Launch>` 로 켠다. 언리얼 `r.ShaderDevelopmentMode` 자리.
     */
    SW_TEST_GLOBAL_VARIABLE( bool, gv_liveShaderRecompile, build::kDebug, "Build the live shader recompiler (Ctrl+F8). On by default in Debug only; scenarios turn it on in Release" );

    LiveShaderSubsystem::LiveShaderSubsystem( const FrameRendererSubsystem& frameRendererSubsystem, const RenderThreadSubsystem& renderThreadSubsystem )
        : _shaderRecompiler{ nullptr }
        , _recompiledSubscription{}
        , _frameRendererSubsystem{ frameRendererSubsystem }
        , _renderThreadSubsystem{ renderThreadSubsystem }
        , _bReady{ SW_FALSE }
    {
    }

    LiveShaderSubsystem::~LiveShaderSubsystem() = default;

    SubsystemInitResult LiveShaderSubsystem::initializeSubsystem()
    {
        if ( gv_liveShaderRecompile )
        {
            _bReady = SW_FALSE;
            if ( _shaderRecompiler == nullptr )
                _shaderRecompiler = make_unique<ShaderRecompiler>();
            if ( _shaderRecompiler->initialize( "Shaders" ) == false )
            {
                SW_LOG_ERROR_ID( "LiveShader.FailedInitializeShaderRecompiler", "Failed to initialize ShaderRecompiler!" );
                return SubsystemInitResult::Succeeded; // 개발 도구 하나 때문에 기동을 멈추지 않는다
            }
            _recompiledSubscription = _shaderRecompiler->bindOnAnyShaderRecompiled( SW_DELEGATE_METHOD( ShaderRecompiledDelegate, &LiveShaderSubsystem::onShaderRecompiled, this ) );
            _bReady                 = SW_TRUE;
        }
        return SubsystemInitResult::Succeeded;
    }

    void LiveShaderSubsystem::shutdownSubsystem()
    {
        _recompiledSubscription.reset(); // 백엔드 교체로 다시 서면 다시 건다
        if ( _shaderRecompiler != nullptr )
            _shaderRecompiler->shutdown();
        _bReady = SW_FALSE;
    }

    void LiveShaderSubsystem::onShaderRecompiled( const vector<string>& listShaderPath )
    {
        FrameRenderer* const           pFrameRenderer      = &_frameRendererSubsystem.getFrameRenderer();
        OffscreenSceneRenderers* const pOffscreenRenderers = &_frameRendererSubsystem.getOffscreenRenderers();
        // 렌더 스레드에 맡기고 기다리지 않는다 — 렌더 쪽 객체(PSO 캐시 · 패스 자원)는 렌더 스레드가 다음 프레임 기록 전에 고친다. 화면 밖 렌더러는 주 렌더러가 함께 쓰는 PSO 캐시를 다시 채운 뒤다.
        RenderThread* const pRenderThread = _renderThreadSubsystem.findRenderThread();
        if ( pRenderThread == nullptr )
        {
            // 렌더 스레드가 없으면 패킷을 실행하는 쪽이 없다 — 렌더러를 만지는 스레드가 이 스레드 하나다.
            pFrameRenderer->onShaderRecompiled( listShaderPath );
            pOffscreenRenderers->onShaderRecompiled( listShaderPath );
            return;
        }
        pRenderThread->enqueueCommand( SW_DELEGATE_LAMBDA( RenderCommandDelegate, [pFrameRenderer, pOffscreenRenderers, listShaderPath]( IRHIDevice& )
        {
            pFrameRenderer->onShaderRecompiled( listShaderPath );
            pOffscreenRenderers->onShaderRecompiled( listShaderPath );
        } ) );
    }

    span<const utf8* const> LiveShaderSubsystem::getSubsystemDependencies() const
    {
        static constexpr const utf8* kArrDependency[] = { FrameRendererSubsystem::kSubsystemName, RenderThreadSubsystem::kSubsystemName };
        return kArrDependency;
    }

    SubsystemHostMask LiveShaderSubsystem::getSubsystemHost() const
    {
        return EngineSubsystemHost::kClient;
    }
} // namespace sw

#include "pch.h"

#include "DevTools/Shader/LiveShaderSubsystem.h"

#include "DevTools/Shader/ShaderRecompiler.h"

#include "Engine/Foundation/EngineSubsystemHost.h"
#include "Engine/Renderer/Frame/FrameRenderer.h"
#include "Engine/Renderer/Frame/FrameRendererSubsystem.h"
#include "Engine/Renderer/RenderThread.h"
#include "Engine/Renderer/RenderThreadSubsystem.h"

namespace sw
{
    SW_LOG_CALLER( "LiveShader" );

    LiveShaderSubsystem::LiveShaderSubsystem( const FrameRendererSubsystem& frameRendererSubsystem, const RenderThreadSubsystem& renderThreadSubsystem )
        : _shaderRecompiler{ nullptr }
        , _recompiledSubscription{}
        , _frameRendererSubsystem{ frameRendererSubsystem }
        , _renderThreadSubsystem{ renderThreadSubsystem }
        , _bReady{ false }
    {
    }

    LiveShaderSubsystem::~LiveShaderSubsystem() = default;

    SubsystemInitResult LiveShaderSubsystem::initializeSubsystem()
    {
#if defined( SW_DEBUG )
        _bReady = false;
        if ( _shaderRecompiler == nullptr )
        {
            SW_MEMORY_SCOPE( Shader );
            _shaderRecompiler = make_unique<ShaderRecompiler>();
        }
        if ( _shaderRecompiler->initialize( "Shaders" ) == false )
        {
            SW_LOG_ERROR( "Failed to initialize ShaderRecompiler!" );
            return SubsystemInitResult::Succeeded; // 개발 도구 하나 때문에 기동을 멈추지 않는다
        }
        _recompiledSubscription = _shaderRecompiler->bindOnAnyShaderRecompiled( SW_DELEGATE_METHOD( ShaderRecompiledDelegate, &LiveShaderSubsystem::onShaderRecompiled, this ) );
        _bReady                 = true;
#endif
        return SubsystemInitResult::Succeeded;
    }

    void LiveShaderSubsystem::shutdownSubsystem()
    {
        _recompiledSubscription.reset(); // 백엔드 교체로 다시 서면 다시 건다
        if ( _shaderRecompiler != nullptr )
            _shaderRecompiler->shutdown();
        _bReady = false;
    }

    void LiveShaderSubsystem::onShaderRecompiled( string_view shaderPath, const ShaderCompileResult& result )
    {
        RenderThread* const pRenderThread = _renderThreadSubsystem.findRenderThread();
        if ( pRenderThread != nullptr )
            pRenderThread->waitIdle( RHIWaitIdleReason::HotReload );
        _frameRendererSubsystem.getFrameRenderer().onShaderRecompiled( shaderPath, result );
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

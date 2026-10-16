#include "pch.h"

#include "Engine/Renderer/LiveShaderSubsystem.h"

#include "Engine/Common/EngineSubsystemHost.h"
#include "Engine/Graphics/Shader/Compile/ShaderRecompiler.h"
#include "Engine/Renderer/Frame/FrameRenderer.h"
#include "Engine/Renderer/Frame/FrameRendererSubsystem.h"
#include "Engine/Renderer/RenderThread.h"
#include "Engine/Renderer/RenderThreadSubsystem.h"

namespace sw
{
    SW_LOG_CALLER( "LiveShader" );

    LiveShaderSubsystem::LiveShaderSubsystem( const FrameRendererSubsystem& frameRendererSubsystem, const RenderThreadSubsystem& renderThreadSubsystem )
        :
#if !defined( SW_SHIPPING )
        _shaderRecompiler{ nullptr }
        ,
#endif
        _frameRendererSubsystem{ frameRendererSubsystem }
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
        _shaderRecompiler->setOnAnyShaderRecompiled( SW_DELEGATE_METHOD( ShaderRecompiledDelegate, &LiveShaderSubsystem::onShaderRecompiled, this ) );
        _bReady = true;
#endif
        return SubsystemInitResult::Succeeded;
    }

    void LiveShaderSubsystem::shutdownSubsystem()
    {
#if !defined( SW_SHIPPING )
        if ( _shaderRecompiler != nullptr )
            _shaderRecompiler->shutdown();
#endif
        _bReady = false;
    }

#if !defined( SW_SHIPPING )
    void LiveShaderSubsystem::onShaderRecompiled( string_view shaderPath, const ShaderCompileResult& result )
    {
        RenderThread* const pRenderThread = _renderThreadSubsystem.findRenderThread();
        if ( pRenderThread != nullptr )
            pRenderThread->waitIdle();
        _frameRendererSubsystem.getFrameRenderer().onShaderRecompiled( shaderPath, result );
    }
#endif

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

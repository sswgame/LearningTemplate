#include "pch.h"

#include "Engine/Renderer/RenderThreadSubsystem.h"

#include "Engine/Common/EngineSubsystemHost.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/RHI.h"
#include "Engine/Renderer/Frame/FrameRendererSubsystem.h"
#include "Engine/Renderer/RHISubsystem.h"
#include "Engine/Renderer/RenderThread.h"

namespace sw
{
    SW_LOG_CALLER( "RenderThread" );

    RenderThreadSubsystem::RenderThreadSubsystem( const RHISubsystem& rhiSubsystem, const FrameRendererSubsystem& frameRendererSubsystem )
        : _renderThread{ nullptr }
        , _rhiSubsystem{ rhiSubsystem }
        , _frameRendererSubsystem{ frameRendererSubsystem }
    {
    }

    RenderThreadSubsystem::~RenderThreadSubsystem() = default;

    SubsystemInitResult RenderThreadSubsystem::initializeSubsystem()
    {
        if ( _renderThread == nullptr )
        {
            SW_MEMORY_SCOPE( RenderCPU );
            _renderThread = make_unique<RenderThread>();
        }
        if ( _renderThread->attach( &_rhiSubsystem.findRHI()->getDevice(), &_frameRendererSubsystem.getFrameRenderer() ) == false )
        {
            SW_LOG_ERROR( "Failed to attach RenderThread!" );
            return SubsystemInitResult::Failed;
        }
        return SubsystemInitResult::Succeeded;
    }

    void RenderThreadSubsystem::shutdownSubsystem()
    {
        _renderThread->waitIdle();
        _renderThread->stop();
    }

    span<const utf8* const> RenderThreadSubsystem::getSubsystemDependencies() const
    {
        static constexpr const utf8* kArrDependency[] = { FrameRendererSubsystem::kSubsystemName };
        return kArrDependency;
    }

    SubsystemHostMask RenderThreadSubsystem::getSubsystemHost() const
    {
        return EngineSubsystemHost::kClient;
    }
} // namespace sw

#include "pch.h"

#include "Engine/Renderer/RenderThreadSubsystem.h"

#include "Engine/Foundation/EngineSubsystemHost.h"
#include "Engine/Foundation/EngineSubsystemRegistration.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/RHI.h"
#include "Engine/Renderer/Frame/FrameRendererSubsystem.h"
#include "Engine/Renderer/RHISubsystem.h"
#include "Engine/Renderer/RenderThread.h"

namespace sw
{
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
            SW_LOG_ERROR_ID( "RenderThread.FailedAttachRenderThread", "Failed to attach RenderThread!" );
            return SubsystemInitResult::Failed;
        }
        _renderThread->setOffscreenRenderers( &_frameRendererSubsystem.getOffscreenRenderers() );
        return SubsystemInitResult::Succeeded;
    }

    void RenderThreadSubsystem::shutdownSubsystem()
    {
        _renderThread->waitIdle( RHIWaitIdleReason::Shutdown );
        _renderThread->stop();
        _renderThread->setOffscreenRenderers( nullptr );
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

namespace sw
{
    // 서브시스템 자리 등록(Engine/Foundation/EngineSubsystemRegistration.h) — 만드는 순서는 자리 표가 정한다.
    SW_ENGINE_SUBSYSTEM( RenderThreadSubsystem, []( SubsystemScope& scope, engine::EngineSubsystemBuild& build )
    {
        if ( build._pStartupSetup->_bRenderer == false )
            return;
        build._pStartupParts->_pRenderThread = &scope.emplaceSubsystem<RenderThreadSubsystem>( scope.get<RHISubsystem>(), scope.get<FrameRendererSubsystem>() );
    } );
} // namespace sw

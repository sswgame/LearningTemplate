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
        // 화면 밖 씬 렌더러도 디바이스 · 주 렌더러와 함께 붙인다 — 스레드가 뜬 뒤 따로 꽂으면 렌더 스레드가 읽는 포인터를 게임 스레드가 덮는 모양이다.
        if ( _renderThread->attach( &_rhiSubsystem.findRHI()->getDevice(), &_frameRendererSubsystem.getFrameRenderer(), &_frameRendererSubsystem.getOffscreenRenderers() ) == false )
        {
            SW_LOG_ERROR_ID( "RenderThread.FailedAttachRenderThread", "Failed to attach RenderThread!" );
            return SubsystemInitResult::Failed;
        }
        return SubsystemInitResult::Succeeded;
    }

    void RenderThreadSubsystem::shutdownSubsystem()
    {
        _renderThread->waitIdle( RHIWaitIdleReason::Shutdown );
        _renderThread->stop(); // 붙인 렌더 쪽 객체 셋을 함께 놓는다
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
    SW_ENGINE_SUBSYSTEM( RenderThreadSubsystem, Startup, ( RHISubsystem, FrameRendererSubsystem ), []( engine::EngineSubsystemFactoryScope& scope, engine::EngineSubsystemBuild& build )
    {
        if ( build._pStartupSetup->_bRenderer == false )
            return;
        build._pStartupParts->_pRenderThread = &scope.emplaceSubsystem<RenderThreadSubsystem>( scope.get<RHISubsystem>(), scope.get<FrameRendererSubsystem>() );
    } );
} // namespace sw

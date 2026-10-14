#include "pch.h"

#include "Engine/Renderer/Frame/FrameRendererSubsystem.h"

#include "Core/Task/TaskManager.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Common/EngineSubsystemHost.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/RHI.h"
#include "Engine/Renderer/Frame/FrameRenderer.h"
#include "Engine/Renderer/Frame/RenderPacketBuilder.h"
#include "Engine/Renderer/RHISubsystem.h"

namespace sw
{
    SW_LOG_CALLER( "FrameRenderer" );

    FrameRendererSubsystem::FrameRendererSubsystem( unique_ptr<FrameRenderer> frameRenderer, const RHISubsystem& rhiSubsystem )
        : _frameRenderer{ std::move( frameRenderer ) }
        , _packetBuilder{ nullptr }
        , _rhiSubsystem{ rhiSubsystem }
    {
        SW_MEMORY_SCOPE( RenderCPU );
        _packetBuilder = make_unique<RenderPacketBuilder>();
    }

    FrameRendererSubsystem::~FrameRendererSubsystem()
    {
        _packetBuilder->destroy();
        _packetBuilder.reset();
        _frameRenderer.reset();
    }

    SubsystemInitResult FrameRendererSubsystem::initializeSubsystem()
    {
        IRHIDevice& device = _rhiSubsystem.findRHI()->getDevice();
        _packetBuilder->initialize( device, &engine::getSubsystem<TaskManager>() );
        if ( _frameRenderer->initialize( &device, &engine::getSubsystem<TaskManager>() ) == false )
        {
            SW_LOG_ERROR( "Failed to initialize FrameRenderer!" );
            return SubsystemInitResult::Failed;
        }
        return SubsystemInitResult::Succeeded;
    }

    void FrameRendererSubsystem::shutdownSubsystem()
    {
        _packetBuilder->shutdown();
        _frameRenderer->shutdown();
    }

    span<const utf8* const> FrameRendererSubsystem::getSubsystemDependencies() const
    {
        static constexpr const utf8* kArrDependency[] = { RHISubsystem::kSubsystemName, TaskManager::kSubsystemName };
        return kArrDependency;
    }

    SubsystemHostMask FrameRendererSubsystem::getSubsystemHost() const
    {
        return EngineSubsystemHost::kClient;
    }
} // namespace sw

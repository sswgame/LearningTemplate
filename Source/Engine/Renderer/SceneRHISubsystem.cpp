#include "pch.h"

#include "Engine/Renderer/SceneRHISubsystem.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Common/EngineSubsystemHost.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/RHI.h"
#include "Engine/Renderer/RHISubsystem.h"
#include "Engine/Scene/SceneManager.h"

namespace sw
{
    SceneRHISubsystem::SceneRHISubsystem( const RHISubsystem& rhiSubsystem )
        : _rhiSubsystem{ rhiSubsystem }
    {
    }

    SubsystemInitResult SceneRHISubsystem::initializeSubsystem()
    {
        engine::getSceneManager().setRHIDevice( &_rhiSubsystem.findRHI()->getDevice() );
        return SubsystemInitResult::Succeeded;
    }

    void SceneRHISubsystem::shutdownSubsystem()
    {
        engine::getSceneManager().setRHIDevice( nullptr );
    }

    span<const utf8* const> SceneRHISubsystem::getSubsystemDependencies() const
    {
        static constexpr const utf8* kArrDependency[] = { RHISubsystem::kSubsystemName, SceneManager::kSubsystemName };
        return kArrDependency;
    }

    SubsystemHostMask SceneRHISubsystem::getSubsystemHost() const
    {
        return EngineSubsystemHost::kClient;
    }
} // namespace sw

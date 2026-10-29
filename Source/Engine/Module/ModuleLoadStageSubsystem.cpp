#include "pch.h"

#include "Engine/Module/ModuleLoadStageSubsystem.h"

#include "Engine/Config/ConfigManager.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Module/HostServices.h"
#include "Engine/Module/ModuleUnloadStageSubsystem.h"
#include "Engine/Reflection/ReflectionSubsystem.h"
#include "Engine/Reflection/TypeRegistry.h"

namespace sw
{
    SW_LOG_CALLER( "ModuleLoadStage" );

    ModuleLoadStageSubsystem::ModuleLoadStageSubsystem( const ISubsystemLocator* pScope )
        : _pScope{ pScope }
    {
    }

    SubsystemInitResult ModuleLoadStageSubsystem::initializeSubsystem()
    {
        IModuleLoader* pHost = locateSubsystem<IModuleLoader>( _pScope );
        if ( pHost != nullptr && pHost->loadTypeProviders() == false )
        {
            SW_LOG_ERROR( "The host could not load its type-providing modules - scenes are not read" );
            return SubsystemInitResult::Failed;
        }
        engine::getSubsystem<TypeRegistry>().markAllModuleTypesRegistered();
        return SubsystemInitResult::Succeeded;
    }

    span<const utf8* const> ModuleLoadStageSubsystem::getSubsystemDependencies() const
    {
        static constexpr const utf8* kArrDependency[] = { ConfigManager::kSubsystemName, ModuleUnloadStageSubsystem::kSubsystemName, ReflectionSubsystem::kSubsystemName };
        return kArrDependency;
    }
} // namespace sw

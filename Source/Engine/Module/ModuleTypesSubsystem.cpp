#include "pch.h"

#include "Engine/Module/ModuleTypesSubsystem.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Config/ConfigManager.h"
#include "Engine/Module/ModuleImageHost.h"
#include "Engine/Module/ModuleImagesSubsystem.h"
#include "Engine/Reflection/ReflectionSubsystem.h"
#include "Engine/Reflection/TypeRegistry.h"

namespace sw
{
    SW_LOG_CALLER( "ModuleTypes" );

    ModuleTypesSubsystem::ModuleTypesSubsystem( const ISubsystemLocator* pScope )
        : _pScope{ pScope }
    {
    }

    SubsystemInitResult ModuleTypesSubsystem::initializeSubsystem()
    {
        IModuleImageHost* pHost = locateSubsystem<IModuleImageHost>( _pScope );
        if ( pHost != nullptr && pHost->loadTypeProviders() == false )
        {
            SW_LOG_ERROR( "The host could not load its type-providing modules - scenes are not read" );
            return SubsystemInitResult::Failed;
        }
        engine::getSubsystem<TypeRegistry>().markAllModuleTypesRegistered();
        return SubsystemInitResult::Succeeded;
    }

    span<const utf8* const> ModuleTypesSubsystem::getSubsystemDependencies() const
    {
        static constexpr const utf8* kArrDependency[] = { ConfigManager::kSubsystemName, ModuleImagesSubsystem::kSubsystemName, ReflectionSubsystem::kSubsystemName };
        return kArrDependency;
    }
} // namespace sw

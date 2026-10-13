#include "pch.h"

#include "Engine/Module/ModuleTypesSubsystem.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Config/ConfigManager.h"
#include "Engine/Module/ModuleImagesSubsystem.h"
#include "Engine/Module/ReflectionSubsystem.h"
#include "Engine/Reflection/TypeRegistry.h"

namespace sw
{
    SW_LOG_CALLER( "ModuleTypes" );

    ModuleTypesSubsystem::ModuleTypesSubsystem( const ModuleTypeLoaderDelegate* pModuleTypeLoader )
        : _pModuleTypeLoader{ pModuleTypeLoader }
    {
    }

    SubsystemInitResult ModuleTypesSubsystem::initializeSubsystem()
    {
        if ( _pModuleTypeLoader != nullptr && _pModuleTypeLoader->isBound() && ( *_pModuleTypeLoader )() == false )
        {
            SW_LOG_ERROR( "The host could not load its type-providing modules - scenes are not read" );
            return SubsystemInitResult::Failed;
        }
        engine::getTypeRegistry().markAllModuleTypesRegistered();
        return SubsystemInitResult::Succeeded;
    }

    span<const utf8* const> ModuleTypesSubsystem::getSubsystemDependencies() const
    {
        static constexpr const utf8* kArrDependency[] = { ConfigManager::kSubsystemName, ModuleImagesSubsystem::kSubsystemName, ReflectionSubsystem::kSubsystemName };
        return kArrDependency;
    }
} // namespace sw

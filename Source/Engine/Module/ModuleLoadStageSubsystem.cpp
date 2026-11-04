#include "pch.h"

#include "Engine/Module/ModuleLoadStageSubsystem.h"

#include "Engine/Config/ConfigManager.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Foundation/EngineSubsystemRegistration.h"
#include "Engine/Module/HostInterfaces.h"
#include "Engine/Module/ModuleUnloadStageSubsystem.h"
#include "Engine/Reflection/ReflectionSubsystem.h"
#include "Engine/Reflection/TypeRegistry.h"

namespace sw
{
    ModuleLoadStageSubsystem::ModuleLoadStageSubsystem( const ISubsystemLocator* pScope )
        : _pScope{ pScope }
    {
    }

    SubsystemInitResult ModuleLoadStageSubsystem::initializeSubsystem()
    {
        IModuleLoader* pHost = locateSubsystem<IModuleLoader>( _pScope );
        if ( pHost != nullptr && pHost->loadTypeProviders() == false )
        {
            SW_LOG_ERROR_ID( "ModuleLoadStage.HostCouldNotLoad", "The host could not load its type-providing modules - scenes are not read" );
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

namespace sw
{
    // 서브시스템 자리 등록(Engine/Foundation/EngineSubsystemRegistration.h) — 만드는 순서는 자리 표가 정한다.
    SW_ENGINE_SUBSYSTEM( ModuleLoadStageSubsystem, []( SubsystemScope& scope, engine::EngineSubsystemBuild& build )
    {
        (void)build;
        (void)scope.emplaceSubsystem<ModuleLoadStageSubsystem>( &scope );
    } );
} // namespace sw

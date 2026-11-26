#include "pch.h"

#include "Engine/Module/ModuleLoadStageSubsystem.h"

#include "Engine/Config/ConfigManager.h"
#include "Engine/Foundation/EngineSubsystemRegistration.h"
#include "Engine/Foundation/EngineSubsystems.h"
#include "Engine/Localization/LocalizationManager.h"
#include "Engine/Module/HostInterfaces.h"
#include "Engine/Reflection/ReflectionSubsystem.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Scene/SceneManager.h"

namespace sw
{
    ModuleLoadStageSubsystem::ModuleLoadStageSubsystem( const ISubsystemLocator* pScope )
        : _pScope{ pScope }
    {
    }

    SubsystemInitResult ModuleLoadStageSubsystem::initializeSubsystem()
    {
        IModuleLoader* pHost = findSubsystemFrom<IModuleLoader>( _pScope );
        if ( pHost != nullptr && pHost->loadTypeProviders() == false )
        {
            SW_LOG_ERROR_ID( "ModuleLoadStage.HostCouldNotLoad", "The host could not load its type-providing modules - scenes are not read" );
            return SubsystemInitResult::Failed;
        }
        engine::getSubsystem<TypeRegistry>().markAllModuleTypesRegistered();
        // 올라온 키트의 현지화 프로젝트를 붙인다(언리얼 플러그인 현지화 타깃). 엔진 프로젝트를 올리는 단계와 순서가 바뀌어도 같은 결과다.
        if ( engine::getSubsystem<LocalizationManager>().syncModuleProjects() == false )
            return SubsystemInitResult::Failed;
        return SubsystemInitResult::Succeeded;
    }

    span<const utf8* const> ModuleLoadStageSubsystem::getSubsystemDependencies() const
    {
        // 현지화 관리자는 Shared 단계라 늘 먼저 서고 여기 적지 않는다(의존 목록은 같은 단계만 받는다).
        // 씬 관리자: 모듈 타입 등록이 진행 중인 씬 로드를 기다리고 로드된 씬의 틱 표를 다시 짓게 한다(`registerModuleTypes`) — 선 뒤에 올린다(초기화 의존 검사가 잡은 자리).
        static constexpr const utf8* kArrDependency[] = { ConfigManager::kSubsystemName, ModuleUnloadStageSubsystem::kSubsystemName,
                                                          ReflectionSubsystem::kSubsystemName, SceneManager::kSubsystemName };
        return kArrDependency;
    }
} // namespace sw

namespace sw
{
    SW_ENGINE_SUBSYSTEM( ModuleLoadStageSubsystem, Process, Startup, (), []( engine::EngineSubsystemFactoryScope& scope, engine::EngineSubsystemBuild& build )
    {
        (void)build;
        (void)scope.emplaceSubsystem<ModuleLoadStageSubsystem>( &scope.getScope() );
    } );
} // namespace sw

#include "pch.h"

#include "Engine/Module/ModuleUnloadStageSubsystem.h"

#include "Core/Compression/CompressionCodecRegistry.h"
#include "Core/File/AsyncFileIO.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Foundation/EngineSubsystemRegistration.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Module/HostInterfaces.h"
#include "Engine/Reflection/ReflectionSubsystem.h"
#include "Engine/Scene/SceneManager.h"

namespace sw
{
    ModuleUnloadStageSubsystem::ModuleUnloadStageSubsystem( const ISubsystemLocator* pScope )
        : _pScope{ pScope }
    {
    }

    void ModuleUnloadStageSubsystem::shutdownSubsystem()
    {
        IModuleLoader* pHost = locateSubsystem<IModuleLoader>( _pScope );
        if ( pHost != nullptr )
            pHost->onScenesReleased();
    }

    span<const utf8* const> ModuleUnloadStageSubsystem::getSubsystemDependencies() const
    {
        static constexpr const utf8* kArrDependency[] = { CompressionCodecRegistry::kSubsystemName, ReflectionSubsystem::kSubsystemName, AssetManager::kSubsystemName,
                                                          TaskManager::kSubsystemName };
        return kArrDependency;
    }

    span<const utf8* const> ModuleUnloadStageSubsystem::getSubsystemDependents() const
    {
        // 오디오는 스스로 이 뒤에 선다고 적는다(`IAudioService::getSubsystemDependencies` — 서버는 오디오를 짓지 않는다).
        static constexpr const utf8* kArrDependent[] = { AsyncFileIO::kSubsystemName, InputManager::kSubsystemName, SceneManager::kSubsystemName };
        return kArrDependent;
    }
} // namespace sw

namespace sw
{
    // 모듈 라이브러리 창구는 호스트 범위에 있다 — 두 단계가 자기 범위에서 부모로 올라가며 찾는다(호스트 범위가 없으면 올리고 내릴 모듈이 없다).
    SW_ENGINE_SUBSYSTEM( ModuleUnloadStageSubsystem, Startup, (), []( engine::EngineSubsystemFactoryScope& scope, engine::EngineSubsystemBuild& build )
    {
        (void)build;
        (void)scope.emplaceSubsystem<ModuleUnloadStageSubsystem>( &scope.getScope() );
    } );
} // namespace sw

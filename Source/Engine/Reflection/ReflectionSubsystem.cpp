#include "pch.h"

#include "Engine/Reflection/ReflectionSubsystem.h"

#include "Engine/Config/ConfigManager.h"
#include "Engine/Foundation/EngineSubsystemRegistration.h"
#include "Engine/Module/ModuleTypeRegistry.h"
#include "Engine/Reflection/TypeRegistry.h"

namespace sw
{
    ReflectionSubsystem::ReflectionSubsystem( unique_ptr<TypeRegistry> typeRegistry )
        : _typeRegistry{ std::move( typeRegistry ) }
    {
    }

    ReflectionSubsystem::~ReflectionSubsystem() = default;

    SubsystemInitResult ReflectionSubsystem::initializeSubsystem()
    {
        engine::registerHostImageTypes();
        return bindEnumNames();
    }

    void ReflectionSubsystem::shutdownSubsystem()
    {
        engine::unbindGlobalVariableEnumNames();
    }

    span<const utf8* const> ReflectionSubsystem::getSubsystemDependents() const
    {
        static constexpr const utf8* kArrDependent[] = { ConfigManager::kSubsystemName }; // 엔진 기본 에셋(위 층)은 자기 의존 칸에 리플렉션을 적는다
        return kArrDependent;
    }

    SubsystemInitResult ReflectionSubsystem::bindEnumNames()
    {
        if ( engine::bindGlobalVariableEnumNames() )
            return SubsystemInitResult::Succeeded;
        engine::unbindGlobalVariableEnumNames();
        return SubsystemInitResult::Failed;
    }
} // namespace sw

namespace sw
{
    // 서브시스템 자리 등록(Engine/Foundation/EngineSubsystemRegistration.h) — 만드는 순서는 자리 표가 정한다.
    // 타입 표 — 하네스가 대역을 넘기지 않았으면 엔진의 표를 만든다.
    SW_ENGINE_SUBSYSTEM( ReflectionSubsystem, []( SubsystemScope& scope, engine::EngineSubsystemBuild& build )
    {
        SW_MEMORY_SCOPE( Reflection );
        engine::StartupSetup& setup = *build._pStartupSetup;
        if ( setup._reflection == nullptr )
            setup._reflection = sw::make_unique<ReflectionSubsystem>( sw::make_unique<TypeRegistry>() );
        const ReflectionSubsystem& reflection = scope.addSubsystem( std::move( setup._reflection ) );
        (void)scope.bind( &reflection.getTypeRegistry() ); // 새 범위 — 겹치지 않는다
    } );
} // namespace sw

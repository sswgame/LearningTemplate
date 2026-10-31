#include "pch.h"

#include "Engine/Reflection/ReflectionSubsystem.h"

#include "Engine/Config/ConfigManager.h"
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

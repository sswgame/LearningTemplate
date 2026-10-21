#include "pch.h"

#include "Engine/Bootstrap/StaticStorageSubsystem.h"

#include "Core/Memory/LifetimeScope.h"

#include "Engine/Bootstrap/ContentRootSubsystem.h"
#include "Engine/Graphics/RHI/RHIRenderResource.h"

namespace sw
{
    SW_LOG_CALLER( "StaticStorageSubsystem" );

    void StaticStorageSubsystem::shutdownSubsystem()
    {
        // 남은 렌더 리소스는 누수다(레지스트리 저장소는 마지막 리소스가 사라질 때 스스로 돌아간다 — 남았으면 돌아가지 않은 채로 보고에 보인다).
        const uint32 registeredResourceCount = RHIRenderResource::getRegisteredCount();
        if ( registeredResourceCount != 0 )
            SW_LOG_WARNING( "%# RHI render resources are still registered at shutdown - a mesh, texture or material leaked", registeredResourceCount );
        // 엔진 수명 범위를 닫는다 — 그 범위가 소유한 저장소(`ScopeOwned`)가 만든 역순으로 소멸한다. 레지스트리 용량은 등록 해제가 돌려준다(`RegistrationList::remove`).
        LifetimeScope::getGlobal().close();
    }

    span<const utf8* const> StaticStorageSubsystem::getSubsystemDependencies() const
    {
        static constexpr const utf8* kArrDependency[] = { ContentRootSubsystem::kSubsystemName };
        return kArrDependency;
    }
} // namespace sw

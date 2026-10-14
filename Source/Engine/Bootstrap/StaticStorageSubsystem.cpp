#include "pch.h"

#include "Engine/Bootstrap/StaticStorageSubsystem.h"

#include "Core/Console/DevCommandRegistry.h"
#include "Core/Memory/LifetimeScope.h"
#include "Core/Module/ModuleUnloadListener.h"

#include "Engine/Automation/AutomationProbe.h"
#include "Engine/Automation/AutomationStepRegistry.h"
#include "Engine/Bootstrap/ContentRootSubsystem.h"
#include "Engine/Graphics/RHI/RHIRenderResource.h"
#include "Engine/Object/Component/SceneTransformStorage.h"
#include "Engine/Resource/ResourcePaths.h"

namespace sw
{
    SW_LOG_CALLER( "StaticStorageSubsystem" );

    void StaticStorageSubsystem::shutdownSubsystem()
    {
        SceneTransformStorage& transformStorage = SceneTransformStorage::get();
        if ( transformStorage.releaseStorage() == false )
            SW_LOG_WARNING( "Scene transform storage still has %# live slots at shutdown - a scene component leaked", transformStorage.getLiveSlotCount() );
        ResourcePaths::clearPathCache();
        if ( RHIRenderResource::releaseRegistryStorage() == false )
            SW_LOG_WARNING( "RHI render resources are still registered at shutdown - a mesh, texture or material leaked" );
        // 엔진 수명 범위를 닫는다 — 그 범위가 소유한 저장소(`ScopeOwned`: 번역 문자열 · 활성 2D 렌더 설정 · 에셋 로드 기록부)가 만든 역순으로 소멸한다.
        LifetimeScope::getGlobal().close();
        // 레지스트리는 모듈이 모두 내려가 엔진 것만 남았다 — 기동 뒤 모듈이 키운 용량을 놓는다.
        IModuleUnloadListener::releaseUnusedStorage();
        AutomationStepRegistry::releaseUnusedStorage();
        AutomationProbes::releaseUnusedStorage();
#if SW_DEV_COMMANDS_ENABLED
        DevCommandRegistry::get().releaseUnusedStorage();
#endif
    }

    span<const utf8* const> StaticStorageSubsystem::getSubsystemDependencies() const
    {
        static constexpr const utf8* kArrDependency[] = { ContentRootSubsystem::kSubsystemName };
        return kArrDependency;
    }
} // namespace sw

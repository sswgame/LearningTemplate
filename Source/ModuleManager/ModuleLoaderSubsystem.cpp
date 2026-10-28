#include "pch.h"

#include "ModuleManager/ModuleLoaderSubsystem.h"

#include "Core/Memory/Memory.h"
#include "Core/Subsystem/SubsystemScope.h"

namespace sw
{
    ModuleLoaderSubsystem::ModuleLoaderSubsystem( ModuleTypeLoaderDelegate moduleTypeLoader )
        : _moduleTypeLoader{ std::move( moduleTypeLoader ) }
        , _scenesReleasedHook{}
    {
    }

    Subscription ModuleLoaderSubsystem::bindScenesReleasedHook( Delegate<void()> hook )
    {
        return _scenesReleasedHook.bind( std::move( hook ) );
    }

    bool ModuleLoaderSubsystem::loadTypeProviders()
    {
        return _moduleTypeLoader.isBound() == false || _moduleTypeLoader();
    }

    void ModuleLoaderSubsystem::onScenesReleased()
    {
        if ( _scenesReleasedHook.isBound() )
            _scenesReleasedHook.get()();
    }

    ModuleLoaderSubsystem* ModuleLoaderSubsystem::initializeHostScope( SubsystemScope& hostScope, ModuleTypeLoaderDelegate moduleTypeLoader, const utf8* pHostName )
    {
        SW_MEMORY_SCOPE( EngineMisc );
        ModuleLoaderSubsystem& moduleLibraryHost = hostScope.addSubsystem( make_unique<ModuleLoaderSubsystem>( std::move( moduleTypeLoader ) ) );
        const bool             bExposed          = hostScope.expose<IModuleLoader>( moduleLibraryHost );
        SW_ASSERT( bExposed );
        (void)bExposed;
        // 이 범위의 서브시스템은 초기화할 일이 없다(창구는 델리게이트만 들고, 자동화 구동기는 엔진이 선 뒤 `begin` 으로 시작한다) — 로거 앞이라 낮은 층 보고(stderr 에 동기로)로 알린다.
        if ( hostScope.getCollection().initializeAll( SubsystemHostMask::All ) == false )
        {
            SW_LOW_LEVEL_ERROR( "[%s] host scope failed: %s", pHostName, hostScope.getCollection().getError().c_str() );
            return nullptr;
        }
        return &moduleLibraryHost;
    }
} // namespace sw

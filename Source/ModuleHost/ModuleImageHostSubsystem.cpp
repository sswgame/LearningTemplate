#include "pch.h"

#include "ModuleHost/ModuleImageHostSubsystem.h"

#include "Core/Memory/Memory.h"
#include "Core/Subsystem/SubsystemScope.h"

#include "Engine/Automation/AutomationSession.h"

namespace sw
{
    ModuleImageHostSubsystem::ModuleImageHostSubsystem( ModuleTypeLoaderDelegate moduleTypeLoader )
        : _moduleTypeLoader{ std::move( moduleTypeLoader ) }
        , _scenesReleasedHook{}
    {
    }

    Subscription ModuleImageHostSubsystem::bindScenesReleasedHook( Delegate<void()> hook )
    {
        return _scenesReleasedHook.bind( std::move( hook ) );
    }

    bool ModuleImageHostSubsystem::loadTypeProviders()
    {
        return _moduleTypeLoader.isBound() == false || _moduleTypeLoader();
    }

    void ModuleImageHostSubsystem::onScenesReleased()
    {
        if ( _scenesReleasedHook.isBound() )
            _scenesReleasedHook.get()();
    }

    ModuleImageHostSubsystem* ModuleImageHostSubsystem::initializeHostScope( SubsystemScope& hostScope, ModuleTypeLoaderDelegate moduleTypeLoader, const utf8* pHostName )
    {
        SW_MEMORY_SCOPE( EngineMisc );
        ModuleImageHostSubsystem& moduleImageHost = hostScope.addSubsystem( make_unique<ModuleImageHostSubsystem>( std::move( moduleTypeLoader ) ) );
        const bool                bExposed        = hostScope.expose<IModuleImageHost>( moduleImageHost );
        SW_ASSERT( bExposed );
        (void)bExposed;
        // 이 범위의 서브시스템은 초기화할 일이 없다(창구는 델리게이트만 들고, 자동화 구동기는 엔진이 선 뒤 `begin` 으로 시작한다) — 로거 앞이라 표준 오류로 알린다.
        if ( hostScope.getCollection().initializeAll( SubsystemHostMask::All ) == false )
        {
            std::fprintf( stderr, "[%s] host scope failed: %s\n", pHostName, hostScope.getCollection().getError().c_str() );
            return nullptr;
        }
        return &moduleImageHost;
    }

    span<const utf8* const> ModuleImageHostSubsystem::getSubsystemDependents() const
    {
        static constexpr const utf8* kArrDependent[] = { AutomationSession::kSubsystemName };
        return kArrDependent;
    }
} // namespace sw

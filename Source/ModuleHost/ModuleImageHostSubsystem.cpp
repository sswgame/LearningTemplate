#include "pch.h"

#include "ModuleHost/ModuleImageHostSubsystem.h"

#include "Engine/Automation/AutomationSession.h"

namespace sw
{
    ModuleImageHostSubsystem::ModuleImageHostSubsystem( ModuleTypeLoaderDelegate moduleTypeLoader )
        : _moduleTypeLoader{ std::move( moduleTypeLoader ) }
        , _scenesReleasedHook{}
    {
    }

    void ModuleImageHostSubsystem::setScenesReleasedHook( Delegate<void()> hook )
    {
        _scenesReleasedHook = std::move( hook );
    }

    bool ModuleImageHostSubsystem::loadTypeProviders()
    {
        return _moduleTypeLoader.isBound() == false || _moduleTypeLoader();
    }

    void ModuleImageHostSubsystem::onScenesReleased()
    {
        if ( _scenesReleasedHook.isBound() )
            _scenesReleasedHook();
    }

    span<const utf8* const> ModuleImageHostSubsystem::getSubsystemDependents() const
    {
        static constexpr const utf8* kArrDependent[] = { AutomationSession::kSubsystemName };
        return kArrDependent;
    }
} // namespace sw

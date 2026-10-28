#include "pch.h"

#include "Engine/Module/ModuleImagesSubsystem.h"

#include "Core/Compression/CompressionCodecRegistry.h"
#include "Core/File/AsyncFileIO.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Audio/IAudioService.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Module/ModuleImageHost.h"
#include "Engine/Reflection/ReflectionSubsystem.h"
#include "Engine/Scene/SceneManager.h"

namespace sw
{
    ModuleImagesSubsystem::ModuleImagesSubsystem( const ISubsystemLocator* pScope )
        : _pScope{ pScope }
    {
    }

    void ModuleImagesSubsystem::shutdownSubsystem()
    {
        IModuleImageHost* pHost = locateSubsystem<IModuleImageHost>( _pScope );
        if ( pHost != nullptr )
            pHost->onScenesReleased();
    }

    span<const utf8* const> ModuleImagesSubsystem::getSubsystemDependencies() const
    {
        static constexpr const utf8* kArrDependency[] = { CompressionCodecRegistry::kSubsystemName, ReflectionSubsystem::kSubsystemName, AssetManager::kSubsystemName,
                                                          TaskManager::kSubsystemName };
        return kArrDependency;
    }

    span<const utf8* const> ModuleImagesSubsystem::getSubsystemDependents() const
    {
        static constexpr const utf8* kArrDependent[] = { AsyncFileIO::kSubsystemName, IAudioService::kSubsystemName, InputManager::kSubsystemName, SceneManager::kSubsystemName };
        return kArrDependent;
    }
} // namespace sw

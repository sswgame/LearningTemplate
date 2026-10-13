#include "pch.h"

#include "Engine/Module/ModuleImagesSubsystem.h"

#include "Core/Compression/CompressionCodecRegistry.h"
#include "Core/File/AsyncFileIO.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Audio/IAudioService.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Module/ReflectionSubsystem.h"
#include "Engine/Resource/AssetManager.h"
#include "Engine/Scene/SceneManager.h"

namespace sw
{
    ModuleImagesSubsystem::ModuleImagesSubsystem( const Delegate<void()>* pOnScenesReleased )
        : _pOnScenesReleased{ pOnScenesReleased }
    {
    }

    void ModuleImagesSubsystem::shutdownSubsystem()
    {
        if ( _pOnScenesReleased != nullptr && _pOnScenesReleased->isBound() )
            ( *_pOnScenesReleased )();
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

#include "pch.h"

#include "Engine/Object/GameObject/SceneEngineServices.h"

#include "Engine/Common/EngineServices.h"

namespace sw
{
    SceneEngineServices SceneEngineServices::makeFromBoundEngine()
    {
        SceneEngineServices sceneServices{};
        sceneServices._pAssetManager     = engine::findSubsystem<AssetManager>();
        sceneServices._pPhysicsSystem    = engine::findSubsystem<PhysicsSystem>();
        sceneServices._pAudioService     = engine::findSubsystem<IAudioService>();
        sceneServices._pDefaultAssets    = engine::findSubsystem<EngineDefaultAssets>();
        sceneServices._pTaskManager      = engine::findSubsystem<TaskManager>();
        sceneServices._pTypeRegistry     = engine::findSubsystem<TypeRegistry>();
        sceneServices._pEngineSubsystems = engine::getEngineSubsystems();
        return sceneServices;
    }
} // namespace sw

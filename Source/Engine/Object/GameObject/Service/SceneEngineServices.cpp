#include "pch.h"

#include "Engine/Object/GameObject/Service/SceneEngineServices.h"

#include "Engine/Foundation/EngineServices.h"

namespace sw
{
    SceneEngineServices SceneEngineServices::makeFromBoundEngine()
    {
        SceneEngineServices sceneServices{};
        sceneServices._pAssetManager      = engine::findSubsystem<AssetManager>();
        sceneServices._pPhysicsSystem     = engine::findSubsystem<PhysicsSystem>();
        sceneServices._pAudioService      = engine::findSubsystem<IAudioService>();
        sceneServices._pDefaultAssets     = engine::findSubsystem<EngineDefaultAssets>();
        sceneServices._pTaskManager       = engine::findSubsystem<TaskManager>();
        sceneServices._pTypeRegistry      = engine::findSubsystem<TypeRegistry>();
        sceneServices._pTickBatchRegistry = engine::findSubsystem<TickBatchRegistry>();
        sceneServices._pEngineSubsystems  = engine::getEngineSubsystems();
        return sceneServices;
    }
} // namespace sw

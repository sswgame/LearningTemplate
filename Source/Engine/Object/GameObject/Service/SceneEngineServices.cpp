#include "pch.h"

#include "Engine/Object/GameObject/Service/SceneEngineServices.h"

#include "Core/Task/TaskManager.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Object/GameObject/Tick/TickBatchRegistry.h"
#include "Engine/Reflection/TypeRegistry.h"

namespace sw
{
    SceneEngineServices SceneEngineServices::makeFromBoundEngine()
    {
        SceneEngineServices sceneServices{};
        sceneServices._pAssetManager      = engine::findSubsystem<AssetManager>();
        sceneServices._pDefaultAssets     = engine::findSubsystem<EngineDefaultAssets>();
        sceneServices._pTaskManager       = engine::findSubsystem<TaskManager>();
        sceneServices._pTypeRegistry      = engine::findSubsystem<TypeRegistry>();
        sceneServices._pTickBatchRegistry = engine::findSubsystem<TickBatchRegistry>();
        sceneServices._pEngineSubsystems  = engine::getEngineSubsystems();
        return sceneServices;
    }
} // namespace sw

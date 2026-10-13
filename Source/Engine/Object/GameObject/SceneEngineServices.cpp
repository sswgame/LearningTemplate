#include "pch.h"

#include "Engine/Object/GameObject/SceneEngineServices.h"

#include "Engine/Common/EngineServices.h"

namespace sw
{
    SceneEngineServices SceneEngineServices::makeFromEngineServices( const EngineServices& services )
    {
        SceneEngineServices sceneServices{};
        sceneServices._pAssetManager  = services._pAssetManager;
        sceneServices._pPhysicsSystem = services._pPhysicsSystem;
        sceneServices._pAudioService  = services._pAudioService;
        sceneServices._pDefaultAssets = services._pEngineDefaultAssets;
        sceneServices._pTaskManager   = services._pTaskManager;
        sceneServices._pTypeRegistry  = services._pTypeRegistry;
        return sceneServices;
    }

    SceneEngineServices SceneEngineServices::makeFromBoundEngine()
    {
        SceneEngineServices sceneServices = makeFromEngineServices( engine::getBoundEngineServices() );
        sceneServices._pEngineSubsystems  = engine::getEngineSubsystems();
        return sceneServices;
    }
} // namespace sw

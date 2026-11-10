#include "pch.h"

#include "GameFramework/Runtime/Foundation/Framework/GameplayDebugTarget.h"

#include "Engine/Console/DebugHUDRegistry.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Scene/SceneManager.h"

#include "GameFramework/Runtime/Foundation/Framework/GameService.h"

namespace sw
{
    GameObjectManager* GameplayDebugTarget::findActiveObjectManager()
    {
        SceneManager* const pSceneManager = game::findSubsystem<SceneManager>();
        return pSceneManager != nullptr ? pSceneManager->findActiveObjectManager() : nullptr;
    }

    string GameplayDebugTarget::getTargetName()
    {
        const DebugHUDRegistry* const pRegistry = engine::findSubsystem<DebugHUDRegistry>();
        return pRegistry != nullptr ? pRegistry->getTargetName() : string{}; // 엔진 서비스가 없는 실행(테스트)은 대상이 없다
    }

    void GameplayDebugTarget::setTargetName( string_view name )
    {
        if ( DebugHUDRegistry* const pRegistry = engine::findSubsystem<DebugHUDRegistry>() )
            pRegistry->setTargetName( name );
    }
} // namespace sw

#include "pch.h"

#include "GameFramework/Base/World/Lifetime/PersistAcrossScenesComponent.h"

#include "Engine/Scene/SceneManager.h"

#include "GameFramework/Base/Foundation/Framework/GameService.h"

namespace sw
{
    PersistAcrossScenesComponent::PersistAcrossScenesComponent()
        : _bPersistent{ true }
    {
    }

    void PersistAcrossScenesComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        setTickGroup( TickGroup::PrePhysics );

        GameObject* pOwner = getOwner();
        if ( pOwner == nullptr )
            return;
        // 씬 매니저가 루트를 씬 전환 너머로 옮겨 심는다(유니티 `Object.PersistAcrossScenes` — 태그를 붙이지 않는다).
        SceneManager* pSceneManager = game::findSubsystem<SceneManager>();
        if ( _bPersistent && pSceneManager != nullptr )
            pSceneManager->markPersistent( pOwner );
    }
} // namespace sw

#include "pch.h"

#include "GameFramework/Base/DontDestroyOnLoadComponent.h"

#include "Engine/Scene/SceneManager.h"

#include "GameFramework/Base/GameService.h"

namespace sw
{
    DontDestroyOnLoadComponent::DontDestroyOnLoadComponent()
        : _persistentTag{ "Persistent" }
        , _bPersistent{ true }
    {
    }

    void DontDestroyOnLoadComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        setTickGroup( TickGroup::PrePhysics );

        GameObject* pOwner = getOwner();
        if ( pOwner == nullptr )
            return;
        // 씬 매니저가 루트를 씬 전환 너머로 옮겨 심는다(유니티 `Object.DontDestroyOnLoad` — 태그를 붙이지 않는다).
        SceneManager* pSceneManager = game::areGameServicesBound() ? game::getService<SceneManager>() : nullptr;
        if ( _bPersistent && pSceneManager != nullptr )
            pSceneManager->markPersistent( pOwner );
    }
} // namespace sw

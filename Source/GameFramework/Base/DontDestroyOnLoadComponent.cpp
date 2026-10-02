#include "pch.h"

#include "GameFramework/Base/DontDestroyOnLoadComponent.h"

#include "Engine/Object/Component/TagSystem.h"
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
        pOwner->addTag( "DontDestroyOnLoad"_tag );
        // 예전에는 태그만 붙였고 그 태그를 읽는 곳이 없어 씬을 바꾸면 같이 사라졌다. 씬 매니저가 루트를 전환 너머로 옮겨 심는다.
        SceneManager* pSceneManager = game::areGameServicesBound() ? game::getService<SceneManager>() : nullptr;
        if ( _bPersistent && pSceneManager != nullptr )
            pSceneManager->markPersistent( pOwner );
    }

    void DontDestroyOnLoadComponent::onEndPlay()
    {
        Component::onEndPlay();
    }
} // namespace sw

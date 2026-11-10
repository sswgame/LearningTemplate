#include "pch.h"

#include "Engine/Object/GameObject/Service/SceneNetLink.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"

namespace sw
{
    SW_SCENE_SERVICE( SceneNetLink, FlushEnd, SceneFrameAccess{} )

    SceneNetLink::SceneNetLink( GameObjectManager& manager )
        : _value{}
    {
        (void)manager;
    }

    bool SceneNetLink::bind( const NetLinkValue& value )
    {
        if ( value.isComplete() == false )
        {
            SW_LOG_ERROR_ID( "SceneNetLink.SceneNetLinkNeeds", "Scene net link needs both the peers and the router - the link stays unbound" );
            return false;
        }
        _value = value;
        return true;
    }

    const SceneNetLink* SceneNetLink::findFor( const Component& component )
    {
        const GameObject*  pOwner   = component.getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        return pManager != nullptr ? &pManager->getSubsystem<SceneNetLink>() : nullptr;
    }
} // namespace sw

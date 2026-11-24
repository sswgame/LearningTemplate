#include "pch.h"

#include "Engine/Object/GameObject/Service/SceneNetLink.h"

#include "Core/Network/Message/NetMessage.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Service/SceneServiceRegistry.h"

namespace sw
{
    SW_SCENE_SERVICE( SceneNetLink, FlushEnd, SceneFrameAccess{} )

    bool NetLinkValue::isComplete() const
    {
        return _pPeers != nullptr && _pRouter != nullptr && _pRouter->getSessionSerial() == _sessionSerial;
    }

    SceneNetLink::SceneNetLink( GameObjectManager& manager )
        : _value{}
    {
        (void)manager;
    }

    bool SceneNetLink::bind( const NetLinkValue& value )
    {
        if ( value.isComplete() == false )
        {
            SW_LOG_ERROR_ID( "SceneNetLink.SceneNetLinkNeeds", "Scene net link needs both the peers and the router of an open session - the link stays unbound" );
            return false;
        }
        _value = value;
        return true;
    }

    const NetLinkValue* SceneNetLink::findBoundFor( const Component& component )
    {
        const GameObject*        pOwner   = component.getOwner();
        const GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        const SceneNetLink*      pLink    = pManager != nullptr ? &pManager->getSubsystem<SceneNetLink>() : nullptr;
        return pLink != nullptr && pLink->isBound() ? &pLink->_value : nullptr;
    }
} // namespace sw

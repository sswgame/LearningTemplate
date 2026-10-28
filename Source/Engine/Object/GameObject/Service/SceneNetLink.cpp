#include "pch.h"

#include "Engine/Object/GameObject/Service/SceneNetLink.h"

#include "Engine/Object/GameObject/GameObjectManager.h"

namespace sw
{
    SW_LOG_CALLER( "SceneNetLink" );

    SW_SCENE_SERVICE( SceneNetLink, FlushEnd )

    SceneNetLink::SceneNetLink( GameObjectManager& manager )
        : _value{}
    {
        (void)manager;
    }

    bool SceneNetLink::bind( const NetLinkValue& value )
    {
        if ( value.isComplete() == false )
        {
            SW_LOG_ERROR( "Scene net link needs both the peers and the router - the link stays unbound" );
            return false;
        }
        _value = value;
        return true;
    }
} // namespace sw

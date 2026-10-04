#include "pch.h"

#include "Engine/Object/Component/Navigation/NavMeshSurfaceComponent.h"

#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/SceneNavigation.h"

namespace sw
{
    NavMeshSurfaceComponent::NavMeshSurfaceComponent()
        : _listAgentType{}
        , _geometrySource{ NavGeometrySource::RenderMeshes }
        , _listExcludeTag{}
        , _boundsHalfExtents{ 0.0f, 0.0f, 0.0f }
    {
    }

    void NavMeshSurfaceComponent::onRegister( GameObjectManager& manager )
    {
        Component::onRegister( manager );
        manager.getSceneNavigation().registerSurface( this );
    }

    void NavMeshSurfaceComponent::onUnregister( GameObjectManager& manager )
    {
        manager.getSceneNavigation().unregisterSurface( this );
        Component::onUnregister( manager );
    }

    bool NavMeshSurfaceComponent::coversAgentType( const hashed_string& agentType, const hashed_string& defaultAgentType ) const
    {
        if ( _listAgentType.empty() )
            return agentType.empty() || agentType == defaultAgentType;
        for ( const hashed_string& covered : _listAgentType )
        {
            if ( covered == agentType || ( agentType.empty() && covered == defaultAgentType ) )
                return true;
        }
        return false;
    }
} // namespace sw

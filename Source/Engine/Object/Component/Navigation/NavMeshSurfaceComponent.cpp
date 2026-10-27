#include "pch.h"

#include "Engine/Object/Component/Navigation/NavMeshSurfaceComponent.h"

#include "Core/Container/VectorUtil.h"

#include "Engine/Object/Component/Navigation/SceneNavigation.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

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
        _navRegistration = manager.getSubsystem<SceneNavigation>().registerSurface( this );
    }

    void NavMeshSurfaceComponent::onUnregister( GameObjectManager& manager )
    {
        _navRegistration.reset();
        Component::onUnregister( manager );
    }

    bool NavMeshSurfaceComponent::coversAgentType( const hashed_string& agentType, const hashed_string& defaultAgentType ) const
    {
        if ( _listAgentType.empty() )
            return agentType.empty() || agentType == defaultAgentType;
        return VectorUtil::containsIf( _listAgentType, [&]( const hashed_string& covered )
        { return covered == agentType || ( agentType.empty() && covered == defaultAgentType ); } );
    }
} // namespace sw

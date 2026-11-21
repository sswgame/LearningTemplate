#include "pch.h"

#include "Engine/Environment/SceneEnvironment.h"

#include "Core/Log/Logger.h"

#include "Engine/Environment/Fog/FogComponent.h"
#include "Engine/Environment/Sky/SkyComponent.h"
#include "Engine/Object/GameObject/Service/SceneServiceRegistry.h"

namespace sw
{
    SW_SCENE_SERVICE( SceneEnvironment, FlushEnd, makeSceneFrameAccess( SceneDataKindSet{}, SceneDataKindSet{}, SceneDataKindSet{ SceneDataKind::Structure } ) )

    SceneEnvironment::SceneEnvironment( GameObjectManager& manager )
        : _manager{ manager }
        , _skySource{ SW_DELEGATE_METHOD( SkySourceDelegate, &SceneEnvironment::findComponentSky, this ) }
        , _fogSource{ SW_DELEGATE_METHOD( FogSourceDelegate, &SceneEnvironment::findComponentFog, this ) }
        , _surfaceDeform{}
        , _bHasSurfaceDeform{ SW_FALSE }
    {
    }

    void SceneEnvironment::onSceneCleared()
    {
        clearSurfaceDeform();
    }

    bool SceneEnvironment::bindSkySource( const SkySourceDelegate& source )
    {
        if ( _skySource.isBound() )
        {
            SW_LOG_ERROR_ID( "Environment.SkySourceAlreadyBound", "The sky slot of this scene is already bound - unbind it first (one owner per scene)" );
            return false;
        }
        _skySource = source;
        return true;
    }

    bool SceneEnvironment::bindFogSource( const FogSourceDelegate& source )
    {
        if ( _fogSource.isBound() )
        {
            SW_LOG_ERROR_ID( "Environment.FogSourceAlreadyBound", "The fog slot of this scene is already bound - unbind it first (one owner per scene)" );
            return false;
        }
        _fogSource = source;
        return true;
    }

    void SceneEnvironment::setSurfaceDeform( const SurfaceDeformBinding& binding )
    {
        _surfaceDeform     = binding;
        _bHasSurfaceDeform = SW_TRUE;
    }

    void SceneEnvironment::clearSurfaceDeform()
    {
        _surfaceDeform     = SurfaceDeformBinding{};
        _bHasSurfaceDeform = SW_FALSE;
    }

    bool SceneEnvironment::findSky( SkyGradient& outGradient ) const
    {
        return _skySource.isBound() && _skySource( outGradient );
    }

    bool SceneEnvironment::findFog( const float3& viewPosition, FogSettings& outSettings ) const
    {
        return _fogSource.isBound() && _fogSource( viewPosition, outSettings );
    }

    bool SceneEnvironment::findSurfaceDeform( SurfaceDeformBinding& outBinding ) const
    {
        outBinding = _surfaceDeform;
        return _bHasSurfaceDeform == SW_TRUE;
    }

    bool SceneEnvironment::findComponentSky( SkyGradient& outGradient ) const
    {
        return SkyComponent::findSky( _manager, outGradient );
    }

    bool SceneEnvironment::findComponentFog( const float3& viewPosition, FogSettings& outSettings ) const
    {
        return FogComponent::findFog( _manager, viewPosition, outSettings );
    }
} // namespace sw

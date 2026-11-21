#include "pch.h"

#include "Engine/Environment/SceneEnvironment.h"

#include "Engine/Environment/Fog/FogComponent.h"
#include "Engine/Environment/Sky/SkyComponent.h"
#include "Engine/Object/GameObject/Service/SceneServiceRegistry.h"

namespace sw
{
    SW_SCENE_SERVICE( SceneEnvironment, FlushEnd, makeSceneFrameAccess( SceneDataKindSet{}, SceneDataKindSet{}, SceneDataKindSet{ SceneDataKind::Structure } ) )

    SceneEnvironment::SceneEnvironment( GameObjectManager& manager )
        : _manager{ manager }

        , _surfaceDeform{}
        , _bHasSurfaceDeform{ SW_FALSE }
    {
    }

    void SceneEnvironment::onSceneCleared()
    {
        clearSurfaceDeform();
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
        return SkyComponent::findSky( _manager, outGradient );
    }

    bool SceneEnvironment::findFog( const float3& viewPosition, FogSettings& outSettings ) const
    {
        return FogComponent::findFog( _manager, viewPosition, outSettings );
    }

    bool SceneEnvironment::findSurfaceDeform( SurfaceDeformBinding& outBinding ) const
    {
        outBinding = _surfaceDeform;
        return _bHasSurfaceDeform == SW_TRUE;
    }

} // namespace sw

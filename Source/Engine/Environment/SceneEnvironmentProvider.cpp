#include "pch.h"

#include "Engine/Environment/SceneEnvironmentProvider.h"

#include "Engine/Environment/Fog/FogComponent.h"
#include "Engine/Environment/Sky/SkyComponent.h"

namespace sw
{
    SW_SCENE_SERVICE( SceneEnvironmentProvider, FlushEnd, makeSceneFrameAccess( SceneDataKindSet{}, SceneDataKindSet{}, SceneDataKindSet{ SceneDataKind::Structure } ) )

    SceneEnvironmentProvider::SceneEnvironmentProvider( GameObjectManager& manager )
        : _manager{ manager }
    {
    }

    bool SceneEnvironmentProvider::contributeSky( SkyGradient& outGradient ) const
    {
        return SkyComponent::findSky( _manager, outGradient );
    }

    bool SceneEnvironmentProvider::contributeFog( const float3& viewPosition, FogSettings& outSettings ) const
    {
        // 카메라가 물속이면 물의 안개가 컴포넌트의 값을 덮는다(`FogComponent::findFog`).
        return FogComponent::findFog( _manager, viewPosition, outSettings );
    }
} // namespace sw

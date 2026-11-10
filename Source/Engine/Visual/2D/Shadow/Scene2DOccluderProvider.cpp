#include "pch.h"

#include "Engine/Visual/2D/Shadow/Scene2DOccluderProvider.h"

#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Visual/2D/Shadow/ShadowCaster2DComponent.h"

namespace sw
{
    SW_SCENE_SERVICE( Scene2DOccluderProvider, FlushEnd, makeSceneFrameAccess( SceneDataKindSet{}, SceneDataKindSet{}, SceneDataKindSet{ SceneDataKind::Structure, SceneDataKind::Transform } ) )

    Scene2DOccluderProvider::Scene2DOccluderProvider( GameObjectManager& manager )
        : _manager{ manager }
    {
    }

    void Scene2DOccluderProvider::contributeLights( const SceneLightPhase phase, vector<GPULight>& inoutList ) const
    {
        if ( phase != SceneLightPhase::ShadowOccluders )
            return;
        // 2D 그림자 가림막 세그먼트는 빛 **뒤에** 붙는다. 상한을 넘으면 뒤에서부터 잘리므로 빛보다 가림막이 먼저 빠진다(lighting_2d.hlsli).
        for ( const ShadowCaster2DComponent* pCaster : _manager.getComponentRegistry().getAll<ShadowCaster2DComponent>() )
        {
            if ( pCaster != nullptr && pCaster->isActive() )
                pCaster->appendGPUShadowSegments( inoutList );
        }
    }
} // namespace sw

#include "pch.h"

#include "Engine/Renderer/Frame/Packet/RenderPacketSceneFill.h"

#include "Engine/Animation/Scene/AnimationSystem.h"
#include "Engine/Environment/Fog/FogComponent.h"
#include "Engine/Environment/Sky/SkyComponent.h"
#include "Engine/Graphics/Upload/GPUUploadQueue.h"
#include "Engine/Object/Component/3D/MeshLODSystem.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Renderer/Frame/FrameRenderer.h"
#include "Engine/Renderer/Frame/Packet/RenderFramePacket.h"
#include "Engine/Renderer/Frame/Packet/RenderPacketSceneCollector.h"
#include "Engine/Renderer/Lighting/GPULightBuffer.h"
#include "Engine/Renderer/Lighting/ShadowSetup.h"
#include "Engine/Renderer/Scene/GPUSceneBuilder.h"
#include "Engine/Scene/Scene.h"

namespace sw
{
    void RenderPacketSceneFill::fill( RenderFramePacket& outPacket, Scene& scene, const RenderSceneViewDesc& view, GPUSceneBuilder& builder, GPUUploadQueue* pUploadQueue )
    {
        outPacket._bValid             = SW_TRUE;
        outPacket._outputRenderTarget = view._outputRenderTarget;
        outPacket._viewportWidth      = view._width;
        outPacket._viewportHeight     = view._height;
        outPacket._mainView           = view._settings;
        outPacket.setMainView( view._viewProj, view._cameraPos );

        // 주 패킷과 같은 빛 규칙이다(`RenderPacketSceneCollector::collectLights`) — 렌더 스레드는 씬을 볼 수 없다.
        const RenderPacketKeyLights lights = RenderPacketSceneCollector::writeKeyLight( outPacket, scene );
        collectSceneLights( &scene, outPacket._listLight, &_listShadowLightIndex, &outPacket._listLightProbe, &outPacket._listLightCookie, &outPacket._listRealtimeProbe );
        const GameObjectManager* pObjects = scene.getObjectManager();
        outPacket._bHasSky                = ( pObjects != nullptr && SkyComponent::findSky( *pObjects, outPacket._sky ) ) ? SW_TRUE : SW_FALSE;
        outPacket._bHasFog                = ( pObjects != nullptr && FogComponent::findFog( *pObjects, outPacket._cameraPos, outPacket._fog ) ) ? SW_TRUE : SW_FALSE;
        ShadowSetupUtil::populateFrame( lights.getShadowCaster(), &outPacket._viewProj, FrameRenderer::getShadowMapResolution(), _listShadowLightIndex, outPacket._listLight,
                                        outPacket._shadow );

        // 이 씬의 애니메이션 LOD · 메시 LOD 는 이 눈 하나로 본다(뷰가 없으면 화면 밖으로 보고 포즈를 거른다).
        if ( scene.getObjectManager() != nullptr )
        {
            _listLODView.clear();
            _listLODView.push_back( LODView::make( outPacket._viewProj, outPacket._cameraPos ) );
            scene.getObjectManager()->getSubsystem<AnimationSystem>().setLODViews( _listLODView );
            scene.getObjectManager()->getSubsystem<MeshLODSystem>().setViews( _listLODView );
        }

        builder.populateFromScene( &scene, outPacket._cameraPos );
        if ( pUploadQueue != nullptr )
        {
            builder.requestGPUUploads( *pUploadQueue );
            pUploadQueue->flush();
        }
        builder.exportCPUSnapshot( outPacket._gpuScene );
    }
} // namespace sw

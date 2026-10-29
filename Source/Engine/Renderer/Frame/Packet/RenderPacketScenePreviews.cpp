#include "pch.h"

#include "Engine/Renderer/Frame/Packet/RenderPacketScenePreviews.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Animation/Scene/AnimationSystem.h"
#include "Engine/Environment/Fog/FogComponent.h"
#include "Engine/Environment/Sky/SkyComponent.h"
#include "Engine/Graphics/Upload/GPUUploadQueue.h"
#include "Engine/Object/Component/3D/MeshLODSystem.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Renderer/Frame/FrameRenderer.h"
#include "Engine/Renderer/Frame/Packet/RenderFramePacket.h"
#include "Engine/Renderer/Frame/Packet/RenderPacketSceneCollector.h"
#include "Engine/Renderer/Frame/View/RenderView.h"
#include "Engine/Renderer/Lighting/GPULightBuffer.h"
#include "Engine/Renderer/Lighting/ShadowSetup.h"
#include "Engine/Renderer/Scene/GPUSceneBuilder.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"

namespace sw
{
    RenderPacketScenePreviews::RenderPacketScenePreviews()
        : _listBuilder{}
        , _listLODView{}
        , _listShadowLightIndex{}
        , _bMergeBatchesAcrossMaterials{ false }
    {
    }

    RenderPacketScenePreviews::~RenderPacketScenePreviews() = default;

    void RenderPacketScenePreviews::setMergeBatchesAcrossMaterials( bool bMerge )
    {
        _bMergeBatchesAcrossMaterials = bMerge;
        for ( ScenePreviewBuilder& preview : _listBuilder )
        {
            preview._builder->setMergeBatchesAcrossMaterials( bMerge );
        }
    }

    void RenderPacketScenePreviews::clear()
    {
        _listBuilder.clear();
    }

    RenderPacketScenePreviews::ScenePreviewBuilder& RenderPacketScenePreviews::getOrCreateBuilder( uint32 sceneKey )
    {
        for ( ScenePreviewBuilder& preview : _listBuilder )
        {
            if ( preview._sceneKey == sceneKey )
                return preview;
        }
        ScenePreviewBuilder created;
        created._builder  = make_unique<GPUSceneBuilder>();
        created._sceneKey = sceneKey;
        created._builder->setMergeBatchesAcrossMaterials( _bMergeBatchesAcrossMaterials );
        _listBuilder.push_back( std::move( created ) );
        return _listBuilder.back();
    }

    void RenderPacketScenePreviews::populate( RenderFramePacket& packet, const HostViewTargets& views, const SceneManager& sceneManager, GPUUploadQueue* pUploadQueue )
    {
        packet._scenePreviewCount = 0;
        for ( ScenePreviewBuilder& preview : _listBuilder )
        {
            preview._bSeen = SW_FALSE;
        }

        const uint32 requestCount = MathUtil::min( views._sceneViewCount, kMaxHostSceneView );
        for ( uint32 requestIndex = 0; requestIndex < requestCount; ++requestIndex )
        {
            const HostSceneView& request = views._arrSceneView[requestIndex];
            Scene* const         pScene  = sceneManager.findPreviewScene( request._sceneKey );
            if ( pScene == nullptr || request._target.isValid() == false )
                continue;

            // 저장소를 다시 쓴다 — 미리보기 패킷의 스냅샷 · 라이트 목록이 프레임마다 새로 할당되지 않게 한다.
            if ( packet._listScenePreview.size() <= packet._scenePreviewCount )
                packet._listScenePreview.push_back( make_unique<RenderFramePacket>() );
            RenderFramePacket& preview = *packet._listScenePreview[packet._scenePreviewCount];
            ++packet._scenePreviewCount;
            preview.resetForFrame();
            preview._bValid             = SW_TRUE;
            preview._sceneKey           = request._sceneKey;
            preview._frameIndex         = packet._frameIndex;
            preview._outputRenderTarget = request._target._renderTarget;
            preview._viewportWidth      = request._target._width;
            preview._viewportHeight     = request._target._height;
            preview._mainView           = request._settings;
            preview.setMainView( request._viewProj, request._cameraPos );

            // 주 패킷과 같은 규칙으로 이 씬의 빛을 싣는다 — 렌더 스레드는 미리보기 씬도 볼 수 없다.
            const RenderPacketKeyLights lights = RenderPacketSceneCollector::writeKeyLight( preview, *pScene );
            collectSceneLights( pScene, preview._listLight, &_listShadowLightIndex, &preview._listLightProbe );
            const GameObjectManager* pPreviewObjects = pScene->getObjectManager();
            preview._bHasSky                         = ( pPreviewObjects != nullptr && SkyComponent::findSky( *pPreviewObjects, preview._sky ) ) ? SW_TRUE : SW_FALSE;
            preview._bHasFog                         = ( pPreviewObjects != nullptr && FogComponent::findFog( *pPreviewObjects, preview._cameraPos, preview._fog ) ) ? SW_TRUE : SW_FALSE;
            ShadowSetupUtil::populateFrame( lights.getShadowCaster(), &preview._viewProj, FrameRenderer::getShadowMapResolution(), _listShadowLightIndex, preview._listLight,
                                            preview._shadow );

            // 미리보기 씬의 애니메이션 LOD · 메시 LOD 는 이 눈 하나로 본다(뷰가 없으면 화면 밖으로 보고 포즈를 거른다).
            if ( pScene->getObjectManager() != nullptr )
            {
                _listLODView.clear();
                _listLODView.push_back( LODView::make( preview._viewProj, preview._cameraPos ) );
                pScene->getObjectManager()->getSubsystem<AnimationSystem>().setLODViews( _listLODView );
                pScene->getObjectManager()->getSubsystem<MeshLODSystem>().setViews( _listLODView );
            }

            ScenePreviewBuilder& entry = getOrCreateBuilder( request._sceneKey );
            entry._bSeen               = SW_TRUE;
            GPUSceneBuilder& builder   = *entry._builder;
            builder.populateFromScene( pScene, preview._cameraPos );
            if ( pUploadQueue != nullptr )
            {
                builder.requestGPUUploads( *pUploadQueue );
                pUploadQueue->flush();
            }
            builder.exportCPUSnapshot( preview._gpuScene );
        }

        // 요청이 끊긴 씬(창을 닫았거나 가렸다)의 빌더를 내린다 — 머티리얼 · 인스턴스 소유를 놓는다.
        for ( size_t index = _listBuilder.size(); index > 0; --index )
        {
            ScenePreviewBuilder& entry = _listBuilder[index - 1];
            if ( entry._bSeen == SW_TRUE )
                continue;
            entry._builder->clear();
            _listBuilder.erase( _listBuilder.begin() + static_cast<ptrdiff_t>( index - 1 ) );
        }
    }
} // namespace sw

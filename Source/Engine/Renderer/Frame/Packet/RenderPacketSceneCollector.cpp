#include "pch.h"

#include "Engine/Renderer/Frame/Packet/RenderPacketSceneCollector.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"

#include "Engine/Animation/Scene/AnimationSystem.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/2D/Render2DSettings.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/Upload/GPUUploadQueue.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Renderer/Frame/FrameRenderer.h"
#include "Engine/Renderer/Frame/Packet/RenderFramePacket.h"
#include "Engine/Renderer/Frame/View/RenderView.h"
#include "Engine/Renderer/Lighting/GPULightBuffer.h"
#include "Engine/Renderer/Lighting/ShadowSetup.h"
#include "Engine/Renderer/Scene/GPUSceneBuilder.h"
#include "Engine/Scene/Scene.h"
#include "Engine/UI/UISystem.h"
#include "Engine/UI/World/UIWidgetComponentService.h"
#include "Engine/Visual/Component/DirectionalLightComponent.h"
#include "Engine/Visual/Component/LightComponent.h"
#include "Engine/Visual/MeshLODSystem.h"

namespace sw
{
    /**
     * @brief `-gv_gpuInstanceLod=0`: GPU 컬링이 인스턴스마다 메시 LOD 단계를 고르는 길을 끕니다(CPU `MeshLODSystem` 이 컴포넌트마다 고른다). 능력이 없는 백엔드(DX11)는 이 값과 상관없이 CPU 선택이다.
     */
    SW_GLOBAL_VARIABLE( bool, gv_gpuInstanceLod, true, "GPU 컬링이 인스턴스마다 메시 LOD 단계를 고른다 (false=CPU 가 컴포넌트마다 고른다)" );
    SW_GLOBAL_VARIABLE( float32, gv_lodCrossfadeSeconds, 0.25f, "GPU 인스턴스 LOD 단계 전환의 상보 디더 크로스페이드 시간(초) — 0 이하면 크로스페이드 없이 바로 바꾼다" );
} // namespace sw

namespace sw
{
    RenderPacketSceneCollector::RenderPacketSceneCollector()
        : _gpuSceneBuilder{ nullptr }
        , _listLODView{}
        , _listShadowLightIndex{}
        , _lightProbeElement{}
        , _listLensFlareAsset{}
        , _bGPUInstanceLODCapable{ SW_FALSE }
    {
    }

    RenderPacketSceneCollector::~RenderPacketSceneCollector() = default;

    void RenderPacketSceneCollector::initialize( IRHIDevice& device )
    {
        if ( _gpuSceneBuilder == nullptr )
            _gpuSceneBuilder = make_unique<GPUSceneBuilder>();
        // GT 쪽 GPUScene 이 배치를 만든다. 텍스처를 인덱스로 고를 수 있는 백엔드면 머티리얼이 달라도
        // 셰이더 타입 단위로 합친다(언리얼 GPUScene).
        const bool bMergeBatchesAcrossMaterials = device.supportsNativeBindlessSampling();
        // GPU 인스턴스 LOD 는 컬링 컴퓨트가 단계를 고르므로 GPU 컬링 능력이 있는 백엔드만이다(DX11 은 CPU 단계 선택이 남는다).
        _bGPUInstanceLODCapable = device.getCapabilities()._bGPUCulling != SW_FALSE;
        _gpuSceneBuilder->setMergeBatchesAcrossMaterials( bMergeBatchesAcrossMaterials );
    }

    void RenderPacketSceneCollector::shutdown()
    {
        _gpuSceneBuilder->clear();
    }

    void RenderPacketSceneCollector::destroy()
    {
        _gpuSceneBuilder.reset();
    }

    bool RenderPacketSceneCollector::isMergeBatchesAcrossMaterials() const
    {
        return _gpuSceneBuilder != nullptr && _gpuSceneBuilder->isMergeBatchesAcrossMaterials();
    }

    RenderPacketKeyLights RenderPacketSceneCollector::writeKeyLight( RenderFramePacket& packet, Scene& scene )
    {
        // 그림자 행렬은 그림자 맵을 가져가는 빛(`DirectionalLightComponent::findShadowCasting`)에서 — 라이트 목록의 그림자 플래그와 같은 빛이다.
        RenderPacketKeyLights lights;
        lights._pLight       = DirectionalLightComponent::findActive( scene.getObjectManager() );
        lights._pShadowLight = DirectionalLightComponent::findShadowCasting( scene.getObjectManager() );
        if ( lights._pLight != nullptr )
        {
            const float3 dir          = lights._pLight->getLightDirection();
            const float3 color        = lights._pLight->getColor();
            packet._lightDirIntensity = float4{ dir._x, dir._y, dir._z, lights._pLight->getIntensity() };
            packet._lightColorAmbient = float4{ color._x, color._y, color._z, lights._pLight->getAmbient() };
            packet._bHasLight         = SW_TRUE;
        }
        return lights;
    }

    RenderPacketKeyLights RenderPacketSceneCollector::collectLights( RenderFramePacket& packet, Scene& activeScene )
    {
        // 주광은 씬이 갖고, 렌더 스레드는 패킷으로만 받는다. executePacket 은 _pScene 을
        // null 로 두므로 렌더 스레드에서 씬을 조회할 수 없다.
        const RenderPacketKeyLights lights = writeKeyLight( packet, activeScene );

        // 씬의 **모든** 라이트다. 방향광 · 점광 · 스폿이 한 목록으로 간다. 위의 키라이트는 앰비언트의 출처이고, 목록이 비면
        // 그 씬은 앰비언트만 받는다(키라이트로 대신 비추지 않는다). 그림자를 켠 점광 · 스폿은 번호를 따로 받아 아래에서 아틀라스 칸을 준다.
        collectSceneLights( &activeScene, packet._listLight, &_listShadowLightIndex, &_lightProbeElement, &packet._listLightCookie, &packet._listRealtimeProbe );
        packet._pLightProbe = _lightProbeElement.getElements();
        collectLensFlares( packet, activeScene );
        collectDecals( packet, activeScene );
        // 하늘도 씬이 갖는다. 없으면 스카이 패스가 그리지 않는다(배경은 SceneColor 클리어 색).
        const GameObjectManager* pActiveObjects = activeScene.getObjectManager();
        packet._bHasSky                         = ( pActiveObjects != nullptr && pActiveObjects->findServiceSky( packet._sky ) ) ? SW_TRUE : SW_FALSE;
        // 표면 변형 층도 씬이 갖는다 — 없으면 빈 값이라 셰이더가 정점을 옮기지 않는다.
        if ( pActiveObjects == nullptr || pActiveObjects->findServiceSurfaceDeform( packet._surfaceDeform ) == false )
            packet._surfaceDeform = SurfaceDeformBinding{};
        return lights;
    }

    void RenderPacketSceneCollector::collectLensFlares( RenderFramePacket& packet, const Scene& activeScene )
    {
        packet._listLensFlare.clear();
        const GameObjectManager* pObjects = activeScene.getObjectManager();
        if ( pObjects == nullptr )
            return;
        const ComponentRegistry& registry = pObjects->getComponentRegistry();
        // 3D 빛 셋만 본다(2D 빛은 화면 평면에 있어 렌즈를 지나지 않는다).
        const LightKind arrKind[] = { LightKind::Directional, LightKind::Point, LightKind::Spot };
        for ( const LightKind kind : arrKind )
        {
            for ( const LightComponent* pLight : registry.getAll<LightComponent>( LightComponent::getRegistryIndex( kind ) ) )
            {
                if ( pLight == nullptr || pLight->isActive() == false || pLight->getLensFlarePath().empty() )
                    continue;
                const string& path    = pLight->getLensFlarePath();
                auto*         pCached = VectorUtil::findIf( _listLensFlareAsset, [&path]( const pair<string, shared_ptr<const LensFlareAsset>>& entry )
                        { return entry.first == path; } );
                if ( pCached == nullptr )
                {
                    shared_ptr<LensFlareAsset> pAsset = make_shared<LensFlareAsset>();
                    if ( const expected<void> loaded = pAsset->loadFromAsset( AssetID{ string_view( path ) } ); loaded.has_value() == false )
                    {
                        SW_LOG_ERROR_ID( "RenderPacketSceneCollector.LensFlareUnreadable", "Lens flare '%#' could not be loaded (%#) - lights using it draw no flare", path, loaded.error().message() );
                        pAsset.reset();
                    }
                    _listLensFlareAsset.emplace_back( path, std::move( pAsset ) );
                    pCached = &_listLensFlareAsset.back();
                }
                if ( pCached->second == nullptr )
                    continue;
                GPULight element{};
                pLight->writeGPULight( element );
                const float3     color = pLight->getColor() * pLight->getIntensity();
                LensFlareRequest request;
                request._position     = pLight->getLightPosition();
                request._direction    = float3{ element._directionType._x, element._directionType._y, element._directionType._z };
                request._color        = color;
                request._bDirectional = kind == LightKind::Directional ? SW_TRUE : SW_FALSE;
                request._pAsset       = pCached->second;
                packet._listLensFlare.push_back( std::move( request ) );
            }
        }
    }

    void RenderPacketSceneCollector::collectDecals( RenderFramePacket& packet, const Scene& activeScene )
    {
        packet._listDecal.clear();
        const GameObjectManager* pObjects = activeScene.getObjectManager();
        if ( pObjects == nullptr )
            return;
        for ( const DecalComponent* pDecal : pObjects->getComponentRegistry().getAll<DecalComponent>() )
        {
            if ( pDecal == nullptr || pDecal->isActive() == false )
                continue;
            DecalDrawData& data = packet._listDecal.emplace_back();
            pDecal->writeDrawData( data );
        }
        // 겹친 데칼은 차례가 큰 것이 위다(언리얼 Sort Order). 같은 차례는 등록 순서(안정 정렬).
        std::stable_sort( packet._listDecal.begin(), packet._listDecal.end(), []( const DecalDrawData& left, const DecalDrawData& right )
        { return left._sortOrder < right._sortOrder; } );
    }

    void RenderPacketSceneCollector::applyMainCamera( RenderFramePacket& packet, Scene& activeScene, CameraComponent& camera )
    {
        // 안개도 씬이 갖는다. 카메라가 물속이면 물의 안개가 컴포넌트의 값을 덮는다(`SceneEnvironmentProvider`).
        const GameObjectManager* pActiveObjects = activeScene.getObjectManager();
        packet._bHasFog                         = ( pActiveObjects != nullptr && pActiveObjects->findServiceFog( camera.getCameraPosition(), packet._fog ) ) ? SW_TRUE : SW_FALSE;
        // 투명 정렬의 깊이 — 직교 카메라는 시선 축(같은 Z 의 스프라이트가 카메라를 따라 앞뒤가 바뀌지 않게), 원근은 거리(render2d.xml).
        _gpuSceneBuilder->setTransparentSortAxis( engine::getSubsystem<Render2DSettings>().computeTransparentSortAxis( camera.isOrthographic(), camera.getCameraForward() ) );
    }

    void RenderPacketSceneCollector::populateShadows( RenderFramePacket& packet, const RenderPacketKeyLights& lights )
    {
        FrameShadowData::populateFrame( lights.getShadowCaster(), packet._bHasViewProj == SW_TRUE ? &packet._viewProj : nullptr, FrameRenderer::getShadowMapResolution(),
                                        _listShadowLightIndex, packet._listLight, packet._shadow );
    }

    void RenderPacketSceneCollector::populateViewShadows( RenderFramePacket& packet, const RenderPacketKeyLights& lights ) const
    {
        for ( RenderViewRequest& view : packet._listView )
        {
            if ( view._bRender == SW_TRUE )
                view._shadow = FrameShadowData::makeViewShadowFor( lights.getShadowCaster(), view._viewProj, packet._shadow );
        }
    }

    void RenderPacketSceneCollector::updateLODViews( const RenderFramePacket& packet, Scene& activeScene )
    {
        // 애니메이션 LOD · 메시 LOD 의 뷰 — 주 시점과 이번 프레임에 그리는 추가 뷰(CCTV · 분할 화면). 다음 프레임 판정이 이것으로 가시성 · 화면 크기를 본다.
        // 갱신 주기로 쉬는 추가 뷰는 넣지 않는다 — 그 뷰에만 보이는 캐릭터는 그 뷰가 그리는 프레임에만 포즈를 만든다.
        GameObjectManager* pObjects = activeScene.getObjectManager();
        if ( pObjects == nullptr )
            return;
        _listLODView.clear();
        if ( packet._bHasViewProj == SW_TRUE )
            _listLODView.push_back( LODView::make( packet._viewProj, packet._cameraPos ) );
        for ( const RenderViewRequest& view : packet._listView )
        {
            if ( view._bRender == SW_TRUE )
                _listLODView.push_back( LODView::make( view._viewProj, view._position ) );
        }
        pObjects->getSubsystem<AnimationSystem>().setLODViews( _listLODView );
        pObjects->getSubsystem<MeshLODSystem>().setViews( _listLODView );
    }

    void RenderPacketSceneCollector::populateGPUSceneSnapshot( RenderFramePacket& packet, Scene* pActiveScene, GPUUploadQueue* pUploadQueue )
    {
        _gpuSceneBuilder->setGPUInstanceLODEnabled( _bGPUInstanceLODCapable && gv_gpuInstanceLod );
        _gpuSceneBuilder->setGPUInstanceLODCrossfadeSeconds( gv_lodCrossfadeSeconds );
        buildSnapshot( packet, pActiveScene, pUploadQueue );
    }

    void RenderPacketSceneCollector::fillSceneView( RenderFramePacket& outPacket, Scene& scene, const RenderSceneViewDesc& view, GPUUploadQueue* pUploadQueue )
    {
        outPacket._bValid             = SW_TRUE;
        outPacket._outputRenderTarget = view._outputRenderTarget;
        outPacket._viewportWidth      = view._width;
        outPacket._viewportHeight     = view._height;
        outPacket._mainView           = view._settings;
        outPacket.setMainView( view._viewProj, view._cameraPos );
        // 주 화면과 같은 단계다(`RenderPacketBuilder::populateScene`). 안개는 카메라 컴포넌트가 아니라 시점 위치로 고른다.
        const RenderPacketKeyLights lights   = collectLights( outPacket, scene );
        const GameObjectManager*    pObjects = scene.getObjectManager();
        outPacket._bHasFog                   = ( pObjects != nullptr && pObjects->findServiceFog( outPacket._cameraPos, outPacket._fog ) ) ? SW_TRUE : SW_FALSE;
        populateShadows( outPacket, lights );
        updateLODViews( outPacket, scene );
        _gpuSceneBuilder->setGPUInstanceLODEnabled( false );
        buildSnapshot( outPacket, &scene, pUploadQueue );
    }

    void RenderPacketSceneCollector::buildSnapshot( RenderFramePacket& packet, Scene* pScene, GPUUploadQueue* pUploadQueue )
    {
        _gpuSceneBuilder->populateFromScene( pScene, packet._cameraPos );
        // 추가 뷰(CCTV · PiP)는 자기 눈으로 투명을 정렬한다 — 주 카메라 순서로 그리면 반대편을 보는 뷰에서 앞뒤가 뒤집힌다.
        _gpuSceneBuilder->computeViewTransparentOrders( packet._listView );

        // 그릴 것이 정해졌으니 **스냅샷을 내보내기 전에** GPU 쪽을 만들어 둔다. 렌더 스레드는 그리기만
        // 하면 된다(새 메시가 등장한 프레임에 RT 가 정점 버퍼 생성을 떠안지 않게).
        if ( pUploadQueue != nullptr )
        {
            _gpuSceneBuilder->requestGPUUploads( *pUploadQueue );
            pUploadQueue->flush();
        }

        SW_PROFILE_SCOPE( "GT.Packet.export" );
        _gpuSceneBuilder->exportCPUSnapshot( packet._gpuScene );
        packet._gpuScene._bMeshesCreatedBeforeHandoff = ( pUploadQueue != nullptr && pUploadQueue->createsMeshes() ) ? SW_TRUE : SW_FALSE;
    }

    void RenderPacketSceneCollector::resolveMarkerInstanceSlots( RenderFramePacket& packet, const UISystem* pUISystem, const Scene* pActiveScene ) const
    {
        if ( packet._canvas._listMarker.empty() || pUISystem == nullptr || pActiveScene == nullptr || _gpuSceneBuilder == nullptr )
            return;
        // 기록이 다른 관리자(미리보기 씬)의 것이면 프리미티브 번호가 이 스냅샷의 것이 아니다 — 월드 점을 쓴다.
        if ( engine::getSubsystem<UIWidgetComponentService>().getMarkers().getGPUMarkerManager() != pActiveScene->getObjectManager() )
            return;
        for ( CanvasMarkerRecord& record : packet._canvas._listMarker )
        {
            if ( record._bActive != 0 && record._primitiveIndex != constant::invalid_index::kUint32 )
                record._instanceSlot = _gpuSceneBuilder->findOpaqueInstanceSlot( record._primitiveIndex );
        }
    }
} // namespace sw

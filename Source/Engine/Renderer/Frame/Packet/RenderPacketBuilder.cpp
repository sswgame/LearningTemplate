#include "pch.h"

#include "Engine/Renderer/Frame/Packet/RenderPacketBuilder.h"

#include "Engine/Graphics/Upload/GPUUploadQueue.h"
#include "Engine/Renderer/Frame/Packet/RenderFramePacket.h"
#include "Engine/Renderer/Frame/View/RenderView.h"
#include "Engine/Scene/Scene.h"

namespace sw
{
    RenderPacketBuilder::RenderPacketBuilder()
        : _packetScratch{ nullptr }
        , _gpuUploadQueue{ nullptr }
        , _scene{}
        , _views{}
        , _previews{}
        , _canvas{}
    {
    }

    RenderPacketBuilder::~RenderPacketBuilder() = default;

    void RenderPacketBuilder::initialize( IRHIDevice& device, TaskManager* pTaskManager )
    {
        const bool bMergeBatchesAcrossMaterials = _scene.initialize( device );
        if ( _packetScratch == nullptr )
            _packetScratch = make_unique<RenderFramePacket>();
        if ( _gpuUploadQueue == nullptr )
            _gpuUploadQueue = make_unique<GPUUploadQueue>();
        _views.initialize();
        // 미리보기 빌더도 주 빌더와 같은 배치 합치기를 쓴다(디바이스가 정한다).
        _previews.setMergeBatchesAcrossMaterials( bMergeBatchesAcrossMaterials );
        _gpuUploadQueue->bindDevice( &device, pTaskManager );
    }

    void RenderPacketBuilder::shutdown()
    {
        _scene.shutdown();
        // 미리보기 빌더도 머티리얼 · 인스턴스를 든다 — 디바이스가 살아 있을 때 놓는다.
        _previews.clear();
    }

    void RenderPacketBuilder::destroy()
    {
        // 만든 순서(initialize)의 역순이다.
        _previews.clear();
        _views.destroy();
        _gpuUploadQueue.reset();
        _packetScratch.reset();
        _scene.destroy();
    }

    bool RenderPacketBuilder::isInitialized() const
    {
        return _packetScratch != nullptr && _scene.isCreated();
    }

    bool RenderPacketBuilder::isMergeBatchesAcrossMaterials() const
    {
        return _scene.isMergeBatchesAcrossMaterials();
    }

    RenderFramePacket& RenderPacketBuilder::beginPacket()
    {
        RenderFramePacket& packet = *_packetScratch;
        packet.resetForFrame();
        packet._bValid = 1;
        return packet;
    }

    void RenderPacketBuilder::populateScene( RenderFramePacket& packet, const HostViewTargets& views, const ViewCameraProviderDelegate& sceneViewCameraProvider,
                                             Scene* pActiveScene, float32 deltaTime, const RHI& rhi, const FrameRenderer* pFrameRenderer )
    {
        const HostViewTarget& mainOutput = views.getMainOutput();
        packet._outputRenderTarget       = mainOutput._renderTarget;
        packet._viewportWidth            = mainOutput._width;
        packet._viewportHeight           = mainOutput._height;

        if ( pActiveScene == nullptr )
            return;

        // 순서가 계약이다: 빛 · 하늘 → 주 시점(카메라 · 안개 · 투명 정렬 축) → 주 시점 그림자 → 추가 뷰 → 추가 뷰 그림자 → LOD 뷰 → 스냅샷.
        const RenderPacketKeyLights lights   = _scene.collectLights( packet, *pActiveScene );
        const RenderPacketMainView  mainView = _views.setupMainView( packet, views, sceneViewCameraProvider, *pActiveScene, deltaTime, rhi, pFrameRenderer );
        if ( mainView._pCamera != nullptr )
            _scene.applyMainCamera( packet, *pActiveScene, *mainView._pCamera );
        // 그림자 배치는 카메라가 정해진 **뒤에** 만든다 — 볼륨을 카메라가 보는 곳에 맞추는 빛이 있다.
        _scene.populateShadows( packet, lights );
        _views.collectExtraViews( packet, views, *pActiveScene, mainView, deltaTime, pFrameRenderer );
        _scene.populateViewShadows( packet, lights );
        _scene.updateLODViews( packet, *pActiveScene );
        _scene.populateGPUSceneSnapshot( packet, pActiveScene, _gpuUploadQueue.get() );
    }

    void RenderPacketBuilder::populateScenePreviews( RenderFramePacket& packet, const HostViewTargets& views, const SceneManager& sceneManager )
    {
        _previews.populate( packet, views, sceneManager, _gpuUploadQueue.get() );
    }

    void RenderPacketBuilder::resolveMarkerInstanceSlots( RenderFramePacket& packet, const UISystem* pUISystem, const Scene* pActiveScene ) const
    {
        _scene.resolveMarkerInstanceSlots( packet, pUISystem, pActiveScene );
    }

    void RenderPacketBuilder::populateCanvas( RenderFramePacket& packet, bool bSceneMain, UISystem* pUISystem, FontService* pFontService, const RHI* pRHI )
    {
        _canvas.populate( packet, bSceneMain, pUISystem, pFontService, pRHI );
    }
} // namespace sw

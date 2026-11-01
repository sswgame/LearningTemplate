#include "pch.h"

#include "Engine/Renderer/Frame/Packet/RenderPacketScenePreviews.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Graphics/Upload/GPUUploadQueue.h"
#include "Engine/Renderer/Frame/Packet/RenderFramePacket.h"
#include "Engine/Renderer/Scene/GPUSceneBuilder.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"

namespace sw
{
    RenderPacketScenePreviews::RenderPacketScenePreviews()
        : _listBuilder{}
        , _fill{}
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
            preview._sceneKey   = request._sceneKey;
            preview._frameIndex = packet._frameIndex;

            ScenePreviewBuilder& entry = getOrCreateBuilder( request._sceneKey );
            entry._bSeen               = SW_TRUE;
            RenderSceneViewDesc view{};
            view._settings           = request._settings;
            view._viewProj           = request._viewProj;
            view._cameraPos          = request._cameraPos;
            view._outputRenderTarget = request._target._renderTarget;
            view._width              = request._target._width;
            view._height             = request._target._height;
            _fill.fill( preview, *pScene, view, *entry._builder, pUploadQueue );
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

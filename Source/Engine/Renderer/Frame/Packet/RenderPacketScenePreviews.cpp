#include "pch.h"

#include "Engine/Renderer/Frame/Packet/RenderPacketScenePreviews.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Graphics/Upload/GPUUploadQueue.h"
#include "Engine/Renderer/Frame/Packet/RenderFramePacket.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"

namespace sw
{
    RenderPacketScenePreviews::RenderPacketScenePreviews()
        : _listEntry{}
        , _pDevice{ nullptr }
    {
    }

    RenderPacketScenePreviews::~RenderPacketScenePreviews() = default;

    void RenderPacketScenePreviews::initialize( IRHIDevice& device )
    {
        _pDevice = &device;
        for ( ScenePreviewEntry& entry : _listEntry )
        {
            entry._collector->initialize( device );
        }
    }

    void RenderPacketScenePreviews::clear()
    {
        for ( ScenePreviewEntry& entry : _listEntry )
        {
            entry._collector->shutdown();
        }
        _listEntry.clear();
    }

    RenderPacketScenePreviews::ScenePreviewEntry& RenderPacketScenePreviews::getOrCreateEntry( uint32 sceneKey )
    {
        for ( ScenePreviewEntry& entry : _listEntry )
        {
            if ( entry._sceneKey == sceneKey )
                return entry;
        }
        ScenePreviewEntry created;
        created._collector = make_unique<RenderPacketSceneCollector>();
        created._collector->initialize( *_pDevice );
        created._sceneKey = sceneKey;
        _listEntry.push_back( std::move( created ) );
        return _listEntry.back();
    }

    void RenderPacketScenePreviews::populate( RenderFramePacket& packet, const FrameViewports& views, const SceneManager& sceneManager, GPUUploadQueue* pUploadQueue )
    {
        packet._scenePreviewCount = 0;
        packet._listRetiredSceneKey.clear();
        if ( _pDevice == nullptr )
            return; // 기동 단계가 디바이스를 주기 전이다(`initialize`)

        const uint32 requestCount = MathUtil::min( views._previewViewCount, kMaxPreviewSceneView );
        for ( uint32 requestIndex = 0; requestIndex < requestCount; ++requestIndex )
        {
            const PreviewSceneView& request = views._arrPreviewView[requestIndex];
            Scene* const            pScene  = sceneManager.findPreviewScene( request._sceneKey );
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

            ScenePreviewEntry&  entry = getOrCreateEntry( request._sceneKey );
            RenderSceneViewDesc view{};
            view._settings           = request._settings;
            view._viewProj           = request._viewProj;
            view._cameraPos          = request._cameraPos;
            view._outputRenderTarget = request._target._renderTarget;
            view._width              = request._target._width;
            view._height             = request._target._height;
            entry._collector->fillSceneView( preview, *pScene, view, pUploadQueue );
        }

        // 사라진 씬의 채우기를 내리고(머티리얼 · 인스턴스 소유를 놓는다) 렌더 스레드에 그 씬의 렌더러를 내리게 한다. 요청만 끊긴 씬(창을 가렸다)은 남긴다 — 다시 보일 때 다시 짓지 않는다.
        for ( size_t index = _listEntry.size(); index > 0; --index )
        {
            ScenePreviewEntry& entry = _listEntry[index - 1];
            if ( sceneManager.findPreviewScene( entry._sceneKey ) != nullptr )
                continue;
            packet._listRetiredSceneKey.push_back( entry._sceneKey );
            entry._collector->shutdown();
            _listEntry.erase( _listEntry.begin() + static_cast<ptrdiff_t>( index - 1 ) );
        }
    }
} // namespace sw

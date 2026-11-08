#include "pch.h"

#include "Engine/Renderer/Frame/OffscreenSceneRenderers.h"

#include "Engine/Graphics/RHI/IRHICommandContext.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Renderer/Frame/FrameRenderer.h"
#include "Engine/Renderer/Frame/Packet/RenderFramePacket.h"

namespace sw
{
    OffscreenSceneRenderers::OffscreenSceneRenderers()
        : _listEntry{}
        , _count{ 0 }
        , _nextCaptureKey{ 1 }
    {
    }

    uint32 OffscreenSceneRenderers::allocateCaptureSceneKey()
    {
        return kCaptureSceneKeyBit | ( _nextCaptureKey.fetch_add( 1, std::memory_order_relaxed ) & ~kCaptureSceneKeyBit );
    }

    OffscreenSceneRenderers::~OffscreenSceneRenderers()
    {
        shutdown();
    }

    FrameRenderer* OffscreenSceneRenderers::execute( FrameRenderer& mainRenderer, IRHIDevice* pDevice, RenderFramePacket& packet, const float4& clearColor, string_view pipelinePath )
    {
        const bool          bCanRender       = mainRenderer.isReady() && pDevice != nullptr && packet._bValid == SW_TRUE;
        const bool          bIsOffscreenView = packet._sceneKey != 0 && packet._outputRenderTarget != 0;
        IRHICommandContext* pFrameStream     = pDevice != nullptr ? pDevice->getFrameStreamContext() : nullptr;
        if ( bCanRender == false || bIsOffscreenView == false || pFrameStream == nullptr )
            return nullptr;

        // 타깃의 열기 · 읽기 전환은 게임 뷰(`RenderThread::executeFrameBody`)와 같은 규칙이다. 그리지 못해도 타깃은 지운 색 · 읽기 상태로 남는다.
        const RHITextureHandle outputTarget = packet._outputRenderTarget;
        RHIRenderPassBeginInfo outputPass{};
        outputPass._bBindColor        = SW_TRUE;
        outputPass._colorTargetCount  = 1;
        outputPass._arrColorTarget[0] = outputTarget;
        outputPass._arrLoadOp[0]      = RHIRenderPassLoadOp::Clear;
        outputPass._arrClearColor[0]  = clearColor;
        pFrameStream->clearRenderTargets( outputPass );
        FrameRenderer* const pRenderer = findOrCreate( mainRenderer, pDevice, packet._sceneKey, pipelinePath );
        const bool           bDrew     = pRenderer != nullptr && pRenderer->executePacket( pDevice, packet ); // 보기 모드는 패킷의 뷰 설정이 정한다
        pFrameStream->prepareTextureForShaderRead( outputTarget );
        return bDrew ? pRenderer : nullptr;
    }

    FrameRenderer* OffscreenSceneRenderers::findOrCreate( FrameRenderer& mainRenderer, IRHIDevice* pDevice, uint32 sceneKey, string_view pipelinePath )
    {
        const bool        bMainPipeline = pipelinePath.empty() || pipelinePath == mainRenderer.getPipelinePath();
        const string_view keyPath       = bMainPipeline ? string_view{} : pipelinePath;
        for ( Entry& entry : _listEntry )
        {
            if ( entry._sceneKey == sceneKey && entry._pipelinePath == keyPath )
                return entry._renderer.get();
        }
        Entry created;
        created._renderer     = make_unique<FrameRenderer>();
        created._sceneKey     = sceneKey;
        created._pipelinePath = string( keyPath );
        if ( created._renderer->initializeOffscreen( pDevice, mainRenderer, keyPath ) == false )
        {
            SW_LOG_ERROR_ID( "FrameRenderer.OffscreenRendererSceneKey", "Offscreen renderer for scene key %# ('%#') could not start: %#", sceneKey,
                             created._renderer->getPipelinePath().c_str(), created._renderer->getStatusMessage() );
            return nullptr;
        }
        SW_LOG_INFO_ID( "Renderer.OffscreenStarted", "Offscreen renderer started for scene key %# ('%#')", sceneKey, created._renderer->getPipelinePath().c_str() );
        FrameRenderer* const pRenderer = created._renderer.get();
        _listEntry.push_back( std::move( created ) );
        _count.store( static_cast<uint32>( _listEntry.size() ) );
        return pRenderer;
    }

    void OffscreenSceneRenderers::retire( uint32 sceneKey )
    {
        for ( size_t index = _listEntry.size(); index > 0; --index )
        {
            Entry& entry = _listEntry[index - 1];
            if ( entry._sceneKey != sceneKey )
                continue;
            entry._renderer->shutdown();
            _listEntry.erase( _listEntry.begin() + static_cast<ptrdiff_t>( index - 1 ) );
        }
        _count.store( static_cast<uint32>( _listEntry.size() ) );
    }

    void OffscreenSceneRenderers::onShaderRecompiled( const vector<string>& listShaderPath )
    {
        for ( Entry& entry : _listEntry )
        {
            entry._renderer->onShaderRecompiled( listShaderPath );
        }
    }

    void OffscreenSceneRenderers::shutdown()
    {
        for ( Entry& entry : _listEntry )
        {
            entry._renderer->shutdown();
        }
        _listEntry.clear();
        _count.store( 0 );
    }
} // namespace sw

/**
 * @file FrameRendererScenePreview.cpp
 * @brief 미리보기 씬(에디터 툴 창의 씬)을 씬마다 따로 둔 렌더러로 그리는 길입니다(`FrameRenderer::executeScenePreviewPacket`).
 */
#include "pch.h"

#include "Engine/Renderer/Frame/FrameRenderer.h"
#include "Engine/Renderer/Frame/RenderFramePacket.h"

namespace sw
{
    SW_LOG_CALLER( "FrameRenderer" );

    bool FrameRenderer::executeScenePreviewPacket( IRHIDevice* pDevice, RenderFramePacket& packet )
    {
        const bool bCanRender      = isReady() && pDevice != nullptr && packet._bValid == SW_TRUE;
        const bool bIsPreviewFrame = packet._sceneKey != 0 && packet._outputRenderTarget != 0;
        if ( bCanRender == false || bIsPreviewFrame == false )
            return false;

        FrameRenderer* pRenderer = nullptr;
        for ( ScenePreviewRenderer& preview : _listScenePreviewRenderer )
        {
            if ( preview._sceneKey == packet._sceneKey )
                pRenderer = preview._renderer.get();
        }
        if ( pRenderer == nullptr )
        {
            // 주 렌더러와 같은 파이프라인 · 작업 관리자로 세운다. 계측 · 공개를 끄는 표시는 initialize 앞에 둔다(파이프라인 로드가 공개를 부른다).
            ScenePreviewRenderer created;
            created._renderer                 = make_unique<FrameRenderer>();
            created._renderer->_bScenePreview = SW_TRUE;
            created._sceneKey                 = packet._sceneKey;
            if ( created._renderer->initialize( pDevice, _pTaskManager, _pipelinePath ) == false )
            {
                SW_LOG_ERROR( "Scene preview renderer for scene key %# could not start: %#", packet._sceneKey, created._renderer->getStatusMessage() );
                return false;
            }
            SW_LOG_INFO( "Scene preview renderer started for scene key %#", packet._sceneKey );
            pRenderer = created._renderer.get();
            _listScenePreviewRenderer.push_back( std::move( created ) );
        }
        // 보기 모드는 패킷의 뷰 설정이 정한다(`HostSceneView::_settings`).
        return pRenderer->executePacket( pDevice, packet );
    }

    void FrameRenderer::retireScenePreviews( const RenderFramePacket& mainPacket )
    {
        for ( size_t index = _listScenePreviewRenderer.size(); index > 0; --index )
        {
            ScenePreviewRenderer& preview = _listScenePreviewRenderer[index - 1];
            bool                  bAlive  = false;
            for ( uint32 previewIndex = 0; previewIndex < mainPacket._scenePreviewCount; ++previewIndex )
            {
                bAlive = bAlive || mainPacket._listScenePreview[previewIndex]->_sceneKey == preview._sceneKey;
            }
            if ( bAlive )
                continue;
            // 디바이스의 해제는 GPU 완료 뒤로 미뤄진다 — 이미 낸 프레임이 쓰는 자원을 지금 부수지 않는다.
            preview._renderer->shutdown();
            _listScenePreviewRenderer.erase( _listScenePreviewRenderer.begin() + static_cast<ptrdiff_t>( index - 1 ) );
        }
    }
} // namespace sw

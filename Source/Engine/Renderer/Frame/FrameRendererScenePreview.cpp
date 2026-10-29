/**
 * @file FrameRendererScenePreview.cpp
 * @brief 미리보기 씬(에디터 툴 창의 씬)을 씬마다 따로 둔 렌더러로 그리는 경로입니다(`FrameRenderer::executeScenePreviewPacket`).
 * @details 씬마다 나누는 것은 GPUScene(인스턴스 · 배치 · 정점 풀 · 머티리얼 그룹 버퍼) · 트랜지언트 · 패스 상수다. PSO 캐시는 주 렌더러의 것을 함께 쓴다(`FramePSOManager::shareCacheOf`) —
 *          창을 다시 열 때마다 엔진 PSO 를 모두 다시 만들던 첫 프레임 히치와 창 수만큼의 중복이 없다(다중 월드 결정 2).
 *
 *          [내리기의 GPU 안전] 창이 닫혀 메인 패킷에서 그 씬의 뷰 요청이 사라지면 `retireScenePreviews` 가 그 렌더러를 바로 `shutdown` 한다.
 *          이미 낸 프레임이 그 렌더러의 자원(버퍼 · 텍스처 · 커맨드 리스트)을 읽고 있을 수 있지만, 그 해제는 모두 디바이스의 해제 큐가 GPU 완료 뒤로
 *          미룬다(`IRHIDevice::enqueueGPURelease`, DX12 · Vulkan 은 펜스, DX11 · GL 은 프레임 지연 — 커맨드 리스트의 할당자도 같은 큐로 돌아간다).
 *          PSO 는 주 렌더러의 캐시에 남으므로 내리기가 PSO 를 건드리지 않는다. 확인은 에디터 시나리오 `editor/previewreopen`(감시 값 `Editor.ScenePreviewRenderers`)이다.
 */
#include "pch.h"

#include "Engine/Renderer/Frame/FrameRenderer.h"
#include "Engine/Renderer/Frame/Packet/RenderFramePacket.h"

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
            // 주 렌더러와 같은 파이프라인과 작업 관리자로 만든다. 계측과 공개를 끄는 표시와 PSO 캐시는 initialize 앞에 둔다(파이프라인 로드가 공개를 부른다).
            ScenePreviewRenderer created;
            created._renderer                 = make_unique<FrameRenderer>();
            created._renderer->_bScenePreview = SW_TRUE;
            created._renderer->_psoManager.shareCacheOf( _psoManager );
            created._sceneKey = packet._sceneKey;
            if ( created._renderer->initialize( pDevice, _pTaskManager, _pipelinePath ) == false )
            {
                SW_LOG_ERROR( "Scene preview renderer for scene key %# could not start: %#", packet._sceneKey, created._renderer->getStatusMessage() );
                return false;
            }
            SW_LOG_INFO( "Scene preview renderer started for scene key %#", packet._sceneKey );
            pRenderer = created._renderer.get();
            _listScenePreviewRenderer.push_back( std::move( created ) );
            _scenePreviewRendererCount.store( static_cast<uint32>( _listScenePreviewRenderer.size() ) );
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
            // 디바이스의 해제는 GPU 완료 뒤로 미뤄진다. 이미 낸 프레임이 쓰는 자원을 지금 파괴하지 않는다(파일 머리 주석).
            preview._renderer->shutdown();
            _listScenePreviewRenderer.erase( _listScenePreviewRenderer.begin() + static_cast<ptrdiff_t>( index - 1 ) );
        }
        _scenePreviewRendererCount.store( static_cast<uint32>( _listScenePreviewRenderer.size() ) );
    }
} // namespace sw

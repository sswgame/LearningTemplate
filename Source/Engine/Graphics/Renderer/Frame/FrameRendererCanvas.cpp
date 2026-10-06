#include "pch.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/RHI/IRHICommandList.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/Renderer/Frame/FrameRenderer.h"
#include "Engine/Graphics/Texture/Texture2D.h"
#include "Engine/Graphics/Texture/TextureCache.h"
#include "Engine/Resource/AssetManager.h"

// 캔버스 렌더 텍스처 대상(월드 공간 UI) — 빌리기 · 그리기 · 돌려주기.

namespace sw
{
    void FrameRenderer::prepareCanvasTargets()
    {
        for ( CanvasTargetState& state : _listCanvasTarget )
            state._bSeen = SW_FALSE;
        const bool bServices = engine::areEngineServicesBound();
        for ( const CanvasTargetDrawList& target : _canvasFrame._listTarget )
        {
            CanvasTargetState* pState = nullptr;
            for ( CanvasTargetState& state : _listCanvasTarget )
            {
                if ( state._path == target._targetPath )
                {
                    pState = &state;
                    break;
                }
            }
            if ( pState == nullptr )
            {
                pState        = &_listCanvasTarget.emplace_back();
                pState->_path = target._targetPath;
            }
            pState->_bSeen = SW_TRUE;
            if ( pState->_pTexture != nullptr || bServices == false || _pDevice == nullptr )
                continue;
            // 머티리얼과 같은 경로로 캐시에서 빌린다 — 크기는 처음 만들 때 정해진다(위젯의 그리기 크기).
            TextureCache& textures = engine::getAssetManager().getTextureManager();
            textures.declareRenderTarget( target._targetPath.view(), static_cast<uint32>( target._list._targetSize._x ),
                                          static_cast<uint32>( target._list._targetSize._y ) );
            pState->_pTexture      = textures.acquire( target._targetPath.view(), _pDevice );
            pState->_drawnRevision = 0;
        }

        // 이번 목록에 없는 대상(위젯 컴포넌트가 사라졌다)은 돌려준다.
        for ( size_t index = _listCanvasTarget.size(); index > 0; --index )
        {
            CanvasTargetState& state = _listCanvasTarget[index - 1];
            if ( state._bSeen == SW_TRUE )
                continue;
            if ( state._pTexture != nullptr && bServices )
                engine::getAssetManager().getTextureManager().release( state._path.view(), _pDevice );
            _listCanvasTarget.erase( _listCanvasTarget.begin() + static_cast<ptrdiff_t>( index - 1 ) );
        }
    }

    void FrameRenderer::drawCanvasTargets()
    {
        _lastDrawnCanvasTargetCount = 0;
        if ( _pCmd == nullptr || _pDevice == nullptr )
            return;
        for ( uint32 targetIndex = 0; targetIndex < static_cast<uint32>( _canvasFrame._listTarget.size() ); ++targetIndex )
        {
            const CanvasTargetDrawList& target = _canvasFrame._listTarget[targetIndex];
            CanvasTargetState*          pState = nullptr;
            for ( CanvasTargetState& state : _listCanvasTarget )
            {
                if ( state._path == target._targetPath )
                {
                    pState = &state;
                    break;
                }
            }
            if ( pState == nullptr || pState->_pTexture == nullptr || pState->_pTexture->isRhiValid() == false )
                continue;
            // 텍스처는 그린 것을 지킨다 — 내용이 같고 같은 텍스처면 다시 그리지 않는다(언리얼 위젯 컴포넌트의 다시 그리기 조건과 같은 자리).
            const RHITextureHandle texture = pState->_pTexture->getHandle();
            if ( target._contentRevision != 0 && target._contentRevision == pState->_drawnRevision && texture == pState->_drawnTexture )
                continue;
            const uint32                 width  = pState->_pTexture->getWidth();
            const uint32                 height = pState->_pTexture->getHeight();
            const RHIPipelineStateHandle pso    = findOutputPso( RenderPassType::Canvas, pState->_pTexture->getFormat() );
            if ( pso == 0 || width == 0 || height == 0 )
                continue;
            RHIRenderPassBeginInfo beginInfo{};
            beginInfo.setColorTarget( texture, target._clearColor, RHIRenderPassLoadOp::Clear );
            beginInfo._bBindColor = SW_TRUE;
            beginInfo._width      = width;
            beginInfo._height     = height;
            _pCmd->beginRenderPass( beginInfo );
            (void)_canvasRenderer.drawList( *_pCmd, target._list, _canvasRenderer.getTargetQuadBase( targetIndex ), pso, width, height,
                                            _pDevice->supportsNativeBindlessSampling() );
            _pCmd->endRenderPass();
            // 그린 텍스처는 셰이더가 읽는다(머티리얼 · 에디터 ImGui 이미지) — 카메라 렌더 텍스처 출력과 같이 읽기 상태로 둔다(Vulkan 레이아웃).
            _pCmd->prepareTextureForShaderRead( texture );
            pState->_drawnRevision = target._contentRevision;
            pState->_drawnTexture  = texture;
            ++_lastDrawnCanvasTargetCount;
        }
    }

    void FrameRenderer::releaseCanvasTargets()
    {
        if ( engine::areEngineServicesBound() )
        {
            for ( const CanvasTargetState& state : _listCanvasTarget )
            {
                if ( state._pTexture != nullptr )
                    engine::getAssetManager().getTextureManager().release( state._path.view(), _pDevice );
            }
        }
        _listCanvasTarget.clear();
    }
} // namespace sw

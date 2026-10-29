#include "pch.h"

#include "Engine/Renderer/Canvas/CanvasRenderer.h"

#include "Core/Math/BitUtil.h"

#include "Engine/Graphics/Canvas/CanvasDrawList.h"
#include "Engine/Graphics/RHI/IRHICommandList.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Graphics/Texture/Texture2D.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Renderer/Frame/Resource/RenderPSOCache.h"

namespace sw
{
    SW_LOG_CALLER( "Canvas" );

    namespace
    {
        struct CanvasRendererInternal
        {
            /** @brief 루트 상수 칸입니다 — canvas.hlsl 의 SwRootConstants 순서(시작 · 텍스처 넷 · 대상 너비 · 높이 · 색각 보정). */
            static constexpr uint32 kRootQuadBase      = 0;
            static constexpr uint32 kRootTexture0      = 1;
            static constexpr uint32 kRootTargetWidth   = kRootTexture0 + shaderslot::kCanvasTextureCount;
            static constexpr uint32 kRootTargetHeight  = kRootTargetWidth + 1;
            static constexpr uint32 kRootColorVision   = kRootTargetHeight + 1;
            static constexpr uint32 kRootConstantCount = kRootColorVision + 1;
            static_assert( kRootConstantCount <= shaderslot::kRootConstantDwords, "canvas root constants exceed the four-backend budget" );

            /** @brief 사각형 하나의 정점 수입니다 — canvas.hlsl 이 SV_VertexID 로 모서리 여섯을 만든다. 메시 없는 드로우라 장치 공용 대체 정점 버퍼가 이만큼 담아야 한다. */
            static constexpr uint32 kVerticesPerQuad = 6;
            static_assert( kVerticesPerQuad <= constant::kFallbackVertexCount, "the fallback vertex buffer must cover a canvas quad (slot 0 is read past its end)" );
        };
    } // namespace
} // namespace sw

namespace sw
{
    CanvasRenderer::CanvasRenderer()
        : _atlas{}
        , _textures{}
        , _quadBuffer{}
        , _markers{}
        , _lastDrawnTargetCount{ 0 }
    {
    }

    CanvasRenderer::~CanvasRenderer() = default;

    void CanvasRenderer::prepareFrame( IRHIDevice& device, CanvasFrameData& inoutFrame, const RenderPSOCache& psoCache )
    {
        _textures.prepareTargets( device, inoutFrame, psoCache );
        _markers.prepare( device, inoutFrame );
        SW_PROFILE_SCOPE( "RT.Canvas.Upload" );

        // 1) 아틀라스 업로드를 거울에 옮긴다(소비한 업로드는 비운다 — 같은 프레임 묶음을 다시 그려도 두 번 올리지 않게).
        _atlas.applyUploads( inoutFrame._listAtlasUpload );
        // 2) 페이지 텍스처 — 없으면 만들고 바뀐 구간을 올린다(GPU 완료를 기다리지 않는다).
        _atlas.uploadPages( device );
        // 경로 그림 — 게임 스레드는 디바이스가 없어 경로만 실었다. 머티리얼 텍스처와 같은 캐시에서 빌린다(기록 전이라 만들 수 있다).
        _textures.acquirePathTextures( device, inoutFrame._mainOutput );
        for ( const CanvasTargetDrawList& target : inoutFrame._listTarget )
        {
            _textures.acquirePathTextures( device, target._list );
        }

        // 3) 사각형 버퍼 — 마커 투영 컴퓨트가 앵커 원소를 쓰는 프레임이면 UAV 가 있어야 한다.
        _quadBuffer.upload( device, inoutFrame, _markers.getMarkerCount() > 0 );
    }

    void CanvasRenderer::recordMarkerProjection( IRHICommandList& cmd, RHIPipelineStateHandle pso, RHIBufferHandle instanceBuffer, RHIDescriptorIndex instanceSRV, RHIDescriptorIndex placeholderSRV,
                                                 GPUAssertMonitor& inoutAssert, bool bShowInstanceSlots )
    {
        _markers.record( cmd, _quadBuffer, pso, instanceBuffer, instanceSRV, placeholderSRV, inoutAssert, bShowInstanceSlots );
    }

    uint32 CanvasRenderer::drawList( IRHICommandList& cmd, const CanvasDrawList& list, uint32 quadBase, RHIPipelineStateHandle pso, uint32 targetWidth,
                                     uint32 targetHeight, bool bNativeBindless, uint32 colorVisionMode ) const
    {
        const bool bNothingToDraw = list.isEmpty() || pso == 0 || _quadBuffer.getSlot().isValid() == false || targetWidth == 0 || targetHeight == 0;
        if ( bNothingToDraw )
            return 0;
        SW_PROFILE_SCOPE( "RT.Canvas.Draw" );

        // 정점은 셰이더가 SV_VertexID 로 만든다 — 정점 버퍼 슬롯은 비운다(풀스크린 패스와 같다).
        cmd.setVertexBuffer( 0, 0, 0, 0 );
        cmd.setVertexBuffer( constant::kInstanceSlotStreamSlot, 0, 0, 0 );
        cmd.setPipelineState( pso );
        // setPipelineState 가 슬롯 상태를 비운다 — 버퍼는 PSO 뒤에 건다.
        cmd.bindStructuredBuffer( _quadBuffer.getSlot()._srv, shaderslot::kPassBuffer );

        const RHIScissorRect wholeTarget{ 0, 0, targetWidth, targetHeight };
        uint32               drawnBatchCount = 0;
        for ( const CanvasBatch& batch : list._listBatch )
        {
            if ( batch._quadCount == 0 )
                continue;
            cmd.setScissorRect( batch._bScissor == SW_TRUE ? batch._scissor : wholeTarget );

            uint32 arrRoot[CanvasRendererInternal::kRootConstantCount]{};
            arrRoot[CanvasRendererInternal::kRootQuadBase]     = quadBase + batch._firstQuad;
            arrRoot[CanvasRendererInternal::kRootTargetWidth]  = BitUtil::floatToBits( static_cast<float32>( targetWidth ) );
            arrRoot[CanvasRendererInternal::kRootTargetHeight] = BitUtil::floatToBits( static_cast<float32>( targetHeight ) );
            arrRoot[CanvasRendererInternal::kRootColorVision]  = colorVisionMode;
            for ( uint32 slot = 0; slot < shaderslot::kCanvasTextureCount; ++slot )
            {
                RHIDescriptorIndex srv = kInvalidDescriptorIndex;
                if ( slot < batch._textureCount )
                    srv = findTextureSRV( batch._arrTexture[slot] );
                if ( srv == kInvalidDescriptorIndex )
                {
                    arrRoot[CanvasRendererInternal::kRootTexture0 + slot] = invalid_index::kUint32;
                    continue;
                }
                if ( bNativeBindless )
                {
                    arrRoot[CanvasRendererInternal::kRootTexture0 + slot] = srv; // 전역 번호
                }
                else
                {
                    // 에뮬 백엔드(DX11 · GL)는 머티리얼 텍스처 슬롯 t16..t19 에 서수로 건다 — MaterialTexture::sampleIndex 가 서수로 고른다.
                    cmd.bindShaderResource( srv, shaderslot::kMaterialTexture0 + slot );
                    arrRoot[CanvasRendererInternal::kRootTexture0 + slot] = slot;
                }
            }
            cmd.setGraphicsRootConstants( 0, CanvasRendererInternal::kRootConstantCount, arrRoot );
            // 시작 인스턴스는 0 — D3D 는 SV_InstanceID 에 시작 인스턴스를 더하지 않는다. 시작 사각형은 루트 상수로 간다.
            cmd.drawInstanced( CanvasRendererInternal::kVerticesPerQuad, batch._quadCount, 0, 0 );
            ++drawnBatchCount;
        }
        // 가위를 대상 전체로 되돌린다(같은 패스에서 뒤에 그리는 것이 있어도 잘리지 않게).
        cmd.setScissorRect( wholeTarget );
        return drawnBatchCount;
    }

    void CanvasRenderer::release( IRHIDevice* pDevice )
    {
        _atlas.release( pDevice );
        _textures.release( pDevice );
        _quadBuffer.release( pDevice );
        _markers.release( pDevice );
    }

    void CanvasRenderer::drawTargets( IRHICommandList& cmd, IRHIDevice& device, const CanvasFrameData& frame, const RenderPSOCache& psoCache )
    {
        _lastDrawnTargetCount = 0;
        for ( uint32 targetIndex = 0; targetIndex < static_cast<uint32>( frame._listTarget.size() ); ++targetIndex )
        {
            const CanvasTargetDrawList& target                 = frame._listTarget[targetIndex];
            CanvasTargetLease*          pState                 = _textures.findTarget( target._targetPath );
            const bool                  bUnusableCanvasTexture = pState == nullptr || pState->_pTexture == nullptr || pState->_pTexture->isRHIValid() == false;
            if ( bUnusableCanvasTexture )
                continue;
            // 텍스처는 그린 것을 지킨다 — 내용이 같고 같은 텍스처면 다시 그리지 않는다.
            const RHITextureHandle texture          = pState->_pTexture->getHandle();
            const bool             bCanvasUnchanged = target._contentRevision != 0 && target._contentRevision == pState->_drawnRevision && texture == pState->_drawnTexture;
            if ( bCanvasUnchanged )
                continue;
            const uint32 width  = pState->_pTexture->getWidth();
            const uint32 height = pState->_pTexture->getHeight();
            // 셋업에 없는 포맷은 등록(`CanvasTextureLeases::prepareTargets`)이 거절하고 알렸다 — 다른 포맷의 PSO 로 대신 그리지 않는다.
            RHIPipelineStateHandle pso{ 0 };
            const bool             bHasPSO              = psoCache.findOutputPSO( RenderPassType::Canvas, pState->_pTexture->getFormat(), pso );
            const bool             bInvalidCanvasTarget = bHasPSO == false || pso == 0 || width == 0 || height == 0;
            if ( bInvalidCanvasTarget )
                continue;
            RHIRenderPassBeginInfo beginInfo{};
            beginInfo.setColorTarget( texture, target._clearColor, RHIRenderPassLoadOp::Clear );
            beginInfo._bBindColor = SW_TRUE;
            beginInfo._width      = width;
            beginInfo._height     = height;
            cmd.beginRenderPass( beginInfo );
            (void)drawList( cmd, target._list, getTargetQuadBase( targetIndex ), pso, width, height, device.supportsNativeBindlessSampling() );
            cmd.endRenderPass();
            // 그린 텍스처는 셰이더가 읽는다(머티리얼 · 에디터 ImGui 이미지) — 카메라 렌더 텍스처 출력과 같이 읽기 상태로 둔다(Vulkan 레이아웃).
            cmd.prepareTextureForShaderRead( texture );
            pState->_drawnRevision = target._contentRevision;
            pState->_drawnTexture  = texture;
            ++_lastDrawnTargetCount;
        }
    }

    RHIDescriptorIndex CanvasRenderer::findTextureSRV( const CanvasTextureRef& texture ) const
    {
        if ( texture._texture != nullptr )
            return texture._texture->isRHIValid() ? texture._texture->getSRV() : kInvalidDescriptorIndex;
        if ( texture._texturePath.empty() == false )
            return _textures.findPathTextureSRV( texture._texturePath );
        return _atlas.findPageSRV( texture._atlasPage );
    }
} // namespace sw

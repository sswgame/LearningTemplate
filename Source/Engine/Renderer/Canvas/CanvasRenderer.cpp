#include "pch.h"

#include "Engine/Renderer/Canvas/CanvasRenderer.h"

#include "Core/Common/BitUtil.h"
#include "Core/Common/HashUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/Log/LogOnce.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/Canvas/CanvasDrawList.h"
#include "Engine/Graphics/Debug/GPUAssert.h"
#include "Engine/Graphics/RHI/IRHICommandList.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Graphics/RHI/Support/RHITransferUtil.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Graphics/Texture/Texture2D.h"
#include "Engine/Graphics/Texture/TextureCache.h"
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

            /** @brief 사각형 버퍼가 모자라면 이 배율로 키운다(프레임마다 조금씩 늘 때 다시 만들기를 줄인다). */
            static constexpr float32 kQuadBufferGrowth = 1.5f;

            /** @brief 마커 투영 컴퓨트의 입력 행 배치입니다 — canvasmarker.hlsl 의 kRow* · kMarkerFlagActive · numthreads 와 같아야 합니다. */
            static constexpr uint32 kRowViewProjection     = 0;
            static constexpr uint32 kRowCamera             = 4;
            static constexpr uint32 kRowViewport           = 5;
            static constexpr uint32 kRowLimits             = 6;
            static constexpr uint32 kRowFirstMarker        = 8;
            static constexpr uint32 kMarkerRowCount        = 3;
            static constexpr uint32 kMarkerFlagActive      = 0x80000000u;
            static constexpr uint32 kMarkerThreadGroupSize = 64;
        };
    } // namespace
} // namespace sw

namespace sw
{
    CanvasRenderer::CanvasRenderer()
        : _listAtlasPage{}
        , _listPathTexture{}
        , _regionScratchBytes{}
        , _listAnchorScratch{}
        , _listMarkerRowScratch{}
        , _listUploadedAnchor{}
        , _quadBuffer{}
        , _markerRowBuffer{}
        , _uploadedRevision{ 0 }
        , _uploadedQuadCount{ 0 }
        , _mainQuadBase{ 0 }
        , _markerCount{ 0 }
        , _bMarkerReadsInstances{ false }
        , _lastUploadCallCount{ 0 }
        , _lastDrawnTargetCount{ 0 }
    {
    }

    CanvasRenderer::~CanvasRenderer() = default;

    void CanvasRenderer::prepareFrame( IRHIDevice& device, CanvasFrameData& inoutFrame, const RenderPSOCache& psoCache )
    {
        prepareTargets( device, inoutFrame, psoCache );
        prepareMarkers( device, inoutFrame );
        SW_PROFILE_SCOPE( "RT.Canvas.Upload" );
        _lastUploadCallCount = 0;

        // 1) 아틀라스 업로드를 거울에 옮긴다(소비한 업로드는 비운다 — 같은 프레임 묶음을 다시 그려도 두 번 올리지 않게).
        for ( const GlyphAtlasUpload& upload : inoutFrame._listAtlasUpload )
        {
            applyAtlasUpload( upload );
        }
        inoutFrame._listAtlasUpload.clear();

        // 2) 페이지 텍스처 — 없으면 만들고(새 디바이스 · 백엔드 교체 뒤에도 거울에서 다시 올린다) 바뀐 구간을 올린다. 프레임 경로라 GPU 완료를 기다리지 않는다:
        //    제출만 하고 완료는 이후 프레임에 폴링한다(`...AndWait` 를 부르면 Debug 단언).
        {
            const RHITransferUtil::ScopedForbidBlockingTransfer forbidBlocking;
            for ( AtlasPage& page : _listAtlasPage )
            {
                uploadAtlasPage( device, page );
            }
        }
        // 경로 그림 — 게임 스레드는 디바이스가 없어 경로만 실었다. 머티리얼 텍스처와 같은 캐시에서 빌린다(기록 전이라 만들 수 있다).
        acquirePathTextures( device, inoutFrame._mainOutput );
        for ( const CanvasTargetDrawList& target : inoutFrame._listTarget )
        {
            acquirePathTextures( device, target._list );
        }

        // 3) 사각형 버퍼 — 앞에 화면 앵커, 그 뒤에 주 출력, 그 뒤에 대상들을 잇는다. 내용 서명(주 출력 · 대상의 내용 번호)과 앵커 수가 같고 버퍼가 그대로면
        //    사각형은 올리지 않고 바뀐 앵커만 올린다(번호 0 은 "모른다" 라 늘 올린다).
        const uint32 anchorCount = static_cast<uint32>( inoutFrame._listAnchor.size() );
        _mainQuadBase            = anchorCount;
        uint32 quadCount         = anchorCount + static_cast<uint32>( inoutFrame._mainOutput._listQuad.size() );
        uint64 signature         = inoutFrame._contentRevision;
        bool   bKnown            = inoutFrame._contentRevision != 0;
        _listTargetQuadBase.clear();
        for ( const CanvasTargetDrawList& target : inoutFrame._listTarget )
        {
            _listTargetQuadBase.push_back( quadCount );
            quadCount += static_cast<uint32>( target._list._listQuad.size() );
            bKnown    = bKnown && target._contentRevision != 0;
            signature = HashUtil::mix64( signature ^ ( target._contentRevision * HashUtil::kGoldenRatio64 ) ^ target._targetPath.getHash() );
        }
        if ( quadCount == 0 )
            return;
        // 마커 투영 컴퓨트가 앵커 원소를 쓰려면 이 버퍼에 UAV 가 있어야 한다. 마커가 처음 올라온 프레임에 UAV 없이 만들어진 버퍼는 다시 만들고 내용을 다시 올린다.
        const bool bNeedsUAV = _markerCount > 0;
        if ( bNeedsUAV && _quadBuffer.isValid() && _quadBuffer._uav == kInvalidDescriptorIndex )
        {
            _quadBuffer.release( &device );
            _uploadedRevision = 0;
        }
        const RHIBufferUsage quadUsage = bNeedsUAV ? ( RHIBufferUsage::Structured | RHIBufferUsage::ShaderResource | RHIBufferUsage::UnorderedAccess ) : RHIBufferUsage::ShaderResource;
        signature                      = signature == 0 ? 1 : signature;
        const bool bSameContent        = bKnown && signature == _uploadedRevision && quadCount == _uploadedQuadCount && _quadBuffer.isValid();
        if ( bSameContent )
        {
            uploadAnchors( device, inoutFrame._listAnchor, false );
            return;
        }
        const CanvasQuad* pQuads = inoutFrame._mainOutput._listQuad.data();
        if ( inoutFrame._listTarget.empty() == false )
        {
            _listQuadScratch.clear();
            _listQuadScratch.insert( _listQuadScratch.end(), inoutFrame._mainOutput._listQuad.begin(), inoutFrame._mainOutput._listQuad.end() );
            for ( const CanvasTargetDrawList& target : inoutFrame._listTarget )
            {
                _listQuadScratch.insert( _listQuadScratch.end(), target._list._listQuad.begin(), target._list._listQuad.end() );
            }
            pQuads = _listQuadScratch.data();
        }
        if ( _quadBuffer._capacityElements < quadCount || _quadBuffer.isValid() == false )
        {
            const uint32 capacity = MathUtil::max( quadCount, static_cast<uint32>( static_cast<float32>( _quadBuffer._capacityElements ) * CanvasRendererInternal::kQuadBufferGrowth ) );
            _quadBuffer.release( &device );
            if ( _quadBuffer.ensureCapacity( &device, sizeof( CanvasQuad ), capacity, quadUsage, true, bNeedsUAV, nullptr ) == false )
            {
                SW_LOG_ERROR( "Failed to create the canvas quad buffer (%# quads) - the canvas is not drawn", capacity );
                _uploadedRevision = 0;
                return;
            }
        }
        const uint32 anchorBytes = anchorCount * static_cast<uint32>( sizeof( CanvasQuad ) );
        _quadBuffer.upload( &device, pQuads, ( quadCount - anchorCount ) * static_cast<uint32>( sizeof( CanvasQuad ) ), anchorBytes );
        uploadAnchors( device, inoutFrame._listAnchor, true );
        _uploadedRevision  = bKnown ? signature : 0;
        _uploadedQuadCount = quadCount;
    }

    void CanvasRenderer::uploadAnchors( IRHIDevice& device, const vector<CanvasAnchor>& listAnchor, bool bForce )
    {
        const bool bSameAnchors = bForce == false && listAnchor.size() == _listUploadedAnchor.size() &&
                                  ( listAnchor.empty() || Memory::compare( listAnchor.data(), _listUploadedAnchor.data(), listAnchor.size() * sizeof( CanvasAnchor ) ) == 0 );
        if ( bSameAnchors || listAnchor.empty() )
        {
            _listUploadedAnchor = listAnchor;
            return;
        }
        // 앵커 하나가 사각형 원소 하나다(셰이더는 그 rect 만 읽는다). 구조 버퍼 하나에 두어 캔버스 패스의 바인딩을 늘리지 않는다.
        _listAnchorScratch.resize( listAnchor.size() );
        for ( size_t index = 0; index < listAnchor.size(); ++index )
        {
            const CanvasAnchor& anchor      = listAnchor[index];
            _listAnchorScratch[index]._rect = float4{ anchor._position._x, anchor._position._y, anchor._scale, 0.0f };
        }
        _quadBuffer.upload( &device, _listAnchorScratch.data(), static_cast<uint32>( listAnchor.size() * sizeof( CanvasQuad ) ) );
        _listUploadedAnchor = listAnchor;
    }

    void CanvasRenderer::prepareMarkers( IRHIDevice& device, const CanvasFrameData& frame )
    {
        using Internal         = CanvasRendererInternal;
        _markerCount           = 0;
        _bMarkerReadsInstances = false;
        if ( frame._listMarker.empty() )
            return;
        // 입력 행 배치는 canvasmarker.hlsl 의 kRow* 와 1:1 이다(뷰 · 투영 4행 · 카메라 · 뷰포트 · 한계 · 여백 1행 뒤에 마커마다 두 행).
        const CanvasMarkerView& view        = frame._markerView;
        const uint32            markerCount = static_cast<uint32>( frame._listMarker.size() );
        const uint32            rowCount    = Internal::kRowFirstMarker + markerCount * Internal::kMarkerRowCount;
        _listMarkerRowScratch.assign( rowCount, float4{} );
        const float32* pMatrix = view._viewProjection.data(); // 행 우선 16 개 — 행 벡터 규약 그대로 올린다(셰이더가 행 곱으로 푼다)
        for ( uint32 row = 0; row < 4; ++row )
        {
            _listMarkerRowScratch[Internal::kRowViewProjection + row] = float4{ pMatrix[row * 4], pMatrix[row * 4 + 1], pMatrix[row * 4 + 2], pMatrix[row * 4 + 3] };
        }
        _listMarkerRowScratch[Internal::kRowCamera]   = float4{ view._cameraPosition._x, view._cameraPosition._y, view._cameraPosition._z, BitUtil::bitsToFloat( markerCount ) };
        _listMarkerRowScratch[Internal::kRowViewport] = float4{ view._viewportSize._x, view._viewportSize._y, view._uiScale, view._edgeMargin };
        _listMarkerRowScratch[Internal::kRowLimits]   = float4{ view._minClipW, view._minDistanceScale, view._maxDistanceScale, 0.0f };
        bool bAnyInstanceSlot                         = false;
        for ( uint32 index = 0; index < markerCount; ++index )
        {
            const CanvasMarkerRecord& record = frame._listMarker[index];
            const uint32              rowAt  = Internal::kRowFirstMarker + index * Internal::kMarkerRowCount;
            const uint32              bits   = record._flags | ( record._bActive != 0 ? Internal::kMarkerFlagActive : 0u );
            _listMarkerRowScratch[rowAt]     = float4{ record._worldPosition._x, record._worldPosition._y, record._worldPosition._z, record._maxDistance };
            _listMarkerRowScratch[rowAt + 1] = float4{ record._screenOffset._x, record._screenOffset._y, record._referenceDistance, BitUtil::bitsToFloat( bits ) };
            _listMarkerRowScratch[rowAt + 2] = float4{ record._worldOffset._x, record._worldOffset._y, record._worldOffset._z, BitUtil::bitsToFloat( record._instanceSlot ) };
            bAnyInstanceSlot                 = bAnyInstanceSlot || record._instanceSlot != invalid_index::kUint32;
        }
        constexpr RHIBufferUsage kRowUsage = RHIBufferUsage::Structured | RHIBufferUsage::ShaderResource;
        if ( _markerRowBuffer.ensureCapacity( &device, sizeof( float4 ), rowCount, kRowUsage, true, false, nullptr ) == false )
        {
            SW_LOG_ERROR( "Failed to create the canvas marker input buffer (%# rows) - GPU-projected markers are not placed", rowCount );
            return;
        }
        _markerRowBuffer.upload( &device, _listMarkerRowScratch.data(), rowCount * static_cast<uint32>( sizeof( float4 ) ) );
        _markerCount           = markerCount;
        _bMarkerReadsInstances = bAnyInstanceSlot;
    }

    void CanvasRenderer::recordMarkerProjection( IRHICommandList& cmd, RHIPipelineStateHandle pso, RHIBufferHandle instanceBuffer, RHIDescriptorIndex instanceSRV, RHIDescriptorIndex placeholderSRV,
                                                 GPUAssertMonitor& inoutAssert, bool bShowInstanceSlots )
    {
        if ( _markerCount == 0 )
            return;
        if ( pso == 0 )
        {
            // 셰이더가 쿠킹되지 않았거나 컴퓨트 능력이 없는 백엔드다. CPU 투영으로 대신하지 않는다 — 켜진 마커의 앵커 배율은 CPU 가 0 으로 올렸으므로 그려지지 않는다.
            SW_LOG_ERROR_ONCE( "The canvas marker projection compute PSO is missing (shader not cooked or no compute support) - GPU-projected markers are not drawn" );
            return;
        }
        if ( _quadBuffer.isValid() == false || _quadBuffer._uav == kInvalidDescriptorIndex || _markerRowBuffer.isValid() == false )
        {
            SW_LOG_ERROR_ONCE( "The canvas quad buffer has no UAV for the marker projection - GPU-projected markers are not drawn" );
            return;
        }
        // 인스턴스 칸을 읽는 마커가 있으면 GPUScene 인스턴스 버퍼(컬링 · 인스턴스 애니메이션이 이미 끝났다)를 읽는다. 읽을 버퍼가 없으면 칸을 해석할 수 없으므로 멈춘다.
        if ( _bMarkerReadsInstances && ( instanceBuffer == 0 || instanceSRV == kInvalidDescriptorIndex ) )
        {
            SW_LOG_ERROR_ONCE( "GPU-projected markers reference GPUScene instances but the instance buffer is not available - the markers are not placed" );
            return;
        }
        using Internal = CanvasRendererInternal;
        SW_PROFILE_SCOPE( "RT.Canvas.MarkerProject" );
        // 앵커 원소는 이 버퍼의 맨 앞이다. 컴퓨트가 쓰고 나면 캔버스 그리기가 읽기로 본다(프로브 갱신과 같은 상태 전환).
        cmd.transitionBuffer( _quadBuffer._buffer, RHIBufferState::UnorderedAccess );
        cmd.setComputePipelineState( pso );
        // g_MarkerRows(t0) / g_MarkerInstances(t1) / g_MarkerQuadsRW(u0). canvasmarker.hlsl 레지스터와 1:1 이다.
        cmd.bindComputeShaderResource( _markerRowBuffer._srv, 0 );
        if ( _bMarkerReadsInstances )
            cmd.transitionBuffer( instanceBuffer, RHIBufferState::ShaderResource );
        cmd.bindComputeShaderResource( _bMarkerReadsInstances ? instanceSRV : placeholderSRV, 1 );
        cmd.bindComputeUAV( _quadBuffer._uav, 0 );
        // 루트 상수 하나(canvasmarker.hlsl g_MarkerDebugMode) — 1 이면 앵커의 네 번째 값에 인스턴스 칸 유무 코드를 쓴다(보기 모드 MarkerSlots).
        const uint32 debugMode = bShowInstanceSlots ? 1u : 0u;
        cmd.setComputeRootConstants( 0, 1, &debugMode );
        inoutAssert.bind( cmd ); // canvasmarker.hlsl 의 assert(u4) — assert 변형 PSO 일 때만 버퍼가 있다
        cmd.dispatchCompute( ( _markerCount + Internal::kMarkerThreadGroupSize - 1u ) / Internal::kMarkerThreadGroupSize, 1, 1 );
        cmd.transitionBuffer( _quadBuffer._buffer, RHIBufferState::ShaderResource );
    }

    uint32 CanvasRenderer::drawList( IRHICommandList& cmd, const CanvasDrawList& list, uint32 quadBase, RHIPipelineStateHandle pso, uint32 targetWidth,
                                     uint32 targetHeight, bool bNativeBindless, uint32 colorVisionMode ) const
    {
        const bool bNothingToDraw = list.isEmpty() || pso == 0 || _quadBuffer.isValid() == false || targetWidth == 0 || targetHeight == 0;
        if ( bNothingToDraw )
            return 0;
        SW_PROFILE_SCOPE( "RT.Canvas.Draw" );

        // 정점은 셰이더가 SV_VertexID 로 만든다 — 정점 버퍼 슬롯은 비운다(풀스크린 패스와 같다).
        cmd.setVertexBuffer( 0, 0, 0, 0 );
        cmd.setVertexBuffer( constant::kInstanceSlotStreamSlot, 0, 0, 0 );
        cmd.setPipelineState( pso );
        // setPipelineState 가 슬롯 상태를 비운다 — 버퍼는 PSO 뒤에 건다.
        cmd.bindStructuredBuffer( _quadBuffer._srv, shaderslot::kPassBuffer );

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
        for ( AtlasPage& page : _listAtlasPage )
        {
            if ( pDevice != nullptr && page._texture != 0 )
            {
                if ( page._srv != kInvalidDescriptorIndex )
                    pDevice->getResourceFactory()->unregisterBindlessTexture( page._srv );
                pDevice->getResourceFactory()->destroyTexture( page._texture );
            }
            // 끝나지 않은 전송이 있어도 파괴할 수 있다 — 백엔드가 GPU 자원 해제를 전송 뒤로 미룬다(전송 계약 6). 티켓은 옛 장치 것이라 버린다.
            page._texture        = 0;
            page._srv            = kInvalidDescriptorIndex;
            page._registerTicket = RHITransferTicket{};
            page._bFailed        = SW_FALSE;
            page._listDirty.clear();
            // 거울은 남긴다 — 다음 prepareFrame 이 텍스처를 다시 만들고 전체를 올린다.
            page._bWholePageDirty = page._bytes.empty() ? SW_FALSE : SW_TRUE;
        }
        if ( _listPathTexture.empty() == false && engine::areEngineServicesBound() )
        {
            TextureCache& textures = engine::getSubsystem<AssetManager>().getAssetCache<TextureCache>();
            for ( const PathTexture& pathTexture : _listPathTexture )
            {
                if ( pathTexture._pTexture != nullptr )
                    textures.release( pathTexture._path.view(), pDevice );
            }
        }
        _listPathTexture.clear();
        if ( engine::areEngineServicesBound() )
        {
            TextureCache& textures = engine::getSubsystem<AssetManager>().getAssetCache<TextureCache>();
            for ( const TargetState& state : _listTarget )
            {
                if ( state._pTexture != nullptr )
                    textures.release( state._path.view(), pDevice );
            }
        }
        _listTarget.clear();
        _quadBuffer.release( pDevice );
        _markerRowBuffer.release( pDevice );
        _markerCount       = 0;
        _uploadedRevision  = 0;
        _uploadedQuadCount = 0;
        _listUploadedAnchor.clear();
    }

    void CanvasRenderer::drawTargets( IRHICommandList& cmd, IRHIDevice& device, const CanvasFrameData& frame, const RenderPSOCache& psoCache )
    {
        _lastDrawnTargetCount = 0;
        for ( uint32 targetIndex = 0; targetIndex < static_cast<uint32>( frame._listTarget.size() ); ++targetIndex )
        {
            const CanvasTargetDrawList& target                 = frame._listTarget[targetIndex];
            TargetState*                pState                 = findTarget( target._targetPath );
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
            // 셋업에 없는 포맷은 등록(`prepareTargets`)이 거절하고 알렸다 — 다른 포맷의 PSO 로 대신 그리지 않는다.
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

    void CanvasRenderer::mergeUploadRegions( vector<GlyphAtlasRect>& inoutListRegion, uint32 maxRegionCount )
    {
        if ( inoutListRegion.size() <= maxRegionCount || inoutListRegion.empty() )
            return;
        uint32 left   = inoutListRegion[0]._x;
        uint32 top    = inoutListRegion[0]._y;
        uint32 right  = left + inoutListRegion[0]._width;
        uint32 bottom = top + inoutListRegion[0]._height;
        for ( const GlyphAtlasRect& region : inoutListRegion )
        {
            left   = MathUtil::min( left, static_cast<uint32>( region._x ) );
            top    = MathUtil::min( top, static_cast<uint32>( region._y ) );
            right  = MathUtil::max( right, static_cast<uint32>( region._x ) + region._width );
            bottom = MathUtil::max( bottom, static_cast<uint32>( region._y ) + region._height );
        }
        GlyphAtlasRect bounds{};
        bounds._page   = inoutListRegion[0]._page;
        bounds._x      = static_cast<uint16>( left );
        bounds._y      = static_cast<uint16>( top );
        bounds._width  = static_cast<uint16>( right - left );
        bounds._height = static_cast<uint16>( bottom - top );
        inoutListRegion.clear();
        inoutListRegion.push_back( bounds );
    }

    void CanvasRenderer::applyAtlasUpload( const GlyphAtlasUpload& upload )
    {
        constexpr uint32 kPageSize  = GlyphAtlas::kPageSize;
        const bool       bInside    = static_cast<uint32>( upload._x ) + upload._width <= kPageSize && static_cast<uint32>( upload._y ) + upload._height <= kPageSize;
        const size_t     texelBytes = upload._bytesPerTexel;
        const bool       bComplete  = texelBytes > 0 && upload._bytes.size() >= static_cast<size_t>( upload._width ) * upload._height * texelBytes;
        if ( bInside == false || bComplete == false )
        {
            SW_LOG_ERROR( "Glyph atlas upload (%#,%# %#x%#) on page %# is outside the page or short of bytes - skipped", upload._x, upload._y, upload._width,
                          upload._height, upload._page );
            return;
        }
        if ( _listAtlasPage.size() <= upload._page )
            _listAtlasPage.resize( static_cast<size_t>( upload._page ) + 1 );
        AtlasPage& page = _listAtlasPage[upload._page];
        if ( page._bytes.empty() || upload._bWholePage == SW_TRUE )
        {
            page._bytesPerTexel = upload._bytesPerTexel;
            page._bytes.assign( static_cast<size_t>( kPageSize ) * kPageSize * texelBytes, 0 );
        }
        for ( uint32 row = 0; row < upload._height; ++row )
        {
            Memory::copy( page._bytes.data() + ( static_cast<size_t>( upload._y + row ) * kPageSize + upload._x ) * texelBytes,
                          upload._bytes.data() + static_cast<size_t>( row ) * upload._width * texelBytes, static_cast<size_t>( upload._width ) * texelBytes );
        }
        const bool bPartialRegionDirty = page._bWholePageDirty == SW_FALSE && upload._width > 0 && upload._height > 0;
        if ( upload._bWholePage == SW_TRUE )
        {
            page._bWholePageDirty = SW_TRUE;
            page._listDirty.clear();
        }
        else if ( bPartialRegionDirty )
        {
            page._listDirty.push_back( GlyphAtlasRect{ upload._page, upload._x, upload._y, upload._width, upload._height } );
        }
    }

    void CanvasRenderer::failAtlasPage( AtlasPage& page, string_view reason )
    {
        const size_t pageIndex = static_cast<size_t>( &page - _listAtlasPage.data() );
        SW_LOG_ERROR( "Glyph atlas page %# %# - its glyphs are not drawn until the canvas GPU resources are recreated", pageIndex, reason );
        page._bFailed        = SW_TRUE;
        page._registerTicket = RHITransferTicket{};
    }

    void CanvasRenderer::uploadAtlasPage( IRHIDevice& device, AtlasPage& page )
    {
        if ( page._bytes.empty() || page._bFailed == SW_TRUE )
            return;
        IRHIResourceFactory& factory = *device.getResourceFactory();
        if ( ensureAtlasPageTexture( factory, page ) == false )
            return;
        if ( registerAtlasPage( factory, page ) == false )
            return;

        // 실패 규칙: `Busy` 는 더티 상태를 그대로 두고 다음 프레임에 다시 낸다(미러가 최신 내용을 들고 있다). `Rejected` 는 다시 내도 안 되는 요청이라
        // 오류를 남기고 이 페이지를 멈춘다(`failAtlasPage`). 어느 쪽도 더티 상태를 버리고 지나가지 않는다.
        if ( page._bWholePageDirty == SW_TRUE )
            uploadWholeAtlasPage( factory, page );
        else
            uploadAtlasRegions( factory, page );
    }

    bool CanvasRenderer::ensureAtlasPageTexture( IRHIResourceFactory& factory, AtlasPage& page )
    {
        if ( page._texture != 0 )
            return true;
        RHITextureDesc desc{};
        desc._width              = GlyphAtlas::kPageSize;
        desc._height             = GlyphAtlas::kPageSize;
        desc._mipLevels          = 1;
        desc._format             = page._bytesPerTexel == 4 ? RHIFormat::R8G8B8A8_UNORM : RHIFormat::R8_UNORM;
        desc._bIsShaderResource  = SW_TRUE;
        desc._bIsRenderTarget    = SW_FALSE;
        desc._bIsDepthStencil    = SW_FALSE;
        desc._bIsUnorderedAccess = SW_FALSE;
        page._texture            = factory.createTexture2D( desc );
        if ( page._texture == 0 )
        {
            failAtlasPage( page, "could not be created" );
            return false;
        }
        // 등록은 첫 전체 업로드가 끝난 뒤다(전송 계약 10) — 끝나지 않은 전송이 있는 텍스처의 등록은 백엔드가 거절한다.
        page._bWholePageDirty = SW_TRUE;
        return true;
    }

    bool CanvasRenderer::registerAtlasPage( IRHIResourceFactory& factory, AtlasPage& page )
    {
        // 새 페이지는 첫 전체 업로드의 티켓이 `Complete` 가 된 프레임에 등록한다. 그 전에는 이 페이지의 글리프를 그리지 않고(SRV 가 없다), 그동안 쌓인 구간은
        // 등록한 프레임에 이어서 올린다(등록 전에 구간을 내면 마지막 전송이 늘 새것이라 등록이 계속 밀린다).
        if ( page._srv != kInvalidDescriptorIndex || page._registerTicket.isValid() == false )
            return true;
        const RHITransferStatus status = factory.pollTransfer( page._registerTicket );
        if ( status == RHITransferStatus::Pending )
            return false;
        if ( status == RHITransferStatus::Failed )
        {
            failAtlasPage( page, "first upload failed on the GPU" );
            return false;
        }
        page._registerTicket = RHITransferTicket{};
        page._srv            = factory.registerBindlessTexture( page._texture );
        if ( page._srv == kInvalidDescriptorIndex )
        {
            failAtlasPage( page, "could not be registered for bindless sampling" );
            return false;
        }
        return true;
    }

    void CanvasRenderer::uploadWholeAtlasPage( IRHIResourceFactory& factory, AtlasPage& page )
    {
        RHITextureUploadDesc upload{};
        upload._pData                  = page._bytes.data();
        upload._sizeBytes              = static_cast<uint32>( page._bytes.size() );
        upload._mipLevels              = 1;
        const RHITransferSubmit submit = factory.submitTextureUpload( page._texture, upload );
        if ( submit._result == RHISubmitResult::Busy )
            return;
        if ( submit.isAccepted() == false )
        {
            failAtlasPage( page, "upload was rejected" );
            return;
        }
        ++_lastUploadCallCount;
        page._bWholePageDirty = SW_FALSE;
        page._listDirty.clear();
        if ( page._srv == kInvalidDescriptorIndex )
            page._registerTicket = submit._ticket;
    }

    void CanvasRenderer::uploadAtlasRegions( IRHIResourceFactory& factory, AtlasPage& page )
    {
        constexpr uint32 kPageSize = GlyphAtlas::kPageSize;
        // 구간은 기다리지 않고 잇달아 낸다. 같은 텍스처에 낸 전송은 낸 순서대로, 뒤에 낼 프레임 커맨드보다 먼저 실행된다(전송 계약 3).
        mergeUploadRegions( page._listDirty, kMaxRegionUploadPerPage );
        size_t submittedCount = 0;
        for ( const GlyphAtlasRect& region : page._listDirty )
        {
            const size_t texelBytes = page._bytesPerTexel;
            _regionScratchBytes.resize( static_cast<size_t>( region._width ) * region._height * texelBytes );
            for ( uint32 row = 0; row < region._height; ++row )
            {
                Memory::copy( _regionScratchBytes.data() + static_cast<size_t>( row ) * region._width * texelBytes,
                              page._bytes.data() + ( static_cast<size_t>( region._y + row ) * kPageSize + region._x ) * texelBytes, static_cast<size_t>( region._width ) * texelBytes );
            }
            RHITextureRegionUploadDesc upload{};
            upload._pData                  = _regionScratchBytes.data();
            upload._sizeBytes              = static_cast<uint32>( _regionScratchBytes.size() );
            upload._x                      = region._x;
            upload._y                      = region._y;
            upload._width                  = region._width;
            upload._height                 = region._height;
            const RHITransferSubmit submit = factory.submitTextureRegionUpload( page._texture, upload );
            if ( submit._result == RHISubmitResult::Busy )
                break;
            if ( submit.isAccepted() == false )
            {
                failAtlasPage( page, "region upload was rejected" );
                return;
            }
            ++submittedCount;
            ++_lastUploadCallCount;
        }
        // 낸 앞부분만 지운다 — `Busy` 에서 멈춘 나머지는 다음 프레임에 같은 순서로 낸다.
        page._listDirty.erase( page._listDirty.begin(), page._listDirty.begin() + static_cast<ptrdiff_t>( submittedCount ) );
    }

    void CanvasRenderer::prepareTargets( IRHIDevice& device, const CanvasFrameData& frame, const RenderPSOCache& psoCache )
    {
        for ( TargetState& state : _listTarget )
        {
            state._bSeen = SW_FALSE;
        }
        const bool bServices = engine::areEngineServicesBound();
        for ( const CanvasTargetDrawList& target : frame._listTarget )
        {
            TargetState* pState = findTarget( target._targetPath );
            if ( pState == nullptr )
            {
                pState        = &_listTarget.emplace_back();
                pState->_path = target._targetPath;
            }
            pState->_bSeen = SW_TRUE;
            if ( bServices == false )
                continue;
            const uint32  width    = static_cast<uint32>( target._list._targetSize._x );
            const uint32  height   = static_cast<uint32>( target._list._targetSize._y );
            TextureCache& textures = engine::getSubsystem<AssetManager>().getAssetCache<TextureCache>();
            if ( pState->_pTexture != nullptr )
            {
                // 위젯이 그리기 크기를 바꿨으면 텍스처를 그 크기로 다시 만든다(같은 객체, 새 GPU 자원). 다시 만든 텍스처는 비어 있으므로 다시 그린다.
                const bool bResized = width > 0 && height > 0 && ( pState->_pTexture->getWidth() != width || pState->_pTexture->getHeight() != height );
                if ( bResized && textures.resizeRenderTarget( target._targetPath.view(), width, height, &device ) )
                    pState->_drawnRevision = 0;
                continue;
            }
            // 머티리얼과 같은 경로로 캐시에서 빌린다 — 처음 만드는 크기는 위젯의 그리기 크기다.
            textures.declareRenderTarget( target._targetPath.view(), width, height );
            pState->_pTexture      = textures.acquire( target._targetPath.view(), &device );
            pState->_drawnRevision = 0;
            // 등록 때 한 번 대상 포맷을 셋업 PSO 와 대조한다 — 없으면 거절을 알리고 그리지 않는다(`drawTargets` 는 다른 포맷의 PSO 로 대신 그리지 않는다).
            RHIPipelineStateHandle pso{ 0 };
            const bool             bHasPSO = pState->_pTexture != nullptr && psoCache.findOutputPSO( RenderPassType::Canvas, pState->_pTexture->getFormat(), pso ) && pso != 0;
            if ( pState->_pTexture != nullptr && bHasPSO == false )
                SW_LOG_ERROR( "Canvas target '%#' has format %# with no canvas PSO from setup - it is rejected and never drawn (add the format to FrameRenderer::createOutputPSOVariants)",
                              target._targetPath.c_str(), static_cast<uint32>( pState->_pTexture->getFormat() ) );
        }

        // 이번 목록에 없는 대상(위젯 컴포넌트가 사라졌다)은 돌려준다.
        for ( size_t index = _listTarget.size(); index > 0; --index )
        {
            TargetState& state = _listTarget[index - 1];
            if ( state._bSeen == SW_TRUE )
                continue;
            if ( state._pTexture != nullptr && bServices )
                engine::getSubsystem<AssetManager>().getAssetCache<TextureCache>().release( state._path.view(), &device );
            _listTarget.erase( _listTarget.begin() + static_cast<ptrdiff_t>( index - 1 ) );
        }
    }

    CanvasRenderer::TargetState* CanvasRenderer::findTarget( const hashed_string& path )
    {
        return VectorUtil::findBy<&TargetState::_path>( _listTarget, path );
    }

    RHIDescriptorIndex CanvasRenderer::findTextureSRV( const CanvasTextureRef& texture ) const
    {
        if ( texture._texture != nullptr )
            return texture._texture->isRHIValid() ? texture._texture->getSRV() : kInvalidDescriptorIndex;
        if ( texture._texturePath.empty() == false )
        {
            for ( const PathTexture& pathTexture : _listPathTexture )
            {
                if ( pathTexture._path == texture._texturePath )
                    return pathTexture._pTexture != nullptr && pathTexture._pTexture->isRHIValid() ? pathTexture._pTexture->getSRV() : kInvalidDescriptorIndex;
            }
            return kInvalidDescriptorIndex;
        }
        if ( texture._atlasPage < _listAtlasPage.size() )
        {
            // 첫 업로드가 끝나기 전(SRV 없음)이거나 멈춘 페이지는 그리지 않는다.
            const AtlasPage& page = _listAtlasPage[texture._atlasPage];
            return page._bFailed == SW_TRUE ? kInvalidDescriptorIndex : page._srv;
        }
        return kInvalidDescriptorIndex;
    }

    void CanvasRenderer::acquirePathTextures( IRHIDevice& device, const CanvasDrawList& list )
    {
        if ( engine::areEngineServicesBound() == false )
            return;
        for ( const CanvasBatch& batch : list._listBatch )
        {
            for ( uint32 slot = 0; slot < batch._textureCount; ++slot )
            {
                const hashed_string& path = batch._arrTexture[slot]._texturePath;
                if ( path.empty() || batch._arrTexture[slot]._texture != nullptr )
                    continue;
                bool bKnown = false;
                for ( const PathTexture& pathTexture : _listPathTexture )
                {
                    bKnown = bKnown || pathTexture._path == path;
                }
                if ( bKnown )
                    continue;
                PathTexture& added = _listPathTexture.emplace_back();
                added._path        = path;
                added._pTexture    = engine::getSubsystem<AssetManager>().getAssetCache<TextureCache>().acquire( path.view(), &device );
            }
        }
    }
} // namespace sw

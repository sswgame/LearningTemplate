#include "pch.h"

#include "Engine/Renderer/Canvas/CanvasRenderer.h"

#include "Core/Common/HashUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/Canvas/CanvasDrawList.h"
#include "Engine/Graphics/RHI/IRHICommandList.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Graphics/Texture/Texture2D.h"
#include "Engine/Graphics/Texture/TextureCache.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Renderer/Frame/Resource/RenderPSOCache.h"
#include "Engine/Resource/AssetManager.h"

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

            /** @brief float 의 비트를 uint32 로 옮깁니다(루트 상수는 dword 배열이다). */
            static uint32 toBits( float32 value )
            {
                uint32 bits = 0;
                Memory::copy( &bits, &value, sizeof( bits ) );
                return bits;
            }
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
        , _listUploadedAnchor{}
        , _quadBuffer{}
        , _uploadedRevision{ 0 }
        , _uploadedQuadCount{ 0 }
        , _mainQuadBase{ 0 }
        , _lastUploadCallCount{ 0 }
        , _lastDrawnTargetCount{ 0 }
        , _bTargetPSOMissingLogged{ SW_FALSE }
    {
    }

    CanvasRenderer::~CanvasRenderer() = default;

    void CanvasRenderer::prepareFrame( IRHIDevice& device, CanvasFrameData& inoutFrame )
    {
        prepareTargets( device, inoutFrame );
        SW_PROFILE_SCOPE( "RT.Canvas.Upload" );
        _lastUploadCallCount = 0;

        // 1) 아틀라스 업로드를 거울에 옮긴다(소비한 업로드는 비운다 — 같은 프레임 묶음을 다시 그려도 두 번 올리지 않게).
        for ( const GlyphAtlasUpload& upload : inoutFrame._listAtlasUpload )
        {
            applyAtlasUpload( upload );
        }
        inoutFrame._listAtlasUpload.clear();

        // 2) 페이지 텍스처 — 없으면 만들고(새 디바이스 · 백엔드 교체 뒤에도 거울에서 다시 올린다) 바뀐 구간을 올린다.
        for ( AtlasPage& page : _listAtlasPage )
        {
            uploadAtlasPage( device, page );
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
        signature               = signature == 0 ? 1 : signature;
        const bool bSameContent = bKnown && signature == _uploadedRevision && quadCount == _uploadedQuadCount && _quadBuffer.isValid();
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
            if ( _quadBuffer.ensureCapacity( &device, sizeof( CanvasQuad ), capacity, RHIBufferUsage::ShaderResource, true, false, nullptr ) == false )
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
            arrRoot[CanvasRendererInternal::kRootTargetWidth]  = CanvasRendererInternal::toBits( static_cast<float32>( targetWidth ) );
            arrRoot[CanvasRendererInternal::kRootTargetHeight] = CanvasRendererInternal::toBits( static_cast<float32>( targetHeight ) );
            arrRoot[CanvasRendererInternal::kRootColorVision]  = colorVisionMode;
            for ( uint32 slot = 0; slot < shaderslot::kCanvasTextureCount; ++slot )
            {
                RHIDescriptorIndex srv = kInvalidDescriptorIndex;
                if ( slot < batch._textureCount )
                    srv = findTextureSrv( batch._arrTexture[slot] );
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
                    // 에뮬 백엔드(DX11 · GL)는 머티리얼 텍스처 슬롯 t12..t15 에 서수로 건다 — swSampleMaterialTexture 가 서수로 고른다.
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
            page._texture = 0;
            page._srv     = kInvalidDescriptorIndex;
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
        _uploadedRevision  = 0;
        _uploadedQuadCount = 0;
        _listUploadedAnchor.clear();
    }

    void CanvasRenderer::drawTargets( IRHICommandList& cmd, IRHIDevice& device, const CanvasFrameData& frame, const RenderPSOCache& psoCache,
                                      RHIPipelineStateHandle fallbackPSO )
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
            const uint32           width  = pState->_pTexture->getWidth();
            const uint32           height = pState->_pTexture->getHeight();
            RHIPipelineStateHandle pso{ 0 };
            if ( psoCache.findOutputPSO( RenderPassType::Canvas, pState->_pTexture->getFormat(), pso ) == false )
            {
                pso = fallbackPSO;
                if ( _bTargetPSOMissingLogged == SW_FALSE )
                {
                    _bTargetPSOMissingLogged = SW_TRUE;
                    SW_LOG_ERROR( "Canvas target has no PSO for format %# from setup - add the format to FrameRenderer::createOutputPSOVariants",
                                  static_cast<uint32>( pState->_pTexture->getFormat() ) );
                }
            }
            const bool bInvalidCanvasTarget = pso == 0 || width == 0 || height == 0;
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
        constexpr uint32 kPageSize = GlyphAtlas::kPageSize;
        const bool       bInside   = static_cast<uint32>( upload._x ) + upload._width <= kPageSize && static_cast<uint32>( upload._y ) + upload._height <= kPageSize;
        const bool       bComplete = upload._bytes.size() >= static_cast<size_t>( upload._width ) * upload._height;
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
            page._bytes.assign( static_cast<size_t>( kPageSize ) * kPageSize, 0 );
        for ( uint32 row = 0; row < upload._height; ++row )
        {
            Memory::copy( page._bytes.data() + static_cast<size_t>( upload._y + row ) * kPageSize + upload._x, upload._bytes.data() + static_cast<size_t>( row ) * upload._width,
                          upload._width );
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

    void CanvasRenderer::uploadAtlasPage( IRHIDevice& device, AtlasPage& page )
    {
        if ( page._bytes.empty() )
            return;
        constexpr uint32     kPageSize = GlyphAtlas::kPageSize;
        IRHIResourceFactory& factory   = *device.getResourceFactory();
        if ( page._texture == 0 )
        {
            RHITextureDesc desc{};
            desc._width              = kPageSize;
            desc._height             = kPageSize;
            desc._mipLevels          = 1;
            desc._format             = RHIFormat::R8_UNORM;
            desc._bIsShaderResource  = SW_TRUE;
            desc._bIsRenderTarget    = SW_FALSE;
            desc._bIsDepthStencil    = SW_FALSE;
            desc._bIsUnorderedAccess = SW_FALSE;
            page._texture            = factory.createTexture2D( desc );
            if ( page._texture == 0 )
            {
                SW_LOG_ERROR( "Failed to create a %#x%# R8 glyph atlas page - its glyphs are not drawn", kPageSize, kPageSize );
                return;
            }
            page._srv             = factory.registerBindlessTexture( page._texture );
            page._bWholePageDirty = SW_TRUE;
        }

        if ( page._bWholePageDirty == SW_TRUE )
        {
            RHITextureUploadDesc upload{};
            upload._pData     = page._bytes.data();
            upload._sizeBytes = static_cast<uint32>( page._bytes.size() );
            upload._mipLevels = 1;
            if ( factory.uploadTexture2D( page._texture, upload ) == false )
                SW_LOG_ERROR( "Glyph atlas page upload failed" );
            ++_lastUploadCallCount;
            page._bWholePageDirty = SW_FALSE;
            page._listDirty.clear();
            return;
        }

        mergeUploadRegions( page._listDirty, kMaxRegionUploadPerPage );
        for ( const GlyphAtlasRect& region : page._listDirty )
        {
            _regionScratchBytes.resize( static_cast<size_t>( region._width ) * region._height );
            for ( uint32 row = 0; row < region._height; ++row )
            {
                Memory::copy( _regionScratchBytes.data() + static_cast<size_t>( row ) * region._width,
                              page._bytes.data() + static_cast<size_t>( region._y + row ) * kPageSize + region._x, region._width );
            }
            RHITextureRegionUploadDesc upload{};
            upload._pData     = _regionScratchBytes.data();
            upload._sizeBytes = static_cast<uint32>( _regionScratchBytes.size() );
            upload._x         = region._x;
            upload._y         = region._y;
            upload._width     = region._width;
            upload._height    = region._height;
            if ( factory.uploadTexture2DRegion( page._texture, upload ) == false )
                SW_LOG_ERROR( "Glyph atlas region upload failed" );
            ++_lastUploadCallCount;
        }
        page._listDirty.clear();
    }

    void CanvasRenderer::prepareTargets( IRHIDevice& device, const CanvasFrameData& frame )
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
        for ( TargetState& state : _listTarget )
        {
            if ( state._path == path )
                return &state;
        }
        return nullptr;
    }

    RHIDescriptorIndex CanvasRenderer::findTextureSrv( const CanvasTextureRef& texture ) const
    {
        if ( texture._texture != nullptr )
            return texture._texture->isRHIValid() ? texture._texture->getSrv() : kInvalidDescriptorIndex;
        if ( texture._texturePath.empty() == false )
        {
            for ( const PathTexture& pathTexture : _listPathTexture )
            {
                if ( pathTexture._path == texture._texturePath )
                    return pathTexture._pTexture != nullptr && pathTexture._pTexture->isRHIValid() ? pathTexture._pTexture->getSrv() : kInvalidDescriptorIndex;
            }
            return kInvalidDescriptorIndex;
        }
        if ( texture._atlasPage < _listAtlasPage.size() )
            return _listAtlasPage[texture._atlasPage]._srv;
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

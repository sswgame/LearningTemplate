#include "pch.h"

#include "Engine/Graphics/Renderer/Canvas/CanvasRenderer.h"

#include "Core/Common/HashUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/Canvas/CanvasDrawList.h"
#include "Engine/Graphics/RHI/IRHICommandList.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Graphics/Texture/Texture2D.h"
#include "Engine/Utility/Profiling/FrameProfiler.h"

namespace sw
{
    SW_LOG_CALLER( "Canvas" );

    namespace
    {
        struct CanvasRendererInternal
        {
            /** @brief 루트 상수 칸입니다 — canvas.hlsl 의 SwRootConstants 순서(시작 · 텍스처 넷 · 대상 너비 · 높이). */
            static constexpr uint32 kRootQuadBase      = 0;
            static constexpr uint32 kRootTexture0      = 1;
            static constexpr uint32 kRootTargetWidth   = kRootTexture0 + shaderslot::kMaterialTextureCount;
            static constexpr uint32 kRootTargetHeight  = kRootTargetWidth + 1;
            static constexpr uint32 kRootConstantCount = kRootTargetHeight + 1;
            static_assert( kRootConstantCount <= shaderslot::kRootConstantDwords, "canvas root constants exceed the four-backend budget" );

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
        , _regionScratchBytes{}
        , _quadBuffer{}
        , _uploadedRevision{ 0 }
        , _uploadedQuadCount{ 0 }
        , _lastUploadCallCount{ 0 }
    {
    }

    CanvasRenderer::~CanvasRenderer() = default;

    void CanvasRenderer::prepareFrame( IRHIDevice& device, CanvasFrameData& inoutFrame )
    {
        SW_PROFILE_SCOPE( "RT.Canvas.Upload" );
        _lastUploadCallCount = 0;

        // 1) 아틀라스 업로드를 거울에 옮긴다(소비한 업로드는 비운다 — 같은 프레임 묶음을 다시 그려도 두 번 올리지 않게).
        for ( const GlyphAtlasUpload& upload : inoutFrame._listAtlasUpload )
            applyAtlasUpload( upload );
        inoutFrame._listAtlasUpload.clear();

        // 2) 페이지 텍스처 — 없으면 만들고(새 디바이스 · 백엔드 교체 뒤에도 거울에서 다시 올린다) 바뀐 구간을 올린다.
        for ( AtlasPage& page : _listAtlasPage )
            uploadAtlasPage( device, page );

        // 3) 사각형 버퍼 — 주 출력 뒤에 대상들을 잇는다. 내용 서명(주 출력 · 대상의 내용 번호)이 같고 버퍼가 그대로면 올리지 않는다
        //    (번호 0 은 "모른다" 라 늘 올린다).
        uint32 quadCount = static_cast<uint32>( inoutFrame._mainOutput._listQuad.size() );
        uint64 signature = inoutFrame._contentRevision;
        bool   bKnown    = inoutFrame._contentRevision != 0;
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
            return;
        const CanvasQuad* pQuads = inoutFrame._mainOutput._listQuad.data();
        if ( inoutFrame._listTarget.empty() == false )
        {
            _listQuadScratch.clear();
            _listQuadScratch.insert( _listQuadScratch.end(), inoutFrame._mainOutput._listQuad.begin(), inoutFrame._mainOutput._listQuad.end() );
            for ( const CanvasTargetDrawList& target : inoutFrame._listTarget )
                _listQuadScratch.insert( _listQuadScratch.end(), target._list._listQuad.begin(), target._list._listQuad.end() );
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
        _quadBuffer.upload( &device, pQuads, quadCount * static_cast<uint32>( sizeof( CanvasQuad ) ) );
        _uploadedRevision  = bKnown ? signature : 0;
        _uploadedQuadCount = quadCount;
    }

    uint32 CanvasRenderer::drawList( IRHICommandList& cmd, const CanvasDrawList& list, uint32 quadBase, RHIPipelineStateHandle pso, uint32 targetWidth,
                                     uint32 targetHeight, bool bNativeBindless ) const
    {
        if ( list.isEmpty() || pso == 0 || _quadBuffer.isValid() == false || targetWidth == 0 || targetHeight == 0 )
            return 0;
        SW_PROFILE_SCOPE( "RT.Canvas.Draw" );

        // 정점은 셰이더가 SV_VertexID 로 만든다 — 정점 버퍼 슬롯은 비운다(풀스크린 패스와 같다).
        cmd.setVertexBuffer( 0, 0, 0, 0 );
        cmd.setVertexBuffer( constant::kInstanceSlotStreamSlot, 0, 0, 0 );
        cmd.setPipelineState( pso );
        // setPipelineState 가 슬롯 상태를 비운다 — 버퍼는 PSO 뒤에 건다.
        cmd.bindStructuredBuffer( _quadBuffer._srv, shaderslot::kCanvasQuadBuffer );

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
            for ( uint32 slot = 0; slot < shaderslot::kMaterialTextureCount; ++slot )
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
                    // 에뮬 백엔드(DX11 · GL)는 머티리얼 텍스처 슬롯 t5..t8 에 서수로 건다 — swSampleMaterialTexture 가 서수로 고른다.
                    cmd.bindShaderResource( srv, shaderslot::kMaterialTexture0 + slot );
                    arrRoot[CanvasRendererInternal::kRootTexture0 + slot] = slot;
                }
            }
            cmd.setGraphicsRootConstants( 0, CanvasRendererInternal::kRootConstantCount, arrRoot );
            // 시작 인스턴스는 0 — D3D 는 SV_InstanceID 에 시작 인스턴스를 더하지 않는다. 시작 사각형은 루트 상수로 간다.
            cmd.drawInstanced( 6, batch._quadCount, 0, 0 );
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
        _quadBuffer.release( pDevice );
        _uploadedRevision  = 0;
        _uploadedQuadCount = 0;
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
        if ( upload._bWholePage == SW_TRUE )
        {
            page._bWholePageDirty = SW_TRUE;
            page._listDirty.clear();
        }
        else if ( page._bWholePageDirty == SW_FALSE && upload._width > 0 && upload._height > 0 )
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

    RHIDescriptorIndex CanvasRenderer::findTextureSrv( const CanvasTextureRef& texture ) const
    {
        if ( texture._texture != nullptr )
            return texture._texture->isRhiValid() ? texture._texture->getSrv() : kInvalidDescriptorIndex;
        if ( texture._atlasPage < _listAtlasPage.size() )
            return _listAtlasPage[texture._atlasPage]._srv;
        return kInvalidDescriptorIndex;
    }
} // namespace sw

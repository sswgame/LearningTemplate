#include "pch.h"

#include "Engine/Renderer/Canvas/CanvasGlyphAtlasMirror.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Graphics/RHI/Transfer/RHITransferUtil.h"

namespace sw
{
    SW_LOG_CALLER( "Canvas" );

    CanvasGlyphAtlasMirror::CanvasGlyphAtlasMirror()
        : _listPage{}
        , _regionScratchBytes{}
        , _lastUploadCallCount{ 0 }
    {
    }

    CanvasGlyphAtlasMirror::~CanvasGlyphAtlasMirror() = default;

    void CanvasGlyphAtlasMirror::applyUploads( vector<GlyphAtlasUpload>& inoutListUpload )
    {
        for ( const GlyphAtlasUpload& upload : inoutListUpload )
        {
            applyUpload( upload );
        }
        inoutListUpload.clear();
    }

    void CanvasGlyphAtlasMirror::uploadPages( IRHIDevice& device )
    {
        _lastUploadCallCount = 0;
        // 페이지 텍스처 — 없으면 만들고(새 디바이스 · 백엔드 교체 뒤에도 거울에서 다시 올린다) 바뀐 구간을 올린다. 프레임 경로라 GPU 완료를 기다리지 않는다:
        // 제출만 하고 완료는 이후 프레임에 폴링한다(`...AndWait` 를 부르면 Debug 단언).
        const RHITransferUtil::ScopedForbidBlockingTransfer forbidBlocking;
        for ( AtlasPage& page : _listPage )
        {
            uploadPage( device, page );
        }
    }

    RHIDescriptorIndex CanvasGlyphAtlasMirror::findPageSRV( uint32 page ) const
    {
        if ( page >= _listPage.size() )
            return kInvalidDescriptorIndex;
        // 첫 업로드가 끝나기 전(SRV 없음)이거나 멈춘 페이지는 그리지 않는다.
        const AtlasPage& atlasPage = _listPage[page];
        return atlasPage._bFailed == SW_TRUE ? kInvalidDescriptorIndex : atlasPage._srv;
    }

    void CanvasGlyphAtlasMirror::release( IRHIDevice* pDevice )
    {
        for ( AtlasPage& page : _listPage )
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
            // 거울은 남긴다 — 다음 uploadPages 가 텍스처를 다시 만들고 전체를 올린다.
            page._bWholePageDirty = page._bytes.empty() ? SW_FALSE : SW_TRUE;
        }
    }

    void CanvasGlyphAtlasMirror::mergeUploadRegions( vector<GlyphAtlasRect>& inoutListRegion, uint32 maxRegionCount )
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

    void CanvasGlyphAtlasMirror::applyUpload( const GlyphAtlasUpload& upload )
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
        if ( _listPage.size() <= upload._page )
            _listPage.resize( static_cast<size_t>( upload._page ) + 1 );
        AtlasPage& page = _listPage[upload._page];
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

    void CanvasGlyphAtlasMirror::failPage( AtlasPage& page, string_view reason )
    {
        const size_t pageIndex = static_cast<size_t>( &page - _listPage.data() );
        SW_LOG_ERROR( "Glyph atlas page %# %# - its glyphs are not drawn until the canvas GPU resources are recreated", pageIndex, reason );
        page._bFailed        = SW_TRUE;
        page._registerTicket = RHITransferTicket{};
    }

    void CanvasGlyphAtlasMirror::uploadPage( IRHIDevice& device, AtlasPage& page )
    {
        if ( page._bytes.empty() || page._bFailed == SW_TRUE )
            return;
        IRHIResourceFactory& factory = *device.getResourceFactory();
        if ( ensurePageTexture( factory, page ) == false )
            return;
        if ( registerPage( factory, page ) == false )
            return;

        // 실패 규칙: `Busy` 는 더티 상태를 그대로 두고 다음 프레임에 다시 낸다(미러가 최신 내용을 들고 있다). `Rejected` 는 다시 내도 안 되는 요청이라
        // 오류를 남기고 이 페이지를 멈춘다(`failPage`). 어느 쪽도 더티 상태를 버리고 지나가지 않는다.
        if ( page._bWholePageDirty == SW_TRUE )
            uploadWholePage( factory, page );
        else
            uploadRegions( factory, page );
    }

    bool CanvasGlyphAtlasMirror::ensurePageTexture( IRHIResourceFactory& factory, AtlasPage& page )
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
            failPage( page, "could not be created" );
            return false;
        }
        // 등록은 첫 전체 업로드가 끝난 뒤다(전송 계약 10) — 끝나지 않은 전송이 있는 텍스처의 등록은 백엔드가 거절한다.
        page._bWholePageDirty = SW_TRUE;
        return true;
    }

    bool CanvasGlyphAtlasMirror::registerPage( IRHIResourceFactory& factory, AtlasPage& page )
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
            failPage( page, "first upload failed on the GPU" );
            return false;
        }
        page._registerTicket = RHITransferTicket{};
        page._srv            = factory.registerBindlessTexture( page._texture );
        if ( page._srv == kInvalidDescriptorIndex )
        {
            failPage( page, "could not be registered for bindless sampling" );
            return false;
        }
        return true;
    }

    void CanvasGlyphAtlasMirror::uploadWholePage( IRHIResourceFactory& factory, AtlasPage& page )
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
            failPage( page, "upload was rejected" );
            return;
        }
        ++_lastUploadCallCount;
        page._bWholePageDirty = SW_FALSE;
        page._listDirty.clear();
        if ( page._srv == kInvalidDescriptorIndex )
            page._registerTicket = submit._ticket;
    }

    void CanvasGlyphAtlasMirror::uploadRegions( IRHIResourceFactory& factory, AtlasPage& page )
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
                failPage( page, "region upload was rejected" );
                return;
            }
            ++submittedCount;
            ++_lastUploadCallCount;
        }
        // 낸 앞부분만 지운다 — `Busy` 에서 멈춘 나머지는 다음 프레임에 같은 순서로 낸다.
        page._listDirty.erase( page._listDirty.begin(), page._listDirty.begin() + static_cast<ptrdiff_t>( submittedCount ) );
    }
} // namespace sw

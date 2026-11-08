#include "pch.h"

#include "Engine/Graphics/Debug/GPUPixelInspector.h"

#include "Core/Container/vector.h"

#include "Engine/Foundation/EngineSubsystemRegistration.h"
#include "Engine/Graphics/Debug/RenderTargetRegistry.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Graphics/RHI/Transfer/RHITransferUtil.h"

namespace sw
{
    void GPUPixelInspector::requestPixelInspection( uint32 x, uint32 y, string_view attachment )
    {
        _pixelX.store( x );
        _pixelY.store( y );
        ScopedLock<mutex> lock{ _mutex };
        _pendingAttachment = string( attachment );
        _pendingX          = x;
        _pendingY          = y;
        _bDumpPending      = SW_TRUE;
    }

    void GPUPixelInspector::selectThread( uint32 thread )
    {
        _thread.store( thread );
    }

    void GPUPixelInspector::clearSelection()
    {
        _pixelX.store( SW_GPU_MESSAGE_NO_COORD );
        _pixelY.store( SW_GPU_MESSAGE_NO_COORD );
        _thread.store( SW_GPU_MESSAGE_NO_COORD );
    }

    GPUMessageSelection GPUPixelInspector::getSelection() const
    {
        GPUMessageSelection selection;
        selection._pixelX = _pixelX.load();
        selection._pixelY = _pixelY.load();
        selection._thread = _thread.load();
        return selection;
    }

    void GPUPixelInspector::setVisualizedAttachment( string_view attachment )
    {
        ScopedLock<mutex> lock{ _mutex };
        _visualizedAttachment = string( attachment );
        _bVisualizing.store( attachment.empty() ? SW_FALSE : SW_TRUE );
    }

    bool GPUPixelInspector::findVisualizedAttachment( string& outAttachment ) const
    {
        if ( _bVisualizing.load() == SW_FALSE )
            return false;
        ScopedLock<mutex> lock{ _mutex };
        outAttachment = _visualizedAttachment;
        return outAttachment.empty() == false;
    }

    bool GPUPixelInspector::takeAttachmentDump( uint32& outX, uint32& outY, string& outAttachment )
    {
        ScopedLock<mutex> lock{ _mutex };
        if ( _bDumpPending == SW_FALSE )
            return false;
        outX          = _pendingX;
        outY          = _pendingY;
        outAttachment = _pendingAttachment;
        _bDumpPending = SW_FALSE;
        return true;
    }

    void GPUPixelInspector::serviceAttachmentDump( IRHIResourceFactory& factory, const RenderTargetRegistry& registry )
    {
        uint32 x = 0;
        uint32 y = 0;
        string attachment;
        if ( takeAttachmentDump( x, y, attachment ) == false )
            return;
        vector<RenderTargetInfo> listTarget;
        registry.snapshot( listTarget );
        uint32 reported = 0;
        for ( const RenderTargetInfo& target : listTarget )
        {
            const bool bDepth   = target._bDepth != SW_FALSE;
            const bool bSkipped = ( attachment.empty() == false && target._name != attachment ) || RenderTargetReadback::isDecodableFormat( target._format, bDepth ) == false;
            const bool bOutside = x >= target._width || y >= target._height;
            if ( bSkipped )
                continue;
            ++reported;
            if ( bOutside )
            {
                SW_LOG_INFO_ID( "GPUInspect.PixelOutside", "pixel %#,%# is outside '%#' (%#x%#)", x, y, target._name.c_str(), target._width, target._height );
                continue;
            }
            vector<uint8>     bytes;
            RHITextureMipSpan layout{};
            float4            value{};
            const bool        bRead = RHITransferUtil::readbackTexture2DAndWait( factory, target._texture, 0, 0, bytes, layout ) &&
                               RenderTargetReadback::readPixel( bytes, layout, target._format, bDepth, x, y, value );
            if ( bRead == false )
            {
                SW_LOG_WARNING_ID( "GPUInspect.ReadFailed", "pixel %#,%#: '%#' could not be read back", x, y, target._name.c_str() );
                continue;
            }
            const uint32 channelCount = RenderTargetReadback::getChannelCount( target._format, bDepth );
            SW_LOG_INFO_ID( "GPUInspect.Pixel", "pixel %#,%# '%#' (%# channels): %# %# %# %#", x, y, target._name.c_str(), channelCount, value._x, value._y, value._z, value._w );
        }
        if ( reported == 0 )
            SW_LOG_WARNING_ID( "GPUInspect.NoAttachment", "pixel %#,%#: no readable attachment named '%#' is published", x, y, attachment.empty() ? "<any>" : attachment.c_str() );
    }
} // namespace sw

namespace sw
{
    // 픽셀 검사기의 선택 — 백엔드를 바꿔도 같은 실행의 것이다(개발 명령이 쓰고 렌더러가 읽는다).
    SW_ENGINE_SERVICE_SUBSYSTEM( GPUPixelInspector, HostOnly, Services, (), SW_ENGINE_DEFAULT_FACTORY( GPUPixelInspector ) );
} // namespace sw

#include "pch.h"

#include "Engine/Graphics/RHI/Diagnostics/RHIDrawDiagnostics.h"

#include "Core/Diagnostics/CallStackCapture.h"
#include "Core/Log/Logger.h"

#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/RHIDescriptors.h"
#include "Engine/Graphics/RHI/RHIGeometryBindings.h"

namespace sw
{
    RHIDrawDiagnostics::RHIDrawDiagnostics( const IRHIDevice& owner )
        : _pOwner{ &owner }
        , _reportCount{ 0 }
    {
    }

    uint32 RHIDrawDiagnostics::countReport() const
    {
        return _reportCount.fetch_add( 1u, std::memory_order_relaxed );
    }

    const utf8* RHIDrawDiagnostics::getBackendName() const
    {
        return _pOwner->getBackendName();
    }

    void RHIDrawDiagnostics::reportDestroyedVertexBuffer( RHIBufferHandle handle ) const
    {
        const uint32 count = countReport();
        if ( count < kMaxReportedDraw )
            SW_LOG_ERROR_ID( "RHI.DrawSkippedVertexBufferDestroyed", "%# draw skipped: its vertex buffer (handle %#) was destroyed while bound - rebind before drawing", getBackendName(), handle );
    }

    void RHIDrawDiagnostics::reportDrawWithoutRenderPass() const
    {
        const uint32 count = countReport();
        if ( count < kMaxReportedDraw )
            SW_LOG_ERROR_ID( "RHI.DrawSkippedNoRenderPass", "%# draw skipped: no render pass is open - something ended the pass (a buffer update inside it?) or the draw was recorded before beginRenderPass", getBackendName() );
    }

    void RHIDrawDiagnostics::reportBufferUpdateInsideRenderPass( const utf8* pWhat ) const
    {
        const uint32 count = countReport();
        if ( count >= kMaxReportedDraw )
            return;
        // 부른 자리를 함께 남긴다 — 업로드를 패스 앞으로 옮길 곳이 곧 그 호출자다.
        CallStack stack{};
        CallStackCapture::capture( stack, 2 );
        SW_LOG_ERROR_ID( "RHI.BufferUpdateInsideRenderPass", "%# %#: a buffer update inside an open render pass - the pass is ended and draws after it in that pass are dropped. Upload before beginRenderPass\n%#",
                         getBackendName(), pWhat, CallStackCapture::symbolize( stack ).c_str() );
    }

    void RHIDrawDiagnostics::reportIndexBufferUnbound() const
    {
        const uint32 count = countReport();
        if ( count < kMaxReportedDraw )
            SW_LOG_ERROR_ID( "RHI.DrawSkippedIndexBufferUnbound", "%# indexed draw skipped: no index buffer is bound - call setIndexBuffer before an indexed draw", getBackendName() );
    }

    void RHIDrawDiagnostics::reportDestroyedIndexBuffer( RHIBufferHandle handle ) const
    {
        const uint32 count = countReport();
        if ( count < kMaxReportedDraw )
            SW_LOG_ERROR_ID( "RHI.DrawSkippedIndexBufferDestroyed", "%# indexed draw skipped: its index buffer (handle %#) was destroyed while bound - rebind before drawing", getBackendName(), handle );
    }

    bool RHIDrawDiagnostics::checkVertexSlotsBound( const RHIVertexLayout& input, const RHIGeometryBindings& bindings ) const
    {
        const uint32              vertexSlotMask   = input._slotMask;
        const RHIMeshVertexFormat meshVertexFormat = input._format;
        for ( uint32 slot = 0; slot < constant::render::kVertexBufferSlotCount; ++slot )
        {
            if ( ( vertexSlotMask & ( 1u << slot ) ) == 0 )
                continue;
            const RHIBufferBinding& binding        = bindings._arrVertexSlot[slot];
            const uint32            expectedStride = constant::render::getVertexSlotStride( meshVertexFormat, slot );
            if ( binding._buffer != 0 && binding._stride == expectedStride )
                continue;
            const uint32 count = countReport();
            if ( count >= kMaxReportedDraw )
                return false;
            if ( binding._buffer == 0 )
                SW_LOG_ERROR_ID( "RHI.DrawSkippedVertexSlotUnbound",
                                 "%# draw skipped: its vertex shader reads vertex buffer slot %# but no buffer is bound - bind the mesh streams (slots 0 and 2) or instance slot stream (slot 1), or drop VertexInput from a shader that builds its vertices",
                                 getBackendName(), slot );
            else
                SW_LOG_ERROR_ID( "RHI.DrawSkippedVertexStrideMismatch",
                                 "%# draw skipped: vertex buffer slot %# has %#-byte elements but the PSO's mesh vertex format (%#) reads %#-byte elements - build the PSO with the mesh's vertex format",
                                 getBackendName(), slot, binding._stride, static_cast<uint32>( meshVertexFormat ), expectedStride );
            return false;
        }
        return true;
    }
} // namespace sw

#include "pch.h"

#include "Engine/Graphics/RHI/Diagnostics/RHIDrawDiagnostics.h"

#include "Core/Diagnostics/CallStackCapture.h"
#include "Core/Log/Logger.h"

#include "Engine/Graphics/Mesh/MeshVertex.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/RHIDescriptors.h"
#include "Engine/Graphics/RHI/RHIGeometryBindings.h"
#include "Engine/Graphics/RHI/RHIMeshVertexLayout.h"

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
            SW_LOG_ERROR_ID( "RHI.DrawSkippedVertexBufferDestroyed", "%# draw skipped: its vertex buffer (handle %#) was destroyed while bound - rebind before drawing", getBackendName(), handle.getPacked() );
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

    void RHIDrawDiagnostics::reportRenderPassInterrupted( const utf8* pOperation ) const
    {
        const uint32 count = countReport();
        if ( count >= kMaxReportedDraw )
            return;
        // 부른 자리를 함께 남긴다 — 패스를 먼저 닫거나 입력을 패스 앞으로 옮길 곳이 곧 그 호출자다.
        CallStack stack{};
        CallStackCapture::capture( stack, 2 );
        SW_LOG_ERROR_ID( "RHI.RenderPassInterrupted", "%# render pass interrupted by %#: work that cannot run inside a pass ended the open pass - draws after it in that pass are dropped. "
                                                      "Call endRenderPass first, or bind inputs before beginRenderPass\n%#",
                         getBackendName(), pOperation, CallStackCapture::symbolize( stack ).c_str() );
    }

    void RHIDrawDiagnostics::reportNullTexture( const utf8* pOperation ) const
    {
        const uint32 count = countReport();
        if ( count >= kMaxReportedDraw )
            return;
        // 부른 자리를 함께 남긴다 — 0 을 만든 쪽(없는 첨부 · 선언하지 않은 접근)이 곧 그 호출자다.
        CallStack stack{};
        CallStackCapture::capture( stack, 2 );
        SW_LOG_ERROR_ID( "RHI.NullTextureHandle", "%# %#: texture handle 0 is invalid - the call was dropped. The back buffer is kRHIBackBufferTexture; 0 comes from a missing attachment or an undeclared graph access\n%#",
                         getBackendName(), pOperation, CallStackCapture::symbolize( stack ).c_str() );
    }

    void RHIDrawDiagnostics::reportConstantBufferRewrite( RHIBufferHandle handle, uint32 offset ) const
    {
        const uint32 count = countReport();
        if ( count >= kMaxReportedDraw )
            return;
        // 부른 자리를 함께 남긴다 — 버퍼를 나눌 곳이 곧 그 호출자다.
        CallStack stack{};
        CallStackCapture::capture( stack, 2 );
        SW_LOG_ERROR_ID( "RHI.ConstantBufferRewrittenInFrame", "%# constant buffer %# (offset %#) was written twice in one frame - earlier draws read the last value. Give each value its own buffer (PassConstantRing)\n%#",
                         getBackendName(), handle.getPacked(), offset, CallStackCapture::symbolize( stack ).c_str() );
    }

    void RHIDrawDiagnostics::reportShaderSlotOutOfRange( const utf8* pOperation, uint32 slot, uint32 firstSlot, uint32 endSlot ) const
    {
        const uint32 count = countReport();
        if ( count >= kMaxReportedDraw )
            return;
        // 부른 자리를 함께 남긴다 — 슬롯 번호를 고칠 곳이 곧 그 호출자다.
        CallStack stack{};
        CallStackCapture::capture( stack, 2 );
        SW_LOG_ERROR_ID( "RHI.ShaderSlotOutOfRange", "%# %#: slot %# is outside [%#, %#) - the binding was dropped\n%#", getBackendName(), pOperation, slot, firstSlot, endSlot,
                         CallStackCapture::symbolize( stack ).c_str() );
    }

    void RHIDrawDiagnostics::reportRootConstantsOverflow( const utf8* pOperation, uint32 firstDword, uint32 count, uint32 capacity ) const
    {
        const uint32 reportCount = countReport();
        if ( reportCount >= kMaxReportedDraw )
            return;
        CallStack stack{};
        CallStackCapture::capture( stack, 2 );
        SW_LOG_ERROR_ID( "RHI.RootConstantsOverflow", "%# %#: %# dwords at %# do not fit the %#-dword root constant block - the write was dropped\n%#", getBackendName(), pOperation, count, firstDword,
                         capacity, CallStackCapture::symbolize( stack ).c_str() );
    }

    void RHIDrawDiagnostics::reportDescriptorNotRegistered( const utf8* pKind, uint32 slot, RHIDescriptorIndex index ) const
    {
        const uint32 count = countReport();
        if ( count < kMaxReportedDraw )
            SW_LOG_ERROR_ID( "RHI.DescriptorNotRegistered", "%# shader slot %# %#: descriptor index %# is not registered (released, or another kind of index) - the slot is left empty", getBackendName(),
                             pKind, slot, index.getValue() );
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
            SW_LOG_ERROR_ID( "RHI.DrawSkippedIndexBufferDestroyed", "%# indexed draw skipped: its index buffer (handle %#) was destroyed while bound - rebind before drawing", getBackendName(), handle.getPacked() );
    }

    bool RHIDrawDiagnostics::validateVertexSlotsBound( const RHIVertexLayout& input, const RHIGeometryBindings& bindings ) const
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

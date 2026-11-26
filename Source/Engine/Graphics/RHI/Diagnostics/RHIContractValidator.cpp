#include "pch.h"

#include "Engine/Graphics/RHI/Diagnostics/RHIContractValidator.h"

#include "Core/Log/Logger.h"

#include "Engine/Graphics/RHI/Diagnostics/RHIDrawDiagnostics.h"

namespace sw
{
    void RHIContractValidator::recordConstantBufferWriteInternal( RHIBufferHandle buffer, uint32 offset, const RHIDrawDiagnostics& diagnostics )
    {
        if ( buffer != 0 && _constantBufferWrites.recordWrite( buffer, offset ) == false )
            diagnostics.reportConstantBufferRewrite( buffer, offset );
    }

    void RHIContractValidator::recordBufferUploadInternal( RHIBufferHandle buffer, const utf8* pWhat )
    {
        if ( _bufferUploads.isUsedByFrameOnOtherThread( buffer ) )
            reportFatalError( "%# of buffer %# from another thread while the render thread records a frame that uses it - the copy has no order against that frame's reads and transitions. "
                              "Send the update to the render thread",
                              pWhat, buffer.getPacked() );
        _bufferUploads.recordContent( buffer );
    }

    void RHIContractValidator::recordTextureUploadInternal( RHITextureHandle texture, const utf8* pWhat, bool bRecordingPasses )
    {
        if ( _textureUploads.hasRecordedUse( texture ) )
            reportFatalError( "%# of texture %# after this frame recorded a transition or a render pass of it - the transfer runs before those commands, against a state they have not set. "
                              "Upload before the frame records (FrameRenderer::updateTextureUploads)",
                              pWhat, texture.getPacked() );
        if ( _textureUploads.isUsedByFrameOnOtherThread( texture ) )
            reportFatalError( "%# of texture %# from another thread while the render thread records a frame that can read it - the transfer has no order against that frame. "
                              "Queue the update for the render thread (TextureCache::requestRegionUpdate)",
                              pWhat, texture.getPacked() );
        if ( bRecordingPasses && _textureUploads.hasEarlierContent( texture ) )
            reportFatalError( "%# of texture %# on the render thread while it records the render graph - the transfer runs before the pass lists already recorded. Upload before the graph (a prepare step)",
                              pWhat, texture.getPacked() );
        _textureUploads.recordContent( texture );
    }

    void RHIContractValidator::assertTextureReadbackOrderInternal( RHITextureHandle texture, const utf8* pWhat )
    {
        if ( _textureUploads.hasRecordedUse( texture ) )
            reportFatalError( "%# of texture %# after this frame recorded a transition or a render pass of it - the copy runs before those commands and reads the previous content. Read back after endFrame",
                              pWhat, texture.getPacked() );
    }

    void RHIContractValidator::reportUploadDuringPassRecording( const utf8* pWhat )
    {
        reportFatalError( "%# on the render thread while it records the render graph - the copy runs before the pass lists already recorded, against the states they transitioned. "
                          "Upload before the graph (a prepare step)",
                          pWhat );
    }
} // namespace sw

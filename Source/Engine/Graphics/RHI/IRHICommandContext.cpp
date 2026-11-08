#include "pch.h"

#include "Engine/Graphics/RHI/IRHICommandContext.h"

#include "Engine/Graphics/RHI/Diagnostics/RHIDrawDiagnostics.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"

namespace sw
{
    IRHICommandContext::IRHICommandContext()
        : _bRenderPassOpen{ SW_FALSE }
        , _bRenderPassNoBackBuffer{ SW_FALSE }
    {
    }

    void IRHICommandContext::beginRenderPass( const RHIRenderPassBeginInfo& beginInfo )
    {
        endRenderPass(); // 패스는 겹치지 않는다
        const RHIRenderPassBeginResult result = beginRenderPassNative( beginInfo );
        _bRenderPassOpen                      = result == RHIRenderPassBeginResult::Begun;
        _bRenderPassNoBackBuffer              = result == RHIRenderPassBeginResult::NoBackBuffer;
    }

    void IRHICommandContext::endRenderPass()
    {
        if ( _bRenderPassOpen == false )
            return;
        endRenderPassNative();
        _bRenderPassOpen = SW_FALSE;
    }

    bool IRHICommandContext::isDrawAllowed() const
    {
        if ( _bRenderPassOpen )
            return true;
        if ( _bRenderPassNoBackBuffer == false )
            getContextDevice().getDrawDiagnostics().reportDrawWithoutRenderPass();
        return false;
    }

    void IRHICommandContext::draw( uint32 vertexCount, uint32 startVertex )
    {
        if ( isDrawAllowed() )
            drawNative( vertexCount, startVertex );
    }

    void IRHICommandContext::drawInstanced( uint32 vertexCount, uint32 instanceCount, uint32 startVertex, uint32 startInstance )
    {
        if ( isDrawAllowed() )
            drawInstancedNative( vertexCount, instanceCount, startVertex, startInstance );
    }

    void IRHICommandContext::drawIndexedInstanced( uint32 indexCount, uint32 instanceCount, uint32 startIndex, int32 baseVertex, uint32 startInstance )
    {
        if ( isDrawAllowed() )
            drawIndexedInstancedNative( indexCount, instanceCount, startIndex, baseVertex, startInstance );
    }

    void IRHICommandContext::drawIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset, uint32 drawCount, RHIBufferHandle countBuffer, uint32 countBufferOffset )
    {
        if ( isDrawAllowed() )
            drawIndirectNative( argumentBuffer, argumentBufferOffset, drawCount, countBuffer, countBufferOffset );
    }

    void IRHICommandContext::drawIndexedIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset, uint32 drawCount, RHIBufferHandle countBuffer, uint32 countBufferOffset )
    {
        if ( isDrawAllowed() )
            drawIndexedIndirectNative( argumentBuffer, argumentBufferOffset, drawCount, countBuffer, countBufferOffset );
    }

    void IRHICommandContext::setComputePipelineState( RHIPipelineStateHandle pso )
    {
        endRenderPass();
        setComputePipelineStateNative( pso );
    }

    void IRHICommandContext::dispatchCompute( uint32 threadGroupCountX, uint32 threadGroupCountY, uint32 threadGroupCountZ )
    {
        endRenderPass();
        dispatchComputeNative( threadGroupCountX, threadGroupCountY, threadGroupCountZ );
    }

    void IRHICommandContext::dispatchIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset )
    {
        endRenderPass();
        dispatchIndirectNative( argumentBuffer, argumentBufferOffset );
    }

    void IRHICommandContext::prepareTextureForShaderRead( RHITextureHandle texture )
    {
        endRenderPass();
        prepareTextureForShaderReadNative( texture );
    }

    void IRHICommandContext::prepareTextureForRenderTarget( RHITextureHandle texture )
    {
        endRenderPass();
        prepareTextureForRenderTargetNative( texture );
    }

    void IRHICommandContext::prepareTextureForUnorderedAccess( RHITextureHandle texture )
    {
        endRenderPass();
        prepareTextureForUnorderedAccessNative( texture );
    }

    void IRHICommandContext::blitTexture( RHITextureHandle src, RHITextureHandle dst )
    {
        endRenderPass();
        blitTextureNative( src, dst );
    }

    void IRHICommandContext::transitionBuffer( RHIBufferHandle buffer, RHIBufferState newState )
    {
        endRenderPass();
        transitionBufferNative( buffer, newState );
    }

    void IRHICommandContext::transitionBuffers( const RHIBufferTransition* pTransition, uint32 count )
    {
        if ( pTransition == nullptr || count == 0 )
            return;
        endRenderPass();
        transitionBuffersNative( pTransition, count );
    }

    void IRHICommandContext::transitionBuffersNative( const RHIBufferTransition* pTransition, uint32 count )
    {
        for ( uint32 index = 0; index < count; ++index )
        {
            transitionBufferNative( pTransition[index]._buffer, pTransition[index]._state );
        }
    }

    void IRHICommandContext::copyBufferToReadback( RHIBufferHandle source, RHIBufferHandle readback, uint32 sizeBytes )
    {
        endRenderPass();
        copyBufferToReadbackNative( source, readback, sizeBytes );
    }

    void IRHICommandContext::uavBarrier( RHIBufferHandle buffer )
    {
        endRenderPass();
        uavBarrierNative( buffer );
    }
} // namespace sw

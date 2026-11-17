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

    void IRHICommandContext::updateConstantBuffer( RHIBufferHandle buffer, const void* pData, uint32 size )
    {
        if ( buffer != 0 && getContextDevice().getConstantBufferWriteLog().recordWrite( buffer ) == false )
            getContextDevice().getDrawDiagnostics().reportConstantBufferRewrite( buffer );
        updateConstantBufferNative( buffer, pData, size );
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

    void IRHICommandContext::transitionTextures( const RHITextureTransition* pTransition, uint32 count )
    {
        if ( pTransition == nullptr || count == 0 )
            return;
        endRenderPass();
        transitionTexturesNative( pTransition, count );
    }

    void IRHICommandContext::transitionTexturesNative( const RHITextureTransition* pTransition, uint32 count )
    {
        for ( uint32 index = 0; index < count; ++index )
        {
            const RHITextureTransition& transition = pTransition[index];
            switch ( transition._state )
            {
                case RHITextureState::ShaderResource:
                    prepareTextureForShaderReadNative( transition._texture );
                    break;
                case RHITextureState::RenderTarget:
                    prepareTextureForRenderTargetNative( transition._texture );
                    break;
                case RHITextureState::UnorderedAccess:
                    prepareTextureForUnorderedAccessNative( transition._texture );
                    break;
            }
        }
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

    void IRHICommandContext::copyBufferRegion( RHIBufferHandle destination, uint64 destinationOffset, RHIBufferHandle source, uint64 sourceOffset, uint64 sizeBytes )
    {
        // 빈 복사 · 같은 버퍼(겹칠 수 있다 — DX12 · Vulkan 은 미정의)는 기록하지 않는다. 범위 검사는 백엔드가 버퍼 크기를 아는 곳에서 한다.
        if ( sizeBytes == 0 || destination == 0 || source == 0 || destination == source )
            return;
        endRenderPass();
        copyBufferRegionNative( destination, destinationOffset, source, sourceOffset, sizeBytes );
    }

    void IRHICommandContext::uavBarrier( RHIBufferHandle buffer )
    {
        endRenderPass();
        uavBarrierNative( buffer );
    }
} // namespace sw

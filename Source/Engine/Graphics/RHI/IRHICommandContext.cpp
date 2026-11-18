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
        endRenderPassBefore( "beginRenderPass" ); // 패스는 겹치지 않는다 — 닫지 않고 새로 열면 알린다
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

    void IRHICommandContext::endRenderPassBefore( const utf8* pOperation )
    {
        if ( _bRenderPassOpen == false )
            return;
        // 패스 밖의 일이 열린 패스를 닫으면 그 패스의 남은 드로우가 모두 빠진다(G버퍼가 패스를 연 뒤 입력을 걸어 장면이 사라졌다 — 132dd00f8e). 언리얼 RHI 검증처럼 그 자리에서 알리고 닫기는 그대로 한다.
        getContextDevice().getDrawDiagnostics().reportRenderPassInterrupted( pOperation );
        endRenderPass();
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
        endRenderPassBefore( "setComputePipelineState" );
        setComputePipelineStateNative( pso );
    }

    void IRHICommandContext::dispatchCompute( uint32 threadGroupCountX, uint32 threadGroupCountY, uint32 threadGroupCountZ )
    {
        endRenderPassBefore( "dispatchCompute" );
        dispatchComputeNative( threadGroupCountX, threadGroupCountY, threadGroupCountZ );
    }

    void IRHICommandContext::dispatchIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset )
    {
        endRenderPassBefore( "dispatchIndirect" );
        dispatchIndirectNative( argumentBuffer, argumentBufferOffset );
    }

    void IRHICommandContext::prepareTextureForShaderRead( RHITextureHandle texture )
    {
        endRenderPassBefore( "prepareTextureForShaderRead" );
        prepareTextureForShaderReadNative( texture );
    }

    void IRHICommandContext::prepareTextureForRenderTarget( RHITextureHandle texture )
    {
        endRenderPassBefore( "prepareTextureForRenderTarget" );
        prepareTextureForRenderTargetNative( texture );
    }

    void IRHICommandContext::prepareTextureForUnorderedAccess( RHITextureHandle texture )
    {
        endRenderPassBefore( "prepareTextureForUnorderedAccess" );
        prepareTextureForUnorderedAccessNative( texture );
    }

    void IRHICommandContext::transitionTextures( const RHITextureTransition* pTransition, uint32 count )
    {
        if ( pTransition == nullptr || count == 0 )
            return;
        endRenderPassBefore( "transitionTextures" );
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
        endRenderPassBefore( "blitTexture" );
        blitTextureNative( src, dst );
    }

    void IRHICommandContext::transitionBuffer( RHIBufferHandle buffer, RHIBufferState newState )
    {
        endRenderPassBefore( "transitionBuffer" );
        transitionBufferNative( buffer, newState );
    }

    void IRHICommandContext::transitionBuffers( const RHIBufferTransition* pTransition, uint32 count )
    {
        if ( pTransition == nullptr || count == 0 )
            return;
        endRenderPassBefore( "transitionBuffers" );
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
        endRenderPassBefore( "copyBufferToReadback" );
        copyBufferToReadbackNative( source, readback, sizeBytes );
    }

    void IRHICommandContext::copyBufferRegion( RHIBufferHandle destination, uint64 destinationOffset, RHIBufferHandle source, uint64 sourceOffset, uint64 sizeBytes )
    {
        // 빈 복사 · 같은 버퍼(겹칠 수 있다 — DX12 · Vulkan 은 미정의)는 기록하지 않는다. 범위 검사는 백엔드가 버퍼 크기를 아는 곳에서 한다.
        if ( sizeBytes == 0 || destination == 0 || source == 0 || destination == source )
            return;
        endRenderPassBefore( "copyBufferRegion" );
        copyBufferRegionNative( destination, destinationOffset, source, sourceOffset, sizeBytes );
    }

    void IRHICommandContext::uavBarrier( RHIBufferHandle buffer )
    {
        endRenderPassBefore( "uavBarrier" );
        uavBarrierNative( buffer );
    }
} // namespace sw

#include "pch.h"

#include "Engine/Graphics/RHI/IRHICommandContext.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Graphics/RHI/Diagnostics/RHIDrawDiagnostics.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Graphics/RHI/RHIDescriptors.h"

namespace sw
{
    IRHICommandContext::IRHICommandContext()
        : _bRenderPassOpen{ SW_FALSE }
        , _bRenderPassNoBackBuffer{ SW_FALSE }
    {
    }

    namespace
    {
        /** @brief 이 패스가 거는 컬러 타깃 중 핸들 0 이 있으면 true 입니다. 컬러 수 0 에 컬러를 걸면 0 번 하나를 건다(백엔드 다섯이 같은 규칙). */
        bool hasNullColorTarget( const RHIRenderPassBeginInfo& beginInfo )
        {
            if ( beginInfo._bBindColor == SW_FALSE )
                return false;
            const uint32 colorCount = beginInfo._colorTargetCount > 0 ? MathUtil::min( beginInfo._colorTargetCount, kMaxColorAttachments ) : 1u;
            for ( uint32 colorIndex = 0; colorIndex < colorCount; ++colorIndex )
            {
                if ( beginInfo._arrColorTarget[colorIndex] == 0 )
                    return true;
            }
            return false;
        }
    } // namespace

    void IRHICommandContext::beginRenderPass( const RHIRenderPassBeginInfo& beginInfo )
    {
        endRenderPassBefore( "beginRenderPass" ); // 패스는 겹치지 않는다 — 닫지 않고 새로 열면 알린다
        // 컬러 타깃 0 은 무효다(백버퍼는 `kRHIBackBufferTexture`) — 열지 않고 알린다. 이 패스의 드로우는 `isDrawAllowed` 가 알리고 버린다.
        if ( hasNullColorTarget( beginInfo ) )
        {
            getContextDevice().getDrawDiagnostics().reportNullTexture( "beginRenderPass" );
            _bRenderPassOpen         = SW_FALSE;
            _bRenderPassNoBackBuffer = SW_FALSE;
            return;
        }
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
        _computeGroupSize = pso != 0 ? getContextDevice().getResourceFactory()->getThreadGroupSize( pso ) : RHIThreadGroupSize{};
    }

    void IRHICommandContext::dispatchThreads( uint32 threadCountX, uint32 threadCountY, uint32 threadCountZ )
    {
        if ( threadCountX == 0 || threadCountY == 0 || threadCountZ == 0 )
            return;
        const bool bNoPipeline = _computeGroupSize.isValid() == false;
        SW_ASSERT( bNoPipeline == false && "dispatchThreads needs a bound compute PSO (its shader gives the thread group size)" );
        if ( bNoPipeline )
            return;
        dispatchCompute( ( threadCountX + _computeGroupSize._x - 1u ) / _computeGroupSize._x, ( threadCountY + _computeGroupSize._y - 1u ) / _computeGroupSize._y,
                         ( threadCountZ + _computeGroupSize._z - 1u ) / _computeGroupSize._z );
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

    bool IRHICommandContext::isTextureHandleValid( RHITextureHandle texture, const utf8* pOperation ) const
    {
        if ( texture != 0 )
            return true;
        getContextDevice().getDrawDiagnostics().reportNullTexture( pOperation );
        return false;
    }

    void IRHICommandContext::prepareTextureForShaderRead( RHITextureHandle texture )
    {
        if ( isTextureHandleValid( texture, "prepareTextureForShaderRead" ) == false )
            return;
        endRenderPassBefore( "prepareTextureForShaderRead" );
        prepareTextureForShaderReadNative( texture );
    }

    void IRHICommandContext::prepareTextureForRenderTarget( RHITextureHandle texture )
    {
        if ( isTextureHandleValid( texture, "prepareTextureForRenderTarget" ) == false )
            return;
        endRenderPassBefore( "prepareTextureForRenderTarget" );
        prepareTextureForRenderTargetNative( texture );
    }

    void IRHICommandContext::prepareTextureForUnorderedAccess( RHITextureHandle texture )
    {
        if ( isTextureHandleValid( texture, "prepareTextureForUnorderedAccess" ) == false )
            return;
        endRenderPassBefore( "prepareTextureForUnorderedAccess" );
        prepareTextureForUnorderedAccessNative( texture );
    }

    void IRHICommandContext::transitionTextures( const RHITextureTransition* pTransition, uint32 count )
    {
        if ( pTransition == nullptr || count == 0 )
            return;
        endRenderPassBefore( "transitionTextures" );
        // 핸들 0 원소는 알리고 그 원소만 뺀다 — 앞뒤의 정상 원소는 원래 차례대로 묶음째 낸다(복사 없이 0 사이 구간마다).
        uint32 runBegin = 0;
        for ( uint32 index = 0; index < count; ++index )
        {
            if ( isTextureHandleValid( pTransition[index]._texture, "transitionTextures" ) )
                continue;
            if ( index > runBegin )
                transitionTexturesNative( pTransition + runBegin, index - runBegin );
            runBegin = index + 1;
        }
        if ( count > runBegin )
            transitionTexturesNative( pTransition + runBegin, count - runBegin );
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
        if ( isTextureHandleValid( src, "blitTexture(src)" ) == false || isTextureHandleValid( dst, "blitTexture(dst)" ) == false )
            return;
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

    void IRHICommandContext::uavBarrierTexture( RHITextureHandle texture )
    {
        if ( isTextureHandleValid( texture, "uavBarrierTexture" ) == false )
            return;
        endRenderPassBefore( "uavBarrierTexture" );
        uavBarrierTextureNative( texture );
    }
} // namespace sw

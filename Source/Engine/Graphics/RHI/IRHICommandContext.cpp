#include "pch.h"

#include "Engine/Graphics/RHI/IRHICommandContext.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Graphics/RHI/Diagnostics/RHIComputeBindingLog.h"
#include "Engine/Graphics/RHI/Diagnostics/RHIDrawDiagnostics.h"
#include "Engine/Graphics/RHI/Diagnostics/RHIUploadLog.h"
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

    void IRHICommandContext::resetBindings()
    {
        _bindings         = RHIRecordingBindings{};
        _computeGroupSize = RHIThreadGroupSize{};
    }

    void IRHICommandContext::markBindingsDirty()
    {
        for ( RHIShaderSlots& slots : _bindings._arrSlots )
        {
            slots.markAllDirty();
        }
        for ( RHIRootConstants& constants : _bindings._arrRootConstants )
        {
            constants.markAllDirty();
        }
    }

    void IRHICommandContext::recordComputeBindings()
    {
        if constexpr ( build::kShipping == false )
        {
            RHIComputeBindingLog& log = getContextDevice().getComputeBindingLog();
            if ( log.isEnabled() == false || _bindings._computePSO == 0 )
                return;
            const RHIShaderSlots& slots = _bindings.getSlots( RHIBindPoint::Compute );
            for ( const RHIShaderSlotKind kind : { RHIShaderSlotKind::ShaderResource, RHIShaderSlotKind::UnorderedAccess } )
            {
                for ( uint32 slot = 0; slot < RHIShaderSlots::kShaderResourceCount; ++slot ) // 걸린 칸만 있다 — u 는 t 보다 좁다
                {
                    if ( slots.isBound( kind, slot ) )
                        log.record( slots.get( kind, slot ), kind == RHIShaderSlotKind::UnorderedAccess, _bindings._computePSO, slot );
                }
            }
        }
    }

    // ------------------------------------------------------------------------------
    // 바인딩 상태
    // ------------------------------------------------------------------------------
    void IRHICommandContext::bindSlot( RHIBindPoint point, RHIShaderSlotKind kind, uint32 firstSlot, uint32 endSlot, RHIDescriptorIndex index, uint32 slot, const utf8* pOperation )
    {
        if ( slot < firstSlot || slot >= endSlot )
        {
            getContextDevice().getDrawDiagnostics().reportShaderSlotOutOfRange( pOperation, slot, firstSlot, endSlot );
            return;
        }
        _bindings.getSlots( point ).bind( kind, slot, index );
    }

    void IRHICommandContext::bindConstantBuffer( RHIBufferSRVIndex constantBufferIndex, uint32 slot )
    {
        bindSlot( RHIBindPoint::Graphics, RHIShaderSlotKind::ConstantBuffer, 0, RHIShaderSlots::kConstantBufferCount, constantBufferIndex, slot, "bindConstantBuffer" );
    }

    void IRHICommandContext::bindStructuredBuffer( RHIBufferSRVIndex index, uint32 slot )
    {
        bindSlot( RHIBindPoint::Graphics, RHIShaderSlotKind::ShaderResource, 0, RHIShaderSlots::kBufferResourceCount, index, slot, "bindStructuredBuffer" );
    }

    void IRHICommandContext::bindShaderResource( RHITextureSRVIndex index, uint32 slot )
    {
        bindSlot( RHIBindPoint::Graphics, RHIShaderSlotKind::ShaderResource, RHIShaderSlots::kBufferResourceCount, RHIShaderSlots::kShaderResourceCount, index, slot, "bindShaderResource" );
    }

    uint32 IRHICommandContext::bindSampledTexture( RHITextureSRVIndex srv, uint32 emulatedSlot )
    {
        if ( getContextDevice().supportsNativeBindlessSampling() )
            return static_cast<uint32>( srv );
        bindShaderResource( srv, emulatedSlot );
        return 0u;
    }

    uint32 IRHICommandContext::bindMaterialTexture( RHITextureSRVIndex srv, uint32 ordinal )
    {
        const IRHIDevice& device = getContextDevice();
        if ( device.supportsNativeBindlessSampling() == false )
            bindShaderResource( srv, shaderslot::kMaterialTexture0 + ordinal );
        return device.getMaterialTextureIndex( srv, ordinal );
    }

    void IRHICommandContext::bindGraphicsUAV( RHIUnorderedAccessIndex index, uint32 slot )
    {
        bindSlot( RHIBindPoint::Graphics, RHIShaderSlotKind::UnorderedAccess, 0, shaderslot::kComputeUAVSlotCount, index, slot, "bindGraphicsUAV" );
    }

    void IRHICommandContext::bindComputeConstantBuffer( RHIBufferSRVIndex constantBufferIndex, uint32 slot )
    {
        bindSlot( RHIBindPoint::Compute, RHIShaderSlotKind::ConstantBuffer, 0, RHIShaderSlots::kConstantBufferCount, constantBufferIndex, slot, "bindComputeConstantBuffer" );
    }

    void IRHICommandContext::bindComputeShaderResource( RHIBufferSRVIndex index, uint32 slot )
    {
        bindSlot( RHIBindPoint::Compute, RHIShaderSlotKind::ShaderResource, 0, RHIShaderSlots::kBufferResourceCount, index, slot, "bindComputeShaderResource" );
    }

    void IRHICommandContext::bindComputeUAV( RHIUnorderedAccessIndex index, uint32 slot )
    {
        bindSlot( RHIBindPoint::Compute, RHIShaderSlotKind::UnorderedAccess, 0, RHIShaderSlots::kUnorderedAccessCount, index, slot, "bindComputeUAV" );
    }

    void IRHICommandContext::writeRootConstants( RHIBindPoint point, uint32 count, const void* pData, uint32 firstDword, const utf8* pOperation )
    {
        if ( _bindings.getRootConstants( point ).write( count, pData, firstDword ) == RHIRootConstantWrite::Overflow )
            getContextDevice().getDrawDiagnostics().reportRootConstantsOverflow( pOperation, firstDword, count, shaderslot::kRootConstantDwords );
    }

    void IRHICommandContext::setComputeRootConstants( uint32 num32BitValues, const void* pData, uint32 destOffsetIn32BitValues )
    {
        writeRootConstants( RHIBindPoint::Compute, num32BitValues, pData, destOffsetIn32BitValues, "setComputeRootConstants" );
    }

    void IRHICommandContext::setGraphicsRootConstants( uint32 num32BitValues, const void* pData, uint32 destOffsetIn32BitValues )
    {
        writeRootConstants( RHIBindPoint::Graphics, num32BitValues, pData, destOffsetIn32BitValues, "setGraphicsRootConstants" );
    }

    void IRHICommandContext::setPipelineState( RHIPipelineStateHandle pso )
    {
        // 등록되지 않은 핸들도 적는다 — 앞 PSO 를 남겨 두면 다음 드로우가 그것으로 대신 그려진다. 그 드로우는 백엔드가 멈춘다.
        _bindings.setGraphicsPipeline( pso );
    }

    void IRHICommandContext::setVertexBuffer( uint32 slot, RHIBufferHandle buffer, uint32 stride, uint32 offset )
    {
        _bindings._geometry.setVertexBuffer( slot, buffer, stride, offset );
    }

    void IRHICommandContext::setIndexBuffer( RHIBufferHandle buffer, uint32 indexStride, uint32 offset )
    {
        _bindings._geometry.setIndexBuffer( buffer, indexStride, offset );
    }

    void IRHICommandContext::setViewport( const RHIViewport& viewport )
    {
        if ( _bRenderPassOpen == false )
            return; // 패스를 열면 패스 전체로 돌아간다 — 패스 밖에서 건 값은 쓰이지 않는다
        setViewportNative( viewport );
        // 가위 = 뷰포트(왼쪽 위가 타깃 밖이면 그만큼 줄인다).
        const uint32 left   = static_cast<uint32>( MathUtil::max( viewport._x, 0.0f ) );
        const uint32 top    = static_cast<uint32>( MathUtil::max( viewport._y, 0.0f ) );
        const uint32 right  = static_cast<uint32>( MathUtil::max( viewport._x + viewport._width, 0.0f ) );
        const uint32 bottom = static_cast<uint32>( MathUtil::max( viewport._y + viewport._height, 0.0f ) );
        applyScissorRect( RHIScissorRect{ left, top, MathUtil::max( right, left ) - left, MathUtil::max( bottom, top ) - top } );
    }

    void IRHICommandContext::setScissorRect( const RHIScissorRect& rect )
    {
        if ( _bRenderPassOpen )
            applyScissorRect( rect );
    }

    void IRHICommandContext::applyScissorRect( const RHIScissorRect& rect )
    {
        // 타깃 밖의 가위는 API 마다 다르다(WebGPU 는 검증 오류) — 여기서 패스 안으로 자른다.
        const uint32 x = MathUtil::min( rect._x, _passExtent._width );
        const uint32 y = MathUtil::min( rect._y, _passExtent._height );
        setScissorRectNative( RHIScissorRect{ x, y, MathUtil::min( rect._width, _passExtent._width - x ), MathUtil::min( rect._height, _passExtent._height - y ) } );
    }

    RHIRenderPassExtent IRHICommandContext::computePassExtent( const RHIRenderPassInfo& beginInfo, const RHIRenderPassExtent& targetExtent )
    {
        return RHIRenderPassExtent{ beginInfo._width > 0 ? MathUtil::min( beginInfo._width, targetExtent._width ) : targetExtent._width,
                                    beginInfo._height > 0 ? MathUtil::min( beginInfo._height, targetExtent._height ) : targetExtent._height };
    }

    namespace
    {
        /** @brief 이 패스가 거는 컬러 타깃 중 핸들 0 이 있으면 true 입니다. 컬러 수 0 에 컬러를 걸면 0 번 하나를 건다(백엔드 다섯이 같은 규칙). */
        bool hasNullColorTarget( const RHIRenderPassInfo& beginInfo )
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

    void IRHICommandContext::beginRenderPass( const RHIRenderPassBeginInfo& passInfo )
    {
        const RHIRenderPassInfo& beginInfo = passInfo.getInfo();
        endRenderPassBefore( "beginRenderPass" ); // 패스는 겹치지 않는다 — 닫지 않고 새로 열면 알린다
        // 컬러 타깃 0 은 무효다(백버퍼는 `kRHIBackBufferTexture`) — 열지 않고 알린다. 이 패스의 드로우는 `isDrawAllowed` 가 알리고 버린다.
        if ( hasNullColorTarget( beginInfo ) )
        {
            getContextDevice().getDrawDiagnostics().reportNullTexture( "beginRenderPass" );
            _bRenderPassOpen         = SW_FALSE;
            _bRenderPassNoBackBuffer = SW_FALSE;
            return;
        }
        if ( beginInfo._bBindColor != SW_FALSE )
        {
            const uint32 colorCount = beginInfo._colorTargetCount > 0 ? MathUtil::min( beginInfo._colorTargetCount, kMaxColorAttachments ) : 1u;
            for ( uint32 colorIndex = 0; colorIndex < colorCount; ++colorIndex )
            {
                recordTextureUse( beginInfo._arrColorTarget[colorIndex] );
            }
        }
        recordTextureUse( beginInfo._depthTarget );
        _bindings.resetGraphics();
        RHIRenderPassExtent            targetExtent{};
        const RHIRenderPassBeginResult result = beginRenderPassNative( beginInfo, targetExtent );
        _bRenderPassOpen                      = result == RHIRenderPassBeginResult::Begun;
        _bRenderPassNoBackBuffer              = result == RHIRenderPassBeginResult::NoBackBuffer;
        if ( _bRenderPassOpen == false )
            return;
        // 앞 패스의 뷰포트 · 가위가 새지 않게 패스 전체로 연다.
        _passExtent = computePassExtent( beginInfo, targetExtent );
        RHIViewport viewport{};
        viewport._width    = static_cast<float32>( _passExtent._width );
        viewport._height   = static_cast<float32>( _passExtent._height );
        viewport._minDepth = 0.0f;
        viewport._maxDepth = 1.0f;
        setViewport( viewport );
    }

    void IRHICommandContext::updateConstantBuffer( RHIBufferHandle buffer, const void* pData, uint32 size )
    {
        if constexpr ( build::kShipping == false )
        {
            if ( buffer != 0 && getContextDevice().getConstantBufferWriteLog().recordWrite( buffer, 0 ) == false )
                getContextDevice().getDrawDiagnostics().reportConstantBufferRewrite( buffer, 0 );
        }
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
        if ( pso == _bindings._computePSO )
            return;
        _bindings.setComputePipeline( pso );
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
        recordComputeBindings();
        dispatchComputeNative( threadGroupCountX, threadGroupCountY, threadGroupCountZ );
    }

    void IRHICommandContext::dispatchIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset )
    {
        endRenderPassBefore( "dispatchIndirect" );
        recordComputeBindings();
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
        recordTextureUse( texture );
        prepareTextureForShaderReadNative( texture );
    }

    void IRHICommandContext::prepareTextureForRenderTarget( RHITextureHandle texture )
    {
        if ( isTextureHandleValid( texture, "prepareTextureForRenderTarget" ) == false )
            return;
        endRenderPassBefore( "prepareTextureForRenderTarget" );
        recordTextureUse( texture );
        prepareTextureForRenderTargetNative( texture );
    }

    void IRHICommandContext::prepareTextureForUnorderedAccess( RHITextureHandle texture )
    {
        if ( isTextureHandleValid( texture, "prepareTextureForUnorderedAccess" ) == false )
            return;
        endRenderPassBefore( "prepareTextureForUnorderedAccess" );
        recordTextureUse( texture );
        prepareTextureForUnorderedAccessNative( texture );
    }

    void IRHICommandContext::transitionTextures( const RHITextureTransition* pTransition, uint32 count )
    {
        if ( pTransition == nullptr || count == 0 )
            return;
        endRenderPassBefore( "transitionTextures" );
        for ( uint32 index = 0; index < count; ++index )
        {
            recordTextureUse( pTransition[index]._texture );
        }
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
        recordTextureUse( src );
        recordTextureUse( dst );
        blitTextureNative( src, dst );
    }

    void IRHICommandContext::recordBufferUse( RHIBufferHandle buffer )
    {
        if constexpr ( build::kShipping == false )
            getContextDevice().getBufferUploadLog().recordUse( buffer );
    }

    void IRHICommandContext::recordTextureUse( RHITextureHandle texture )
    {
        if constexpr ( build::kShipping == false )
            getContextDevice().getTextureUploadLog().recordUse( texture );
    }

    void IRHICommandContext::transitionBuffer( RHIBufferHandle buffer, RHIBufferState newState )
    {
        endRenderPassBefore( "transitionBuffer" );
        recordBufferUse( buffer );
        transitionBufferNative( buffer, newState );
    }

    void IRHICommandContext::transitionBuffers( const RHIBufferTransition* pTransition, uint32 count )
    {
        if ( pTransition == nullptr || count == 0 )
            return;
        endRenderPassBefore( "transitionBuffers" );
        for ( uint32 index = 0; index < count; ++index )
        {
            recordBufferUse( pTransition[index]._buffer );
        }
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
        const uint64 sourceBytes   = findBufferBytes( source );
        const uint64 readbackBytes = findBufferBytes( readback );
        if ( sourceBytes == 0 || readbackBytes == 0 )
            return; // 놓은 핸들이다
        if ( sizeBytes > sourceBytes || sizeBytes > readbackBytes )
        {
            SW_LOG_ERROR_ID( "RHI.CopyBufferToReadbackOutside", "copyBufferToReadback: %# bytes do not fit the source (%# bytes) and the readback buffer (%# bytes); skipped", sizeBytes, sourceBytes, readbackBytes );
            return;
        }
        endRenderPassBefore( "copyBufferToReadback" );
        recordBufferUse( source );
        copyBufferToReadbackNative( source, readback, sizeBytes );
    }

    void IRHICommandContext::copyBufferRegion( RHIBufferHandle destination, uint64 destinationOffset, RHIBufferHandle source, uint64 sourceOffset, uint64 sizeBytes )
    {
        // 빈 복사 · 같은 버퍼(겹칠 수 있다 — DX12 · Vulkan 은 미정의)는 기록하지 않는다.
        if ( sizeBytes == 0 || destination == 0 || source == 0 || destination == source )
            return;
        const uint64 sourceBytes      = findBufferBytes( source );
        const uint64 destinationBytes = findBufferBytes( destination );
        if ( sourceBytes == 0 || destinationBytes == 0 )
            return; // 놓은 핸들이다
        // 범위 밖 복사를 다섯 백엔드가 저마다 다루면 Vulkan(하위 할당)은 이웃 버퍼를 조용히 덮고 GL 은 그 복사만, WebGPU 는 명령 묶음 전체를 버린다 — 한 곳에서 거절하고 알린다.
        // https://registry.khronos.org/vulkan/specs/latest/man/html/vkCmdCopyBuffer.html (VUID-vkCmdCopyBuffer-size-00115 · 00116)
        const bool bSourceInside      = sizeBytes <= sourceBytes && sourceOffset <= sourceBytes - sizeBytes;
        const bool bDestinationInside = sizeBytes <= destinationBytes && destinationOffset <= destinationBytes - sizeBytes;
        if ( bSourceInside == false || bDestinationInside == false )
        {
            SW_LOG_ERROR_ID( "RHI.CopyBufferRegionOutside", "copyBufferRegion: %# bytes from %# of a %#-byte buffer to %# of a %#-byte buffer is outside a buffer; skipped", sizeBytes, sourceOffset, sourceBytes,
                             destinationOffset, destinationBytes );
            return;
        }
        endRenderPassBefore( "copyBufferRegion" );
        recordBufferUse( destination );
        recordBufferUse( source );
        copyBufferRegionNative( destination, destinationOffset, source, sourceOffset, sizeBytes );
    }

    void IRHICommandContext::uavBarrier( RHIBufferHandle buffer )
    {
        endRenderPassBefore( "uavBarrier" );
        uavBarrierNative( buffer );
    }

    void IRHICommandContext::beginTextureAlias( RHITextureHandle texture )
    {
        if ( isTextureHandleValid( texture, "beginTextureAlias" ) == false )
            return;
        endRenderPassBefore( "beginTextureAlias" );
        recordTextureUse( texture );
        beginTextureAliasNative( texture );
    }

    void IRHICommandContext::uavBarrierTexture( RHITextureHandle texture )
    {
        if ( isTextureHandleValid( texture, "uavBarrierTexture" ) == false )
            return;
        endRenderPassBefore( "uavBarrierTexture" );
        recordTextureUse( texture );
        uavBarrierTextureNative( texture );
    }
} // namespace sw

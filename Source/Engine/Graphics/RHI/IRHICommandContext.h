/**
 * @file IRHICommandContext.h
 * @brief RHI 커맨드 컨텍스트 인터페이스 및 관련 선언
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#include "Engine/Graphics/RHI/ICommandReplayTarget.h"
#include "Engine/Graphics/RHI/RHICommandListDefaults.h"

namespace sw
{
	class RHIDeferredCommandList;

	/**
	 * @class IRHICommandList
	 * @brief GPU 그래픽스/컴퓨트 명령을 기록하는 커맨드 리스트
	 */
	class SW_API IRHICommandList
	{
	public:
		// ------------------------------------------------------------------------------
		// 1) 수명 — 복사 금지, asDeferred는 지연 리플레이용 다운캐스트
		// ------------------------------------------------------------------------------
		virtual ~IRHICommandList()							 = default;
		IRHICommandList()									 = default;
		IRHICommandList( const IRHICommandList& )			 = delete;
		IRHICommandList& operator=( const IRHICommandList& ) = delete;

		virtual RHIDeferredCommandList*		  asDeferred() { return nullptr; }
		virtual const RHIDeferredCommandList* asDeferred() const { return nullptr; }

		// ------------------------------------------------------------------------------
		// 2) 기록 범위 · 뷰포트 · PSO · 렌더 패스
		// ------------------------------------------------------------------------------
		virtual void beginCommandList()											= 0;
		virtual void endCommandList()											= 0;
		virtual void setViewport( const RHIViewport& viewport )					= 0;
		virtual void setPipelineState( RHIPipelineStateHandle pso )				= 0;
		virtual void beginRenderPass( const RHIRenderPassBeginInfo& beginInfo ) = 0;
		virtual void endRenderPass()											= 0;

		// ------------------------------------------------------------------------------
		// 3) 드로우 — 메시 버텍스/인덱스, 머티리얼 CB
		// ------------------------------------------------------------------------------
		virtual void setVertexBuffer( uint32 slot, RHIBufferHandle buffer, uint32 stride, uint32 offset = 0 ) = 0;
		/**
		 * @brief 삼각형 리스트를 그립니다.
		 * @param passCbDescriptorIndex b0(PassCB) 에 바인딩할 상수 버퍼.
		 * @param materialCbDescriptorIndex b1(MaterialCB) 에 바인딩할 상수 버퍼.
		 */
		virtual void draw( uint32 vertexCount, uint32 startVertex = 0,
						   RHIDescriptorIndex passCbDescriptorIndex		= kInvalidDescriptorIndex,
						   RHIDescriptorIndex materialCbDescriptorIndex = kInvalidDescriptorIndex )		 = 0;
		virtual void setIndexBuffer( RHIBufferHandle buffer, uint32 indexStride = 4, uint32 offset = 0 ) = 0;

		// ------------------------------------------------------------------------------
		// 4) 컴퓨트 — PSO, 디스패치, 루트 상수, UAV
		// ------------------------------------------------------------------------------
		virtual void setComputePipelineState( RHIPipelineStateHandle pso )																				= 0;
		virtual void dispatchCompute( uint32 threadGroupCountX, uint32 threadGroupCountY, uint32 threadGroupCountZ )									= 0;
		virtual void setComputeRootConstants( uint32 rootParameterIndex, uint32 num32BitValues, const void* pData, uint32 destOffsetIn32BitValues = 0 ) = 0;
		virtual void bindComputeUAV( RHIDescriptorIndex index, uint32 slot )																			= 0;
		virtual void bindShaderResource( RHIDescriptorIndex index, uint32 slot )																		= 0;

		// ------------------------------------------------------------------------------
		// 5) 배리어 · blit — 샘플링 가능 전환, 컬러 복사 (dst==0은 스왑체인)
		// ------------------------------------------------------------------------------
		virtual void prepareTextureForShaderRead( RHITextureHandle texture )   = 0;
		virtual void blitTexture( RHITextureHandle src, RHITextureHandle dst ) = 0;

		// ------------------------------------------------------------------------------
		// 6) 인디렉트 — 드로우/디스패치, 버퍼 상태 전이, 멀티 드로우
		// ------------------------------------------------------------------------------
		virtual void drawIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset = 0,
								   RHIDescriptorIndex passCbDescriptorIndex		= kInvalidDescriptorIndex,
								   RHIDescriptorIndex materialCbDescriptorIndex = kInvalidDescriptorIndex ) = 0;
		virtual void dispatchIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset = 0 )	= 0;
		virtual void transitionBuffer( RHIBufferHandle buffer, RHIBufferState newState )					= 0;
		virtual void drawIndexedIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset = 0 ) = 0;
		virtual void multiDrawIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset, uint32 maxCommandCount,
										RHIBufferHandle countBuffer = 0, uint32 countBufferOffset = 0 )
		{
			defaultMultiDrawIndirect( argumentBuffer, argumentBufferOffset, maxCommandCount, countBuffer, countBufferOffset,
									  [this]( RHIBufferHandle buf, uint32 off )
			{ drawIndirect( buf, off ); } );
		}

		// ------------------------------------------------------------------------------
		// 7) GPU 디버그 마커
		// ------------------------------------------------------------------------------
		virtual void beginEventMarker( const utf8* pName ) = 0;
		virtual void endEventMarker()					   = 0;
	};

	/**
	 * @class IRHICommandContext
	 * @brief 즉시 실행 가능한 커맨드 컨텍스트 인터페이스.
	 *        ICommandReplayTarget을 상속하여 지연된 커맨드 리스트의 재현 타겟 역할을 수행합니다.
	 */
	class SW_API IRHICommandContext : public ICommandReplayTarget
	{
	public:
		IRHICommandContext()																			 = default;
		virtual ~IRHICommandContext() override															 = default;
		IRHICommandContext( const IRHICommandContext& )													 = delete;
		IRHICommandContext& operator=( const IRHICommandContext& )										 = delete;
		virtual void		beginOffscreenPass( RHITextureHandle colorTarget, const float4& clearColor ) = 0;
		virtual void		endOffscreenPass( RHITextureHandle colorTarget )							 = 0;
	};

} // namespace sw

#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#include "Engine/Graphics/RHI/RHITypes.h"

namespace sw
{
	/**
	 * @class IRHIResource
	 * @brief RHI 버퍼, 텍스처, PSO, 렌더 패스 등의 생성/파괴를 담당하는 인터페이스
	 */
	class SW_API IRHIResource
	{
	public:
		IRHIResource()								   = default;
		virtual ~IRHIResource()						   = default;
		IRHIResource( const IRHIResource& )			   = delete;
		IRHIResource& operator=( const IRHIResource& ) = delete;

		// ------------------------------------------------------------------------------
		// 리소스 — PSO, 렌더 패스, 버퍼, 텍스처
		// ------------------------------------------------------------------------------
		/** @brief 그래픽스 파이프라인 상태(PSO)를 만듭니다. */
		virtual RHIPipelineStateHandle createPipelineState( const RHIPipelineStateDesc& desc ) = 0;

		/** @brief 컴퓨트 파이프라인 상태(PSO)를 만듭니다. */
		virtual RHIPipelineStateHandle createComputePipelineState( string_view shaderPath, string_view entryPoint = "CSMain" ) = 0;

		/** @brief 파이프라인 상태 객체를 해제합니다. */
		virtual void destroyPipelineState( RHIPipelineStateHandle pso ) = 0;

		/** @brief 렌더 패스 객체를 만듭니다. */
		virtual RHIRenderPassHandle createRenderPass( const RHIRenderPassDesc& desc ) = 0;

		/** @brief 렌더 패스 객체를 해제합니다. */
		virtual void destroyRenderPass( RHIRenderPassHandle pass ) = 0;

		/** @brief 상수 버퍼를 만듭니다. */
		virtual RHIBufferHandle createConstantBuffer( uint32 size ) = 0;

		/** @brief 상수 버퍼 데이터를 갱신합니다. */
		virtual void updateConstantBuffer( RHIBufferHandle buffer, const void* pData, uint32 size ) = 0;

		/** @brief Structured / Storage 버퍼를 만듭니다. */
		virtual RHIBufferHandle createStructuredBuffer( uint32 elementSize, uint32 elementCount ) = 0;

		/** @brief Structured / Storage 버퍼 데이터를 갱신합니다. */
		virtual void updateStructuredBuffer( RHIBufferHandle buffer, const void* pData, uint32 size ) = 0;

		/** @brief 불변/정적 버텍스 버퍼 (POSITION+COLOR 레이아웃). */
		virtual RHIBufferHandle createVertexBuffer( const void* pData, uint32 sizeBytes ) = 0;

		/** @brief 범용 버퍼 (structured / UAV / 인디렉트 인자 / 인덱스). */
		virtual RHIBufferHandle createBuffer( const RHIBufferDesc& desc )
		{
			if ( hasFlag( desc._usage, RHIBufferUsage::Vertex ) && desc._pInitialData != nullptr && desc._sizeBytes > 0 )
				return createVertexBuffer( desc._pInitialData, desc._sizeBytes );
			if ( hasFlag( desc._usage, RHIBufferUsage::Constant ) )
				return createConstantBuffer( desc._sizeBytes > 0 ? desc._sizeBytes : 256u );
			const uint32	elemSize  = desc._elementSize > 0 ? desc._elementSize : 4u;
			const uint32	elemCount = desc._elementCount > 0
										  ? desc._elementCount
										  : ( desc._sizeBytes > 0 ? ( desc._sizeBytes / elemSize ) : 1u );
			RHIBufferHandle h		  = createStructuredBuffer( elemSize, elemCount );
			if ( h != 0 && desc._pInitialData != nullptr && desc._sizeBytes > 0 )
				updateStructuredBuffer( h, desc._pInitialData, desc._sizeBytes );
			return h;
		}

		/** @brief 인덱스 버퍼 (uint16/uint32). */
		virtual RHIBufferHandle createIndexBuffer( const void* pData, uint32 sizeBytes, uint32 indexStride = 4 )
		{
			(void)indexStride;
			RHIBufferDesc desc{};
			desc._sizeBytes	   = sizeBytes;
			desc._usage		   = RHIBufferUsage::Index | RHIBufferUsage::ShaderResource;
			desc._pInitialData = pData;
			return createBuffer( desc );
		}

		/** @brief GPU 버퍼 리소스를 삭제합니다. */
		virtual void destroyBuffer( RHIBufferHandle buffer ) = 0;

		/** @brief 2D 텍스처 (RenderTarget 포함)를 만듭니다. */
		virtual RHITextureHandle createTexture2D( const RHITextureDesc& desc ) = 0;

		/** @brief GPU 텍스처 리소스를 삭제합니다. */
		virtual void destroyTexture( RHITextureHandle texture ) = 0;

		// ------------------------------------------------------------------------------
		// Bindless — 텍스처/버퍼/UAV 등록과 해제
		// ------------------------------------------------------------------------------
		/** @brief Bindless 테이블에 텍스처를 등록하고 SRV 인덱스를 발급합니다. */
		virtual RHIDescriptorIndex registerBindlessTexture( RHITextureHandle texture ) = 0;

		/** @brief Bindless 테이블에 버퍼를 등록하고 인덱스를 발급합니다. */
		virtual RHIDescriptorIndex registerBindlessResource( RHIBufferHandle buffer ) = 0;

		/** @brief Bindless 리소스 등록을 해제합니다. */
		virtual void unregisterBindlessResource( RHIDescriptorIndex index ) = 0;

		/** @brief Bindless UAV를 등록하고 인덱스를 발급합니다. */
		virtual RHIDescriptorIndex registerBindlessUAV( RHIBufferHandle buffer ) = 0;

		/** @brief Bindless UAV 등록을 해제합니다. */
		virtual void unregisterBindlessUAV( RHIDescriptorIndex index ) = 0;
	};
} // namespace sw

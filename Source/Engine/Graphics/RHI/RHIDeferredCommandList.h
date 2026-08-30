/**
 * @file RHIDeferredCommandList.h
 * @brief 기록 후 리플레이하는 IRHICommandList (DX11/GL/DX12 래퍼/VK)
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Graphics/RHI/IRHICommandContext.h"

namespace sw
{
	class IRHIDevice;
	/**
	 * @class RHIDeferredCommandList
	 * @brief 소프트웨어 CommandList: CPU `Cmd` 벡터에 기록 후 Context API로 replay.
	 * @details Deferred **CommandList** Mode ≠ Deferred **Context**.
	 *          Immediate Mode: endCommandList → Immediate Context로 flush(replay).
	 *          Deferred Mode: executeCommandList → Immediate Context에 replay.
	 *          replay는 네이티브 ExecuteCommandLists/FinishCommandList가 아니라 `_listCmd` 순회 재생이다.
	 */
	class SW_API RHIDeferredCommandList final : public IRHICommandList
	{
	public:
		/** @brief 모드와 (Immediate용) 디바이스로 빈 리스트를 만듭니다. */
		explicit RHIDeferredCommandList( RHICommandListMode mode = RHICommandListMode::Deferred, IRHICommandContext* pContext = nullptr );

		// ------------------------------------------------------------------------------
		// 1) 다운캐스트 — 지연 리플레이 대상임을 표시
		// ------------------------------------------------------------------------------
		/** @brief 지연 리플레이용 다운캐스트 (this). */
		RHIDeferredCommandList* asDeferred() override { return this; }
		/** @brief 지연 리플레이용 const 다운캐스트 (this). */
		const RHIDeferredCommandList* asDeferred() const override { return this; }

		// ------------------------------------------------------------------------------
		// 2) 기록 범위
		// ------------------------------------------------------------------------------
		/** @brief 커맨드 기록을 시작합니다. */
		void beginCommandList() override;
		/** @brief 커맨드 기록을 끝냅니다. Immediate면 replay. */
		void endCommandList() override;

		// ------------------------------------------------------------------------------
		// 3) 기록 — 뷰포트 · PSO · 렌더 패스 · 드로우
		// ------------------------------------------------------------------------------
		/** @brief 뷰포트 설정을 기록합니다. */
		void setViewport( const RHIViewport& viewport ) override;
		/** @brief 그래픽스 PSO 바인드를 기록합니다. */
		void setPipelineState( RHIPipelineStateHandle pso ) override;
		/** @brief 렌더 패스 시작을 기록합니다. */
		void beginRenderPass( const RHIRenderPassBeginInfo& beginInfo ) override;
		/** @brief 렌더 패스 종료를 기록합니다. */
		void endRenderPass() override;
		/** @brief 버텍스 버퍼 바인드를 기록합니다. */
		void setVertexBuffer( uint32 slot, RHIBufferHandle buffer, uint32 stride, uint32 offset = 0 ) override;
		/** @brief 삼각형 리스트 드로우를 기록합니다. */
		void draw( uint32 vertexCount, uint32 startVertex = 0, RHIDescriptorIndex materialDescriptorIndex = kInvalidDescriptorIndex ) override;
		/** @brief 인덱스 버퍼 바인드를 기록합니다. */
		void setIndexBuffer( RHIBufferHandle buffer, uint32 indexStride = 4, uint32 offset = 0 ) override;

		// ------------------------------------------------------------------------------
		// 4) 기록 — 컴퓨트 · UAV · SRV
		// ------------------------------------------------------------------------------
		/** @brief 컴퓨트 PSO 바인드를 기록합니다. */
		void setComputePipelineState( RHIPipelineStateHandle pso ) override;
		/** @brief 컴퓨트 디스패치를 기록합니다. */
		void dispatchCompute( uint32 threadGroupCountX, uint32 threadGroupCountY, uint32 threadGroupCountZ ) override;
		/** @brief 컴퓨트 루트 상수 푸시를 기록합니다. */
		void setComputeRootConstants( uint32 rootParameterIndex, uint32 num32BitValues, const void* pData, uint32 destOffsetIn32BitValues = 0 ) override;
		/** @brief 컴퓨트 UAV 바인드를 기록합니다. */
		void bindComputeUAV( RHIDescriptorIndex index, uint32 slot ) override;
		/** @brief 셰이더 텍스처 슬롯 바인드를 기록합니다. */
		void bindShaderResource( RHIDescriptorIndex index, uint32 slot ) override;

		// ------------------------------------------------------------------------------
		// 5) 기록 — 배리어 · blit · 인디렉트 · 마커
		// ------------------------------------------------------------------------------
		/** @brief 텍스처 샘플링 가능 전환을 기록합니다. */
		void prepareTextureForShaderRead( RHITextureHandle texture ) override;
		/** @brief 텍스처 blit을 기록합니다. */
		void blitTexture( RHITextureHandle src, RHITextureHandle dst ) override;
		/** @brief GPU 인디렉트 드로우를 기록합니다. */
		void drawIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset = 0,
						   RHIDescriptorIndex materialDescriptorIndex = kInvalidDescriptorIndex ) override;
		/** @brief GPU 인디렉트 컴퓨트 디스패치를 기록합니다. */
		void dispatchIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset = 0 ) override;
		/** @brief 버퍼 상태 전이를 기록합니다. */
		void transitionBuffer( RHIBufferHandle buffer, RHIBufferState newState ) override;
		/** @brief 인덱스 인디렉트 드로우를 기록합니다. */
		void drawIndexedIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset = 0 ) override;
		/** @brief 멀티 인디렉트 드로우를 기록합니다. */
		void multiDrawIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset, uint32 maxCommandCount,
								RHIBufferHandle _countBuffer = 0, uint32 countBufferOffset = 0 ) override;
		/** @brief GPU 이벤트 마커 시작을 기록합니다. */
		void beginEventMarker( const utf8* pName ) override;
		/** @brief GPU 이벤트 마커 종료를 기록합니다. */
		void endEventMarker() override;

		// ------------------------------------------------------------------------------
		// 6) 리플레이 · 상태 — executeCommandList에서 호출
		// ------------------------------------------------------------------------------
		/** @brief 기록된 `_listCmd`를 Context 가상호출로 순서 재생 (네이티브 CL Execute ≠). */
		void replay( IRHICommandContext* pContext ) const;

		/** @brief Immediate 모드 flush 대상을 설정합니다 (endCommandList가 replay 가능). */
		void setContext( IRHICommandContext* pContext ) { _pContext = pContext; }

		/** @brief 기록/제출 모드를 반환합니다. */
		RHICommandListMode getMode() const { return _mode; }
		/** @brief 이미 제출됐으면 true (idempotent execute). */
		bool isApplied() const { return _bApplied; }
		/** @brief 현재 기록 중이면 true. */
		bool isRecording() const { return _bRecording; }
		/** @brief 기록된 명령 개수를 반환합니다. */
		size_t commandCount() const { return _listCmd.size(); }

		/** @brief 이미 제출된 것으로 표시합니다 (idempotent execute). */
		void markApplied() { _bApplied = true; }

		/** @brief Deferred CommandList를 Immediate Context에 soft-replay합니다 (null이면 markApplied 금지). */
		static void execute( IRHIDevice* pDevice, IRHICommandList* pCmdList );

	private:
		enum class Op : uint8
		{
			SetViewport,
			SetPipelineState,
			BeginRenderPass,
			EndRenderPass,
			SetVertexBuffer,
			Draw,
			SetIndexBuffer,
			SetComputePipelineState,
			DispatchCompute,
			SetComputeRootConstants,
			BindComputeUAV,
			BindShaderResource,
			PrepareTextureForShaderRead,
			BlitTexture,
			DrawIndirect,
			DispatchIndirect,
			TransitionBuffer,
			DrawIndexedIndirect,
			MultiDrawIndirect,
			BeginEventMarker,
			EndEventMarker,
		};

		struct Cmd
		{
			Op					   _op{};
			RHIViewport			   _viewport{};
			RHIPipelineStateHandle _pso{ 0 };
			RHIRenderPassBeginInfo _beginInfo{};
			RHIDescriptorIndex	   _materialIndex	= kInvalidDescriptorIndex;
			RHIDescriptorIndex	   _descriptorIndex = kInvalidDescriptorIndex;
			uint32				   _slot{ 0 };
			uint32				   _stride{ 0 };
			uint32				   _offset{ 0 };
			uint32				   _vertexCount{ 0 };
			uint32				   _startVertex{ 0 };
			uint32				   _indexStride = 4;
			uint32				   _dispatchX{ 0 };
			uint32				   _dispatchY{ 0 };
			uint32				   _dispatchZ{ 0 };
			uint32				   _rootParameterIndex{ 0 };
			uint32				   _destOffsetIn32BitValues{ 0 };
			RHIBufferHandle		   _buffer{ 0 };
			RHIBufferHandle		   _argumentBuffer{ 0 };
			uint32				   _argumentOffset{ 0 };
			RHIBufferHandle		   _countBuffer{ 0 };
			uint32				   _countOffset{ 0 };
			uint32				   _maxCommandCount{ 0 };
			RHIBufferState		   _bufferState = RHIBufferState::Common;
			RHITextureHandle	   _srcTexture{ 0 };
			RHITextureHandle	   _dstTexture{ 0 };
			vector<uint32>		   _listRootConstantWord;
			string				   _eventName;
		};

		/** @brief 명령을 큐에 넣습니다. */
		void push( Cmd cmd );
		/** @brief 기록 스레드가 맞는지 검사합니다. */
		void assertRecordingThread() const;

		RHICommandListMode	_mode = RHICommandListMode::Deferred;
		IRHICommandContext* _pContext{ nullptr };
		std::thread::id		_recordingThread{};
		vector<Cmd>			_listCmd;
		bool				_bRecording{ false };
		bool				_bApplied{ false };
	};
} // namespace sw

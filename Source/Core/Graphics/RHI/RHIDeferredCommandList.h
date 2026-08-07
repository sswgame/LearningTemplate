#pragma once

#include "Core/Graphics/RHI/IRHIDevice.h"

/**
 * @file RHIDeferredCommandList.h
 * @brief Record-then-replay IRHICommandList for immediate-mode backends (DX11/GL/DX12 wrapper/VK).
 */

namespace sw
{
	/**
	 * @class RHIDeferredCommandList
	 * @brief Records GPU commands; call replay(device) from IRHIDevice::executeCommandList.
	 */
	class SW_API RHIDeferredCommandList final : public IRHICommandList
	{
	public:
		RHIDeferredCommandList() = default;

		void beginCommandList() override;
		void endCommandList() override;

		void setViewport( const RHIViewport& viewport ) override;
		void setPipelineState( RHIPipelineStateHandle pso ) override;
		void beginRenderPass( const RHIRenderPassBeginInfo& beginInfo ) override;
		void endRenderPass() override;
		void drawTriangle( RHIDescriptorIndex materialDescriptorIndex ) override;
		void dispatchCompute( uint32 threadGroupCountX, uint32 threadGroupCountY, uint32 threadGroupCountZ ) override;
		void setComputeRootConstants( uint32 rootParameterIndex, uint32 num32BitValues, const void* data, uint32 destOffsetIn32BitValues = 0 ) override;
		void drawIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset = 0 ) override;
		void dispatchIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset = 0 ) override;
		void beginEventMarker( const utf8* name ) override;
		void endEventMarker() override;

		/** @brief Replay recorded commands onto device (must implement setViewport / setComputeRootConstants). */
		void replay( IRHIDevice* device ) const;

		bool		 isRecording() const { return _bRecording; }
		std::size_t	 commandCount() const { return _cmds.size(); }

	private:
		enum class Op : uint8
		{
			SetViewport,
			SetPipelineState,
			BeginRenderPass,
			EndRenderPass,
			DrawTriangle,
			DispatchCompute,
			SetComputeRootConstants,
			DrawIndirect,
			DispatchIndirect,
			BeginEventMarker,
			EndEventMarker,
		};

		struct Cmd
		{
			Op						 op{};
			RHIViewport				 viewport{};
			RHIPipelineStateHandle	 pso			  = 0;
			RHIRenderPassBeginInfo	 beginInfo{};
			RHIDescriptorIndex		 materialIndex	  = kInvalidDescriptorIndex;
			uint32					 dispatchX		  = 0;
			uint32					 dispatchY		  = 0;
			uint32					 dispatchZ		  = 0;
			uint32					 rootParameterIndex = 0;
			uint32					 destOffsetIn32BitValues = 0;
			RHIBufferHandle			 argumentBuffer	  = 0;
			uint32					 argumentOffset	  = 0;
			std::vector<uint32>		 rootConstantWords;
			std::string				 eventName;
		};

		void push( Cmd cmd );

		std::vector<Cmd> _cmds;
		bool			 _bRecording = false;
	};

	/** @brief Helper for backends whose createCommandList returns RHIDeferredCommandList. */
	inline void executeDeferredCommandList( IRHIDevice* device, IRHICommandList* cmdList )
	{
		if ( device == nullptr || cmdList == nullptr )
			return;
		static_cast<RHIDeferredCommandList*>( cmdList )->replay( device );
	}
} // namespace sw

#pragma once
/**
 * @file FrameRenderer.h
 * @brief Loads RenderPass XML, builds RenderGraph, executes per-frame clear/draw hooks
 */

#include "Core/Common/CommonHeaders.h"
#include "Core/Common/CommonMacros.h"
#include "Core/Graphics/RenderPass/RenderGraph.h"
#include "Core/Graphics/RenderPass/RenderPassResource.h"

namespace sw
{
	class IRHIDevice;
	class Material;

	/**
	 * @class FrameRenderer
	 * @brief Frame path: RenderPassManager XML → RenderGraph → IRHIDevice hooks
	 */
	class SW_API FrameRenderer
	{
	public:
		FrameRenderer();
		~FrameRenderer() = default;

		FrameRenderer( const FrameRenderer& )			 = delete;
		FrameRenderer& operator=( const FrameRenderer& ) = delete;

		bool initialize( IRHIDevice* device, const std::string& pipelineXmlPath = "Engine/RenderPass/ForwardPipeline.xml" );
		void shutdown();

		/** @brief Rebuild graph from a pipeline XML (sync load). */
		bool loadPipeline( const std::string& pipelineXmlPath );

		/** @brief Execute compiled graph (per-pass markers, clear/draw hooks). Does not begin/end swapchain frame. */
		bool execute( IRHIDevice* device, Material* material = nullptr );

		bool			   isReady() const { return _bReady != 0; }
		const RenderGraph& getGraph() const { return _graph; }

	private:
		void bindPassCallbacks( Material* material );
		void executePass( const std::string& passType, const std::string& passName, Material* material );
		bool tryGetAttachmentClearColor( const std::string& attachmentName, float32 outClearColor[4] ) const;

		IRHIDevice*		   _device = nullptr;
		RenderPassResource _pipelineResource;
		RenderGraph		   _graph;
		std::string		   _pipelinePath;
		float32			   _clearColor[4]{ 0.12f, 0.15f, 0.18f, 1.0f };
		uint8			   _bReady : 1;
		[[maybe_unused]] uint8 _reservedFlags : 7;
	};
} // namespace sw

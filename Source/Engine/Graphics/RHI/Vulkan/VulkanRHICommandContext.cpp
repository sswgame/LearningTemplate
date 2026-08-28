#include "pch.h"

#include "Engine/Graphics/RHI/Vulkan/VulkanRHICommandContext.h"

#include "Engine/Graphics/RHI/Vulkan/VulkanRHIDevice.h"

#include <vulkan/vulkan.h>

namespace sw
{
	SW_LOG_CALLER( "Vulkan" );

	static void setVkClearColor( VkClearValue& dst, const float32* pClear )
	{
		dst.color.float32[0] = pClear[0];
		dst.color.float32[1] = pClear[1];
		dst.color.float32[2] = pClear[2];
		dst.color.float32[3] = pClear[3];
	}

	static void mapStateVal( RHIBufferState state, VkAccessFlags& access, VkPipelineStageFlags& stage )
	{
		switch ( state )
		{
			case RHIBufferState::UnorderedAccess:
				access = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
				stage  = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
				break;
			case RHIBufferState::ShaderResource:
				access = VK_ACCESS_SHADER_READ_BIT;
				stage  = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
						VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
				break;
			case RHIBufferState::IndirectArgument:
				access = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
				stage  = VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT;
				break;
			case RHIBufferState::CopyDest:
				access = VK_ACCESS_TRANSFER_WRITE_BIT;
				stage  = VK_PIPELINE_STAGE_TRANSFER_BIT;
				break;
			case RHIBufferState::VertexOrConstant:
				access = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_UNIFORM_READ_BIT;
				stage  = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
						VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
				break;
			case RHIBufferState::Index:
				access = VK_ACCESS_INDEX_READ_BIT;
				stage  = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
				break;
			case RHIBufferState::Common:
			default:
				access = 0;
				stage  = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
				break;
		}
	}

	void VulkanRHICommandContext::beginOffscreenPass( RHITextureHandle colorTarget, float32 clearColor[4] )
	{
		if ( colorTarget == 0 )
		{
			_pDevice->beginFrame( clearColor );
			return;
		}

		VulkanRHIDevice::VulkanTextureRecord* pResolved = _pDevice->resolveTexture( colorTarget );
		if ( pResolved == nullptr || _pDevice->_offscreenCommandBuffer == VK_NULL_HANDLE )
			return;
		VulkanRHIDevice::VulkanTextureRecord& record = *pResolved;
		if ( record._framebuffer == VK_NULL_HANDLE || record._renderPass == VK_NULL_HANDLE )
		{
			SW_LOG_ERROR( "beginOffscreenPass: texture has no framebuffer." );
			return;
		}

		vkWaitForFences( _pDevice->_device, 1, &_pDevice->_offscreenFence, VK_TRUE, UINT64_MAX );
		vkResetFences( _pDevice->_device, 1, &_pDevice->_offscreenFence );
		vkResetCommandBuffer( _pDevice->_offscreenCommandBuffer, 0 );

		VkCommandBufferBeginInfo beginInfo{};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		vkBeginCommandBuffer( _pDevice->_offscreenCommandBuffer, &beginInfo );

		_pDevice->transitionImageLayout( _pDevice->_offscreenCommandBuffer, record._image, record._layout,
										 VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
										 VK_IMAGE_ASPECT_COLOR_BIT );
		record._layout = static_cast<uint32>( VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL );

		_pDevice->_bOffscreenPassActive	 = 1;
		_pDevice->_bFrameStarted		 = 1; // allow Immediate Context draws during offscreen
		_pDevice->_bRenderPassActive	 = 0;
		_pDevice->_activeOffscreenTarget = colorTarget;

		// Default clear pass (FrameRenderer may restart passes on this same buffer).
		RHIRenderPassBeginInfo rpBegin{};
		Memory::copy( rpBegin._arrClearColor, clearColor, sizeof( rpBegin._arrClearColor ) );
		rpBegin._width	= record._width;
		rpBegin._height = record._height;
		beginRenderPass( rpBegin );
	}

	void VulkanRHICommandContext::blitTexture( RHITextureHandle src, RHITextureHandle dst )
	{
		VkCommandBuffer cmd = _pDevice->currentCommandBuffer();
		if ( cmd == VK_NULL_HANDLE || src == 0 )
			return;

		if ( _pDevice->_bRenderPassActive )
		{
			vkCmdEndRenderPass( cmd );
			_pDevice->_bRenderPassActive = false;
		}

		VulkanRHIDevice::VulkanTextureRecord* pSrcResolved = _pDevice->resolveTexture( src );
		if ( pSrcResolved == nullptr || pSrcResolved->_image == VK_NULL_HANDLE || pSrcResolved->_bDepthStencil != 0 )
			return;

		VulkanRHIDevice::VulkanTextureRecord& srcRec = *pSrcResolved;
		_pDevice->transitionImageLayout( cmd, srcRec._image, srcRec._layout,
										 VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
										 VK_IMAGE_ASPECT_COLOR_BIT );
		srcRec._layout = static_cast<uint32>( VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL );

		VkImage dstImage = VK_NULL_HANDLE;
		uint32	dstW	 = _pDevice->_swapChainExtentWidth;
		uint32	dstH	 = _pDevice->_swapChainExtentHeight;

		if ( dst == 0 )
		{
			if ( _pDevice->_imageIndex >= _pDevice->_listSwapChainImage.size() )
				return;
			dstImage = _pDevice->_listSwapChainImage[_pDevice->_imageIndex];
			_pDevice->transitionImageLayout( cmd, dstImage, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
											 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
											 VK_IMAGE_ASPECT_COLOR_BIT );
		}
		else
		{
			VulkanRHIDevice::VulkanTextureRecord* pDstResolved = _pDevice->resolveTexture( dst );
			if ( pDstResolved == nullptr || pDstResolved->_image == VK_NULL_HANDLE )
				return;
			dstImage = pDstResolved->_image;
			dstW	 = pDstResolved->_width;
			dstH	 = pDstResolved->_height;
			_pDevice->transitionImageLayout( cmd, dstImage, pDstResolved->_layout,
											 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
											 VK_IMAGE_ASPECT_COLOR_BIT );
			pDstResolved->_layout = static_cast<uint32>( VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL );
		}

		VkImageBlit blit{};
		blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		blit.srcSubresource.layerCount = 1;
		blit.srcOffsets[1]			   = { static_cast<int32>( srcRec._width ), static_cast<int32>( srcRec._height ), 1 };
		blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		blit.dstSubresource.layerCount = 1;
		blit.dstOffsets[1]			   = { static_cast<int32>( dstW ), static_cast<int32>( dstH ), 1 };

		vkCmdBlitImage( cmd, srcRec._image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
						dstImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR );

		if ( dst == 0 )
		{
			_pDevice->transitionImageLayout( cmd, dstImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
											 VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
											 VK_IMAGE_ASPECT_COLOR_BIT );
		}
		else
		{
			_pDevice->transitionImageLayout( cmd, dstImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
											 VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
											 VK_IMAGE_ASPECT_COLOR_BIT );
			VulkanRHIDevice::VulkanTextureRecord* pDstResolved = _pDevice->resolveTexture( dst );
			if ( pDstResolved != nullptr )
				pDstResolved->_layout = static_cast<uint32>( VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL );
		}
	}

	void VulkanRHICommandContext::bindShaderResource( RHIDescriptorIndex index, uint32 slot )
	{
		VkCommandBuffer cmd = _pDevice->currentCommandBuffer();
		if ( _pDevice->supportsNativeBindlessSampling() )
			return;

		VkDescriptorSet descSet = VK_NULL_HANDLE;
		if ( index < static_cast<RHIDescriptorIndex>( _pDevice->_listRegisteredTexture.size() ) &&
			 _pDevice->_listRegisteredTexture[index] != VK_NULL_HANDLE &&
			 _pDevice->_listRegisteredTexture[index] != _pDevice->_bindlessTextureSet )
			descSet = _pDevice->_listRegisteredTexture[index];

		if ( descSet != VK_NULL_HANDLE )
		{
			vkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _pDevice->_pipelineLayout, 2 + slot, 1, &descSet, 0, nullptr );
			vkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _pDevice->_pipelineLayout, 2 + slot, 1, &descSet, 0, nullptr );
		}
	}

	void VulkanRHICommandContext::prepareTextureForShaderRead( RHITextureHandle texture )
	{
		VkCommandBuffer cmd = _pDevice->currentCommandBuffer();
		if ( cmd == VK_NULL_HANDLE || texture == 0 )
			return;
		VulkanRHIDevice::VulkanTextureRecord* pResolved = _pDevice->resolveTexture( texture );
		if ( pResolved == nullptr || pResolved->_image == VK_NULL_HANDLE )
			return;

		if ( _pDevice->_bRenderPassActive )
		{
			vkCmdEndRenderPass( cmd );
			_pDevice->_bRenderPassActive = false;
		}

		VulkanRHIDevice::VulkanTextureRecord& record	   = *pResolved;
		const uint32						  targetLayout = ( record._bDepthStencil != 0 )
															   ? static_cast<uint32>( VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL )
															   : static_cast<uint32>( VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );
		if ( record._layout == targetLayout )
			return;

		const uint32 aspect = ( record._bDepthStencil != 0 )
								? _pDevice->depthAspectMask()
								: static_cast<uint32>( VK_IMAGE_ASPECT_COLOR_BIT );
		_pDevice->transitionImageLayout( cmd, record._image, record._layout, targetLayout, aspect );
		record._layout = targetLayout;
	}

	void VulkanRHICommandContext::endOffscreenPass( RHITextureHandle colorTarget )
	{
		if ( colorTarget == 0 || _pDevice->_bOffscreenPassActive == 0 || _pDevice->_offscreenCommandBuffer == VK_NULL_HANDLE )
			return;

		VulkanRHIDevice::VulkanTextureRecord* pResolved = _pDevice->resolveTexture( colorTarget );
		if ( pResolved == nullptr )
			return;

		VulkanRHIDevice::VulkanTextureRecord& record = *pResolved;
		if ( _pDevice->_bRenderPassActive )
		{
			vkCmdEndRenderPass( _pDevice->_offscreenCommandBuffer );
			_pDevice->_bRenderPassActive = false;
		}
		_pDevice->transitionImageLayout( _pDevice->_offscreenCommandBuffer, record._image,
										 VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
										 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
										 VK_IMAGE_ASPECT_COLOR_BIT );
		record._layout = static_cast<uint32>( VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );

		vkEndCommandBuffer( _pDevice->_offscreenCommandBuffer );

		VkSubmitInfo submitInfo{};
		submitInfo.sType			  = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers	  = &_pDevice->_offscreenCommandBuffer;
		vkQueueSubmit( _pDevice->_graphicsQueue, 1, &submitInfo, _pDevice->_offscreenFence );
		vkWaitForFences( _pDevice->_device, 1, &_pDevice->_offscreenFence, VK_TRUE, UINT64_MAX );

		_pDevice->_bOffscreenPassActive	 = 0;
		_pDevice->_bFrameStarted		 = 0;
		_pDevice->_activeOffscreenTarget = 0;
	}

	void VulkanRHICommandContext::setPipelineState( RHIPipelineStateHandle pso )
	{
		VkCommandBuffer cmd = _pDevice->currentCommandBuffer();
		if ( cmd == VK_NULL_HANDLE )
			return;

		_pDevice->_activeGraphicsPso							  = pso;
		VkPipeline										  pipe	  = _pDevice->_pipeline;
		const VulkanRHIDevice::VulkanPipelineStateRecord* pRecord = _pDevice->_pipelineStates.get( pso );
		if ( pRecord != nullptr )
		{
			if ( pRecord->_pipeline != VK_NULL_HANDLE )
				pipe = pRecord->_pipeline;
		}

		if ( pipe != VK_NULL_HANDLE )
			vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe );
	}

	void VulkanRHICommandContext::setComputePipelineState( RHIPipelineStateHandle pso )
	{
		VkCommandBuffer cmd = _pDevice->currentCommandBuffer();
		if ( cmd == VK_NULL_HANDLE )
			return;

		if ( _pDevice->_bRenderPassActive )
		{
			vkCmdEndRenderPass( cmd );
			_pDevice->_bRenderPassActive = false;
		}

		const VulkanRHIDevice::VulkanPipelineStateRecord* pRecord = _pDevice->_pipelineStates.get( pso );
		if ( pRecord != nullptr )
		{
			if ( pRecord->_pipeline != VK_NULL_HANDLE )
				vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pRecord->_pipeline );

			VkDescriptorSet set0 = _pDevice->_descriptorSet;
			if ( set0 == VK_NULL_HANDLE && _pDevice->_listRegisteredDescriptorSet.empty() == false )
				set0 = _pDevice->_listRegisteredDescriptorSet[0];
			if ( set0 != VK_NULL_HANDLE && _pDevice->_pipelineLayout != VK_NULL_HANDLE )
				vkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _pDevice->_pipelineLayout, 0, 1, &set0, 0, nullptr );
		}
	}

	void VulkanRHICommandContext::beginRenderPass( const RHIRenderPassBeginInfo& beginInfo )
	{
		VkCommandBuffer cmd = _pDevice->currentCommandBuffer();
		if ( cmd == VK_NULL_HANDLE )
			return;

		const bool	 bBindColor = beginInfo._bBindColor != 0;
		const uint32 colorCount = bBindColor ? ( beginInfo._colorTargetCount > 0 ? beginInfo._colorTargetCount : 1u ) : 0u;
		const bool	 bHasDepth	= beginInfo._depthTarget != 0;

		// Depth-only still uses composite path when a depth target is provided.
		if ( bBindColor == 0 && bHasDepth == false )
			return;

		RHITextureHandle colorHandles[kMaxColorAttachments]{};
		for ( uint32 attachmentIndex = 0; attachmentIndex < colorCount && attachmentIndex < kMaxColorAttachments; ++attachmentIndex )
		{
			colorHandles[attachmentIndex] = ( beginInfo._colorTargetCount > 0 ) ? beginInfo._arrColorTargets[attachmentIndex] : beginInfo._colorTarget;
		}

		if ( colorCount == 1 && colorHandles[0] == 0 && _pDevice->_activeOffscreenTarget != 0 )
			colorHandles[0] = _pDevice->_activeOffscreenTarget;

		// Composite FB for MRT, color+depth, or depth-only. Keep plain single-RT / swapchain path otherwise.
		const bool bUseComposite = ( colorCount > 1 ) || ( colorCount == 1 && colorHandles[0] != 0 && bHasDepth ) ||
								   ( colorCount == 0 && bHasDepth );

		VkRenderPass  renderPass  = _pDevice->_renderPass;
		VkFramebuffer framebuffer = VK_NULL_HANDLE;
		VkExtent2D	  extent{ _pDevice->_swapChainExtentWidth, _pDevice->_swapChainExtentHeight };
		VkClearValue  clearValues[kMaxColorAttachments + 1]{};
		uint32		  clearCount{ 0 };

		if ( bUseComposite )
		{
			VulkanRHIDevice::CompositeFbKey key{};
			key._colorCount = ( colorCount > kMaxColorAttachments ) ? kMaxColorAttachments : colorCount;
			for ( uint32 colorIndex = 0; colorIndex < key._colorCount; ++colorIndex )
			{
				key._arrColors[colorIndex] = colorHandles[colorIndex];
				key._arrColorLoadOps[colorIndex] =
					static_cast<uint8>( ( beginInfo._colorTargetCount > 0 ) ? beginInfo._arrLoadOps[colorIndex] : beginInfo._loadOp );
			}
			key._depth		 = beginInfo._depthTarget;
			key._depthLoadOp = static_cast<uint8>( beginInfo._depthLoadOp );

			if ( _pDevice->_bRenderPassActive )
			{
				vkCmdEndRenderPass( cmd );
				_pDevice->_bRenderPassActive = false;
			}

			for ( uint32 colorIndex = 0; colorIndex < key._colorCount; ++colorIndex )
			{
				VulkanRHIDevice::VulkanTextureRecord* pTex = _pDevice->resolveTexture( key._arrColors[colorIndex] );
				if ( pTex == nullptr )
					return;
				constexpr uint32 aspect = VK_IMAGE_ASPECT_COLOR_BIT;
				_pDevice->transitionImageLayout( cmd, pTex->_image, pTex->_layout,
												 VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, aspect );
				pTex->_layout = static_cast<uint32>( VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL );
			}
			if ( key._depth != 0 )
			{
				VulkanRHIDevice::VulkanTextureRecord* pTex = _pDevice->resolveTexture( key._depth );
				if ( pTex == nullptr )
					return;
				const uint32 aspect = _pDevice->depthAspectMask();
				_pDevice->transitionImageLayout( cmd, pTex->_image, pTex->_layout,
												 VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, aspect );
				pTex->_layout = static_cast<uint32>( VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL );
			}

			VulkanRHIDevice::CompositeFbRecord composite{};
			if ( _pDevice->ensureCompositeFramebuffer( key, composite ) == false )
				return;

			renderPass	= composite._renderPass;
			framebuffer = composite._framebuffer;
			extent		= { composite._width, composite._height };
			// Track RT only — do NOT set _bOffscreenPassActive (that routes to a separate CB).
			if ( key._colorCount > 0 )
				_pDevice->_activeOffscreenTarget = key._arrColors[0];

			for ( uint32 colorIndex = 0; colorIndex < key._colorCount; ++colorIndex )
			{
				const float32* pClear = ( beginInfo._colorTargetCount > 0 ) ? beginInfo._arrClearColors[colorIndex] : beginInfo._arrClearColor;
				setVkClearColor( clearValues[clearCount++], pClear );
			}
			if ( key._depth != 0 )
			{
				clearValues[clearCount].depthStencil.depth	 = beginInfo._clearDepth;
				clearValues[clearCount].depthStencil.stencil = 0;
				++clearCount;
			}
		}
		else
		{
			// Existing single-RT path (swapchain or per-texture offscreen FB).
			RHITextureHandle colorTarget = ( colorCount > 0 ) ? colorHandles[0] : 0;
			if ( colorTarget != 0 )
			{
				VulkanRHIDevice::VulkanTextureRecord* pTex = _pDevice->resolveTexture( colorTarget );
				if ( pTex == nullptr || pTex->_framebuffer == VK_NULL_HANDLE || pTex->_renderPass == VK_NULL_HANDLE )
					return;

				constexpr uint32 aspect = VK_IMAGE_ASPECT_COLOR_BIT;
				_pDevice->transitionImageLayout( cmd, pTex->_image, pTex->_layout,
												 VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, aspect );
				pTex->_layout					 = static_cast<uint32>( VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL );
				renderPass						 = pTex->_renderPass;
				framebuffer						 = pTex->_framebuffer;
				extent							 = { pTex->_width, pTex->_height };
				_pDevice->_activeOffscreenTarget = colorTarget;
			}
			else
			{
				if ( _pDevice->_renderPass == VK_NULL_HANDLE || _pDevice->_listSwapChainFramebuffer.empty() || _pDevice->_imageIndex >= _pDevice->_listSwapChainFramebuffer.size() )
					return;
				framebuffer = _pDevice->_listSwapChainFramebuffer[_pDevice->_imageIndex];
				extent		= { _pDevice->_swapChainExtentWidth, _pDevice->_swapChainExtentHeight };
			}

			if ( _pDevice->_bRenderPassActive )
			{
				vkCmdEndRenderPass( cmd );
				_pDevice->_bRenderPassActive = false;
			}

			const float32* pClear = ( beginInfo._colorTargetCount > 0 ) ? beginInfo._arrClearColors[0] : beginInfo._arrClearColor;
			setVkClearColor( clearValues[0], pClear );
			clearCount = 1;
		}

		if ( beginInfo._width > 0 && beginInfo._height > 0 )
		{
			extent.width  = MathUtil::min( extent.width, beginInfo._width );
			extent.height = MathUtil::min( extent.height, beginInfo._height );
		}

		VkRenderPassBeginInfo renderPassInfo{};
		renderPassInfo.sType			 = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		renderPassInfo.renderPass		 = renderPass;
		renderPassInfo.framebuffer		 = framebuffer;
		renderPassInfo.renderArea.offset = { 0, 0 };
		renderPassInfo.renderArea.extent = extent;
		renderPassInfo.clearValueCount	 = clearCount;
		renderPassInfo.pClearValues		 = clearValues;

		vkCmdBeginRenderPass( cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE );
		_pDevice->_bRenderPassActive = true;

		// Match DX12 beginRenderPass: viewport = pass extent, DX Y orientation.
		RHIViewport vp{};
		vp._width	 = static_cast<float32>( extent.width );
		vp._height	 = static_cast<float32>( extent.height );
		vp._minDepth = 0.0f;
		vp._maxDepth = 1.0f;
		setViewport( vp );
	}

	void VulkanRHICommandContext::endRenderPass()
	{
		VkCommandBuffer cmd = _pDevice->currentCommandBuffer();
		if ( cmd == VK_NULL_HANDLE || _pDevice->_bRenderPassActive == 0 )
			return;
		vkCmdEndRenderPass( cmd );
		_pDevice->_bRenderPassActive = false;
	}

	void VulkanRHICommandContext::transitionBuffer( RHIBufferHandle buffer, RHIBufferState newState )
	{
		VkCommandBuffer						 cmd	 = _pDevice->currentCommandBuffer();
		VulkanRHIDevice::VulkanBufferRecord* pRecord = _pDevice->resolveAllocatedBuffer( buffer );
		if ( cmd == VK_NULL_HANDLE || pRecord == nullptr )
			return;

		if ( pRecord->_buffer == VK_NULL_HANDLE || pRecord->_state == newState )
			return;

		if ( _pDevice->_bRenderPassActive )
		{
			vkCmdEndRenderPass( cmd );
			_pDevice->_bRenderPassActive = false;
		}

		VkAccessFlags		 srcAccess{ 0 };
		VkAccessFlags		 dstAccess{ 0 };
		VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
		VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
		mapStateVal( pRecord->_state, srcAccess, srcStage );
		mapStateVal( newState, dstAccess, dstStage );

		VkBufferMemoryBarrier barrier{};
		barrier.sType				= VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
		barrier.srcAccessMask		= srcAccess;
		barrier.dstAccessMask		= dstAccess;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.buffer				= pRecord->_buffer;
		barrier.offset				= 0;
		barrier.size				= VK_WHOLE_SIZE;

		vkCmdPipelineBarrier( cmd, srcStage, dstStage, 0, 0, nullptr, 1, &barrier, 0, nullptr );
		pRecord->_state = newState;
	}

	void VulkanRHICommandContext::bindComputeUAV( RHIDescriptorIndex index, uint32 slot )
	{
		VkCommandBuffer cmd = _pDevice->currentCommandBuffer();
		if ( cmd == VK_NULL_HANDLE || _pDevice->_pipelineLayout == VK_NULL_HANDLE || slot >= 4 )
			return;

		if ( _pDevice->_bRenderPassActive )
		{
			vkCmdEndRenderPass( cmd );
			_pDevice->_bRenderPassActive = false;
		}

		VkDescriptorSet descSet = VK_NULL_HANDLE;
		if ( index < static_cast<RHIDescriptorIndex>( _pDevice->_listRegisteredUAV.size() ) && _pDevice->_listRegisteredUAV[index] != VK_NULL_HANDLE )
			descSet = _pDevice->_listRegisteredUAV[index];
		else if ( index < static_cast<RHIDescriptorIndex>( _pDevice->_listRegisteredDescriptorSet.size() ) && _pDevice->_listRegisteredDescriptorSet[index] != VK_NULL_HANDLE )
			descSet = _pDevice->_listRegisteredDescriptorSet[index];

		if ( descSet != VK_NULL_HANDLE )
		{
			vkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _pDevice->_pipelineLayout, 6 + slot, 1, &descSet, 0, nullptr );
		}
	}

	void VulkanRHICommandContext::setVertexBuffer( uint32 slot, RHIBufferHandle buffer, uint32 stride, uint32 offset )
	{
		(void)slot;
		_pDevice->_boundMeshVb	   = buffer;
		_pDevice->_boundMeshStride = stride > 0 ? stride : static_cast<uint32>( sizeof( RHIVertex ) );
		_pDevice->_boundMeshOffset = offset;
	}

	void VulkanRHICommandContext::setIndexBuffer( RHIBufferHandle buffer, uint32 indexStride, uint32 offset )
	{
		_pDevice->_boundIndexBuffer = buffer;
		_pDevice->_boundIndexStride = ( indexStride == 2 ) ? 2u : 4u;
		_pDevice->_boundIndexOffset = offset;
	}

	void VulkanRHICommandContext::draw( uint32 vertexCount, uint32 startVertex, RHIDescriptorIndex materialDescriptorIndex )
	{
		VkCommandBuffer cmd = _pDevice->currentCommandBuffer();
		const bool		bCanDraw =
			( cmd != VK_NULL_HANDLE && _pDevice->_pipelineLayout != VK_NULL_HANDLE && vertexCount > 0 );
		if ( bCanDraw == false )
			return;

		VkPipeline										  pipeline = _pDevice->_pipeline;
		const VulkanRHIDevice::VulkanPipelineStateRecord* pRecord  = _pDevice->_pipelineStates.get( _pDevice->_activeGraphicsPso );
		if ( pRecord != nullptr )
		{
			if ( pRecord->_pipeline != VK_NULL_HANDLE )
				pipeline = pRecord->_pipeline;
		}
		else if ( _pDevice->_activeOffscreenTarget != 0 && _pDevice->_offscreenPipeline != VK_NULL_HANDLE )
			pipeline = _pDevice->_offscreenPipeline;
		if ( pipeline == VK_NULL_HANDLE )
			return;

		vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline );

		const bool bValidMaterialDescriptor =
			( materialDescriptorIndex != kInvalidDescriptorIndex &&
			  materialDescriptorIndex < static_cast<RHIDescriptorIndex>( _pDevice->_listRegisteredDescriptorSet.size() ) &&
			  _pDevice->_listRegisteredDescriptorSet[materialDescriptorIndex] != VK_NULL_HANDLE );
		if ( bValidMaterialDescriptor )
		{
			const VkDescriptorSet descSet = _pDevice->_listRegisteredDescriptorSet[materialDescriptorIndex];
			vkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _pDevice->_pipelineLayout, 0, 1, &descSet, 0, nullptr );
		}
		else if ( _pDevice->_descriptorSet != VK_NULL_HANDLE )
		{
			vkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _pDevice->_pipelineLayout, 0, 1, &_pDevice->_descriptorSet, 0, nullptr );
		}

		if ( _pDevice->_bindlessTextureSet != VK_NULL_HANDLE )
			vkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _pDevice->_pipelineLayout, 1, 1, &_pDevice->_bindlessTextureSet, 0, nullptr );

		if ( materialDescriptorIndex != kInvalidDescriptorIndex )
		{
			constexpr VkShaderStageFlags kPushStages =
				VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;
			uint32 matIndex = materialDescriptorIndex;
			vkCmdPushConstants( cmd, _pDevice->_pipelineLayout, kPushStages, 0, sizeof( uint32 ), &matIndex );
		}

		if ( _pDevice->_boundMeshVb != 0 )
		{
			const VulkanRHIDevice::VulkanBufferRecord* pVb = _pDevice->resolveAllocatedBuffer( _pDevice->_boundMeshVb );
			if ( pVb != nullptr && pVb->_buffer != VK_NULL_HANDLE )
			{
				VkBuffer	 arrVertexBuffers[] = { pVb->_buffer };
				VkDeviceSize arrOffsets[]		= { static_cast<VkDeviceSize>( _pDevice->_boundMeshOffset ) };
				vkCmdBindVertexBuffers( cmd, 0, 1, arrVertexBuffers, arrOffsets );
			}
		}
		else if ( _pDevice->_vertexBuffer != VK_NULL_HANDLE )
		{
			VkBuffer	 arrVertexBuffers[] = { _pDevice->_vertexBuffer };
			VkDeviceSize arrOffsets[]		= { 0 };
			vkCmdBindVertexBuffers( cmd, 0, 1, arrVertexBuffers, arrOffsets );
		}

		vkCmdDraw( cmd, vertexCount, 1, startVertex, 0 );
	}

	void VulkanRHICommandContext::dispatchCompute( uint32 threadGroupCountX, uint32 threadGroupCountY, uint32 threadGroupCountZ )
	{
		VkCommandBuffer cmd = _pDevice->currentCommandBuffer();
		if ( cmd == VK_NULL_HANDLE )
			return;

		if ( _pDevice->_bRenderPassActive )
		{
			vkCmdEndRenderPass( cmd );
			_pDevice->_bRenderPassActive = false;
		}

		vkCmdDispatch( cmd, threadGroupCountX, threadGroupCountY, threadGroupCountZ );
	}

	void VulkanRHICommandContext::setViewport( const RHIViewport& viewport )
	{
		VkCommandBuffer cmd = _pDevice->currentCommandBuffer();
		if ( cmd == VK_NULL_HANDLE )
			return;

		// RHIViewport is DirectX-style (top-left origin, +Y down in pixel space, +NDC Y = up).
		// Negative VkViewport.height maps Vulkan NDC to the same orientation as DX/GL_UPPER_LEFT.
		VkViewport vkViewport{};
		vkViewport.x		= viewport._x;
		vkViewport.y		= viewport._y + viewport._height;
		vkViewport.width	= viewport._width;
		vkViewport.height	= -viewport._height;
		vkViewport.minDepth = viewport._minDepth;
		vkViewport.maxDepth = viewport._maxDepth;
		vkCmdSetViewport( cmd, 0, 1, &vkViewport );

		VkRect2D scissor{};
		scissor.offset.x	  = static_cast<int32>( viewport._x );
		scissor.offset.y	  = static_cast<int32>( viewport._y );
		scissor.extent.width  = viewport._width > 0.0f ? static_cast<uint32>( viewport._width ) : 0u;
		scissor.extent.height = viewport._height > 0.0f ? static_cast<uint32>( viewport._height ) : 0u;
		vkCmdSetScissor( cmd, 0, 1, &scissor );
	}

	void VulkanRHICommandContext::setComputeRootConstants( uint32 rootParameterIndex, uint32 num32BitValues, const void* pData, uint32 destOffsetIn32BitValues )
	{
		(void)rootParameterIndex;
		VkCommandBuffer cmd = _pDevice->currentCommandBuffer();
		const bool		bCanPush =
			( cmd != VK_NULL_HANDLE && _pDevice->_pipelineLayout != VK_NULL_HANDLE && pData != nullptr && num32BitValues > 0 );
		if ( bCanPush == false )
			return;

		constexpr VkShaderStageFlags kPushStages =
			VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;
		vkCmdPushConstants( cmd, _pDevice->_pipelineLayout, kPushStages, destOffsetIn32BitValues * 4, num32BitValues * 4, pData );
	}

	void VulkanRHICommandContext::drawIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset,
												RHIDescriptorIndex materialDescriptorIndex )
	{
		VkCommandBuffer							   cmd	   = _pDevice->currentCommandBuffer();
		const VulkanRHIDevice::VulkanBufferRecord* pRecord = _pDevice->resolveAllocatedBuffer( argumentBuffer );
		if ( cmd == VK_NULL_HANDLE || pRecord == nullptr )
			return;

		const bool bValidMaterialDescriptor =
			( materialDescriptorIndex != kInvalidDescriptorIndex &&
			  materialDescriptorIndex < static_cast<RHIDescriptorIndex>( _pDevice->_listRegisteredDescriptorSet.size() ) &&
			  _pDevice->_listRegisteredDescriptorSet[materialDescriptorIndex] != VK_NULL_HANDLE &&
			  _pDevice->_pipelineLayout != VK_NULL_HANDLE );
		if ( bValidMaterialDescriptor )
		{
			const VkDescriptorSet descSet = _pDevice->_listRegisteredDescriptorSet[materialDescriptorIndex];
			vkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _pDevice->_pipelineLayout, 0, 1, &descSet, 0, nullptr );
			uint32						 matIndex = materialDescriptorIndex;
			constexpr VkShaderStageFlags kPushStages =
				VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;
			vkCmdPushConstants( cmd, _pDevice->_pipelineLayout, kPushStages, 0, sizeof( uint32 ), &matIndex );
		}

		if ( _pDevice->_bindlessTextureSet != VK_NULL_HANDLE )
			vkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _pDevice->_pipelineLayout, 1, 1, &_pDevice->_bindlessTextureSet, 0, nullptr );

		if ( pRecord->_buffer != VK_NULL_HANDLE )
		{
			const VulkanRHIDevice::VulkanBufferRecord* pVb = _pDevice->resolveAllocatedBuffer( _pDevice->_boundMeshVb );
			if ( pVb != nullptr && pVb->_buffer != VK_NULL_HANDLE )
			{
				VkBuffer	 arrVertexBuffers[] = { pVb->_buffer };
				VkDeviceSize arrOffsets[]		= { static_cast<VkDeviceSize>( _pDevice->_boundMeshOffset ) };
				vkCmdBindVertexBuffers( cmd, 0, 1, arrVertexBuffers, arrOffsets );
			}
			else if ( _pDevice->_vertexBuffer != VK_NULL_HANDLE )
			{
				VkBuffer	 arrVertexBuffers[] = { _pDevice->_vertexBuffer };
				VkDeviceSize arrOffsets[]		= { 0 };
				vkCmdBindVertexBuffers( cmd, 0, 1, arrVertexBuffers, arrOffsets );
			}
			vkCmdDrawIndirect( cmd, pRecord->_buffer, argumentBufferOffset, 1, sizeof( VkDrawIndirectCommand ) );
		}
	}

	void VulkanRHICommandContext::drawIndexedIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset )
	{
		VkCommandBuffer							   cmd	 = _pDevice->currentCommandBuffer();
		const VulkanRHIDevice::VulkanBufferRecord* pArgs = _pDevice->resolveAllocatedBuffer( argumentBuffer );
		const VulkanRHIDevice::VulkanBufferRecord* pIb	 = _pDevice->resolveAllocatedBuffer( _pDevice->_boundIndexBuffer );
		const bool								   bValidArgs =
			( cmd != VK_NULL_HANDLE && pArgs != nullptr && pIb != nullptr && pArgs->_buffer != VK_NULL_HANDLE && pIb->_buffer != VK_NULL_HANDLE );
		if ( bValidArgs == false )
			return;

		const VulkanRHIDevice::VulkanBufferRecord* pVb = _pDevice->resolveAllocatedBuffer( _pDevice->_boundMeshVb );
		if ( pVb != nullptr )
		{
			VkBuffer	 arrVertexBuffers[] = { pVb->_buffer };
			VkDeviceSize arrOffsets[]		= { static_cast<VkDeviceSize>( _pDevice->_boundMeshOffset ) };
			vkCmdBindVertexBuffers( cmd, 0, 1, arrVertexBuffers, arrOffsets );
		}

		const VkIndexType indexType = ( _pDevice->_boundIndexStride == 2 ) ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
		vkCmdBindIndexBuffer( cmd, pIb->_buffer, _pDevice->_boundIndexOffset, indexType );
		vkCmdDrawIndexedIndirect( cmd, pArgs->_buffer, argumentBufferOffset, 1, sizeof( VkDrawIndexedIndirectCommand ) );
	}

	void VulkanRHICommandContext::dispatchIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset )
	{
		VkCommandBuffer							   cmd	   = _pDevice->currentCommandBuffer();
		const VulkanRHIDevice::VulkanBufferRecord* pRecord = _pDevice->resolveAllocatedBuffer( argumentBuffer );
		if ( cmd == VK_NULL_HANDLE || pRecord == nullptr )
			return;

		if ( pRecord->_buffer != VK_NULL_HANDLE )
			vkCmdDispatchIndirect( cmd, pRecord->_buffer, argumentBufferOffset );
	}

	void VulkanRHICommandContext::multiDrawIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset, uint32 maxCommandCount,
													 RHIBufferHandle countBuffer, uint32 countBufferOffset )
	{
		VkCommandBuffer							   cmd	 = _pDevice->currentCommandBuffer();
		const VulkanRHIDevice::VulkanBufferRecord* pArgs = _pDevice->resolveAllocatedBuffer( argumentBuffer );
		const bool								   bValidMultiArgs =
			( cmd != VK_NULL_HANDLE && pArgs != nullptr && pArgs->_buffer != VK_NULL_HANDLE && maxCommandCount > 0 );
		if ( bValidMultiArgs == false )
			return;

		constexpr uint32 stride = sizeof( RHIDrawIndirectCommand );

		if ( countBuffer != 0 && _pDevice->_bDrawIndirectCount != 0 )
		{
			const VulkanRHIDevice::VulkanBufferRecord* pCountRec = _pDevice->resolveAllocatedBuffer( countBuffer );
			if ( pCountRec != nullptr )
			{
				if ( pCountRec->_buffer != VK_NULL_HANDLE )
				{
					vkCmdDrawIndirectCount( cmd, pArgs->_buffer, argumentBufferOffset, pCountRec->_buffer, countBufferOffset,
											maxCommandCount, stride );
					return;
				}
			}
		}

		if ( _pDevice->_bMultiDrawIndirect != 0 && maxCommandCount > 1 )
		{
			vkCmdDrawIndirect( cmd, pArgs->_buffer, argumentBufferOffset, maxCommandCount, stride );
			return;
		}

		for ( uint32 commandIndex = 0; commandIndex < maxCommandCount; ++commandIndex )
		{
			vkCmdDrawIndirect( cmd, pArgs->_buffer, argumentBufferOffset + commandIndex * stride, 1, stride );
		}
	}

	void VulkanRHICommandContext::beginEventMarker( const utf8* pName )
	{
		VkCommandBuffer cmd = _pDevice->currentCommandBuffer();
		if ( cmd == VK_NULL_HANDLE || pName == nullptr || _pDevice->_instance == VK_NULL_HANDLE )
			return;

		PFN_vkCmdBeginDebugUtilsLabelEXT pFn = reinterpret_cast<PFN_vkCmdBeginDebugUtilsLabelEXT>(
			vkGetInstanceProcAddr( _pDevice->_instance, "vkCmdBeginDebugUtilsLabelEXT" ) );
		if ( pFn == nullptr )
			return;

		VkDebugUtilsLabelEXT label{};
		label.sType		 = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
		label.pLabelName = pName;
		pFn( cmd, &label );
	}

	void VulkanRHICommandContext::endEventMarker()
	{
		VkCommandBuffer cmd = _pDevice->currentCommandBuffer();
		if ( cmd == VK_NULL_HANDLE || _pDevice->_instance == VK_NULL_HANDLE )
			return;

		PFN_vkCmdEndDebugUtilsLabelEXT pFn = reinterpret_cast<PFN_vkCmdEndDebugUtilsLabelEXT>(
			vkGetInstanceProcAddr( _pDevice->_instance, "vkCmdEndDebugUtilsLabelEXT" ) );
		if ( pFn != nullptr )
			pFn( cmd );
	}
} // namespace sw

#include "pch.h"

#include "Engine/Graphics/RHI/Vulkan/VulkanRHIResource.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/RHI/FrameResourceRing.h"
#include "Engine/Graphics/RHI/Vulkan/VulkanRHIDevice.h"
#include "Engine/Graphics/Shader/ShaderCache.h"

#include <vulkan/vulkan.h>

namespace
{
	sw::ShaderCompileResult compileShader( const sw::ShaderCompileDesc& desc )
	{
		if ( sw::engine::areEngineServicesBound() )
			return sw::engine::getShaderCache().getOrCompile( desc );
		return sw::ShaderCompiler::compileHLSL( desc );
	}

	inline VkFormat toVulkanTextureFormat( sw::RHIFormat format )
	{
		switch ( format )
		{
			case sw::RHIFormat::R8G8B8A8_UNORM:
				return VK_FORMAT_R8G8B8A8_UNORM;
			case sw::RHIFormat::B8G8R8A8_UNORM:
				return VK_FORMAT_B8G8R8A8_UNORM;
			case sw::RHIFormat::R16G16B16A16_FLOAT:
				return VK_FORMAT_R16G16B16A16_SFLOAT;
			case sw::RHIFormat::D24_UNORM_S8_UINT:
				return VK_FORMAT_D24_UNORM_S8_UINT;
			case sw::RHIFormat::R32G32B32_FLOAT:
				return VK_FORMAT_R32G32B32_SFLOAT;
			case sw::RHIFormat::R32G32_FLOAT:
				return VK_FORMAT_R32G32_SFLOAT;
			case sw::RHIFormat::R32_FLOAT:
				return VK_FORMAT_R32_SFLOAT;
			default:
				break;
		}
		return VK_FORMAT_UNDEFINED;
	}
} // namespace

namespace sw
{
	SW_LOG_CALLER( "VulkanRHIResource" );

	RHIPipelineStateHandle VulkanRHIResource::createPipelineState( const RHIPipelineStateDesc& desc )
	{
		ShaderCompileDesc vsDesc{};
		vsDesc._filePath			 = desc._vertexShaderPath;
		vsDesc._entryPoint			 = desc._vertexEntryPoint;
		vsDesc._stage				 = ShaderStage::Vertex;
		vsDesc._targetFormat		 = ShaderTargetFormat::SPIRV_Vulkan;
		ShaderCompileResult vsResult = compileShader( vsDesc );

		const bool			bDepthOnly		= ( desc._numRenderTargets == 0 && desc._bEnableDepthTest != 0 );
		const bool			bHasPixelShader = desc._pixelShaderPath.empty() == false && bDepthOnly == false;
		ShaderCompileResult psResult{};
		if ( bHasPixelShader )
		{
			ShaderCompileDesc psDesc{};
			psDesc._filePath	 = desc._pixelShaderPath;
			psDesc._entryPoint	 = desc._pixelEntryPoint;
			psDesc._stage		 = ShaderStage::Pixel;
			psDesc._targetFormat = ShaderTargetFormat::SPIRV_Vulkan;
			psResult			 = compileShader( psDesc );
		}

		if ( vsResult._bSuccess == false || ( bHasPixelShader && psResult._bSuccess == false ) )
		{
			SW_LOG_WARNING( "createPipelineState: shader compile failed (vs=%# ps=%#)",
							vsResult._bSuccess, psResult._bSuccess );
			return 0;
		}

		if ( _pDevice->_pipelineLayout == VK_NULL_HANDLE )
		{
			SW_LOG_ERROR( "createPipelineState: pipeline layout is null" );
			return 0;
		}

		VkShaderModuleCreateInfo vsInfo{};
		vsInfo.sType	= VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		vsInfo.codeSize = vsResult._bytecode.size();
		vsInfo.pCode	= reinterpret_cast<const uint32*>( vsResult._bytecode.data() );
		VkShaderModule vertShaderModule{ VK_NULL_HANDLE };
		vkCreateShaderModule( _pDevice->_device, &vsInfo, nullptr, &vertShaderModule );

		VkShaderModule fragShaderModule{ VK_NULL_HANDLE };
		if ( bHasPixelShader )
		{
			VkShaderModuleCreateInfo psInfo{};
			psInfo.sType	= VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
			psInfo.codeSize = psResult._bytecode.size();
			psInfo.pCode	= reinterpret_cast<const uint32*>( psResult._bytecode.data() );
			vkCreateShaderModule( _pDevice->_device, &psInfo, nullptr, &fragShaderModule );
		}

		VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
		vertShaderStageInfo.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		vertShaderStageInfo.stage  = VK_SHADER_STAGE_VERTEX_BIT;
		vertShaderStageInfo.module = vertShaderModule;
		vertShaderStageInfo.pName  = "VSMain";

		VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
		if ( bHasPixelShader )
		{
			fragShaderStageInfo.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			fragShaderStageInfo.stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
			fragShaderStageInfo.module = fragShaderModule;
			fragShaderStageInfo.pName  = "PSMain";
		}

		VkPipelineShaderStageCreateInfo arrShaderStage[] = { vertShaderStageInfo, fragShaderStageInfo };

		VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
		vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

		VkVertexInputBindingDescription bindingDescription{};
		bindingDescription.binding	 = 0;
		bindingDescription.stride	 = static_cast<uint32>( sizeof( RHIVertex ) );
		bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

		VkVertexInputAttributeDescription arrAttributeDescription[2]{};
		arrAttributeDescription[0].binding	= 0;
		arrAttributeDescription[0].location = 0;
		arrAttributeDescription[0].format	= VK_FORMAT_R32G32B32_SFLOAT;
		arrAttributeDescription[0].offset	= SW_OFFSET_OF( RHIVertex, _arrPosition );
		arrAttributeDescription[1].binding	= 0;
		arrAttributeDescription[1].location = 1;
		arrAttributeDescription[1].format	= VK_FORMAT_R32G32B32A32_SFLOAT;
		arrAttributeDescription[1].offset	= SW_OFFSET_OF( RHIVertex, _arrColor );

		vertexInputInfo.vertexBindingDescriptionCount	= 1;
		vertexInputInfo.pVertexBindingDescriptions		= &bindingDescription;
		vertexInputInfo.vertexAttributeDescriptionCount = 2;
		vertexInputInfo.pVertexAttributeDescriptions	= arrAttributeDescription;

		VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
		inputAssembly.sType					 = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		inputAssembly.topology				 = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		inputAssembly.primitiveRestartEnable = VK_FALSE;

		VkPipelineViewportStateCreateInfo viewportState{};
		viewportState.sType			= VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		viewportState.viewportCount = 1;
		viewportState.scissorCount	= 1;

		VkPipelineRasterizationStateCreateInfo rasterizer{};
		rasterizer.sType				   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		rasterizer.depthClampEnable		   = VK_FALSE;
		rasterizer.rasterizerDiscardEnable = VK_FALSE;
		rasterizer.polygonMode			   = ( desc._fillMode == RHIFillMode::Wireframe ) ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
		rasterizer.lineWidth			   = 1.0f;
		rasterizer.cullMode				   = ( desc._cullMode == RHICullMode::Front ) ? VK_CULL_MODE_FRONT_BIT : ( ( desc._cullMode == RHICullMode::Back ) ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_NONE );
		rasterizer.frontFace			   = VK_FRONT_FACE_CLOCKWISE;

		VkPipelineMultisampleStateCreateInfo multisampling{};
		multisampling.sType				   = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		multisampling.sampleShadingEnable  = VK_FALSE;
		multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

		const uint32 numRT		= bDepthOnly ? 0u : ( ( desc._numRenderTargets > 0 ) ? desc._numRenderTargets : 1u );
		const uint32 blendCount = ( numRT > kMaxColorAttachments ) ? kMaxColorAttachments : numRT;

		VkPipelineColorBlendAttachmentState arrColorBlendAttachment[kMaxColorAttachments]{};
		for ( uint32 blendIndex = 0; blendIndex < blendCount; ++blendIndex )
		{
			arrColorBlendAttachment[blendIndex].colorWriteMask		= VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
																	  VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
			arrColorBlendAttachment[blendIndex].blendEnable			= desc._bEnableBlend ? VK_TRUE : VK_FALSE;
			arrColorBlendAttachment[blendIndex].srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
			arrColorBlendAttachment[blendIndex].dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
			arrColorBlendAttachment[blendIndex].colorBlendOp		= VK_BLEND_OP_ADD;
			arrColorBlendAttachment[blendIndex].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
			arrColorBlendAttachment[blendIndex].dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
			arrColorBlendAttachment[blendIndex].alphaBlendOp		= VK_BLEND_OP_ADD;
		}

		VkPipelineColorBlendStateCreateInfo colorBlending{};
		colorBlending.sType			  = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		colorBlending.logicOpEnable	  = VK_FALSE;
		colorBlending.attachmentCount = blendCount;
		colorBlending.pAttachments	  = blendCount > 0 ? arrColorBlendAttachment : nullptr;

		VkRenderPass pipelineRp = _pDevice->ensurePipelineRenderPass( desc );
		if ( pipelineRp == VK_NULL_HANDLE )
			pipelineRp = _pDevice->_renderPass;

		VkPipelineDepthStencilStateCreateInfo depthStencil{};
		depthStencil.sType				   = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
		depthStencil.depthTestEnable	   = desc._bEnableDepthTest ? VK_TRUE : VK_FALSE;
		depthStencil.depthWriteEnable	   = ( depthStencil.depthTestEnable != VK_FALSE && desc._bEnableDepthWrite ) ? VK_TRUE : VK_FALSE;
		depthStencil.depthCompareOp		   = VK_COMPARE_OP_LESS;
		depthStencil.depthBoundsTestEnable = VK_FALSE;
		depthStencil.stencilTestEnable	   = VK_FALSE;

		vector<VkDynamicState>			 listDynamicState = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
		VkPipelineDynamicStateCreateInfo dynamicState{};
		dynamicState.sType			   = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
		dynamicState.dynamicStateCount = static_cast<uint32>( listDynamicState.size() );
		dynamicState.pDynamicStates	   = listDynamicState.data();

		VkGraphicsPipelineCreateInfo pipelineInfo{};
		pipelineInfo.sType				 = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		pipelineInfo.stageCount			 = bHasPixelShader ? 2 : 1;
		pipelineInfo.pStages			 = arrShaderStage;
		pipelineInfo.pVertexInputState	 = &vertexInputInfo;
		pipelineInfo.pInputAssemblyState = &inputAssembly;
		pipelineInfo.pViewportState		 = &viewportState;
		pipelineInfo.pRasterizationState = &rasterizer;
		pipelineInfo.pMultisampleState	 = &multisampling;
		pipelineInfo.pDepthStencilState	 = &depthStencil;
		pipelineInfo.pColorBlendState	 = &colorBlending;
		pipelineInfo.pDynamicState		 = &dynamicState;
		pipelineInfo.layout				 = _pDevice->_pipelineLayout;
		pipelineInfo.renderPass			 = pipelineRp;
		pipelineInfo.subpass			 = 0;
		pipelineInfo.basePipelineHandle	 = VK_NULL_HANDLE;

		VkPipeline newPipeline;
		if ( vkCreateGraphicsPipelines( _pDevice->_device, _pDevice->_pipelineCache, 1, &pipelineInfo, nullptr, &newPipeline ) != VK_SUCCESS )
			newPipeline = _pDevice->_pipeline;

		vkDestroyShaderModule( _pDevice->_device, vertShaderModule, nullptr );
		if ( fragShaderModule != VK_NULL_HANDLE )
			vkDestroyShaderModule( _pDevice->_device, fragShaderModule, nullptr );

		VulkanRHIDevice::VulkanPipelineStateRecord record{};
		record._pipeline = newPipeline;
		return _pDevice->_pipelineStates.insert( std::move( record ) );
	}

	RHIPipelineStateHandle VulkanRHIResource::createComputePipelineState( string_view shaderPath, string_view entryPoint )
	{
		ShaderCompileDesc csDesc{};
		csDesc._filePath			 = shaderPath;
		csDesc._entryPoint			 = entryPoint;
		csDesc._stage				 = ShaderStage::Compute;
		csDesc._targetFormat		 = ShaderTargetFormat::SPIRV_Vulkan;
		ShaderCompileResult csResult = compileShader( csDesc );

		if ( csResult._bSuccess == false )
		{
			VulkanRHIDevice::VulkanPipelineStateRecord record{};
			return _pDevice->_pipelineStates.insert( std::move( record ) );
		}

		VkShaderModuleCreateInfo csInfo{};
		csInfo.sType	= VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		csInfo.codeSize = csResult._bytecode.size();
		csInfo.pCode	= reinterpret_cast<const uint32*>( csResult._bytecode.data() );
		VkShaderModule compShaderModule;
		vkCreateShaderModule( _pDevice->_device, &csInfo, nullptr, &compShaderModule );

		VkPipelineShaderStageCreateInfo compShaderStageInfo{};
		compShaderStageInfo.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		compShaderStageInfo.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
		compShaderStageInfo.module = compShaderModule;
		compShaderStageInfo.pName  = "CSMain";

		VkComputePipelineCreateInfo pipelineInfo{};
		pipelineInfo.sType	= VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
		pipelineInfo.layout = _pDevice->_pipelineLayout;
		pipelineInfo.stage	= compShaderStageInfo;

		VkPipeline newPipeline;
		if ( vkCreateComputePipelines( _pDevice->_device, _pDevice->_pipelineCache, 1, &pipelineInfo, nullptr, &newPipeline ) != VK_SUCCESS )
			newPipeline = VK_NULL_HANDLE;

		vkDestroyShaderModule( _pDevice->_device, compShaderModule, nullptr );

		VulkanRHIDevice::VulkanPipelineStateRecord record{};
		record._pipeline = newPipeline;
		return _pDevice->_pipelineStates.insert( std::move( record ) );
	}

	void VulkanRHIResource::destroyPipelineState( RHIPipelineStateHandle pso )
	{
		VulkanRHIDevice::VulkanPipelineStateRecord record{};
		if ( _pDevice->_pipelineStates.take( pso, record ) == false )
			return;
		if ( record._pipeline != VK_NULL_HANDLE && record._pipeline != _pDevice->_pipeline )
		{
			VkDevice   dev	= _pDevice->_device;
			VkPipeline pipe = record._pipeline;
			_pDevice->_releaseQueue.enqueueRelease( SW_DELEGATE_LAMBDA( RHIResourceReleaseDelegate, [dev, pipe]()
			{
				vkDestroyPipeline( dev, pipe, nullptr );
			} ) );
		}
		if ( _pDevice->_activeGraphicsPso == pso )
			_pDevice->_activeGraphicsPso = 0;
	}

	RHIRenderPassHandle VulkanRHIResource::createRenderPass( const RHIRenderPassDesc& desc )
	{
		VulkanRHIDevice::VulkanRenderPassRecord record{};

		if ( desc._listColorAttachment.empty() && desc._bHasDepthStencil == 0 )
		{
			SW_LOG_ERROR( "createRenderPass requires color or depth attachments." );
			return 0;
		}

		auto toLoadOp = []( RHIRenderPassLoadOp op ) -> VkAttachmentLoadOp
		{
			switch ( op )
			{
				case RHIRenderPassLoadOp::Clear:
					return VK_ATTACHMENT_LOAD_OP_CLEAR;
				case RHIRenderPassLoadOp::Load:
					return VK_ATTACHMENT_LOAD_OP_LOAD;
				case RHIRenderPassLoadOp::DontCare:
					return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
				default:
					break;
			}
			return VK_ATTACHMENT_LOAD_OP_CLEAR;
		};
		auto toStoreOp = []( RHIRenderPassStoreOp op ) -> VkAttachmentStoreOp
		{
			switch ( op )
			{
				case RHIRenderPassStoreOp::Store:
					return VK_ATTACHMENT_STORE_OP_STORE;
				case RHIRenderPassStoreOp::DontCare:
					return VK_ATTACHMENT_STORE_OP_DONT_CARE;
				default:
					break;
			}
			return VK_ATTACHMENT_STORE_OP_STORE;
		};

		VkAttachmentDescription attachments[kMaxColorAttachments + 1]{};
		VkAttachmentReference	colorRefs[kMaxColorAttachments]{};
		const uint32			colorCount =
			desc._listColorAttachment.size() > kMaxColorAttachments
				? kMaxColorAttachments
				: static_cast<uint32>( desc._listColorAttachment.size() );

		for ( uint32 colorIndex = 0; colorIndex < colorCount; ++colorIndex )
		{
			const RHIRenderPassAttachment& att	   = desc._listColorAttachment[colorIndex];
			attachments[colorIndex].format		   = toVulkanTextureFormat( att._format );
			attachments[colorIndex].samples		   = VK_SAMPLE_COUNT_1_BIT;
			attachments[colorIndex].loadOp		   = toLoadOp( att._loadOp );
			attachments[colorIndex].storeOp		   = toStoreOp( att._storeOp );
			attachments[colorIndex].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			attachments[colorIndex].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			attachments[colorIndex].initialLayout  = ( att._loadOp == RHIRenderPassLoadOp::Clear ) ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			attachments[colorIndex].finalLayout	   = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			colorRefs[colorIndex].attachment	   = colorIndex;
			colorRefs[colorIndex].layout		   = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		}

		VkAttachmentReference depthRef{};
		uint32				  attachCount = colorCount;
		if ( desc._bHasDepthStencil != 0 )
		{
			attachments[attachCount].format			= static_cast<VkFormat>( _pDevice->_depthFormat );
			attachments[attachCount].samples		= VK_SAMPLE_COUNT_1_BIT;
			attachments[attachCount].loadOp			= VK_ATTACHMENT_LOAD_OP_CLEAR;
			attachments[attachCount].storeOp		= VK_ATTACHMENT_STORE_OP_STORE;
			attachments[attachCount].stencilLoadOp	= VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			attachments[attachCount].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			attachments[attachCount].initialLayout	= VK_IMAGE_LAYOUT_UNDEFINED;
			attachments[attachCount].finalLayout	= VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
			depthRef.attachment						= attachCount;
			depthRef.layout							= VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
			++attachCount;
		}

		if ( attachCount == 0 )
		{
			record._renderPass = _pDevice->_renderPass;
			record._bOwned	   = 0;
			_pDevice->_listRenderPass.push_back( record );
			return _pDevice->_listRenderPass.size();
		}

		VkSubpassDescription subpass{};
		subpass.pipelineBindPoint		= VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.colorAttachmentCount	= colorCount;
		subpass.pColorAttachments		= colorCount > 0 ? colorRefs : nullptr;
		subpass.pDepthStencilAttachment = desc._bHasDepthStencil ? &depthRef : nullptr;

		VkSubpassDependency dependency{};
		dependency.srcSubpass	 = VK_SUBPASS_EXTERNAL;
		dependency.dstSubpass	 = 0;
		dependency.srcStageMask	 = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
		dependency.srcAccessMask = 0;
		dependency.dstStageMask	 = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
		dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

		VkRenderPassCreateInfo rpInfo{};
		rpInfo.sType		   = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		rpInfo.attachmentCount = attachCount;
		rpInfo.pAttachments	   = attachments;
		rpInfo.subpassCount	   = 1;
		rpInfo.pSubpasses	   = &subpass;
		rpInfo.dependencyCount = 1;
		rpInfo.pDependencies   = &dependency;

		VkRenderPass created = VK_NULL_HANDLE;
		if ( vkCreateRenderPass( _pDevice->_device, &rpInfo, nullptr, &created ) != VK_SUCCESS )
		{
			SW_LOG_ERROR( "createRenderPass(desc) failed" );
			return 0;
		}

		record._renderPass = created;
		record._bOwned	   = 1;
		_pDevice->_listRenderPass.push_back( record );
		return _pDevice->_listRenderPass.size();
	}

	void VulkanRHIResource::destroyRenderPass( RHIRenderPassHandle pass )
	{
		if ( pass == 0 || pass > _pDevice->_listRenderPass.size() )
			return;
		VulkanRHIDevice::VulkanRenderPassRecord& record = _pDevice->_listRenderPass[pass - 1];
		if ( record._bOwned != 0 && record._renderPass != VK_NULL_HANDLE &&
			 record._renderPass != _pDevice->_renderPass )
		{
			vkDestroyRenderPass( _pDevice->_device, record._renderPass, nullptr );
		}
		record = {};
	}

	RHIBufferHandle VulkanRHIResource::createConstantBuffer( uint32 size )
	{
		const uint32		  aligned = MathUtil::align( size, 256u );
		const uint32		  total	  = aligned * FrameResourceRing::kFrameCount;
		const RHIBufferHandle handle  = _pDevice->createVulkanBuffer( total, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, nullptr );
		if ( handle != 0 )
			_pDevice->_mapCbSlotSize[handle] = aligned;
		return handle;
	}

	void VulkanRHIResource::updateConstantBuffer( RHIBufferHandle buffer, const void* pData, uint32 size )
	{
		VulkanRHIDevice::VulkanBufferRecord* pRecord = _pDevice->resolveAllocatedBuffer( buffer );
		if ( pRecord == nullptr || pData == nullptr || size == 0 )
			return;

		if ( pRecord->_memory == VK_NULL_HANDLE )
			return;

		uint32	   slotSize = size;
		const auto slotIt	= _pDevice->_mapCbSlotSize.find( buffer );
		if ( slotIt != _pDevice->_mapCbSlotSize.end() )
			slotSize = slotIt->second;
		const uint32 offset = ( _pDevice->_currentFrame % FrameResourceRing::kFrameCount ) * slotSize;

		void* pMapped{ nullptr };
		if ( vkMapMemory( _pDevice->_device, pRecord->_memory, offset, size, 0, &pMapped ) == VK_SUCCESS )
		{
			Memory::copy( pMapped, pData, size );
			vkUnmapMemory( _pDevice->_device, pRecord->_memory );
		}

		for ( size_t bufferIndex = 0; bufferIndex < _pDevice->_listBindlessSourceBuffer.size(); ++bufferIndex )
		{
			if ( _pDevice->_listBindlessSourceBuffer[bufferIndex] != buffer || bufferIndex >= _pDevice->_listRegisteredDescriptorSet.size() )
				continue;
			VkDescriptorSet set = _pDevice->_listRegisteredDescriptorSet[bufferIndex];
			if ( set == VK_NULL_HANDLE )
				continue;
			VkDescriptorBufferInfo bufferInfo{};
			bufferInfo.buffer = pRecord->_buffer;
			bufferInfo.offset = offset;
			bufferInfo.range  = slotSize;
			VkWriteDescriptorSet descriptorWrite{};
			descriptorWrite.sType			= VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			descriptorWrite.dstSet			= set;
			descriptorWrite.dstBinding		= 0;
			descriptorWrite.dstArrayElement = 0;
			descriptorWrite.descriptorType	= VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			descriptorWrite.descriptorCount = 1;
			descriptorWrite.pBufferInfo		= &bufferInfo;
			vkUpdateDescriptorSets( _pDevice->_device, 1, &descriptorWrite, 0, nullptr );
		}
	}

	RHIBufferHandle VulkanRHIResource::createStructuredBuffer( uint32 elementSize, uint32 elementCount )
	{
		if ( elementSize == 0 || elementCount == 0 )
			return 0;
		constexpr uint32 usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
								 VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		return _pDevice->createVulkanBuffer( elementSize * elementCount, usage, nullptr );
	}

	void VulkanRHIResource::updateStructuredBuffer( RHIBufferHandle buffer, const void* pData, uint32 size )
	{
		updateConstantBuffer( buffer, pData, size );
	}

	RHIBufferHandle VulkanRHIResource::createVertexBuffer( const void* pData, uint32 sizeBytes )
	{
		if ( _pDevice->_device == nullptr || pData == nullptr || sizeBytes == 0 )
			return 0;

		VkBufferCreateInfo bufferInfo{};
		bufferInfo.sType	   = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferInfo.size		   = sizeBytes;
		bufferInfo.usage	   = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
		bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		VkBuffer buffer = VK_NULL_HANDLE;
		if ( vkCreateBuffer( _pDevice->_device, &bufferInfo, nullptr, &buffer ) != VK_SUCCESS )
		{
			SW_LOG_ERROR( "Failed to create VkBuffer for Vertex Buffer!" );
			return 0;
		}

		VkMemoryRequirements memRequirements;
		vkGetBufferMemoryRequirements( _pDevice->_device, buffer, &memRequirements );

		uint32 memoryTypeIndex{ 0 };
		if ( _pDevice->findMemoryType( memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, memoryTypeIndex ) == false )
		{
			vkDestroyBuffer( _pDevice->_device, buffer, nullptr );
			SW_LOG_ERROR( "Failed to find a host visible memory type for Vertex Buffer!" );
			return 0;
		}

		VkMemoryAllocateInfo allocInfo{};
		allocInfo.sType			  = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocInfo.allocationSize  = memRequirements.size;
		allocInfo.memoryTypeIndex = memoryTypeIndex;

		VkDeviceMemory memory = VK_NULL_HANDLE;
		if ( vkAllocateMemory( _pDevice->_device, &allocInfo, nullptr, &memory ) != VK_SUCCESS )
		{
			vkDestroyBuffer( _pDevice->_device, buffer, nullptr );
			SW_LOG_ERROR( "Failed to allocate memory for Vertex Buffer!" );
			return 0;
		}

		vkBindBufferMemory( _pDevice->_device, buffer, memory, 0 );

		void* pMapped{ nullptr };
		if ( vkMapMemory( _pDevice->_device, memory, 0, sizeBytes, 0, &pMapped ) == VK_SUCCESS && pMapped != nullptr )
		{
			Memory::copy( pMapped, pData, sizeBytes );
			vkUnmapMemory( _pDevice->_device, memory );
		}

		VulkanRHIDevice::VulkanBufferRecord record{};
		record._buffer = buffer;
		record._memory = memory;
		record._size   = sizeBytes;
		return _pDevice->_gpuBuffers.insert( std::move( record ) );
	}

	void VulkanRHIResource::destroyBuffer( RHIBufferHandle buffer )
	{
		if ( buffer == 0 )
			return;
		if ( buffer == _pDevice->_boundMeshVb )
			_pDevice->_boundMeshVb = 0;
		if ( buffer == _pDevice->_boundIndexBuffer )
			_pDevice->_boundIndexBuffer = 0;
		_pDevice->_mapCbSlotSize.erase( buffer );

		VulkanRHIDevice::VulkanBufferRecord owned;
		if ( _pDevice->_gpuBuffers.take( buffer, owned ) == false )
			return;

		for ( size_t bufferIndex = 0; bufferIndex < _pDevice->_listBindlessSourceBuffer.size(); ++bufferIndex )
		{
			if ( _pDevice->_listBindlessSourceBuffer[bufferIndex] != buffer )
				continue;
			if ( bufferIndex < _pDevice->_listRegisteredDescriptorSet.size() && _pDevice->_listRegisteredDescriptorSet[bufferIndex] != VK_NULL_HANDLE )
			{
				VkDevice		 dev  = _pDevice->_device;
				VkDescriptorPool pool = _pDevice->_descriptorPool;
				VkDescriptorSet	 set  = _pDevice->_listRegisteredDescriptorSet[bufferIndex];
				_pDevice->_releaseQueue.enqueueRelease( SW_DELEGATE_LAMBDA( RHIResourceReleaseDelegate, [dev, pool, set]()
				{
					vkFreeDescriptorSets( dev, pool, 1, &set );
				} ) );
				_pDevice->_listRegisteredDescriptorSet[bufferIndex] = VK_NULL_HANDLE;
			}
			_pDevice->_listBindlessSourceBuffer[bufferIndex] = 0;
			_pDevice->_listBindlessFree.push_back( static_cast<uint32>( bufferIndex ) );
		}
		for ( size_t bufferIndex = 0; bufferIndex < _pDevice->_listUavSourceBuffer.size(); ++bufferIndex )
		{
			if ( _pDevice->_listUavSourceBuffer[bufferIndex] != buffer )
				continue;
			if ( bufferIndex < _pDevice->_listRegisteredUAV.size() && _pDevice->_listRegisteredUAV[bufferIndex] != VK_NULL_HANDLE )
			{
				VkDevice		 dev  = _pDevice->_device;
				VkDescriptorPool pool = _pDevice->_descriptorPool;
				VkDescriptorSet	 set  = _pDevice->_listRegisteredUAV[bufferIndex];
				_pDevice->_releaseQueue.enqueueRelease( SW_DELEGATE_LAMBDA( RHIResourceReleaseDelegate, [dev, pool, set]()
				{
					vkFreeDescriptorSets( dev, pool, 1, &set );
				} ) );
				_pDevice->_listRegisteredUAV[bufferIndex] = VK_NULL_HANDLE;
			}
			_pDevice->_listUavSourceBuffer[bufferIndex] = 0;
			_pDevice->_listUavFree.push_back( static_cast<uint32>( bufferIndex ) );
		}

		VkBuffer	   buf = owned._buffer;
		VkDeviceMemory mem = owned._memory;
		VkDevice	   dev = _pDevice->_device;
		_pDevice->_releaseQueue.enqueueRelease( SW_DELEGATE_LAMBDA( RHIResourceReleaseDelegate, [dev, buf, mem]()
		{
			if ( buf != VK_NULL_HANDLE )
				vkDestroyBuffer( dev, buf, nullptr );
			if ( mem != VK_NULL_HANDLE )
				vkFreeMemory( dev, mem, nullptr );
		} ) );
	}

	RHITextureHandle VulkanRHIResource::createTexture2D( const RHITextureDesc& desc )
	{
		if ( _pDevice->_device == nullptr || desc._width == 0 || desc._height == 0 )
			return 0;

		VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
		if ( desc._bIsRenderTarget )
			usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
		if ( desc._bIsDepthStencil )
			usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
		if ( desc._bIsUnorderedAccess )
			usage |= VK_IMAGE_USAGE_STORAGE_BIT;

		const VkFormat requested = toVulkanTextureFormat( desc._format );
		VkFormat	   format	 = requested;
		if ( desc._bIsDepthStencil != 0 || desc._format == sw::RHIFormat::D24_UNORM_S8_UINT )
		{
			if ( _pDevice->_depthFormat == 0 )
			{
				SW_LOG_ERROR( "createTexture2D: depth format not selected." );
				return 0;
			}
			format = static_cast<VkFormat>( _pDevice->_depthFormat );
		}

		VkImageCreateInfo imageInfo{};
		imageInfo.sType			= VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imageInfo.imageType		= VK_IMAGE_TYPE_2D;
		imageInfo.extent.width	= desc._width;
		imageInfo.extent.height = desc._height;
		imageInfo.extent.depth	= 1;
		imageInfo.mipLevels		= desc._mipLevels > 0 ? desc._mipLevels : 1;
		imageInfo.arrayLayers	= 1;
		imageInfo.format		= format;
		imageInfo.tiling		= VK_IMAGE_TILING_OPTIMAL;
		imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		imageInfo.usage			= usage;
		imageInfo.samples		= VK_SAMPLE_COUNT_1_BIT;
		imageInfo.sharingMode	= VK_SHARING_MODE_EXCLUSIVE;

		VulkanRHIDevice::VulkanTextureRecord record{};
		record._width		  = desc._width;
		record._height		  = desc._height;
		record._format		  = static_cast<uint32>( format );
		record._layout		  = static_cast<uint32>( VK_IMAGE_LAYOUT_UNDEFINED );
		record._bRenderTarget = desc._bIsRenderTarget ? 1 : 0;
		record._bDepthStencil = desc._bIsDepthStencil ? 1 : 0;
		record._bindlessIndex = kInvalidDescriptorIndex;

		if ( vkCreateImage( _pDevice->_device, &imageInfo, nullptr, &record._image ) != VK_SUCCESS )
		{
			SW_LOG_ERROR( "Failed to create VkImage for Texture2D." );
			return 0;
		}

		VkMemoryRequirements memRequirements;
		vkGetImageMemoryRequirements( _pDevice->_device, record._image, &memRequirements );

		uint32 memoryTypeIndex{ 0 };
		if ( _pDevice->findMemoryType( memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, memoryTypeIndex ) == false )
		{
			vkDestroyImage( _pDevice->_device, record._image, nullptr );
			SW_LOG_ERROR( "Failed to find a device local memory type for Texture2D." );
			return 0;
		}

		VkMemoryAllocateInfo allocInfo{};
		allocInfo.sType			  = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocInfo.allocationSize  = memRequirements.size;
		allocInfo.memoryTypeIndex = memoryTypeIndex;

		if ( vkAllocateMemory( _pDevice->_device, &allocInfo, nullptr, &record._memory ) != VK_SUCCESS )
		{
			vkDestroyImage( _pDevice->_device, record._image, nullptr );
			SW_LOG_ERROR( "Failed to allocate memory for Texture2D." );
			return 0;
		}

		vkBindImageMemory( _pDevice->_device, record._image, record._memory, 0 );

		VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
		if ( desc._bIsDepthStencil != 0 )
			aspect = static_cast<VkImageAspectFlags>( _pDevice->depthAspectMask() );

		VkImageViewCreateInfo viewInfo{};
		viewInfo.sType							 = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewInfo.image							 = record._image;
		viewInfo.viewType						 = VK_IMAGE_VIEW_TYPE_2D;
		viewInfo.format							 = format;
		viewInfo.subresourceRange.aspectMask	 = aspect;
		viewInfo.subresourceRange.baseMipLevel	 = 0;
		viewInfo.subresourceRange.levelCount	 = imageInfo.mipLevels;
		viewInfo.subresourceRange.baseArrayLayer = 0;
		viewInfo.subresourceRange.layerCount	 = 1;

		if ( vkCreateImageView( _pDevice->_device, &viewInfo, nullptr, &record._imageView ) != VK_SUCCESS )
		{
			vkDestroyImage( _pDevice->_device, record._image, nullptr );
			vkFreeMemory( _pDevice->_device, record._memory, nullptr );
			SW_LOG_ERROR( "Failed to create VkImageView for Texture2D." );
			return 0;
		}

		if ( record._bRenderTarget && _pDevice->createOffscreenFramebuffer( record ) == false )
			SW_LOG_WARNING( "createTexture2D: framebuffer creation failed — texture kept without offscreen pass." );

		return _pDevice->_gpuTextures.insert( std::move( record ) );
	}

	void VulkanRHIResource::destroyTexture( RHITextureHandle texture )
	{
		if ( texture == 0 )
			return;

		VulkanRHIDevice::VulkanTextureRecord* pSlot = _pDevice->resolveTexture( texture );
		if ( pSlot == nullptr )
			return;

		_pDevice->destroyCompositeFramebuffersUsing( texture );

		if ( pSlot->_bindlessIndex != kInvalidDescriptorIndex )
		{
			const RHIDescriptorIndex index = pSlot->_bindlessIndex;
			if ( _pDevice->_bindlessTextureSet != VK_NULL_HANDLE && index < _pDevice->_listRegisteredTexture.size() &&
				 _pDevice->_listRegisteredTexture[index] == _pDevice->_bindlessTextureSet )
			{
				_pDevice->writeBindlessTextureSlot( index, _pDevice->_bindlessDummyView );
				_pDevice->_listRegisteredTexture[index] = VK_NULL_HANDLE;
				_pDevice->_listTextureFree.push_back( index );
			}
			else if ( index < _pDevice->_listRegisteredTexture.size() && _pDevice->_listRegisteredTexture[index] != VK_NULL_HANDLE )
			{
				VkDevice		 dev  = _pDevice->_device;
				VkDescriptorPool pool = _pDevice->_descriptorPool;
				VkDescriptorSet	 set  = _pDevice->_listRegisteredTexture[index];
				_pDevice->_releaseQueue.enqueueRelease( SW_DELEGATE_LAMBDA( RHIResourceReleaseDelegate, [dev, pool, set]()
				{
					vkFreeDescriptorSets( dev, pool, 1, &set );
				} ) );
				_pDevice->_listRegisteredTexture[index] = VK_NULL_HANDLE;
				_pDevice->_listTextureFree.push_back( index );
			}
			pSlot->_bindlessIndex = kInvalidDescriptorIndex;
		}

		_pDevice->destroyOffscreenFramebuffer( *pSlot );
		VulkanRHIDevice::VulkanTextureRecord owned;
		if ( _pDevice->_gpuTextures.take( texture, owned ) == false )
			return;
		VkDevice	   dev	 = _pDevice->_device;
		VkImageView	   view	 = owned._imageView;
		VkImage		   image = owned._image;
		VkDeviceMemory mem	 = owned._memory;
		_pDevice->_releaseQueue.enqueueRelease( SW_DELEGATE_LAMBDA( RHIResourceReleaseDelegate, [dev, view, image, mem]()
		{
			if ( view != VK_NULL_HANDLE )
				vkDestroyImageView( dev, view, nullptr );
			if ( image != VK_NULL_HANDLE )
				vkDestroyImage( dev, image, nullptr );
			if ( mem != VK_NULL_HANDLE )
				vkFreeMemory( dev, mem, nullptr );
		} ) );
	}

	RHIDescriptorIndex VulkanRHIResource::registerBindlessTexture( RHITextureHandle texture )
	{
		if ( texture == 0 || _pDevice->_descriptorPool == VK_NULL_HANDLE || _pDevice->_defaultSampler == VK_NULL_HANDLE )
			return kInvalidDescriptorIndex;

		VulkanRHIDevice::VulkanTextureRecord* pResolved = _pDevice->resolveTexture( texture );
		if ( pResolved == nullptr || pResolved->_imageView == VK_NULL_HANDLE )
			return kInvalidDescriptorIndex;

		// DEPTH|STENCIL image views cannot be written as sampled descriptors.
		if ( pResolved->_bDepthStencil != 0 )
			return kInvalidDescriptorIndex;

		VulkanRHIDevice::VulkanTextureRecord& record = *pResolved;
		if ( record._bindlessIndex != kInvalidDescriptorIndex )
			return record._bindlessIndex;

		RHIDescriptorIndex descriptorIndex;
		if ( _pDevice->_listTextureFree.empty() == false )
		{
			descriptorIndex = _pDevice->_listTextureFree.back();
			_pDevice->_listTextureFree.pop_back();
		}
		else
			descriptorIndex = static_cast<RHIDescriptorIndex>( _pDevice->_listRegisteredTexture.size() );

		if ( descriptorIndex >= _pDevice->kBindlessTextureCount )
		{
			SW_LOG_ERROR( "Bindless texture table full." );
			return kInvalidDescriptorIndex;
		}

		if ( _pDevice->_bindlessTextureSet != VK_NULL_HANDLE )
		{
			_pDevice->writeBindlessTextureSlot( descriptorIndex, record._imageView );
			if ( descriptorIndex >= _pDevice->_listRegisteredTexture.size() )
				_pDevice->_listRegisteredTexture.resize( descriptorIndex + 1 );
			_pDevice->_listRegisteredTexture[descriptorIndex] = _pDevice->_bindlessTextureSet; // shared array set
			record._bindlessIndex							  = descriptorIndex;
			return descriptorIndex;
		}

		// Fallback: one descriptor set per texture (slot bind path).
		if ( _pDevice->_textureDescriptorSetLayout == VK_NULL_HANDLE )
			return kInvalidDescriptorIndex;

		VkDescriptorSetAllocateInfo allocInfo{};
		allocInfo.sType				 = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorPool	 = _pDevice->_descriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts		 = &_pDevice->_textureDescriptorSetLayout;

		VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
		if ( vkAllocateDescriptorSets( _pDevice->_device, &allocInfo, &descriptorSet ) != VK_SUCCESS )
		{
			SW_LOG_ERROR( "Failed to allocate VkDescriptorSet for bindless texture!" );
			return kInvalidDescriptorIndex;
		}

		VkDescriptorImageInfo imageInfo{};
		imageInfo.sampler	  = _pDevice->_defaultSampler;
		imageInfo.imageView	  = record._imageView;
		imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		VkWriteDescriptorSet write{};
		write.sType			  = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write.dstSet		  = descriptorSet;
		write.dstBinding	  = 0;
		write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		write.descriptorCount = 1;
		write.pImageInfo	  = &imageInfo;
		vkUpdateDescriptorSets( _pDevice->_device, 1, &write, 0, nullptr );

		if ( descriptorIndex >= _pDevice->_listRegisteredTexture.size() )
			_pDevice->_listRegisteredTexture.resize( descriptorIndex + 1 );
		_pDevice->_listRegisteredTexture[descriptorIndex] = descriptorSet;
		record._bindlessIndex							  = descriptorIndex;
		return descriptorIndex;
	}

	RHIDescriptorIndex VulkanRHIResource::registerBindlessResource( RHIBufferHandle buffer )
	{
		const VulkanRHIDevice::VulkanBufferRecord* pRecord = _pDevice->resolveAllocatedBuffer( buffer );
		if ( pRecord == nullptr || _pDevice->_descriptorPool == VK_NULL_HANDLE || _pDevice->_descriptorSetLayout == VK_NULL_HANDLE )
			return kInvalidDescriptorIndex;
		const bool			  bIsStorage = ( pRecord->_usage & VK_BUFFER_USAGE_STORAGE_BUFFER_BIT ) != 0;
		VkDescriptorSetLayout setLayout	 = bIsStorage ? _pDevice->_uavDescriptorSetLayout : _pDevice->_descriptorSetLayout;

		VkDescriptorSetAllocateInfo allocInfo{};
		allocInfo.sType				 = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorPool	 = _pDevice->_descriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts		 = &setLayout;

		VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
		if ( vkAllocateDescriptorSets( _pDevice->_device, &allocInfo, &descriptorSet ) != VK_SUCCESS )
			return kInvalidDescriptorIndex;

		VkDescriptorBufferInfo bufferInfo{};
		bufferInfo.buffer = pRecord->_buffer;
		bufferInfo.offset = 0;
		const auto slotIt = _pDevice->_mapCbSlotSize.find( buffer );
		bufferInfo.range  = ( slotIt != _pDevice->_mapCbSlotSize.end() ) ? slotIt->second : pRecord->_size;

		VkWriteDescriptorSet descriptorWrite{};
		descriptorWrite.sType			= VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptorWrite.dstSet			= descriptorSet;
		descriptorWrite.dstBinding		= 0;
		descriptorWrite.dstArrayElement = 0;
		descriptorWrite.descriptorType	= bIsStorage ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		descriptorWrite.descriptorCount = 1;
		descriptorWrite.pBufferInfo		= &bufferInfo;

		vkUpdateDescriptorSets( _pDevice->_device, 1, &descriptorWrite, 0, nullptr );

		RHIDescriptorIndex descriptorIndex;
		if ( _pDevice->_listBindlessFree.empty() == false )
		{
			descriptorIndex = _pDevice->_listBindlessFree.back();
			_pDevice->_listBindlessFree.pop_back();
		}
		else
			descriptorIndex = static_cast<RHIDescriptorIndex>( _pDevice->_listRegisteredDescriptorSet.size() );

		if ( descriptorIndex >= _pDevice->_listRegisteredDescriptorSet.size() )
		{
			_pDevice->_listRegisteredDescriptorSet.resize( descriptorIndex + 1 );
			_pDevice->_listBindlessSourceBuffer.resize( descriptorIndex + 1 );
		}
		_pDevice->_listRegisteredDescriptorSet[descriptorIndex] = descriptorSet;
		_pDevice->_listBindlessSourceBuffer[descriptorIndex]	= buffer;
		return descriptorIndex;
	}

	void VulkanRHIResource::unregisterBindlessResource( RHIDescriptorIndex index )
	{
		if ( index < _pDevice->_listRegisteredDescriptorSet.size() )
		{
			VkDescriptorSet set = _pDevice->_listRegisteredDescriptorSet[index];
			if ( set != VK_NULL_HANDLE )
			{
				VkDevice		 dev  = _pDevice->_device;
				VkDescriptorPool pool = _pDevice->_descriptorPool;
				_pDevice->_releaseQueue.enqueueRelease( SW_DELEGATE_LAMBDA( RHIResourceReleaseDelegate, [dev, pool, set]()
				{
					vkFreeDescriptorSets( dev, pool, 1, &set );
				} ) );
				_pDevice->_listRegisteredDescriptorSet[index] = VK_NULL_HANDLE;
			}
			if ( index < _pDevice->_listBindlessSourceBuffer.size() )
				_pDevice->_listBindlessSourceBuffer[index] = 0;
			_pDevice->_listBindlessFree.push_back( index );
		}
	}

	RHIDescriptorIndex VulkanRHIResource::registerBindlessUAV( RHIBufferHandle buffer )
	{
		const VulkanRHIDevice::VulkanBufferRecord* pRecord = _pDevice->resolveAllocatedBuffer( buffer );
		if ( pRecord == nullptr || _pDevice->_descriptorPool == VK_NULL_HANDLE || _pDevice->_uavDescriptorSetLayout == VK_NULL_HANDLE )
			return kInvalidDescriptorIndex;

		VkDescriptorSetAllocateInfo allocInfo{};
		allocInfo.sType				 = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorPool	 = _pDevice->_descriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts		 = &_pDevice->_uavDescriptorSetLayout;

		VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
		if ( vkAllocateDescriptorSets( _pDevice->_device, &allocInfo, &descriptorSet ) != VK_SUCCESS )
		{
			SW_LOG_ERROR( "Failed to allocate VkDescriptorSet for UAV!" );
			return kInvalidDescriptorIndex;
		}

		VkDescriptorBufferInfo bufferInfo{};
		bufferInfo.buffer = pRecord->_buffer;
		bufferInfo.offset = 0;
		bufferInfo.range  = pRecord->_size;

		VkWriteDescriptorSet descriptorWrite{};
		descriptorWrite.sType			= VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptorWrite.dstSet			= descriptorSet;
		descriptorWrite.dstBinding		= 0;
		descriptorWrite.dstArrayElement = 0;
		descriptorWrite.descriptorType	= VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		descriptorWrite.descriptorCount = 1;
		descriptorWrite.pBufferInfo		= &bufferInfo;

		vkUpdateDescriptorSets( _pDevice->_device, 1, &descriptorWrite, 0, nullptr );

		RHIDescriptorIndex descriptorIndex;
		if ( _pDevice->_listUavFree.empty() == false )
		{
			descriptorIndex = _pDevice->_listUavFree.back();
			_pDevice->_listUavFree.pop_back();
		}
		else
			descriptorIndex = static_cast<RHIDescriptorIndex>( _pDevice->_listRegisteredUAV.size() );

		if ( descriptorIndex >= _pDevice->_listRegisteredUAV.size() )
		{
			_pDevice->_listRegisteredUAV.resize( descriptorIndex + 1 );
			_pDevice->_listUavSourceBuffer.resize( descriptorIndex + 1 );
		}
		_pDevice->_listRegisteredUAV[descriptorIndex]	= descriptorSet;
		_pDevice->_listUavSourceBuffer[descriptorIndex] = buffer;
		return descriptorIndex;
	}

	void VulkanRHIResource::unregisterBindlessUAV( RHIDescriptorIndex index )
	{
		if ( index < _pDevice->_listRegisteredUAV.size() )
		{
			VkDescriptorSet set = _pDevice->_listRegisteredUAV[index];
			if ( set != VK_NULL_HANDLE )
			{
				VkDevice		 dev  = _pDevice->_device;
				VkDescriptorPool pool = _pDevice->_descriptorPool;
				_pDevice->_releaseQueue.enqueueRelease( SW_DELEGATE_LAMBDA( RHIResourceReleaseDelegate, [dev, pool, set]()
				{
					vkFreeDescriptorSets( dev, pool, 1, &set );
				} ) );
				_pDevice->_listRegisteredUAV[index] = VK_NULL_HANDLE;
			}
			if ( index < _pDevice->_listUavSourceBuffer.size() )
				_pDevice->_listUavSourceBuffer[index] = 0;
			_pDevice->_listUavFree.push_back( index );
		}
	}
} // namespace sw

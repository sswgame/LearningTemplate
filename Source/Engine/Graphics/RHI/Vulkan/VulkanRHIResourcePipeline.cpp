/**
 * @file VulkanRHIResourcePipeline.cpp
 * @brief Vulkan 의 파이프라인 상태 객체 — PSO, 셰이더 스테이지, 렌더패스 객체
 * @details `VulkanRHIResource` 의 일부다. 리소스(버퍼/텍스처)를 만드는 것과 파이프라인을 만드는 것은
 *          배우는 내용이 다르고 백엔드별 차이도 가장 크게 드러나는 곳이라 따로 둔다.
 */
#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/RHI/Support/FrameResourceRing.h"
#include "Engine/Graphics/RHI/Support/RHIIndexFreeList.h"
#include "Engine/Graphics/RHI/Vulkan/VulkanRHIDevice.h"
#include "Engine/Graphics/RHI/Vulkan/VulkanRHIDeviceInternal.h"
#include "Engine/Graphics/RHI/Vulkan/VulkanRHIResource.h"
#include "Engine/Graphics/Shader/ShaderCache.h"

#include <vulkan/vulkan.h>

namespace sw
{
    namespace
    {
        struct VulkanRHIResourceInternal
        {
            static ShaderCompileResult compileShader( const ShaderCompileDesc& desc )
            {
                if ( engine::areEngineServicesBound() )
                    return engine::getShaderCache().getOrCompile( desc );
                return ShaderCompiler::compileHLSL( desc );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "VulkanRHIResource" );

    RHIPipelineStateHandle VulkanRHIResource::createPipelineState( const RHIPipelineStateDesc& desc )
    {
        ShaderCompileDesc vsDesc{};
        vsDesc._filePath             = desc._vertexShaderPath;
        vsDesc._entryPoint           = desc._vertexEntryPoint;
        vsDesc._stage                = ShaderStage::Vertex;
        vsDesc._targetFormat         = ShaderTargetFormat::SPIRV_Vulkan;
        ShaderCompileResult vsResult = VulkanRHIResourceInternal::compileShader( vsDesc );

        const bool          bDepthOnly      = ( desc._numRenderTargets == 0 && desc._bEnableDepthTest != 0 );
        const bool          bHasPixelShader = desc._pixelShaderPath.empty() == false && bDepthOnly == false;
        ShaderCompileResult psResult{};
        if ( bHasPixelShader )
        {
            ShaderCompileDesc psDesc{};
            psDesc._filePath     = desc._pixelShaderPath;
            psDesc._entryPoint   = desc._pixelEntryPoint;
            psDesc._stage        = ShaderStage::Pixel;
            psDesc._targetFormat = ShaderTargetFormat::SPIRV_Vulkan;
            psResult             = VulkanRHIResourceInternal::compileShader( psDesc );
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
        vsInfo.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        vsInfo.codeSize = vsResult._bytecode.size();
        vsInfo.pCode    = reinterpret_cast<const uint32*>( vsResult._bytecode.data() );
        VkShaderModule vertShaderModule{ VK_NULL_HANDLE };
        vkCreateShaderModule( _pDevice->_device, &vsInfo, nullptr, &vertShaderModule );

        VkShaderModule fragShaderModule{ VK_NULL_HANDLE };
        if ( bHasPixelShader )
        {
            VkShaderModuleCreateInfo psInfo{};
            psInfo.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            psInfo.codeSize = psResult._bytecode.size();
            psInfo.pCode    = reinterpret_cast<const uint32*>( psResult._bytecode.data() );
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
        bindingDescription.binding   = 0;
        bindingDescription.stride    = static_cast<uint32>( sizeof( RHIVertex ) );
        bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        VkVertexInputAttributeDescription arrAttributeDescription[2]{};
        arrAttributeDescription[0].binding  = 0;
        arrAttributeDescription[0].location = 0;
        arrAttributeDescription[0].format   = VK_FORMAT_R32G32B32_SFLOAT;
        arrAttributeDescription[0].offset   = SW_OFFSET_OF( RHIVertex, _arrPosition );
        arrAttributeDescription[1].binding  = 0;
        arrAttributeDescription[1].location = 1;
        arrAttributeDescription[1].format   = VK_FORMAT_R32G32B32A32_SFLOAT;
        arrAttributeDescription[1].offset   = SW_OFFSET_OF( RHIVertex, _arrColor );

        vertexInputInfo.vertexBindingDescriptionCount   = 1;
        vertexInputInfo.pVertexBindingDescriptions      = &bindingDescription;
        vertexInputInfo.vertexAttributeDescriptionCount = 2;
        vertexInputInfo.pVertexAttributeDescriptions    = arrAttributeDescription;

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        inputAssembly.primitiveRestartEnable = VK_FALSE;

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount  = 1;

        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.depthClampEnable        = VK_FALSE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.polygonMode             = ( desc._fillMode == RHIFillMode::Wireframe ) ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth               = 1.0f;
        rasterizer.cullMode                = ( desc._cullMode == RHICullMode::Front ) ? VK_CULL_MODE_FRONT_BIT : ( ( desc._cullMode == RHICullMode::Back ) ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_NONE );
        rasterizer.frontFace               = VK_FRONT_FACE_CLOCKWISE;

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.sampleShadingEnable  = VK_FALSE;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        const uint32 numRT      = bDepthOnly ? 0u : ( ( desc._numRenderTargets > 0 ) ? desc._numRenderTargets : 1u );
        const uint32 blendCount = ( numRT > kMaxColorAttachments ) ? kMaxColorAttachments : numRT;

        VkPipelineColorBlendAttachmentState arrColorBlendAttachment[kMaxColorAttachments]{};
        for ( uint32 blendIndex = 0; blendIndex < blendCount; ++blendIndex )
        {
            arrColorBlendAttachment[blendIndex].colorWriteMask      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                                                      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            arrColorBlendAttachment[blendIndex].blendEnable         = desc._bEnableBlend ? VK_TRUE : VK_FALSE;
            arrColorBlendAttachment[blendIndex].srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            arrColorBlendAttachment[blendIndex].dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            arrColorBlendAttachment[blendIndex].colorBlendOp        = VK_BLEND_OP_ADD;
            arrColorBlendAttachment[blendIndex].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            arrColorBlendAttachment[blendIndex].dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            arrColorBlendAttachment[blendIndex].alphaBlendOp        = VK_BLEND_OP_ADD;
        }

        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.logicOpEnable   = VK_FALSE;
        colorBlending.attachmentCount = blendCount;
        colorBlending.pAttachments    = blendCount > 0 ? arrColorBlendAttachment : nullptr;

        VkRenderPass pipelineRp = _pDevice->ensurePipelineRenderPass( desc );
        if ( pipelineRp == VK_NULL_HANDLE )
            pipelineRp = _pDevice->_renderPass;

        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable       = desc._bEnableDepthTest ? VK_TRUE : VK_FALSE;
        depthStencil.depthWriteEnable      = ( depthStencil.depthTestEnable != VK_FALSE && desc._bEnableDepthWrite ) ? VK_TRUE : VK_FALSE;
        depthStencil.depthCompareOp        = VK_COMPARE_OP_LESS;
        depthStencil.depthBoundsTestEnable = VK_FALSE;
        depthStencil.stencilTestEnable     = VK_FALSE;

        vector<VkDynamicState>           listDynamicState = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = static_cast<uint32>( listDynamicState.size() );
        dynamicState.pDynamicStates    = listDynamicState.data();

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount          = bHasPixelShader ? 2 : 1;
        pipelineInfo.pStages             = arrShaderStage;
        pipelineInfo.pVertexInputState   = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState      = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState   = &multisampling;
        pipelineInfo.pDepthStencilState  = &depthStencil;
        pipelineInfo.pColorBlendState    = &colorBlending;
        pipelineInfo.pDynamicState       = &dynamicState;
        pipelineInfo.layout              = _pDevice->_pipelineLayout;
        pipelineInfo.renderPass          = pipelineRp;
        pipelineInfo.subpass             = 0;
        pipelineInfo.basePipelineHandle  = VK_NULL_HANDLE;

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
        csDesc._filePath             = shaderPath;
        csDesc._entryPoint           = entryPoint;
        csDesc._stage                = ShaderStage::Compute;
        csDesc._targetFormat         = ShaderTargetFormat::SPIRV_Vulkan;
        ShaderCompileResult csResult = VulkanRHIResourceInternal::compileShader( csDesc );

        if ( csResult._bSuccess == false )
        {
            VulkanRHIDevice::VulkanPipelineStateRecord record{};
            return _pDevice->_pipelineStates.insert( std::move( record ) );
        }

        VkShaderModuleCreateInfo csInfo{};
        csInfo.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        csInfo.codeSize = csResult._bytecode.size();
        csInfo.pCode    = reinterpret_cast<const uint32*>( csResult._bytecode.data() );
        // 예전엔 초기화도 하지 않은 핸들에 결과를 받아 검사 없이 썼다 — 생성이 실패하면 쓰레기
        // 값을 파이프라인 생성에 넘기고 vkDestroyShaderModule 까지 불렀다.
        VkShaderModule compShaderModule{ VK_NULL_HANDLE };
        if ( vkCreateShaderModule( _pDevice->_device, &csInfo, nullptr, &compShaderModule ) != VK_SUCCESS ||
             compShaderModule == VK_NULL_HANDLE )
        {
            SW_LOG_ERROR( "createComputePipelineState: vkCreateShaderModule failed" );
            return 0;
        }

        VkPipelineShaderStageCreateInfo compShaderStageInfo{};
        compShaderStageInfo.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        compShaderStageInfo.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
        compShaderStageInfo.module = compShaderModule;
        compShaderStageInfo.pName  = "CSMain";

        VkComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineInfo.layout = _pDevice->_pipelineLayout;
        pipelineInfo.stage  = compShaderStageInfo;

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
            VkDevice   dev  = _pDevice->_device;
            VkPipeline pipe = record._pipeline;
            _pDevice->_releaseQueue.enqueueGpuRelease( SW_DELEGATE_LAMBDA( RHIResourceReleaseDelegate, [dev, pipe]()
            {
                vkDestroyPipeline( dev, pipe, nullptr );
            } ),
                                                       _pDevice->_frameFenceCounter + 1 );
        }
        if ( _pDevice->_recordingState._activeGraphicsPso == pso )
            _pDevice->_recordingState._activeGraphicsPso = 0;
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
        VkAttachmentReference   colorRefs[kMaxColorAttachments]{};
        const uint32            colorCount =
            desc._listColorAttachment.size() > kMaxColorAttachments
                ? kMaxColorAttachments
                : static_cast<uint32>( desc._listColorAttachment.size() );

        for ( uint32 colorIndex = 0; colorIndex < colorCount; ++colorIndex )
        {
            const RHIRenderPassAttachment& att     = desc._listColorAttachment[colorIndex];
            attachments[colorIndex].format         = VulkanRHIDeviceInternal::toVulkanTextureFormat( att._format );
            attachments[colorIndex].samples        = VK_SAMPLE_COUNT_1_BIT;
            attachments[colorIndex].loadOp         = toLoadOp( att._loadOp );
            attachments[colorIndex].storeOp        = toStoreOp( att._storeOp );
            attachments[colorIndex].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            attachments[colorIndex].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            attachments[colorIndex].initialLayout  = ( att._loadOp == RHIRenderPassLoadOp::Clear ) ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            attachments[colorIndex].finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            colorRefs[colorIndex].attachment       = colorIndex;
            colorRefs[colorIndex].layout           = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        }

        VkAttachmentReference depthRef{};
        uint32                attachCount = colorCount;
        if ( desc._bHasDepthStencil != 0 )
        {
            attachments[attachCount].format         = static_cast<VkFormat>( _pDevice->_depthFormat );
            attachments[attachCount].samples        = VK_SAMPLE_COUNT_1_BIT;
            attachments[attachCount].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
            attachments[attachCount].storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
            attachments[attachCount].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            attachments[attachCount].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            attachments[attachCount].initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
            attachments[attachCount].finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            depthRef.attachment                     = attachCount;
            depthRef.layout                         = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            ++attachCount;
        }

        if ( attachCount == 0 )
        {
            record._renderPass = _pDevice->_renderPass;
            record._bOwned     = 0;
            _pDevice->_listRenderPass.push_back( record );
            return _pDevice->_listRenderPass.size();
        }

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount    = colorCount;
        subpass.pColorAttachments       = colorCount > 0 ? colorRefs : nullptr;
        subpass.pDepthStencilAttachment = desc._bHasDepthStencil ? &depthRef : nullptr;

        VkSubpassDependency dependency{};
        dependency.srcSubpass    = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass    = 0;
        dependency.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.srcAccessMask = 0;
        dependency.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo rpInfo{};
        rpInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rpInfo.attachmentCount = attachCount;
        rpInfo.pAttachments    = attachments;
        rpInfo.subpassCount    = 1;
        rpInfo.pSubpasses      = &subpass;
        rpInfo.dependencyCount = 1;
        rpInfo.pDependencies   = &dependency;

        VkRenderPass created = VK_NULL_HANDLE;
        if ( vkCreateRenderPass( _pDevice->_device, &rpInfo, nullptr, &created ) != VK_SUCCESS )
        {
            SW_LOG_ERROR( "createRenderPass(desc) failed" );
            return 0;
        }

        record._renderPass = created;
        record._bOwned     = 1;
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
} // namespace sw

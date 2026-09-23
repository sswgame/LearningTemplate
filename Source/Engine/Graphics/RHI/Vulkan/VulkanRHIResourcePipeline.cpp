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
#include "Engine/Graphics/RHI/Support/RHIShaderRequest.h"
#include "Engine/Graphics/RHI/Vulkan/VulkanRHIDevice.h"
#include "Engine/Graphics/RHI/Vulkan/VulkanRHIDeviceInternal.h"
#include "Engine/Graphics/RHI/Vulkan/VulkanRHIResource.h"
#include "Engine/Graphics/Shader/Compile/ShaderCache.h"

#include <vulkan/vulkan.h>

namespace sw
{
    SW_LOG_CALLER( "VulkanRHIResource" );

    RHIPipelineStateHandle VulkanRHIResource::createPipelineState( const RHIPipelineStateDesc& desc )
    {
        // 서술체 해석(진입점 기본값·define·뎁스 전용 판정·RT 수)은 RHIShaderRequest 하나가 한다 — 백엔드는 받기만 한다.
        // 예전엔 여기서 직접 읽으면서 define 을 아예 안 옮겨, Vulkan 만 SW_FORWARD·머티리얼 퍼뮤테이션·SW_VIEWMODE_UNLIT 을
        // 컴파일러에 넘긴 적이 없었다 — 네 곳에 복사된 규칙은 한 곳만 빠져도 그렇게 조용히 어긋난다.
        const RHIGraphicsShaderRequest request         = RHIShaderRequest::resolveGraphics( desc, ShaderTargetFormat::SPIRV_Vulkan );
        const ShaderCompileDesc&       vsDesc          = request._vertex;
        const ShaderCompileDesc&       psDesc          = request._pixel;
        const bool                     bHasPixelShader = request._bHasPixelShader != SW_FALSE;
        ShaderCompileResult            vsResult{};
        ShaderCompileResult            psResult{};
        if ( RHIShaderRequest::compileGraphics( request, vsResult, psResult ) == false )
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
        // 컴파일에 쓴 진입점과 같은 이름이어야 한다 — 예전엔 "VSMain" 으로 박혀 있어 파이프라인 XML 이 다른 진입점을
        // 쓰는 순간 Vulkan 만 파이프라인 생성에 실패할 자리였다(desc 는 이 함수가 끝날 때까지 살아 있다).
        vertShaderStageInfo.pName = vsDesc._entryPoint.c_str();

        VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
        if ( bHasPixelShader )
        {
            fragShaderStageInfo.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            fragShaderStageInfo.stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
            fragShaderStageInfo.module = fragShaderModule;
            fragShaderStageInfo.pName  = psDesc._entryPoint.c_str();
        }

        VkPipelineShaderStageCreateInfo arrShaderStage[] = { vertShaderStageInfo, fragShaderStageInfo };

        VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
        vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

        // 바인딩 0 = 메시 정점(정점 스텝), 바인딩 1 = 인스턴스 슬롯 스트림(인스턴스 스텝, uint 하나).
        VkVertexInputBindingDescription arrBindingDescription[2]{};
        arrBindingDescription[0].binding   = 0;
        arrBindingDescription[0].stride    = static_cast<uint32>( sizeof( RHIVertex ) );
        arrBindingDescription[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        arrBindingDescription[1].binding   = constant::kInstanceSlotStreamSlot;
        arrBindingDescription[1].stride    = constant::kInstanceSlotStreamStride;
        arrBindingDescription[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

        // 정점 속성은 **공용 표**(constant::arrVertexAttribute)에서 만든다 — DX11·DX12·GL 과 같은 표다.
        VkVertexInputAttributeDescription arrAttributeDescription[constant::kVertexAttributeCount]{};
        for ( uint32 attributeIndex = 0; attributeIndex < constant::kVertexAttributeCount; ++attributeIndex )
        {
            const RHIVertexAttribute& attribute              = constant::arrVertexAttribute[attributeIndex];
            arrAttributeDescription[attributeIndex].binding  = attribute._inputSlot;
            arrAttributeDescription[attributeIndex].location = attribute._location;
            arrAttributeDescription[attributeIndex].format   = ( attribute._bUint != SW_FALSE )   ? VK_FORMAT_R32_UINT
                                                             : ( attribute._componentCount == 4 ) ? VK_FORMAT_R32G32B32A32_SFLOAT
                                                             : ( attribute._componentCount == 2 ) ? VK_FORMAT_R32G32_SFLOAT
                                                                                                  : VK_FORMAT_R32G32B32_SFLOAT;
            arrAttributeDescription[attributeIndex].offset   = attribute._byteOffset;
        }

        vertexInputInfo.vertexBindingDescriptionCount   = 2;
        vertexInputInfo.pVertexBindingDescriptions      = arrBindingDescription;
        vertexInputInfo.vertexAttributeDescriptionCount = constant::kVertexAttributeCount;
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
        // 기능을 못 켠 디바이스에서 LINE 을 요청하면 파이프라인 생성 자체가 거절된다 — 화면이 비는 대신
        // 솔리드로 그린다(요청은 "보기 방식" 이고, 그리지 못하는 것보다 다르게 보이는 편이 낫다).
        const bool bWantWireframe = ( desc._fillMode == RHIFillMode::Wireframe ) && ( _pDevice->_bFillModeNonSolid != 0 );
        rasterizer.polygonMode    = bWantWireframe ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth      = 1.0f;
        rasterizer.cullMode       = ( desc._cullMode == RHICullMode::Front ) ? VK_CULL_MODE_FRONT_BIT : ( ( desc._cullMode == RHICullMode::Back ) ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_NONE );
        rasterizer.frontFace      = VK_FRONT_FACE_CLOCKWISE;

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.sampleShadingEnable  = VK_FALSE;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        const uint32 numRT      = request._numRenderTargets;
        const uint32 blendCount = ( numRT > kMaxColorAttachments ) ? kMaxColorAttachments : numRT;

        VkPipelineColorBlendAttachmentState arrColorBlendAttachment[kMaxColorAttachments]{};
        for ( uint32 blendIndex = 0; blendIndex < blendCount; ++blendIndex )
        {
            arrColorBlendAttachment[blendIndex].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
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
        return _pDevice->_pipelineStates.insert( record );
    }

    RHIPipelineStateHandle VulkanRHIResource::createComputePipelineState( string_view shaderPath, string_view entryPoint )
    {
        ShaderCompileDesc csDesc{};
        csDesc._filePath             = shaderPath;
        csDesc._entryPoint           = entryPoint;
        csDesc._stage                = ShaderStage::Compute;
        csDesc._targetFormat         = ShaderTargetFormat::SPIRV_Vulkan;
        ShaderCompileResult csResult = RHIShaderRequest::compile( csDesc );

        if ( csResult._bSuccess == false )
        {
            VulkanRHIDevice::VulkanPipelineStateRecord record{};
            return _pDevice->_pipelineStates.insert( record );
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
        return _pDevice->_pipelineStates.insert( record );
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

        if ( desc._listColorAttachment.empty() && desc._bHasDepthStencil == SW_FALSE )
        {
            SW_LOG_ERROR( "createRenderPass requires color or depth attachments." );
            return 0;
        }

        auto toLoadOp = []( RHIRenderPassLoadOp loadOp ) -> VkAttachmentLoadOp
        {
            switch ( loadOp )
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
        auto toStoreOp = []( RHIRenderPassStoreOp storeOp ) -> VkAttachmentStoreOp
        {
            switch ( storeOp )
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

        VulkanRHIRenderPassCache::RenderPassSpec spec{};
        const uint32                             colorCount =
            desc._listColorAttachment.size() > kMaxColorAttachments
                                            ? kMaxColorAttachments
                                            : static_cast<uint32>( desc._listColorAttachment.size() );
        for ( uint32 colorIndex = 0; colorIndex < colorCount; ++colorIndex )
        {
            const RHIRenderPassAttachment& att = desc._listColorAttachment[colorIndex];
            spec.addColor( static_cast<uint32>( VulkanRHIDeviceInternal::toVulkanTextureFormat( att._format ) ), toLoadOp( att._loadOp ),
                           ( att._loadOp == RHIRenderPassLoadOp::Clear ) ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                           VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL );
            spec._arrColorStoreOp[colorIndex] = toStoreOp( att._storeOp );
        }
        if ( desc._bHasDepthStencil != SW_FALSE )
            spec.setDepth( _pDevice->_depthFormat, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL );

        if ( spec._colorCount == 0 && spec._depthFormat == 0 )
            return _pDevice->_renderPassCache.addRenderPassRecord( _pDevice->_renderPass, false ); // 스왑체인 RP 별칭 — 소유하지 않는다

        VkRenderPass created = _pDevice->createRenderPassFromSpec( spec );
        if ( created == VK_NULL_HANDLE )
        {
            SW_LOG_ERROR( "createRenderPass(desc) failed" );
            return 0;
        }

        return _pDevice->_renderPassCache.addRenderPassRecord( created, true );
    }

    void VulkanRHIResource::destroyRenderPass( RHIRenderPassHandle pass )
    {
        _pDevice->_renderPassCache.destroyRenderPassRecord( _pDevice->_device, pass, _pDevice->_renderPass );
    }
} // namespace sw

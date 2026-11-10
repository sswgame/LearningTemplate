#include "pch.h"

#include "Engine/Graphics/RHI/WebGPU/WebGPURHIResourceFactory.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Log/LogOnce.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"

#include "Engine/Graphics/RHI/Backend/RHIShaderRequest.h"
#include "Engine/Graphics/RHI/WebGPU/WebGPURHIDevice.h"
#include "Engine/Graphics/Shader/Compile/WebGPUShaderContainer.h"

namespace sw
{
    namespace
    {
        struct WebGPURHIResourceFactoryInternal
        {
            static constexpr uint64 kCopyAlignment        = 4;   ///< 큐 쓰기 · 버퍼 복사의 크기 · 오프셋 정렬(명세)
            static constexpr uint64 kRowPitchAlignment    = 256; ///< 텍스처 → 버퍼 복사의 행 정렬(명세)
            static constexpr uint64 kUniformSizeAlignment = 16;  ///< 상수 버퍼 크기(WGSL uniform 구조체 크기는 16 의 배수)

            /** @brief 블렌드를 섞을 수 있는 첨부 형식인지입니다. 32 비트 실수는 `float32-blendable` 없이 섞지 못한다 — 그 첨부는 덮어쓴다(Vulkan 백엔드와 같은 규칙). */
            static bool isBlendable( WGPUTextureFormat format )
            {
                return format != WGPUTextureFormat_R32Float && format != WGPUTextureFormat_RG32Float && format != WGPUTextureFormat_RGBA32Float;
            }

            static bool isDepthFormat( WGPUTextureFormat format )
            {
                return format == WGPUTextureFormat_Depth24PlusStencil8 || format == WGPUTextureFormat_Depth32FloatStencil8 || format == WGPUTextureFormat_Depth32Float ||
                       format == WGPUTextureFormat_Depth24Plus || format == WGPUTextureFormat_Depth16Unorm;
            }

            /** @brief 블록 압축의 물리 크기(블록 배수로 올린 밉 크기)입니다. 압축이 아니면 그대로입니다. */
            static uint32 toPhysicalExtent( uint32 extent, uint32 blockSize ) { return MathUtil::align( MathUtil::max( extent, 1u ), blockSize ); }

            /** @brief 큐가 지금까지 낸 일이 끝나면 부르는 콜백입니다. userdata1 은 전송 표지입니다. */
            static void onQueueWorkDone( WGPUQueueWorkDoneStatus status, WGPUStringView message, void* pUserdata1, void* pUserdata2 )
            {
                (void)pUserdata2;
                WebGPUTransferProbe* const pProbe = static_cast<WebGPUTransferProbe*>( pUserdata1 );
                if ( status != WGPUQueueWorkDoneStatus_Success )
                {
                    SW_LOG_ERROR_ONCE_ID( "WebGPU.TransferWorkFailed", "WebGPU: a transfer submission did not complete (status %#): %#", static_cast<uint32>( status ), WebGPURHIUtil::fromStringView( message ) );
                    pProbe->_bFailed.store( SW_TRUE );
                }
                pProbe->_bDone.store( SW_TRUE );
            }
        };
    } // namespace

    WebGPURHIResourceFactory::WebGPURHIResourceFactory( WebGPURHIDevice* pDevice )
        : _pDevice{ pDevice }
    {
    }

    // ------------------------------------------------------------------------------
    // 셰이더 · 파이프라인
    // ------------------------------------------------------------------------------
    WGPUShaderModule WebGPURHIResourceFactory::createShaderModule( const vector<uint8>& bytecode, const utf8* pLabel, vector<uint32>& ioListBinding )
    {
        string_view wgsl;
        if ( WebGPUShaderContainer::findWgsl( bytecode, wgsl ) == false )
        {
            SW_LOG_ERROR_ID( "WebGPU.NotShaderContainer", "WebGPU: '%#' is not a WebGPU shader container (cook the shaders with the WGSL_WebGPU target)", pLabel );
            return nullptr;
        }
        vector<uint32> listBinding;
        WebGPUShaderContainer::collectBindings( wgsl, listBinding );
        ioListBinding.insert( ioListBinding.end(), listBinding.begin(), listBinding.end() );
        VectorUtil::sortUnique( ioListBinding );

        WGPUShaderSourceWGSL source{};
        source.chain.sType = WGPUSType_ShaderSourceWGSL;
        source.code        = WebGPURHIUtil::toStringView( wgsl );
        WGPUShaderModuleDescriptor desc{};
        desc.nextInChain = &source.chain;
        desc.label       = WebGPURHIUtil::toStringView( pLabel );
        return wgpuDeviceCreateShaderModule( _pDevice->_device, &desc );
    }

    // 길이 유지(긴 함수 정리 기준): 셰이더 · 정점 입력 · 래스터 · 깊이 · 블렌드 · 생성의 순서가 곧 파이프라인 서술이라 나누지 않는다.
    RHIPipelineStateHandle WebGPURHIResourceFactory::createPipelineStateNative( const RHIPipelineStateDesc& desc )
    {
        using Internal = WebGPURHIResourceFactoryInternal;
        if ( _pDevice->_bInitialized == SW_FALSE )
            return 0;
        const RHIGraphicsShaderRequest request = RHIShaderRequest::resolveGraphics( desc, ShaderTargetFormat::WGSL_WebGPU );
        if ( request._bHasGeometryShader != SW_FALSE || request._bHasTessellation != SW_FALSE )
        {
            // WebGPU 에는 지오메트리 · 테셀레이션 단계가 없다 — 그 패스의 PSO 는 만들지 않는다(장치 기능이 없는 Vulkan 과 같은 규칙).
            SW_LOG_ERROR_ID( "WebGPU.OptionalStageUnsupported", "WebGPU has no geometry or tessellation stage - PSO '%#' is not created", desc._vertexShaderPath.c_str() );
            return 0;
        }
        ShaderCompileResult vsResult{};
        ShaderCompileResult psResult{};
        if ( RHIShaderRequest::compileGraphics( request, vsResult, psResult ) == false )
            return 0;

        WebGPUPipelineRecord   record{};
        const WGPUShaderModule vsModule = createShaderModule( vsResult._bytecode, desc._vertexShaderPath.c_str(), record._listBinding );
        const bool             bPixel   = request._bHasPixelShader != SW_FALSE;
        const WGPUShaderModule psModule = bPixel ? createShaderModule( psResult._bytecode, desc._pixelShaderPath.c_str(), record._listBinding ) : nullptr;
        if ( vsModule == nullptr || ( bPixel && psModule == nullptr ) )
        {
            if ( vsModule != nullptr )
                wgpuShaderModuleRelease( vsModule );
            if ( psModule != nullptr )
                wgpuShaderModuleRelease( psModule );
            return 0;
        }

        // 정점 입력: 셰이더가 읽는 속성만, 슬롯마다 한 레이아웃(슬롯 번호 = 버퍼 자리). 원소 크기는 슬롯이 정한다 — WebGPU 는 보폭이 파이프라인에 박힌다.
        const uint32              attributeMask = RHIShaderRequest::computeConsumedVertexAttributeMask( vsResult._bytecode, ShaderTargetFormat::WGSL_WebGPU );
        const RHIVertexAttribute* pAttribute    = constant::render::getMeshVertexAttributes( desc._meshVertexFormat );
        WGPUVertexAttribute       arrAttribute[constant::render::kVertexBufferSlotCount][constant::render::kVertexAttributeCount]{};
        uint32                    arrAttributeCount[constant::render::kVertexBufferSlotCount]{};
        WGPUVertexBufferLayout    arrBufferLayout[constant::render::kVertexBufferSlotCount]{};
        uint32                    bufferLayoutCount = 0;
        for ( uint32 attributeIndex = 0; attributeIndex < constant::render::kVertexAttributeCount; ++attributeIndex )
        {
            if ( ( attributeMask & ( 1u << attributeIndex ) ) == 0 )
                continue;
            const RHIVertexAttribute& source = pAttribute[attributeIndex];
            WGPUVertexAttribute&      target = arrAttribute[source._inputSlot][arrAttributeCount[source._inputSlot]++];
            target.format                    = WebGPURHIUtil::toVertexFormat( source._format );
            target.offset                    = source._byteOffset;
            target.shaderLocation            = source._location;
        }
        for ( uint32 slot = 0; slot < constant::render::kVertexBufferSlotCount; ++slot )
        {
            WGPUVertexBufferLayout& layout = arrBufferLayout[slot];
            if ( arrAttributeCount[slot] == 0 )
            {
                layout.stepMode = WGPUVertexStepMode_Undefined; // 안 쓰는 자리 = Undefined + 속성 0 개(webgpu-headers #439 — 옛 VertexBufferNotUsed). 뒤 슬롯이 쓰면 앞 자리를 비워 둔다
                continue;
            }
            const bool bInstance  = pAttribute[0]._inputSlot == slot ? false : constant::render::kInstanceSlotStreamSlot == slot;
            layout.stepMode       = bInstance ? WGPUVertexStepMode_Instance : WGPUVertexStepMode_Vertex;
            layout.arrayStride    = constant::render::getVertexSlotStride( desc._meshVertexFormat, slot );
            layout.attributeCount = arrAttributeCount[slot];
            layout.attributes     = arrAttribute[slot];
            bufferLayoutCount     = slot + 1;
            record._vertexLayout._slotMask |= 1u << slot;
        }
        record._vertexLayout._format = desc._meshVertexFormat;

        const RHIRenderState&        state = desc._renderState;
        WGPURenderPipelineDescriptor pipelineDesc{};
        pipelineDesc.label               = WebGPURHIUtil::toStringView( desc._vertexShaderPath.c_str() );
        pipelineDesc.layout              = nullptr; // 자동 레이아웃 — 셰이더가 쓰는 바인딩만 그룹 0 에 든다(`WebGPUShaderContainer::collectBindings`)
        pipelineDesc.vertex.module       = vsModule;
        pipelineDesc.vertex.entryPoint   = WebGPURHIUtil::toStringView( request._vertex._entryPoint.c_str() );
        pipelineDesc.vertex.bufferCount  = bufferLayoutCount;
        pipelineDesc.vertex.buffers      = bufferLayoutCount > 0 ? arrBufferLayout : nullptr;
        pipelineDesc.primitive.topology  = WebGPURHIUtil::toTopology( desc._topology );
        pipelineDesc.primitive.cullMode  = WebGPURHIUtil::toCullMode( state._cullMode );
        pipelineDesc.primitive.frontFace = WebGPURHIUtil::toFrontFace( state._frontFace );
        // 깊이 클립을 끄는 상태는 `depth-clip-control` 기능이 있어야 한다 — 없으면 클립한 채 그린다(그림자 팬케이크가 가까운 평면에서 잘린다, 로그 한 번).
        pipelineDesc.primitive.unclippedDepth = state._bEnableDepthClip == SW_FALSE && _pDevice->_bDepthClipControl != SW_FALSE;
        if ( state._bEnableDepthClip == SW_FALSE && _pDevice->_bDepthClipControl == SW_FALSE )
            SW_LOG_WARNING_ONCE_ID( "WebGPU.DepthClipControlMissing", "WebGPU: depth-clip-control is missing - PSOs that disable depth clip draw clipped" );
        pipelineDesc.multisample.count = 1;
        pipelineDesc.multisample.mask  = MathUtil::kMaxUInt32;

        WGPUDepthStencilState depthStencil{};
        const bool            bDepth = state._bEnableDepthTest != SW_FALSE && desc._depthStencilFormat != RHIFormat::Unknown;
        if ( bDepth )
        {
            depthStencil.format              = _pDevice->toTextureFormat( desc._depthStencilFormat );
            depthStencil.depthWriteEnabled   = state._bEnableDepthWrite != SW_FALSE ? WGPUOptionalBool_True : WGPUOptionalBool_False;
            depthStencil.depthCompare        = WebGPURHIUtil::toCompareFunction( state._depthCompare );
            depthStencil.depthBias           = state._depthBias;
            depthStencil.depthBiasSlopeScale = state._slopeScaledDepthBias;
            const auto makeFace              = [&state]( RHIStencilOp fail, RHIStencilOp depthFail, RHIStencilOp pass, RHICompareFunc compare )
            {
                WGPUStencilFaceState face{};
                const bool           bStencil = state._bEnableStencil != SW_FALSE;
                face.compare                  = bStencil ? WebGPURHIUtil::toCompareFunction( compare ) : WGPUCompareFunction_Always;
                face.failOp                   = bStencil ? WebGPURHIUtil::toStencilOperation( fail ) : WGPUStencilOperation_Keep;
                face.depthFailOp              = bStencil ? WebGPURHIUtil::toStencilOperation( depthFail ) : WGPUStencilOperation_Keep;
                face.passOp                   = bStencil ? WebGPURHIUtil::toStencilOperation( pass ) : WGPUStencilOperation_Keep;
                return face;
            };
            depthStencil.stencilFront     = makeFace( state._stencilFrontFail, state._stencilFrontDepthFail, state._stencilFrontPass, state._stencilFrontCompare );
            depthStencil.stencilBack      = makeFace( state._stencilBackFail, state._stencilBackDepthFail, state._stencilBackPass, state._stencilBackCompare );
            depthStencil.stencilReadMask  = state._bEnableStencil != SW_FALSE ? state._stencilReadMask : 0u;
            depthStencil.stencilWriteMask = state._bEnableStencil != SW_FALSE ? state._stencilWriteMask : 0u;
            pipelineDesc.depthStencil     = &depthStencil;
        }

        WGPUColorTargetState arrTarget[kMaxColorAttachments]{};
        WGPUBlendState       arrBlend[kMaxColorAttachments]{};
        WGPUFragmentState    fragment{};
        if ( bPixel )
        {
            const uint32 targetCount = MathUtil::min( request._numRenderTargets, kMaxColorAttachments );
            for ( uint32 targetIndex = 0; targetIndex < targetCount; ++targetIndex )
            {
                const RHIBlendTargetState& blend  = state._arrBlendTarget[targetIndex];
                WGPUColorTargetState&      target = arrTarget[targetIndex];
                target.format                     = _pDevice->toTextureFormat( desc._arrRTVFormat[targetIndex] );
                target.writeMask                  = static_cast<WGPUColorWriteMask>( blend._colorWriteMask ); // 비트가 같다(R 1 · G 2 · B 4 · A 8)
                if ( blend._bEnableBlend != SW_FALSE && Internal::isBlendable( target.format ) )
                {
                    arrBlend[targetIndex].color = WGPUBlendComponent{ WebGPURHIUtil::toBlendOperation( blend._colorOp ), WebGPURHIUtil::toBlendFactor( blend._srcColor ),
                                                                      WebGPURHIUtil::toBlendFactor( blend._dstColor ) };
                    arrBlend[targetIndex].alpha = WGPUBlendComponent{ WebGPURHIUtil::toBlendOperation( blend._alphaOp ), WebGPURHIUtil::toBlendFactor( blend._srcAlpha ),
                                                                      WebGPURHIUtil::toBlendFactor( blend._dstAlpha ) };
                    target.blend                = &arrBlend[targetIndex];
                }
            }
            fragment.module       = psModule;
            fragment.entryPoint   = WebGPURHIUtil::toStringView( request._pixel._entryPoint.c_str() );
            fragment.targetCount  = targetCount;
            fragment.targets      = arrTarget;
            pipelineDesc.fragment = &fragment;
        }

        record._renderPipeline = wgpuDeviceCreateRenderPipeline( _pDevice->_device, &pipelineDesc );
        wgpuShaderModuleRelease( vsModule );
        if ( psModule != nullptr )
            wgpuShaderModuleRelease( psModule );
        if ( record._renderPipeline == nullptr )
            return 0;
        if ( record._listBinding.empty() == false )
            record._groupLayout = wgpuRenderPipelineGetBindGroupLayout( record._renderPipeline, 0 );
        record._stencilRef = state._stencilRef;
        return _pDevice->_pipelines.insert( std::move( record ) );
    }

    RHIPipelineStateHandle WebGPURHIResourceFactory::createComputePipelineStateNative( string_view shaderPath, string_view entryPoint, const vector<string>& listDefine )
    {
        if ( _pDevice->_bInitialized == SW_FALSE )
            return 0;
        const ShaderCompileDesc   csDesc   = RHIShaderRequest::resolveCompute( shaderPath, entryPoint, listDefine, ShaderTargetFormat::WGSL_WebGPU );
        const ShaderCompileResult csResult = RHIShaderRequest::compile( csDesc );
        if ( csResult._bSuccess == false )
            return 0;
        WebGPUPipelineRecord   record{};
        const string           label    = string( shaderPath );
        const WGPUShaderModule csModule = createShaderModule( csResult._bytecode, label.c_str(), record._listBinding );
        if ( csModule == nullptr )
            return 0;
        WGPUComputePipelineDescriptor desc{};
        desc.label              = WebGPURHIUtil::toStringView( label.c_str() );
        desc.layout             = nullptr;
        desc.compute.module     = csModule;
        desc.compute.entryPoint = WebGPURHIUtil::toStringView( csDesc._entryPoint.c_str() );
        record._computePipeline = wgpuDeviceCreateComputePipeline( _pDevice->_device, &desc );
        wgpuShaderModuleRelease( csModule );
        if ( record._computePipeline == nullptr )
            return 0;
        if ( record._listBinding.empty() == false )
            record._groupLayout = wgpuComputePipelineGetBindGroupLayout( record._computePipeline, 0 );
        return _pDevice->_pipelines.insert( std::move( record ) );
    }

    void WebGPURHIResourceFactory::destroyPipelineState( RHIPipelineStateHandle pso )
    {
        WebGPUPipelineRecord record{};
        if ( _pDevice->_pipelines.take( pso, record ) == false )
            return;
        WGPURenderPipeline  renderPipeline  = record._renderPipeline;
        WGPUComputePipeline computePipeline = record._computePipeline;
        WGPUBindGroupLayout layout          = record._groupLayout;
        _pDevice->enqueueGPURelease( SW_DELEGATE_LAMBDA( RHIResourceReleaseDelegate, [renderPipeline, computePipeline, layout]()
        {
            if ( layout != nullptr )
                wgpuBindGroupLayoutRelease( layout );
            if ( renderPipeline != nullptr )
                wgpuRenderPipelineRelease( renderPipeline );
            if ( computePipeline != nullptr )
                wgpuComputePipelineRelease( computePipeline );
        } ) );
    }

    RHIRenderPassHandle WebGPURHIResourceFactory::createRenderPass( const RHIRenderPassDesc& desc )
    {
        // WebGPU 에는 패스 객체가 없다 — 패스는 `beginRenderPass` 가 그때의 첨부로 연다. 서술만 들고 핸들을 준다(다른 백엔드와 같은 수명).
        return _pDevice->_renderPasses.insert( desc );
    }

    void WebGPURHIResourceFactory::destroyRenderPass( RHIRenderPassHandle pass )
    {
        _pDevice->_renderPasses.erase( pass );
    }

    // ------------------------------------------------------------------------------
    // 버퍼
    // ------------------------------------------------------------------------------
    RHIBufferHandle WebGPURHIResourceFactory::createBufferWithUsage( uint64 sizeBytes, uint32 elementSize, WGPUBufferUsage usage, const void* pData, const utf8* pLabel )
    {
        using Internal = WebGPURHIResourceFactoryInternal;
        _pDevice->requireRegistryMutableNow( "WebGPU.createBuffer" );
        const uint64         alignedSize = MathUtil::align( MathUtil::max<uint64>( sizeBytes, Internal::kCopyAlignment ), Internal::kCopyAlignment );
        WGPUBufferDescriptor desc{};
        desc.label              = WebGPURHIUtil::toStringView( pLabel );
        desc.usage              = usage;
        desc.size               = alignedSize;
        const WGPUBuffer buffer = wgpuDeviceCreateBuffer( _pDevice->_device, &desc );
        if ( buffer == nullptr )
        {
            SW_LOG_ERROR_ID( "WebGPU.CreateBufferFailed", "WebGPU: wgpuDeviceCreateBuffer failed (%# bytes, %#)", alignedSize, pLabel );
            return 0;
        }
        if ( pData != nullptr && sizeBytes > 0 )
        {
            // 큐 쓰기 크기는 4 의 배수다 — 끝을 0 으로 채운 사본으로 올린다(원본 뒤를 읽지 않는다).
            vector<uint8> padded( static_cast<size_t>( alignedSize ), 0 );
            Memory::copy( padded.data(), pData, static_cast<size_t>( sizeBytes ) );
            wgpuQueueWriteBuffer( _pDevice->_queue, buffer, 0, padded.data(), padded.size() );
        }
        WebGPUBufferRecord record{};
        record._buffer      = buffer;
        record._sizeBytes   = alignedSize;
        record._elementSize = elementSize;
        record._usage       = usage;
        return _pDevice->_buffers.insert( std::move( record ) );
    }

    RHIBufferHandle WebGPURHIResourceFactory::createConstantBuffer( uint32 size )
    {
        const uint64 aligned = MathUtil::align( static_cast<uint64>( MathUtil::max( size, 1u ) ), WebGPURHIResourceFactoryInternal::kUniformSizeAlignment );
        return createBufferWithUsage( aligned, 0, WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst, nullptr, "SW Constant Buffer" );
    }

    void WebGPURHIResourceFactory::updateConstantBuffer( RHIBufferHandle buffer, const void* pData, uint32 size )
    {
        WebGPUBufferRecord* const pRecord = _pDevice->findBuffer( buffer );
        if ( pRecord == nullptr || pData == nullptr || size == 0 )
            return;
        _pDevice->requireNoOpenRenderPassForUpload( "WebGPU.updateConstantBuffer" );
        // 프레임 밖 갱신은 버퍼 자체에 쓴다 — 기록 중의 이름 바꾼 칸은 이제 낡았다(다음 그리기는 버퍼를 읽는다).
        const uint64  alignedSize = MathUtil::align( static_cast<uint64>( size ), WebGPURHIResourceFactoryInternal::kCopyAlignment );
        vector<uint8> padded( static_cast<size_t>( MathUtil::min( alignedSize, pRecord->_sizeBytes ) ), 0 );
        Memory::copy( padded.data(), pData, MathUtil::min<size_t>( size, padded.size() ) );
        wgpuQueueWriteBuffer( _pDevice->_queue, pRecord->_buffer, 0, padded.data(), padded.size() );
        pRecord->_overrideSerial = 0;
    }

    RHIBufferHandle WebGPURHIResourceFactory::createStructuredBuffer( uint32 elementSize, uint32 elementCount )
    {
        if ( elementSize == 0 || elementCount == 0 )
            return 0;
        // 컬링 컴퓨트가 간접 인자를 구조 버퍼로 쓰고 그 버퍼를 간접 그리기로 읽는다 — 한 버퍼에 Storage | Indirect 를 같이 준다(WebGPU 는 허용한다).
        const WGPUBufferUsage usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc | WGPUBufferUsage_Indirect;
        return createBufferWithUsage( static_cast<uint64>( elementSize ) * elementCount, elementSize, usage, nullptr, "SW Structured Buffer" );
    }

    void WebGPURHIResourceFactory::writeBufferRegions( RHIBufferHandle buffer, const void* pBaseSource, const RHIBufferCopyRegion* pRegions, uint32 regionCount )
    {
        const WebGPUBufferRecord* const pRecord = _pDevice->findBuffer( buffer );
        if ( pRecord == nullptr || pBaseSource == nullptr || pRegions == nullptr )
            return;
        const uint8* const pBase = static_cast<const uint8*>( pBaseSource );
        for ( uint32 regionIndex = 0; regionIndex < regionCount; ++regionIndex )
        {
            const RHIBufferCopyRegion& region   = pRegions[regionIndex];
            const bool                 bAligned = region._dstOffset % WebGPURHIResourceFactoryInternal::kCopyAlignment == 0 && region._sizeBytes % WebGPURHIResourceFactoryInternal::kCopyAlignment == 0;
            const bool                 bInside  = static_cast<uint64>( region._dstOffset ) + region._sizeBytes <= pRecord->_sizeBytes;
            if ( bAligned == false || bInside == false )
            {
                // 큐 쓰기는 4 바이트 정렬이어야 한다(명세) — 정렬 밖 구간을 앞뒤로 넓히면 GPU 쪽의 이웃 바이트를 덮는다. 엔진의 GPU 데이터는 4 바이트 단위라 여기 닿으면 부르는 쪽의 결함이다.
                SW_LOG_ERROR_ID( "WebGPU.UnalignedBufferRegion", "WebGPU: buffer region (offset %#, %# bytes) is not 4-byte aligned or exceeds %# bytes - dropped", region._dstOffset,
                                 region._sizeBytes, pRecord->_sizeBytes );
                continue;
            }
            if ( region._sizeBytes > 0 )
                wgpuQueueWriteBuffer( _pDevice->_queue, pRecord->_buffer, region._dstOffset, pBase + region._srcOffset, region._sizeBytes );
        }
    }

    void WebGPURHIResourceFactory::updateStructuredBufferRegions( RHIBufferHandle buffer, const void* pBaseSource, const RHIBufferCopyRegion* pRegions, uint32 regionCount )
    {
        _pDevice->requireNoOpenRenderPassForUpload( "WebGPU.updateStructuredBufferRegions" );
        writeBufferRegions( buffer, pBaseSource, pRegions, regionCount );
    }

    RHIBufferHandle WebGPURHIResourceFactory::createVertexBuffer( const void* pData, uint32 sizeBytes )
    {
        // 메시 풀은 정점을 저장 버퍼로도 읽는다(모프 · 스킨 컴퓨트) — Storage 를 함께 준다.
        return createBufferWithUsage( sizeBytes, 0, WGPUBufferUsage_Vertex | WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst, pData, "SW Vertex Buffer" );
    }

    void WebGPURHIResourceFactory::updateGeometryBufferRegions( RHIBufferHandle buffer, const void* pBaseSource, const RHIBufferCopyRegion* pRegions, uint32 regionCount )
    {
        _pDevice->requireNoOpenRenderPassForUpload( "WebGPU.updateGeometryBufferRegions" );
        writeBufferRegions( buffer, pBaseSource, pRegions, regionCount );
    }

    RHIBufferHandle WebGPURHIResourceFactory::createIndexBuffer( const void* pData, uint32 sizeBytes )
    {
        return createBufferWithUsage( sizeBytes, 0, WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst, pData, "SW Index Buffer" );
    }

    void WebGPURHIResourceFactory::destroyBuffer( RHIBufferHandle buffer )
    {
        _pDevice->requireRegistryMutableNow( "WebGPU.destroyBuffer" );
        WebGPUBufferRecord record{};
        if ( _pDevice->_buffers.take( buffer, record ) == false )
            return;
        {
            // 이 버퍼의 번호를 모두 놓는다(다섯 백엔드 같은 계약).
            const WebGPUBindlessEntry entry{ buffer, SW_FALSE };
            UniqueLock<shared_mutex>  lock{ _pDevice->_bindlessMutex };
            const auto                isThis = [entry]( const WebGPUBindlessEntry& other )
            { return other == entry; };
            const auto ignore = []( RHIDescriptorIndex ) {};
            _pDevice->_bufferTable.releaseIf( RHIBindlessReuse::Immediately, isThis, ignore );
            _pDevice->_uavTable.releaseIf( RHIBindlessReuse::Immediately, isThis, ignore );
        }
        const WGPUBuffer native = record._buffer;
        _pDevice->enqueueGPURelease( SW_DELEGATE_LAMBDA( RHIResourceReleaseDelegate, [native]()
        {
            wgpuBufferDestroy( native );
            wgpuBufferRelease( native );
        } ) );
    }

    RHIBufferHandle WebGPURHIResourceFactory::createReadbackBuffer( uint32 elementSize, uint32 elementCount )
    {
        const RHIBufferHandle     handle  = createBufferWithUsage( static_cast<uint64>( elementSize ) * elementCount, elementSize, WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst, nullptr,
                                                                   "SW Readback Buffer" );
        WebGPUBufferRecord* const pRecord = _pDevice->findBuffer( handle );
        if ( pRecord != nullptr )
            pRecord->_bReadback = SW_TRUE;
        return handle;
    }

    bool WebGPURHIResourceFactory::readReadbackBuffer( RHIBufferHandle readback, uint32 sizeBytes, void* pDestination )
    {
        const WebGPUBufferRecord* const pRecord = _pDevice->findBuffer( readback );
        if ( pRecord == nullptr || pRecord->_bReadback == SW_FALSE || pDestination == nullptr )
        {
            SW_LOG_ERROR_ID( "WebGPU.ReadNotReadbackBuffer", "WebGPU: readReadbackBuffer got a handle that is not a readback buffer (%#)", readback );
            return false;
        }
        if ( sizeBytes > pRecord->_sizeBytes )
        {
            SW_LOG_ERROR_ID( "WebGPU.ReadbackTooLarge", "WebGPU: readReadbackBuffer of %# bytes exceeds the %# byte buffer", sizeBytes, pRecord->_sizeBytes );
            return false;
        }
        if ( pRecord->_bReadbackReady == SW_FALSE )
        {
            // 매핑이 끝나기 전이다 — 복사한 프레임의 완료 콜백 안에서만 부른다는 계약을 어겼다(프레임 완료는 그 프레임의 매핑까지 기다린다).
            SW_LOG_ERROR_ID( "WebGPU.ReadbackNotMapped", "WebGPU: readReadbackBuffer before the copy frame's mapping finished (%#)", readback );
            return false;
        }
        const void* const pMapped = wgpuBufferGetConstMappedRange( pRecord->_buffer, 0, pRecord->_sizeBytes );
        if ( pMapped == nullptr )
            return false;
        Memory::copy( pDestination, pMapped, sizeBytes );
        return true;
    }

    // ------------------------------------------------------------------------------
    // 텍스처
    // ------------------------------------------------------------------------------
    // 길이 유지(긴 함수 정리 기준): 형식 · 용도 · 생성 · 샘플 뷰 · 면 뷰의 순서가 곧 의미라 나누지 않는다.
    RHITextureHandle WebGPURHIResourceFactory::createTexture2D( const RHITextureDesc& desc )
    {
        using Internal = WebGPURHIResourceFactoryInternal;
        _pDevice->requireRegistryMutableNow( "WebGPU.createTexture2D" );
        const WGPUTextureFormat format = _pDevice->toTextureFormat( desc._format );
        if ( format == WGPUTextureFormat_Undefined || desc._width == 0 || desc._height == 0 )
        {
            SW_LOG_ERROR_ID( "WebGPU.TextureFormatUnsupported", "WebGPU: cannot create a %#x%# texture of RHIFormat %# on this device", desc._width, desc._height, static_cast<uint32>( desc._format ) );
            return 0;
        }
        const bool       bDepth    = Internal::isDepthFormat( format );
        const uint32     layers    = MathUtil::max( desc._arraySize, 1u );
        const uint32     mipLevels = MathUtil::max( desc._mipLevels, 1u );
        WGPUTextureUsage usage     = WGPUTextureUsage_CopyDst | WGPUTextureUsage_CopySrc;
        if ( desc._bIsShaderResource != SW_FALSE )
            usage |= WGPUTextureUsage_TextureBinding;
        if ( desc._bIsRenderTarget != SW_FALSE || desc._bIsDepthStencil != SW_FALSE )
            usage |= WGPUTextureUsage_RenderAttachment;
        if ( desc._bIsUnorderedAccess != SW_FALSE )
            usage |= WGPUTextureUsage_StorageBinding;

        WGPUTextureDescriptor textureDesc{};
        textureDesc.label         = WebGPURHIUtil::toStringView( "SW Texture" );
        textureDesc.usage         = usage;
        textureDesc.dimension     = WGPUTextureDimension_2D;
        textureDesc.size          = WGPUExtent3D{ desc._width, desc._height, layers };
        textureDesc.format        = format;
        textureDesc.mipLevelCount = mipLevels;
        textureDesc.sampleCount   = 1;
        WebGPUTextureRecord record{};
        record._texture = wgpuDeviceCreateTexture( _pDevice->_device, &textureDesc );
        if ( record._texture == nullptr )
            return 0;
        record._desc           = desc;
        record._wgpuFormat     = format;
        record._pUploadSerials = make_unique<RHITextureUploadSerials>();

        WGPUTextureViewDescriptor sampleDesc{};
        sampleDesc.format          = format;
        sampleDesc.dimension       = desc._dimension == RHITextureDimension::TextureCube ? WGPUTextureViewDimension_Cube
                                                                                         : ( desc._dimension == RHITextureDimension::Texture2DArray ? WGPUTextureViewDimension_2DArray : WGPUTextureViewDimension_2D );
        sampleDesc.baseMipLevel    = 0;
        sampleDesc.mipLevelCount   = mipLevels;
        sampleDesc.baseArrayLayer  = 0;
        sampleDesc.arrayLayerCount = layers;
        sampleDesc.aspect          = bDepth ? WGPUTextureAspect_DepthOnly : WGPUTextureAspect_All; // 깊이를 샘플할 때는 깊이 측면만(스텐실과 함께 뷰할 수 없다)
        record._sampleView         = wgpuTextureCreateView( record._texture, &sampleDesc );
        for ( uint32 layer = 0; layer < layers; ++layer )
        {
            WGPUTextureViewDescriptor sliceDesc{};
            sliceDesc.format          = format;
            sliceDesc.dimension       = WGPUTextureViewDimension_2D;
            sliceDesc.baseMipLevel    = 0;
            sliceDesc.mipLevelCount   = 1;
            sliceDesc.baseArrayLayer  = layer;
            sliceDesc.arrayLayerCount = 1;
            sliceDesc.aspect          = WGPUTextureAspect_All;
            record._listSliceView.push_back( wgpuTextureCreateView( record._texture, &sliceDesc ) );
        }
        // 셰이더가 읽는 깊이는 R32 실수 사본으로 읽는다 — WGSL 은 깊이 텍스처를 `texture_depth_*` 로만 받는데 엔진 셰이더는 실수 텍스처로 읽는다(`createDepthSampleCopy`).
        if ( bDepth && desc._bIsShaderResource != SW_FALSE && _pDevice->createDepthSampleCopy( record ) == false )
        {
            SW_LOG_ERROR_ID( "WebGPU.DepthSampleCopyFailed", "WebGPU: the R32 float copy of a %#x%# depth texture could not be created", desc._width, desc._height );
            WebGPUTextureRecord failed = std::move( record );
            _pDevice->releaseDepthSampleCopy( failed );
            for ( const WGPUTextureView view : failed._listSliceView )
            {
                if ( view != nullptr )
                    wgpuTextureViewRelease( view );
            }
            if ( failed._sampleView != nullptr )
                wgpuTextureViewRelease( failed._sampleView );
            wgpuTextureDestroy( failed._texture );
            wgpuTextureRelease( failed._texture );
            return 0;
        }
        return _pDevice->_textures.insert( std::move( record ) );
    }

    void WebGPURHIResourceFactory::destroyTexture( RHITextureHandle texture )
    {
        _pDevice->requireRegistryMutableNow( "WebGPU.destroyTexture" );
        WebGPUTextureRecord record{};
        if ( _pDevice->_textures.take( texture, record ) == false )
            return;
        // 그 텍스처의 번호를 모두 놓는다(다섯 백엔드 같은 계약 — `registerBindlessTexture`).
        {
            const WebGPUBindlessEntry entry{ texture, SW_TRUE };
            UniqueLock<shared_mutex>  lock{ _pDevice->_bindlessMutex };
            const auto                isThis = [entry]( const WebGPUBindlessEntry& other )
            { return other == entry; };
            const auto ignore = []( RHIDescriptorIndex ) {};
            _pDevice->_textureTable.releaseIf( RHIBindlessReuse::Immediately, isThis, ignore );
            _pDevice->_uavTable.releaseIf( RHIBindlessReuse::Immediately, isThis, ignore );
        }
        _pDevice->releaseDepthSampleCopy( record );
        const WGPUTexture       native     = record._texture;
        const WGPUTextureView   sampleView = record._sampleView;
        vector<WGPUTextureView> listSlice  = std::move( record._listSliceView );
        _pDevice->enqueueGPURelease( SW_DELEGATE_LAMBDA( RHIResourceReleaseDelegate, [native, sampleView, listSlice]()
        {
            for ( const WGPUTextureView view : listSlice )
            {
                wgpuTextureViewRelease( view );
            }
            if ( sampleView != nullptr )
                wgpuTextureViewRelease( sampleView );
            wgpuTextureDestroy( native );
            wgpuTextureRelease( native );
        } ) );
    }

    RHIFormat WebGPURHIResourceFactory::getTextureFormat( RHITextureHandle texture ) const
    {
        const WebGPUTextureRecord* const pRecord = _pDevice->findTexture( texture );
        return pRecord != nullptr ? pRecord->_desc._format : RHIFormat::Unknown;
    }

    // ------------------------------------------------------------------------------
    // 전송
    // ------------------------------------------------------------------------------
    uint64 WebGPURHIResourceFactory::enqueueQueueFence()
    {
        unique_ptr<WebGPUTransferProbe> probe  = make_unique<WebGPUTransferProbe>();
        WebGPUTransferProbe* const      pProbe = probe.get();
        const uint64                    serial = _pDevice->_transferQueue.enqueue( std::move( probe ) );
        WGPUQueueWorkDoneCallbackInfo   info{};
        info.mode      = WGPUCallbackMode_AllowSpontaneous;
        info.callback  = &WebGPURHIResourceFactoryInternal::onQueueWorkDone;
        info.userdata1 = pProbe;
        (void)wgpuQueueOnSubmittedWorkDone( _pDevice->_queue, info );
        return serial;
    }

    RHITransferSubmit WebGPURHIResourceFactory::submitTextureUpload( RHITextureHandle texture, const RHITextureUploadDesc& desc )
    {
        WebGPUTextureRecord* const pRecord = _pDevice->findTexture( texture );
        if ( pRecord == nullptr || _pDevice->_bDeviceLost != SW_FALSE )
            return RHITransferSubmit::rejected();
        const RHITextureDesc& textureDesc = pRecord->_desc;
        RHITextureMipSpan     arrSpan[constant::render::kMaxTextureMipCount]{};
        const uint32          spanCount = resolveTextureUploadMips( desc, textureDesc._format, textureDesc._width, textureDesc._height, MathUtil::max( textureDesc._mipLevels, 1u ),
                                                                    arrSpan, constant::render::kMaxTextureMipCount );
        if ( spanCount == 0 || desc._arraySlice >= MathUtil::max( textureDesc._arraySize, 1u ) )
        {
            SW_LOG_ERROR_ID( "WebGPU.UploadRejected", "WebGPU: submitTextureUpload rejected (format %#, %# bytes, slice %#)", static_cast<uint32>( textureDesc._format ), desc._sizeBytes, desc._arraySlice );
            return RHITransferSubmit::rejected();
        }
        const RHIFormatBlockInfo block = getRHIFormatBlockInfo( textureDesc._format );
        for ( uint32 spanIndex = 0; spanIndex < spanCount; ++spanIndex )
        {
            const RHITextureMipSpan& span = arrSpan[spanIndex];
            WGPUTexelCopyTextureInfo target{};
            target.texture  = pRecord->_texture;
            target.mipLevel = span._mip;
            target.origin   = WGPUOrigin3D{ 0, 0, desc._arraySlice };
            target.aspect   = WGPUTextureAspect_All;
            WGPUTexelCopyBufferLayout layout{};
            layout.bytesPerRow  = span._rowBytes;
            layout.rowsPerImage = span._sizeBytes / MathUtil::max( span._rowBytes, 1u );
            const WGPUExtent3D extent{ WebGPURHIResourceFactoryInternal::toPhysicalExtent( span._width, block._blockWidth ),
                                       WebGPURHIResourceFactoryInternal::toPhysicalExtent( span._height, block._blockHeight ), 1 };
            // 큐 쓰기는 데이터를 그 자리에서 복사한다(전송 계약 1) — 행 정렬(256)은 버퍼 복사의 규칙이라 여기에는 없다.
            wgpuQueueWriteTexture( _pDevice->_queue, &target, span._pData, span._sizeBytes, &layout, &extent );
        }
        const uint64 serial = enqueueQueueFence();
        pRecord->_pUploadSerials->recordWrite( serial );
        return _pDevice->_transferSerials.makeAccepted( serial );
    }

    RHITransferSubmit WebGPURHIResourceFactory::submitTextureRegionUpload( RHITextureHandle texture, const RHITextureRegionUploadDesc& desc )
    {
        WebGPUTextureRecord* const pRecord = _pDevice->findTexture( texture );
        if ( pRecord == nullptr || _pDevice->_bDeviceLost != SW_FALSE )
            return RHITransferSubmit::rejected();
        const RHITextureDesc& textureDesc = pRecord->_desc;
        uint32                rowBytes    = 0;
        if ( validateTextureRegionUpload( textureDesc._format, textureDesc._width, textureDesc._height, MathUtil::max( textureDesc._mipLevels, 1u ), MathUtil::max( textureDesc._arraySize, 1u ),
                                          desc, rowBytes ) == false )
            return RHITransferSubmit::rejected();
        WGPUTexelCopyTextureInfo target{};
        target.texture  = pRecord->_texture;
        target.mipLevel = desc._mip;
        target.origin   = WGPUOrigin3D{ desc._x, desc._y, desc._arraySlice };
        target.aspect   = WGPUTextureAspect_All;
        WGPUTexelCopyBufferLayout layout{};
        layout.bytesPerRow  = rowBytes;
        layout.rowsPerImage = desc._height;
        const WGPUExtent3D extent{ desc._width, desc._height, 1 };
        wgpuQueueWriteTexture( _pDevice->_queue, &target, desc._pData, desc._sizeBytes, &layout, &extent );
        const uint64 serial = enqueueQueueFence();
        pRecord->_pUploadSerials->recordWrite( serial );
        return _pDevice->_transferSerials.makeAccepted( serial );
    }

    // 길이 유지(긴 함수 정리 기준): 검사 · 배치 · 읽기 버퍼 · 복사 제출 · 매핑 요청의 순서가 곧 의미라 나누지 않는다.
    RHITransferSubmit WebGPURHIResourceFactory::submitTextureReadback( RHITextureHandle texture, uint32 mip, uint32 arraySlice, RHITextureMipSpan& outLayout )
    {
        using Internal                     = WebGPURHIResourceFactoryInternal;
        WebGPUTextureRecord* const pRecord = _pDevice->findTexture( texture );
        if ( pRecord == nullptr || _pDevice->_bDeviceLost != SW_FALSE )
            return RHITransferSubmit::rejected();
        const RHITextureDesc& textureDesc = pRecord->_desc;
        const bool            bDepth      = Internal::isDepthFormat( pRecord->_wgpuFormat );
        if ( mip >= MathUtil::max( textureDesc._mipLevels, 1u ) || arraySlice >= MathUtil::max( textureDesc._arraySize, 1u ) )
            return RHITransferSubmit::rejected();
        if ( bDepth && pRecord->_wgpuFormat != WGPUTextureFormat_Depth32FloatStencil8 && pRecord->_wgpuFormat != WGPUTextureFormat_Depth32Float )
        {
            // depth24plus 는 복사 원본이 될 수 없다(명세) — 이 장치는 depth32float-stencil8 이 없다(docs/PlatformTargets.md).
            SW_LOG_ERROR_ID( "WebGPU.DepthReadbackUnsupported", "WebGPU: depth readback needs depth32float-stencil8, which this device lacks" );
            return RHITransferSubmit::rejected();
        }
        const RHIFormat layoutFormat = bDepth ? kRHIDepthReadbackFormat : textureDesc._format;
        if ( computeRHITextureMipLayout( layoutFormat, textureDesc._width, textureDesc._height, mip, outLayout ) == false )
            return RHITransferSubmit::rejected();
        const uint32 rowCount       = outLayout._sizeBytes / MathUtil::max( outLayout._rowBytes, 1u );
        const uint32 paddedRowBytes = static_cast<uint32>( MathUtil::align( static_cast<uint64>( outLayout._rowBytes ), Internal::kRowPitchAlignment ) );

        WGPUBufferDescriptor bufferDesc{};
        bufferDesc.label        = WebGPURHIUtil::toStringView( "SW Texture Readback" );
        bufferDesc.usage        = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst;
        bufferDesc.size         = static_cast<uint64>( paddedRowBytes ) * rowCount;
        const WGPUBuffer buffer = wgpuDeviceCreateBuffer( _pDevice->_device, &bufferDesc );
        if ( buffer == nullptr )
            return RHITransferSubmit::rejected();

        WGPUCommandEncoderDescriptor encoderDesc{};
        const WGPUCommandEncoder     encoder = wgpuDeviceCreateCommandEncoder( _pDevice->_device, &encoderDesc );
        WGPUTexelCopyTextureInfo     source{};
        source.texture  = pRecord->_texture;
        source.mipLevel = mip;
        source.origin   = WGPUOrigin3D{ 0, 0, arraySlice };
        source.aspect   = bDepth ? WGPUTextureAspect_DepthOnly : WGPUTextureAspect_All;
        WGPUTexelCopyBufferInfo target{};
        target.layout.bytesPerRow      = paddedRowBytes;
        target.layout.rowsPerImage     = rowCount;
        target.buffer                  = buffer;
        const RHIFormatBlockInfo block = getRHIFormatBlockInfo( textureDesc._format );
        const WGPUExtent3D       extent{ Internal::toPhysicalExtent( outLayout._width, block._blockWidth ), Internal::toPhysicalExtent( outLayout._height, block._blockHeight ), 1 };
        wgpuCommandEncoderCopyTextureToBuffer( encoder, &source, &target, &extent );
        WGPUCommandBufferDescriptor commandDesc{};
        WGPUCommandBuffer           commands = wgpuCommandEncoderFinish( encoder, &commandDesc );
        // 따로 제출한다 — 앞서 제출된 프레임 뒤에 실행되고(순서 규칙 3d) 지금 기록 중인 프레임은 아직 큐에 없다.
        wgpuQueueSubmit( _pDevice->_queue, 1, &commands );
        wgpuCommandBufferRelease( commands );
        wgpuCommandEncoderRelease( encoder );

        WebGPUTextureReadback payload{};
        payload._buffer                        = buffer;
        payload._layout                        = outLayout;
        payload._paddedRowBytes                = paddedRowBytes;
        payload._bDepth                        = bDepth ? SW_TRUE : SW_FALSE;
        unique_ptr<WebGPUTransferProbe> probe  = make_unique<WebGPUTransferProbe>();
        WebGPUTransferProbe* const      pProbe = probe.get();
        const uint64                    serial = _pDevice->_transferQueue.enqueue( std::move( probe ) );
        pProbe->_pDevice                       = _pDevice;
        pProbe->_serial                        = serial;
        _pDevice->_readbackTable.add( serial, std::move( payload ) );
        // 매핑 콜백이 결과를 담고 표지를 켠다(결과는 시리얼로 표에서 찾는다 — 버려졌으면 버퍼만 놓는다).
        WGPUBufferMapCallbackInfo mapInfo{};
        mapInfo.mode      = WGPUCallbackMode_AllowSpontaneous;
        mapInfo.callback  = &WebGPURHIResourceFactory::onReadbackMapped;
        mapInfo.userdata1 = pProbe;
        (void)wgpuBufferMapAsync( buffer, WGPUMapMode_Read, 0, bufferDesc.size, mapInfo );
        return _pDevice->_transferSerials.makeAccepted( serial );
    }

    void WebGPURHIResourceFactory::onReadbackMapped( WGPUMapAsyncStatus status, WGPUStringView message, void* pUserdata1, void* pUserdata2 )
    {
        (void)pUserdata2;
        WebGPUTransferProbe* const pProbe  = static_cast<WebGPUTransferProbe*>( pUserdata1 );
        WebGPURHIDevice* const     pDevice = pProbe->_pDevice;
        const uint64               serial  = pProbe->_serial;
        WebGPUTextureReadback      payload{};
        const bool                 bFound = pDevice->_readbackTable.take( serial, payload );
        const bool                 bLive  = bFound && payload._bDiscarded == SW_FALSE;
        if ( bLive && status == WGPUMapAsyncStatus_Success )
        {
            const RHITextureMipSpan& layout   = payload._layout;
            const uint32             rowCount = layout._sizeBytes / MathUtil::max( layout._rowBytes, 1u );
            const uint8* const       pMapped  = static_cast<const uint8*>( wgpuBufferGetConstMappedRange( payload._buffer, 0, static_cast<size_t>( payload._paddedRowBytes ) * rowCount ) );
            payload._bytes.resize( layout._sizeBytes );
            for ( uint32 row = 0; pMapped != nullptr && row < rowCount; ++row )
            {
                Memory::copy( payload._bytes.data() + static_cast<size_t>( row ) * layout._rowBytes, pMapped + static_cast<size_t>( row ) * payload._paddedRowBytes, layout._rowBytes );
            }
            wgpuBufferUnmap( payload._buffer );
        }
        else if ( bLive && status != WGPUMapAsyncStatus_Success )
        {
            SW_LOG_ERROR_ID( "WebGPU.ReadbackMapFailed", "WebGPU: readback mapping failed (status %#): %#", static_cast<uint32>( status ), WebGPURHIUtil::fromStringView( message ) );
        }
        if ( payload._buffer != nullptr )
        {
            wgpuBufferDestroy( payload._buffer );
            wgpuBufferRelease( payload._buffer );
            payload._buffer = nullptr;
        }
        if ( bLive )
            pDevice->_readbackTable.add( serial, std::move( payload ) ); // 버퍼는 놓았다 — 남은 것은 바이트뿐이다(`takeTextureReadback`)
        if ( status != WGPUMapAsyncStatus_Success )
            pProbe->_bFailed.store( SW_TRUE );
        pProbe->_bDone.store( SW_TRUE );
    }

    RHITransferStatus WebGPURHIResourceFactory::pollTransfer( RHITransferTicket ticket )
    {
        _pDevice->processCompletions();
        return _pDevice->_transferSerials.classify( ticket );
    }

    bool WebGPURHIResourceFactory::takeTextureReadback( RHITransferTicket ticket, vector<uint8>& outBytes )
    {
        if ( _pDevice->_transferSerials.classify( ticket ) != RHITransferStatus::Complete )
        {
            SW_LOG_ERROR_ID( "WebGPU.ReadbackNotComplete", "WebGPU: takeTextureReadback before the readback completed (serial %#)", ticket._serial );
            return false;
        }
        WebGPUTextureReadback payload{};
        if ( _pDevice->_readbackTable.take( ticket._serial, payload ) == false )
        {
            SW_LOG_ERROR_ID( "WebGPU.ReadbackMissing", "WebGPU: no readback result for serial %# (taken, discarded or an upload ticket)", ticket._serial );
            return false;
        }
        outBytes = std::move( payload._bytes );
        return true;
    }

    void WebGPURHIResourceFactory::discardTextureReadback( RHITransferTicket ticket )
    {
        // 결과만 버린다 — 매핑이 아직이면 콜백이 표에서 찾지 못하고 버퍼만 놓는다. 버퍼를 여기서 놓으면 진행 중 매핑이 끝나지 않은 버퍼를 놓는다.
        WebGPUTextureReadback payload{};
        if ( _pDevice->_readbackTable.take( ticket._serial, payload ) == false || payload._buffer == nullptr )
            return;
        payload._bytes.clear();
        payload._bDiscarded = SW_TRUE;
        _pDevice->_readbackTable.add( ticket._serial, std::move( payload ) );
    }

    // ------------------------------------------------------------------------------
    // bindless
    // ------------------------------------------------------------------------------
    RHIDescriptorIndex WebGPURHIResourceFactory::registerBindlessTexture( RHITextureHandle texture )
    {
        _pDevice->requireRegistryMutableNow( "WebGPU.registerBindlessTexture" );
        const WebGPUTextureRecord* const pRecord = _pDevice->findTexture( texture );
        if ( pRecord == nullptr || _pDevice->_transferSerials.isTextureReadyToRegister( *pRecord->_pUploadSerials, texture ) == false )
            return kInvalidDescriptorIndex;
        UniqueLock<shared_mutex> lock{ _pDevice->_bindlessMutex };
        return _pDevice->_textureTable.allocate( WebGPUBindlessEntry{ texture, SW_TRUE } );
    }

    bool WebGPURHIResourceFactory::isTextureReadyToSample( RHITextureHandle texture )
    {
        _pDevice->processCompletions();
        const WebGPUTextureRecord* const pRecord = _pDevice->findTexture( texture );
        return pRecord != nullptr && _pDevice->_transferSerials.isFirstUploadComplete( *pRecord->_pUploadSerials );
    }

    void WebGPURHIResourceFactory::unregisterBindlessTexture( RHIDescriptorIndex index )
    {
        _pDevice->requireRegistryMutableNow( "WebGPU.unregisterBindlessTexture" );
        UniqueLock<shared_mutex> lock{ _pDevice->_bindlessMutex };
        (void)_pDevice->_textureTable.release( index, RHIBindlessReuse::Immediately );
    }

    RHIDescriptorIndex WebGPURHIResourceFactory::registerBindlessBufferSRV( RHIBufferHandle buffer )
    {
        _pDevice->requireRegistryMutableNow( "WebGPU.registerBindlessBufferSRV" );
        if ( _pDevice->findBuffer( buffer ) == nullptr )
            return kInvalidDescriptorIndex;
        UniqueLock<shared_mutex> lock{ _pDevice->_bindlessMutex };
        return _pDevice->_bufferTable.allocate( WebGPUBindlessEntry{ buffer, SW_FALSE } );
    }

    void WebGPURHIResourceFactory::unregisterBindlessResource( RHIDescriptorIndex index )
    {
        _pDevice->requireRegistryMutableNow( "WebGPU.unregisterBindlessResource" );
        UniqueLock<shared_mutex> lock{ _pDevice->_bindlessMutex };
        (void)_pDevice->_bufferTable.release( index, RHIBindlessReuse::Immediately );
    }

    RHIDescriptorIndex WebGPURHIResourceFactory::registerBindlessBufferUAV( RHIBufferHandle buffer )
    {
        _pDevice->requireRegistryMutableNow( "WebGPU.registerBindlessBufferUAV" );
        if ( _pDevice->findBuffer( buffer ) == nullptr )
            return kInvalidDescriptorIndex;
        UniqueLock<shared_mutex> lock{ _pDevice->_bindlessMutex };
        return _pDevice->_uavTable.allocate( WebGPUBindlessEntry{ buffer, SW_FALSE } );
    }

    RHIDescriptorIndex WebGPURHIResourceFactory::registerBindlessTextureUAV( RHITextureHandle texture )
    {
        _pDevice->requireRegistryMutableNow( "WebGPU.registerBindlessTextureUAV" );
        const WebGPUTextureRecord* const pRecord = _pDevice->findTexture( texture );
        if ( pRecord == nullptr || pRecord->_desc._bIsUnorderedAccess == SW_FALSE )
            return kInvalidDescriptorIndex;
        UniqueLock<shared_mutex> lock{ _pDevice->_bindlessMutex };
        return _pDevice->_uavTable.allocate( WebGPUBindlessEntry{ texture, SW_TRUE } );
    }

    void WebGPURHIResourceFactory::unregisterBindlessUAV( RHIDescriptorIndex index )
    {
        _pDevice->requireRegistryMutableNow( "WebGPU.unregisterBindlessUAV" );
        UniqueLock<shared_mutex> lock{ _pDevice->_bindlessMutex };
        (void)_pDevice->_uavTable.release( index, RHIBindlessReuse::Immediately );
    }
} // namespace sw

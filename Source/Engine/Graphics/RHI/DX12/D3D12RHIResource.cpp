#include "pch.h"

#include "Engine/Graphics/RHI/DX12/D3D12RHIResource.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Common/EnginePlatformHeaders.h"
#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/RHI/DX12/D3D12RHIDevice.h"
#include "Engine/Graphics/RHI/RHIDxgiFormat.h"
#include "Engine/Graphics/Shader/ShaderCache.h"

#if defined( SW_PLATFORM_WINDOWS )
namespace sw
{
    namespace
    {
        ShaderCompileResult compileShader( const ShaderCompileDesc& desc )
        {
            if ( engine::areEngineServicesBound() )
                return engine::getShaderCache().getOrCompile( desc );
            return ShaderCompiler::compileHLSL( desc );
        }
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "D3D12RHIResource" );

    RHIPipelineStateHandle D3D12RHIResource::createPipelineState( const RHIPipelineStateDesc& desc )
    {
        Microsoft::WRL::ComPtr<ID3D12PipelineState> pso;
        auto                                        fillDefines = [&]( ShaderCompileDesc& cd )
        {
            for ( const string& def : desc._listShaderDefine )
            {
                ShaderMacroDefine m{};
                const size_t      eq = def.find( '=' );
                if ( eq == string::npos )
                {
                    m._name  = def;
                    m._value = "1";
                }
                else
                {
                    m._name  = def.substr( 0, eq );
                    m._value = def.substr( eq + 1 );
                }
                cd._listDefine.push_back( std::move( m ) );
            }
        };

        ShaderCompileDesc vsDesc{};
        vsDesc._filePath     = desc._vertexShaderPath;
        vsDesc._entryPoint   = desc._vertexEntryPoint.empty() ? "VSMain" : desc._vertexEntryPoint;
        vsDesc._stage        = ShaderStage::Vertex;
        vsDesc._targetFormat = ShaderTargetFormat::DXIL_D3D12;
        fillDefines( vsDesc );
        ShaderCompileResult vsResult = compileShader( vsDesc );

        ShaderCompileDesc psDesc{};
        psDesc._filePath     = desc._pixelShaderPath;
        psDesc._entryPoint   = desc._pixelEntryPoint.empty() ? "PSMain" : desc._pixelEntryPoint;
        psDesc._stage        = ShaderStage::Pixel;
        psDesc._targetFormat = ShaderTargetFormat::DXIL_D3D12;
        fillDefines( psDesc );
        ShaderCompileResult psResult = compileShader( psDesc );

        if ( vsResult._bSuccess && psResult._bSuccess )
        {
            D3D12_INPUT_ELEMENT_DESC inputElementDescs[] = {
                {"POSITION", 0,    DXGI_FORMAT_R32G32B32_FLOAT, 0,  0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
                {   "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
            };

            D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
            psoDesc.InputLayout              = { inputElementDescs, _countof( inputElementDescs ) };
            psoDesc.pRootSignature           = _pDevice->_rootSignature.Get();
            psoDesc.VS                       = { vsResult._bytecode.data(), vsResult._bytecode.size() };
            psoDesc.PS                       = { psResult._bytecode.data(), psResult._bytecode.size() };
            psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
            psoDesc.RasterizerState.CullMode = ( desc._cullMode == RHICullMode::Front )
                                                 ? D3D12_CULL_MODE_FRONT
                                                 : ( ( desc._cullMode == RHICullMode::Back ) ? D3D12_CULL_MODE_BACK : D3D12_CULL_MODE_NONE );
            psoDesc.SampleMask               = MathUtil::MaxUInt32;
            psoDesc.PrimitiveTopologyType    = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            psoDesc.NumRenderTargets         = desc._numRenderTargets > 0 ? desc._numRenderTargets : 1;
            if ( psoDesc.NumRenderTargets > 8 )
                psoDesc.NumRenderTargets = 8;
            for ( UINT rtvIndex = 0; rtvIndex < psoDesc.NumRenderTargets; ++rtvIndex )
            {
                auto& rtBlend                 = psoDesc.BlendState.RenderTarget[rtvIndex];
                rtBlend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
                psoDesc.RTVFormats[rtvIndex]  = toDxgiFormat( desc._arrRtvFormat[rtvIndex] );
                if ( desc._bEnableBlend != 0 )
                {
                    rtBlend.BlendEnable    = TRUE;
                    rtBlend.SrcBlend       = D3D12_BLEND_SRC_ALPHA;
                    rtBlend.DestBlend      = D3D12_BLEND_INV_SRC_ALPHA;
                    rtBlend.BlendOp        = D3D12_BLEND_OP_ADD;
                    rtBlend.SrcBlendAlpha  = D3D12_BLEND_ONE;
                    rtBlend.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
                    rtBlend.BlendOpAlpha   = D3D12_BLEND_OP_ADD;
                }
            }
            if ( desc._bEnableDepthTest != 0 )
            {
                psoDesc.DepthStencilState.DepthEnable    = TRUE;
                psoDesc.DepthStencilState.DepthWriteMask = ( desc._bEnableDepthWrite != 0 )
                                                             ? D3D12_DEPTH_WRITE_MASK_ALL
                                                             : D3D12_DEPTH_WRITE_MASK_ZERO;
                psoDesc.DepthStencilState.DepthFunc      = D3D12_COMPARISON_FUNC_LESS_EQUAL;
                psoDesc.DSVFormat                        = toDxgiFormat( desc._depthStencilFormat );
            }
            else
            {
                psoDesc.DepthStencilState.DepthEnable    = FALSE;
                psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
                psoDesc.DSVFormat                        = DXGI_FORMAT_UNKNOWN;
            }
            psoDesc.SampleDesc.Count = 1;

            _pDevice->_device->CreateGraphicsPipelineState( &psoDesc, IID_PPV_ARGS( pso.GetAddressOf() ) );
        }

        return _pDevice->_pipelineStates.insert( { pso } );
    }

    RHIPipelineStateHandle D3D12RHIResource::createComputePipelineState( string_view shaderPath, string_view entryPoint )
    {
        Microsoft::WRL::ComPtr<ID3D12PipelineState> pso;
        if ( shaderPath.empty() == false )
        {
            ShaderCompileDesc csDesc{};
            csDesc._filePath        = shaderPath;
            csDesc._entryPoint      = entryPoint;
            csDesc._stage           = ShaderStage::Compute;
            csDesc._targetFormat    = ShaderTargetFormat::DXIL_D3D12;
            ShaderCompileResult res = compileShader( csDesc );
            if ( res._bSuccess )
            {
                D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc{};
                psoDesc.pRootSignature = _pDevice->_computeRootSignature ? _pDevice->_computeRootSignature.Get() : _pDevice->_rootSignature.Get();
                psoDesc.CS             = { res._bytecode.data(), res._bytecode.size() };

                const HRESULT hr = _pDevice->_device->CreateComputePipelineState( &psoDesc, IID_PPV_ARGS( pso.GetAddressOf() ) );
                if ( FAILED( hr ) )
                {
                    SW_LOG_ERROR( "CreateComputePipelineState failed hr=0x%#", static_cast<uint32>( hr ) );
                    _pDevice->flushDebugMessages( "CreateComputePipelineState" );
                    return 0;
                }
            }
            else
                return 0;
        }
        return _pDevice->_pipelineStates.insert( { pso } );
    }

    void D3D12RHIResource::destroyPipelineState( RHIPipelineStateHandle pso )
    {
        if ( pso == 0 )
            return;
        D3D12RHIDevice::D3D12PipelineStateRecord record{};
        if ( _pDevice->_pipelineStates.take( pso, record ) == false )
            return;
        if ( _pDevice->_frameStreamState._activeGraphicsPso == pso )
            _pDevice->_frameStreamState._activeGraphicsPso = 0;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> owned     = record._pso;
        auto                                        releaseCb = [owned]()
        { (void)owned.Get(); };
        _pDevice->_releaseQueue.enqueueGpuRelease( SW_DELEGATE_LAMBDA( RHIResourceReleaseDelegate, releaseCb ), _pDevice->_fenceValue );
    }

    RHIRenderPassHandle D3D12RHIResource::createRenderPass( const RHIRenderPassDesc& desc )
    {
        D3D12RHIDevice::D3D12RenderPassRecord record{};
        record._desc   = desc;
        record._bAlive = 1;
        _pDevice->_listRenderPass.push_back( record );
        return _pDevice->_listRenderPass.size();
    }

    void D3D12RHIResource::destroyRenderPass( RHIRenderPassHandle pass )
    {
        if ( pass == 0 || pass > _pDevice->_listRenderPass.size() )
            return;
        _pDevice->_listRenderPass[pass - 1]._bAlive = 0;
    }

    RHIBufferHandle D3D12RHIResource::createConstantBuffer( uint32 size )
    {
        const UINT            alignedSize = MathUtil::align( size, constant::kConstantBufferAlignment );
        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

        D3D12_RESOURCE_DESC resDesc{};
        resDesc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
        resDesc.Width            = static_cast<UINT64>( alignedSize ) * constant::kMaxFrameCountInFlight;
        resDesc.Height           = 1;
        resDesc.DepthOrArraySize = 1;
        resDesc.MipLevels        = 1;
        resDesc.Format           = DXGI_FORMAT_UNKNOWN;
        resDesc.SampleDesc.Count = 1;
        resDesc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        Microsoft::WRL::ComPtr<ID3D12Resource> buffer;
        if ( FAILED( _pDevice->_device->CreateCommittedResource( &heapProps, D3D12_HEAP_FLAG_NONE, &resDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS( buffer.GetAddressOf() ) ) ) )
            return 0;

        void* pMapped{ nullptr };
        if ( FAILED( buffer->Map( 0, nullptr, &pMapped ) ) || pMapped == nullptr )
            return 0;

        const RHIBufferHandle handle        = _pDevice->storeBuffer( buffer );
        _pDevice->_mapCbAlignedSize[handle] = alignedSize;
        _pDevice->_mapCbMapped[handle]      = pMapped;
        return handle;
    }

    void D3D12RHIResource::updateConstantBuffer( RHIBufferHandle buffer, const void* pData, uint32 size )
    {
        if ( buffer == 0 || pData == nullptr )
            return;

        // CBV SizeInBytes must be a multiple of 256 (D3D12 requirement).
        uint32     alignedSize = MathUtil::align( size, constant::kConstantBufferAlignment );
        const auto sizeIt      = _pDevice->_mapCbAlignedSize.find( buffer );
        if ( sizeIt != _pDevice->_mapCbAlignedSize.end() )
            alignedSize = sizeIt->second;
        const uint32 slot   = _pDevice->_frameRing.currentIndex();
        const uint32 offset = slot * alignedSize;

        const auto mapIt = _pDevice->_mapCbMapped.find( buffer );
        if ( mapIt == _pDevice->_mapCbMapped.end() || mapIt->second == nullptr )
            return;
        Memory::copy( static_cast<uint8*>( mapIt->second ) + offset, pData, size );

        std::shared_lock<std::shared_mutex> lock{ _pDevice->_bindlessMutex };
        for ( D3D12RHIDevice::BindlessResourceRecord& rec : _pDevice->_listRegisteredBindless )
        {
            if ( rec._buffer != buffer || rec._resource == nullptr )
                continue;
            D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc{};
            cbvDesc.BufferLocation = rec._resource->GetGPUVirtualAddress() + offset;
            cbvDesc.SizeInBytes    = alignedSize;
            _pDevice->_device->CreateConstantBufferView( &cbvDesc, rec._cpuHandle );
        }
    }

    RHIBufferHandle D3D12RHIResource::createStructuredBuffer( uint32 elementSize, uint32 elementCount )
    {
        UINT                  alignedSize = elementSize * elementCount;
        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC resDesc{};
        resDesc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
        resDesc.Width            = alignedSize;
        resDesc.Height           = 1;
        resDesc.DepthOrArraySize = 1;
        resDesc.MipLevels        = 1;
        resDesc.Format           = DXGI_FORMAT_UNKNOWN;
        resDesc.SampleDesc.Count = 1;
        resDesc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        resDesc.Flags            = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        Microsoft::WRL::ComPtr<ID3D12Resource> buffer;
        if ( FAILED( _pDevice->_device->CreateCommittedResource( &heapProps, D3D12_HEAP_FLAG_NONE, &resDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS( buffer.GetAddressOf() ) ) ) )
            return 0;

        const RHIBufferHandle handle = _pDevice->storeBuffer( buffer );
        {
            std::scoped_lock<mutex> lock{ _pDevice->_resourceStateMutex };
            _pDevice->_mapStructuredBufferState[handle] = D3D12_RESOURCE_STATE_COMMON;
        }
        _pDevice->_mapStructuredStride[handle] = elementSize > 0 ? elementSize : 4u;
        return handle;
    }

    void D3D12RHIResource::updateStructuredBuffer( RHIBufferHandle buffer, const void* pData, uint32 size )
    {
        if ( buffer == 0 || pData == nullptr || size == 0 || _pDevice->_device == nullptr || _pDevice->_commandQueue == nullptr )
            return;

        ID3D12Resource* pDest = _pDevice->resolveBuffer( buffer );
        if ( pDest == nullptr )
            return;

        // 프레임 링 슬롯 하나를 재사용한다(매 호출마다 업로드 힙/얼로케이터/리스트를 새로 만들지 않음).
        // 이 슬롯을 다시 쓸 차례가 됐다는 건 waitForRingSlot()이 이미 constant::kMaxFrameCountInFlight 프레임 전 제출의
        // GPU 완료를 보장했다는 뜻이라 별도 대기(waitForPreviousFrame) 없이 안전하다.
        D3D12RHIDevice::StructuredUploadSlot& slot = _pDevice->_arrStructuredUploadSlot[_pDevice->_frameRing.currentIndex()];

        if ( slot._uploadHeap == nullptr || slot._capacity < size )
        {
            const uint64 newCapacity = MathUtil::align( static_cast<uint64>( size ) * 2, 65536ull );

            D3D12_HEAP_PROPERTIES uploadHeap{};
            uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;

            D3D12_RESOURCE_DESC uploadDesc{};
            uploadDesc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
            uploadDesc.Width            = newCapacity;
            uploadDesc.Height           = 1;
            uploadDesc.DepthOrArraySize = 1;
            uploadDesc.MipLevels        = 1;
            uploadDesc.Format           = DXGI_FORMAT_UNKNOWN;
            uploadDesc.SampleDesc.Count = 1;
            uploadDesc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

            Microsoft::WRL::ComPtr<ID3D12Resource> newHeap;
            if ( FAILED( _pDevice->_device->CreateCommittedResource( &uploadHeap, D3D12_HEAP_FLAG_NONE, &uploadDesc,
                                                                     D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS( newHeap.GetAddressOf() ) ) ) )
            {
                SW_LOG_ERROR( "updateStructuredBuffer: failed to (re)create staging upload buffer (%# bytes)", newCapacity );
                return;
            }

            void* pMapped{ nullptr };
            if ( FAILED( newHeap->Map( 0, nullptr, &pMapped ) ) || pMapped == nullptr )
            {
                SW_LOG_ERROR( "updateStructuredBuffer: Map failed on staging buffer" );
                return;
            }

            slot._uploadHeap = newHeap;
            slot._pMapped    = pMapped;
            slot._capacity   = newCapacity;
        }

        Memory::copy( slot._pMapped, pData, size );

        if ( slot._copyAllocator == nullptr )
        {
            if ( FAILED( _pDevice->_device->CreateCommandAllocator( D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS( slot._copyAllocator.GetAddressOf() ) ) ) )
            {
                SW_LOG_ERROR( "updateStructuredBuffer: failed to create copy command allocator" );
                return;
            }
        }
        else if ( FAILED( slot._copyAllocator->Reset() ) )
        {
            SW_LOG_ERROR( "updateStructuredBuffer: copy allocator Reset failed" );
            return;
        }

        if ( slot._copyCommandList == nullptr )
        {
            if ( FAILED( _pDevice->_device->CreateCommandList( 0, D3D12_COMMAND_LIST_TYPE_DIRECT, slot._copyAllocator.Get(), nullptr, IID_PPV_ARGS( slot._copyCommandList.GetAddressOf() ) ) ) )
            {
                SW_LOG_ERROR( "updateStructuredBuffer: failed to create copy command list" );
                return;
            }
        }
        else if ( FAILED( slot._copyCommandList->Reset( slot._copyAllocator.Get(), nullptr ) ) )
        {
            SW_LOG_ERROR( "updateStructuredBuffer: copy command list Reset failed" );
            return;
        }

        ID3D12GraphicsCommandList* pList = slot._copyCommandList.Get();

        D3D12_RESOURCE_STATES stateBefore = D3D12_RESOURCE_STATE_COMMON;
        {
            std::scoped_lock<mutex>                                                     lock{ _pDevice->_resourceStateMutex };
            const unordered_map<RHIBufferHandle, D3D12_RESOURCE_STATES>::const_iterator stateIt = _pDevice->_mapStructuredBufferState.find( buffer );
            if ( stateIt != _pDevice->_mapStructuredBufferState.end() )
                stateBefore = stateIt->second;
        }

        if ( stateBefore != D3D12_RESOURCE_STATE_COPY_DEST )
        {
            D3D12_RESOURCE_BARRIER toCopyDest{};
            toCopyDest.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            toCopyDest.Transition.pResource   = pDest;
            toCopyDest.Transition.StateBefore = stateBefore;
            toCopyDest.Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_DEST;
            toCopyDest.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            pList->ResourceBarrier( 1, &toCopyDest );
        }

        pList->CopyBufferRegion( pDest, 0, slot._uploadHeap.Get(), 0, size );

        D3D12_RESOURCE_BARRIER toUav{};
        toUav.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        toUav.Transition.pResource   = pDest;
        toUav.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        toUav.Transition.StateAfter  = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        toUav.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        pList->ResourceBarrier( 1, &toUav );

        pList->Close();
        ID3D12CommandList* lists[] = { pList };
        _pDevice->_commandQueue->ExecuteCommandLists( 1, lists );

        {
            std::scoped_lock<mutex> lock{ _pDevice->_resourceStateMutex };
            _pDevice->_mapStructuredBufferState[buffer] = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        }
    }

    RHIBufferHandle D3D12RHIResource::createVertexBuffer( const void* pData, uint32 sizeBytes )
    {
        if ( _pDevice->_device == nullptr || pData == nullptr || sizeBytes == 0 )
            return 0;

        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

        D3D12_RESOURCE_DESC resDesc{};
        resDesc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
        resDesc.Width            = sizeBytes;
        resDesc.Height           = 1;
        resDesc.DepthOrArraySize = 1;
        resDesc.MipLevels        = 1;
        resDesc.Format           = DXGI_FORMAT_UNKNOWN;
        resDesc.SampleDesc.Count = 1;
        resDesc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        Microsoft::WRL::ComPtr<ID3D12Resource> buffer;
        if ( FAILED( _pDevice->_device->CreateCommittedResource( &heapProps, D3D12_HEAP_FLAG_NONE, &resDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS( buffer.GetAddressOf() ) ) ) )
            return 0;

        void* pMapped{ nullptr };
        if ( FAILED( buffer->Map( 0, nullptr, &pMapped ) ) || pMapped == nullptr )
            return 0;
        Memory::copy( pMapped, pData, sizeBytes );
        buffer->Unmap( 0, nullptr );

        return _pDevice->storeBuffer( buffer );
    }

    void D3D12RHIResource::destroyBuffer( RHIBufferHandle buffer )
    {
        if ( buffer == 0 )
            return;
        if ( buffer == _pDevice->_frameStreamState._boundMeshVb )
            _pDevice->_frameStreamState._boundMeshVb = 0;
        if ( buffer == _pDevice->_frameStreamState._boundIndexBuffer )
            _pDevice->_frameStreamState._boundIndexBuffer = 0;
        {
            std::scoped_lock<mutex> lock{ _pDevice->_resourceStateMutex };
            _pDevice->_mapStructuredBufferState.erase( buffer );
        }
        const auto mapIt = _pDevice->_mapCbMapped.find( buffer );
        if ( mapIt != _pDevice->_mapCbMapped.end() && mapIt->second != nullptr )
        {
            ID3D12Resource* pRes = _pDevice->resolveBuffer( buffer );
            if ( pRes != nullptr )
                pRes->Unmap( 0, nullptr );
            _pDevice->_mapCbMapped.erase( mapIt );
        }
        _pDevice->_mapCbAlignedSize.erase( buffer );
        Microsoft::WRL::ComPtr<ID3D12Resource> owned;
        if ( _pDevice->_gpuBuffers.take( buffer, owned ) == false )
            return;

        {
            std::unique_lock<std::shared_mutex> lock{ _pDevice->_bindlessMutex };
            for ( D3D12RHIDevice::BindlessResourceRecord& rec : _pDevice->_listRegisteredBindless )
            {
                if ( rec._buffer != buffer )
                    continue;
                rec._resource.Reset();
                rec._buffer = 0;
            }
            for ( D3D12RHIDevice::BindlessResourceRecord& rec : _pDevice->_listRegisteredUAV )
            {
                if ( rec._buffer != buffer )
                    continue;
                rec._resource.Reset();
                rec._buffer = 0;
            }
        }

        auto releaseCb = [owned]()
        { (void)owned.Get(); };
        _pDevice->_releaseQueue.enqueueGpuRelease( SW_DELEGATE_LAMBDA( RHIResourceReleaseDelegate, releaseCb ), _pDevice->_fenceValue );
    }

    RHITextureHandle D3D12RHIResource::createTexture2D( const RHITextureDesc& desc )
    {
        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

        const bool        bDepth      = desc._bIsDepthStencil != 0;
        const DXGI_FORMAT typelessFmt = bDepth ? DXGI_FORMAT_R24G8_TYPELESS : toDxgiFormat( desc._format );
        const DXGI_FORMAT dsvFmt      = toDxgiFormat( constant::kDepthStencilFormat );
        const DXGI_FORMAT colorFmt    = toDxgiFormat( desc._format );

        D3D12_RESOURCE_DESC resDesc{};
        resDesc.Dimension          = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        resDesc.Alignment          = 0;
        resDesc.Width              = desc._width;
        resDesc.Height             = desc._height;
        resDesc.DepthOrArraySize   = 1;
        resDesc.MipLevels          = static_cast<UINT16>( desc._mipLevels );
        resDesc.Format             = typelessFmt;
        resDesc.SampleDesc.Count   = 1;
        resDesc.SampleDesc.Quality = 0;
        resDesc.Layout             = D3D12_TEXTURE_LAYOUT_UNKNOWN;

        D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE;
        if ( desc._bIsRenderTarget )
            flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        if ( bDepth )
            flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        if ( desc._bIsUnorderedAccess )
            flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        // Depth+SRV: do not deny shader resource.
        resDesc.Flags = flags;

        D3D12_CLEAR_VALUE  clearValue{};
        D3D12_CLEAR_VALUE* pClearValue{ nullptr };
        if ( desc._bIsRenderTarget )
        {
            clearValue.Format   = colorFmt;
            clearValue.Color[0] = desc._clearColor._x;
            clearValue.Color[1] = desc._clearColor._y;
            clearValue.Color[2] = desc._clearColor._z;
            clearValue.Color[3] = desc._clearColor._w;
            pClearValue         = &clearValue;
        }
        else if ( bDepth )
        {
            clearValue.Format               = dsvFmt;
            clearValue.DepthStencil.Depth   = desc._clearDepth;
            clearValue.DepthStencil.Stencil = desc._clearStencil;
            pClearValue                     = &clearValue;
        }

        Microsoft::WRL::ComPtr<ID3D12Resource> texture;
        if ( FAILED( _pDevice->_device->CreateCommittedResource( &heapProps, D3D12_HEAP_FLAG_NONE, &resDesc,
                                                                 D3D12_RESOURCE_STATE_COMMON, pClearValue, IID_PPV_ARGS( texture.GetAddressOf() ) ) ) )
            return 0;

        const RHITextureHandle                 handle  = _pDevice->storeTexture( texture );
        ID3D12Resource*                        pNative = _pDevice->resolveTexture( handle );
        D3D12RHIDevice::OffscreenTextureRecord record{};
        record._state    = D3D12_RESOURCE_STATE_COMMON;
        record._format   = bDepth ? dsvFmt : colorFmt;
        record._width    = desc._width;
        record._height   = desc._height;
        record._bHasRtv  = 0;
        record._bHasDsv  = 0;
        record._reserved = 0;

        if ( pNative != nullptr && desc._bIsRenderTarget && _pDevice->_rtvHeap != nullptr )
        {
            uint32 rtvSlot{ 0 };
            if ( _pDevice->_listFreeOffscreenRtvIndex.empty() == false )
            {
                rtvSlot = _pDevice->_listFreeOffscreenRtvIndex.back();
                _pDevice->_listFreeOffscreenRtvIndex.pop_back();
            }
            else if ( _pDevice->_nextOffscreenRtvIndex < D3D12RHIDevice::kMaxOffscreenRtvs )
                rtvSlot = _pDevice->_nextOffscreenRtvIndex++;
            else
                rtvSlot = D3D12RHIDevice::kMaxOffscreenRtvs;
            if ( rtvSlot < D3D12RHIDevice::kMaxOffscreenRtvs )
            {
                record._rtvIndex  = _pDevice->_bufferCount + rtvSlot;
                record._rtvHandle = _pDevice->_rtvHeap->GetCPUDescriptorHandleForHeapStart();
                record._rtvHandle.ptr += static_cast<SIZE_T>( record._rtvIndex ) * _pDevice->_rtvDescriptorSize;
                D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
                rtvDesc.Format               = colorFmt;
                rtvDesc.ViewDimension        = D3D12_RTV_DIMENSION_TEXTURE2D;
                rtvDesc.Texture2D.MipSlice   = 0;
                rtvDesc.Texture2D.PlaneSlice = 0;
                _pDevice->_device->CreateRenderTargetView( pNative, &rtvDesc, record._rtvHandle );
                record._bHasRtv = 1;
            }
        }

        if ( pNative != nullptr && bDepth && _pDevice->_dsvHeap != nullptr )
        {
            uint32 dsvSlot{ 0 };
            if ( _pDevice->_listFreeOffscreenDsvIndex.empty() == false )
            {
                dsvSlot = _pDevice->_listFreeOffscreenDsvIndex.back();
                _pDevice->_listFreeOffscreenDsvIndex.pop_back();
            }
            else if ( _pDevice->_nextOffscreenDsvIndex < D3D12RHIDevice::kMaxOffscreenDsvs )
                dsvSlot = _pDevice->_nextOffscreenDsvIndex++;
            else
                dsvSlot = D3D12RHIDevice::kMaxOffscreenDsvs;
            if ( dsvSlot < D3D12RHIDevice::kMaxOffscreenDsvs )
            {
                record._dsvIndex  = dsvSlot;
                record._dsvHandle = _pDevice->_dsvHeap->GetCPUDescriptorHandleForHeapStart();
                record._dsvHandle.ptr += static_cast<SIZE_T>( record._dsvIndex ) * _pDevice->_device->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_DSV );
                D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
                dsvDesc.Format             = dsvFmt;
                dsvDesc.ViewDimension      = D3D12_DSV_DIMENSION_TEXTURE2D;
                dsvDesc.Flags              = D3D12_DSV_FLAG_NONE;
                dsvDesc.Texture2D.MipSlice = 0;
                _pDevice->_device->CreateDepthStencilView( pNative, &dsvDesc, record._dsvHandle );
                record._bHasDsv = 1;
            }
        }

        _pDevice->_mapOffscreenTexture[handle] = record;
        return handle;
    }

    void D3D12RHIResource::destroyTexture( RHITextureHandle texture )
    {
        if ( texture == 0 )
            return;
        auto it = _pDevice->_mapOffscreenTexture.find( texture );
        if ( it != _pDevice->_mapOffscreenTexture.end() )
        {
            if ( it->second._bHasRtv != 0 && it->second._rtvIndex >= _pDevice->_bufferCount )
                _pDevice->_listFreeOffscreenRtvIndex.push_back( it->second._rtvIndex - _pDevice->_bufferCount );
            if ( it->second._bHasDsv != 0 )
                _pDevice->_listFreeOffscreenDsvIndex.push_back( it->second._dsvIndex );
            _pDevice->_mapOffscreenTexture.erase( it );
        }
        Microsoft::WRL::ComPtr<ID3D12Resource> owned;
        if ( _pDevice->_gpuTextures.take( texture, owned ) == false )
            return;

        {
            std::unique_lock<std::shared_mutex> lock{ _pDevice->_bindlessMutex };
            for ( D3D12RHIDevice::BindlessResourceRecord& rec : _pDevice->_listRegisteredBindless )
            {
                if ( rec._texture != texture )
                    continue;
                rec._resource.Reset();
                rec._texture = 0;
            }
        }

        auto releaseCb = [owned]()
        { (void)owned.Get(); };
        _pDevice->_releaseQueue.enqueueGpuRelease( SW_DELEGATE_LAMBDA( RHIResourceReleaseDelegate, releaseCb ), _pDevice->_fenceValue );
    }

    RHIDescriptorIndex D3D12RHIResource::registerBindlessTexture( RHITextureHandle texture )
    {
        if ( texture == 0 || _pDevice->_cbvHeap == nullptr )
            return kInvalidDescriptorIndex;

        auto* pRes = _pDevice->resolveTexture( texture );
        if ( pRes == nullptr )
            return kInvalidDescriptorIndex;

        std::unique_lock<std::shared_mutex> lock{ _pDevice->_bindlessMutex };
        RHIDescriptorIndex                  index;
        if ( _pDevice->_listFreeBindless.empty() == false )
        {
            index = _pDevice->_listFreeBindless.back();
            _pDevice->_listFreeBindless.pop_back();
        }
        else
        {
            if ( _pDevice->_allocatedDescriptorsCount >= D3D12RHIDevice::kMaxShaderVisibleDescriptors )
            {
                SW_LOG_ERROR( "Shader visible descriptor heap overflow! Max: %#", D3D12RHIDevice::kMaxShaderVisibleDescriptors );
                return kInvalidDescriptorIndex;
            }
            index = _pDevice->_allocatedDescriptorsCount++;
        }

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.ViewDimension                 = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping       = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MostDetailedMip     = 0;
        srvDesc.Texture2D.MipLevels           = pRes->GetDesc().MipLevels;
        srvDesc.Texture2D.PlaneSlice          = 0;
        srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

        const DXGI_FORMAT resFmt = pRes->GetDesc().Format;
        if ( resFmt == DXGI_FORMAT_R24G8_TYPELESS || resFmt == DXGI_FORMAT_D24_UNORM_S8_UINT )
            srvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
        else
            srvDesc.Format = resFmt;

        D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle( _pDevice->_cbvHeap->GetCPUDescriptorHandleForHeapStart() );
        cpuHandle.ptr += index * _pDevice->_cbvDescriptorSize;

        D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle( _pDevice->_cbvHeap->GetGPUDescriptorHandleForHeapStart() );
        gpuHandle.ptr += index * _pDevice->_cbvDescriptorSize;

        _pDevice->_device->CreateShaderResourceView( pRes, &srvDesc, cpuHandle );

        if ( index >= _pDevice->_listRegisteredBindless.size() )
            _pDevice->_listRegisteredBindless.resize( index + 1 );
        _pDevice->_listRegisteredBindless[index]          = { pRes, cpuHandle, gpuHandle };
        _pDevice->_listRegisteredBindless[index]._texture = texture;

        return index;
    }

    RHIDescriptorIndex D3D12RHIResource::registerBindlessResource( RHIBufferHandle buffer )
    {
        if ( buffer == 0 || _pDevice->_cbvHeap == nullptr )
            return kInvalidDescriptorIndex;

        auto* pRes = _pDevice->resolveBuffer( buffer );
        if ( pRes == nullptr )
            return kInvalidDescriptorIndex;

        std::unique_lock<std::shared_mutex> lock{ _pDevice->_bindlessMutex };
        RHIDescriptorIndex                  index;
        if ( _pDevice->_listFreeBindless.empty() == false )
        {
            index = _pDevice->_listFreeBindless.back();
            _pDevice->_listFreeBindless.pop_back();
        }
        else
        {
            if ( _pDevice->_allocatedDescriptorsCount >= D3D12RHIDevice::kMaxShaderVisibleDescriptors )
            {
                SW_LOG_ERROR( "Shader visible descriptor heap overflow! Max: %#", D3D12RHIDevice::kMaxShaderVisibleDescriptors );
                return kInvalidDescriptorIndex;
            }
            index = _pDevice->_allocatedDescriptorsCount++;
        }

        D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle( _pDevice->_cbvHeap->GetCPUDescriptorHandleForHeapStart() );
        cpuHandle.ptr += index * _pDevice->_cbvDescriptorSize;

        D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle( _pDevice->_cbvHeap->GetGPUDescriptorHandleForHeapStart() );
        gpuHandle.ptr += index * _pDevice->_cbvDescriptorSize;

        // 구조 버퍼면 StructuredBuffer SRV (ResourceDescriptorHeap[idx] 가 StructuredBuffer<T> 로 읽힘).
        // 그 외(상수 버퍼 ring)면 CBV.
        const auto strideIt = _pDevice->_mapStructuredStride.find( buffer );
        if ( strideIt != _pDevice->_mapStructuredStride.end() && strideIt->second > 0 )
        {
            const UINT                      stride = strideIt->second;
            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
            srvDesc.ViewDimension              = D3D12_SRV_DIMENSION_BUFFER;
            srvDesc.Shader4ComponentMapping    = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Format                     = DXGI_FORMAT_UNKNOWN;
            srvDesc.Buffer.FirstElement        = 0;
            srvDesc.Buffer.NumElements         = static_cast<UINT>( pRes->GetDesc().Width ) / stride;
            srvDesc.Buffer.StructureByteStride = stride;
            srvDesc.Buffer.Flags               = D3D12_BUFFER_SRV_FLAG_NONE;
            _pDevice->_device->CreateShaderResourceView( pRes, &srvDesc, cpuHandle );

            if ( index >= _pDevice->_listRegisteredBindless.size() )
                _pDevice->_listRegisteredBindless.resize( index + 1 );
            _pDevice->_listRegisteredBindless[index]         = { pRes, cpuHandle, gpuHandle };
            _pDevice->_listRegisteredBindless[index]._buffer = buffer;
            return index;
        }

        D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc{};
        cbvDesc.BufferLocation = pRes->GetGPUVirtualAddress();

        const auto sizeIt = _pDevice->_mapCbAlignedSize.find( buffer );
        if ( sizeIt != _pDevice->_mapCbAlignedSize.end() )
        {
            cbvDesc.BufferLocation += static_cast<UINT64>( _pDevice->_frameRing.currentIndex() ) * sizeIt->second;
            cbvDesc.SizeInBytes = sizeIt->second;
        }
        else
        {
            // Non-ring buffers: CBV size must be 256-byte aligned and <= resource width.
            const UINT width = static_cast<UINT>( pRes->GetDesc().Width );
            // 텍스처 행 정렬(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT) — 상수버퍼 정렬과 이름만 같은 별개 값이다.
            const UINT aligned  = MathUtil::align( width, 256u );
            cbvDesc.SizeInBytes = ( aligned <= width ) ? aligned : ( width & ~255u );
            if ( cbvDesc.SizeInBytes == 0 )
                return kInvalidDescriptorIndex;
        }

        _pDevice->_device->CreateConstantBufferView( &cbvDesc, cpuHandle );

        if ( index >= _pDevice->_listRegisteredBindless.size() )
            _pDevice->_listRegisteredBindless.resize( index + 1 );
        _pDevice->_listRegisteredBindless[index]         = { pRes, cpuHandle, gpuHandle };
        _pDevice->_listRegisteredBindless[index]._buffer = buffer;

        return index;
    }

    void D3D12RHIResource::unregisterBindlessResource( RHIDescriptorIndex index )
    {
        std::unique_lock<std::shared_mutex> lock{ _pDevice->_bindlessMutex };
        if ( index < _pDevice->_listRegisteredBindless.size() )
        {
            _pDevice->_listRegisteredBindless[index]._resource = nullptr;
            _pDevice->_listRegisteredBindless[index]._buffer   = 0;
            _pDevice->_listRegisteredBindless[index]._texture  = 0;
            _pDevice->_listFreeBindless.push_back( index );
        }
    }

    RHIDescriptorIndex D3D12RHIResource::registerBindlessUAV( RHIBufferHandle buffer )
    {
        if ( buffer == 0 || _pDevice->_cbvHeap == nullptr )
            return kInvalidDescriptorIndex;

        ID3D12Resource* pRes = _pDevice->resolveBuffer( buffer );
        if ( pRes == nullptr )
            return kInvalidDescriptorIndex;

        std::unique_lock<std::shared_mutex> lock{ _pDevice->_bindlessMutex };
        RHIDescriptorIndex                  descriptorIndex{ 0 };
        bool                                bReuseHeapSlot = false;
        if ( _pDevice->_listFreeUav.empty() == false )
        {
            descriptorIndex = _pDevice->_listFreeUav.back();
            _pDevice->_listFreeUav.pop_back();
            bReuseHeapSlot = true;
        }
        else
        {
            descriptorIndex = static_cast<RHIDescriptorIndex>( _pDevice->_listRegisteredUAV.size() );
            _pDevice->_listRegisteredUAV.push_back( D3D12RHIDevice::BindlessResourceRecord{} );
        }

        D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle{};
        D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle{};
        if ( bReuseHeapSlot && descriptorIndex < _pDevice->_listRegisteredUAV.size() &&
             _pDevice->_listRegisteredUAV[descriptorIndex]._cpuHandle.ptr != 0 )
        {
            cpuHandle = _pDevice->_listRegisteredUAV[descriptorIndex]._cpuHandle;
            gpuHandle = _pDevice->_listRegisteredUAV[descriptorIndex]._gpuHandle;
        }
        else
        {
            if ( _pDevice->_allocatedDescriptorsCount >= D3D12RHIDevice::kMaxShaderVisibleDescriptors )
            {
                SW_LOG_ERROR( "Shader visible descriptor heap overflow! Max: %#", D3D12RHIDevice::kMaxShaderVisibleDescriptors );
                return kInvalidDescriptorIndex;
            }
            const RHIDescriptorIndex heapSlot = _pDevice->_allocatedDescriptorsCount++;
            cpuHandle                         = _pDevice->_cbvHeap->GetCPUDescriptorHandleForHeapStart();
            cpuHandle.ptr += heapSlot * _pDevice->_cbvDescriptorSize;
            gpuHandle = _pDevice->_cbvHeap->GetGPUDescriptorHandleForHeapStart();
            gpuHandle.ptr += heapSlot * _pDevice->_cbvDescriptorSize;
        }

        if ( pRes->GetDesc().Width < 4 )
            return kInvalidDescriptorIndex;

        // RWByteAddressBuffer / RAW UAV: R32_TYPELESS + RAW, StructureByteStride must be 0.
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
        uavDesc.ViewDimension              = D3D12_UAV_DIMENSION_BUFFER;
        uavDesc.Format                     = DXGI_FORMAT_R32_TYPELESS;
        uavDesc.Buffer.FirstElement        = 0;
        uavDesc.Buffer.NumElements         = static_cast<UINT>( pRes->GetDesc().Width / 4 );
        uavDesc.Buffer.StructureByteStride = 0;
        uavDesc.Buffer.Flags               = D3D12_BUFFER_UAV_FLAG_RAW;

        _pDevice->_device->CreateUnorderedAccessView( pRes, nullptr, &uavDesc, cpuHandle );

        if ( descriptorIndex >= _pDevice->_listRegisteredUAV.size() )
            _pDevice->_listRegisteredUAV.resize( descriptorIndex + 1 );
        _pDevice->_listRegisteredUAV[descriptorIndex]         = { pRes, cpuHandle, gpuHandle };
        _pDevice->_listRegisteredUAV[descriptorIndex]._buffer = buffer;

        return descriptorIndex;
    }

    void D3D12RHIResource::unregisterBindlessUAV( RHIDescriptorIndex index )
    {
        std::unique_lock<std::shared_mutex> lock{ _pDevice->_bindlessMutex };
        if ( index < _pDevice->_listRegisteredUAV.size() )
        {
            _pDevice->_listRegisteredUAV[index]._resource = nullptr;
            _pDevice->_listRegisteredUAV[index]._buffer   = 0;
            _pDevice->_listRegisteredUAV[index]._texture  = 0;
            _pDevice->_listFreeUav.push_back( index );
        }
    }

} // namespace sw
#endif

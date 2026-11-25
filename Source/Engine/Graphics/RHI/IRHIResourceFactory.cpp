#include "pch.h"

#include "Engine/Graphics/RHI/IRHIResourceFactory.h"

#include "Core/Foundation/EnumUtil.h"
#include "Core/Time/BusyClock.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/RHI/Diagnostics/RHIUploadLog.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/RHICapabilities.h"
#include "Engine/Graphics/RHI/RHICompiledShaderStages.h"
#include "Engine/Graphics/RHI/RHIPipelineStateDesc.h"
#include "Engine/Graphics/Shader/Compile/RHIShaderRequest.h"
#include "Engine/Graphics/Shader/Compile/ShaderCache.h"

namespace sw
{
    RHICompiledGraphicsStages::RHICompiledGraphicsStages() noexcept
        : _vertex{}
        , _pixel{}
        , _geometry{}
        , _hull{}
        , _domain{}
        , _compute{}
        , _numRenderTargets{ 1 }
        , _consumedAttributeMask{ 0 }
        , _vertexSlotMask{ 0 }
        , _bHasVertexShader{ SW_FALSE }
        , _bHasPixelShader{ SW_FALSE }
        , _bDepthOnly{ SW_FALSE }
        , _bHasGeometryShader{ SW_FALSE }
        , _bHasTessellation{ SW_FALSE }
        , _bHasComputeShader{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    bool IRHIResourceFactory::isValidBufferDesc( const RHIBufferDesc& desc, const utf8* pCaller )
    {
        const bool bOtherKind = EnumUtil::hasAnyFlag( desc._usage, RHIBufferUsage::Vertex | RHIBufferUsage::Index | RHIBufferUsage::Constant );
        if ( bOtherKind )
        {
            SW_LOG_ERROR_ID( "RHI.BufferDescOtherKind", "%#: vertex, index and constant buffers have their own create functions (usage %#)", pCaller, static_cast<uint32>( desc._usage ) );
            return false;
        }
        const bool bSized = desc._elementSize != 0 && desc._elementCount != 0 && static_cast<uint64>( desc._elementSize ) * desc._elementCount == desc._sizeBytes;
        if ( bSized == false )
        {
            SW_LOG_ERROR_ID( "RHI.BufferDescSize", "%#: the buffer desc needs element size and count and size bytes = size x count (%# x %# != %#)", pCaller, desc._elementSize, desc._elementCount,
                             desc._sizeBytes );
            return false;
        }
        return true;
    }

    RHIBufferHandle IRHIResourceFactory::createBuffer( const RHIBufferDesc& desc )
    {
        if ( isValidBufferDesc( desc, "createBuffer" ) == false )
            return 0;
        const RHIBufferHandle buffer = createStructuredBuffer( desc._elementSize, desc._elementCount );
        if ( buffer != 0 && desc._pInitialData != nullptr )
            updateStructuredBuffer( buffer, desc._pInitialData, desc._sizeBytes );
        return buffer;
    }

    void IRHIResourceFactory::bindDevice( IRHIDevice& device, ShaderTargetFormat shaderTargetFormat, const RHICapabilities& startupCapabilities )
    {
        _pUploadDevice        = &device;
        _shaderTargetFormat   = shaderTargetFormat;
        _pStartupCapabilities = &startupCapabilities;
    }

    void IRHIResourceFactory::beginBufferUpload( RHIBufferHandle buffer, const utf8* pWhat )
    {
        if ( _pUploadDevice != nullptr )
            _pUploadDevice->beginBufferUpload( buffer, pWhat );
    }

    RHIBufferHandle IRHIResourceFactory::recordContent( RHIBufferHandle buffer, const void* pContent )
    {
        if constexpr ( build::kShipping == false )
        {
            if ( _pUploadDevice != nullptr && pContent != nullptr )
                _pUploadDevice->getBufferUploadLog().recordContent( buffer );
        }
        return buffer;
    }

    void IRHIResourceFactory::updateStructuredBufferRegions( RHIBufferHandle buffer, const void* pBaseSource, const RHIBufferCopyRegion* pRegions, uint32 regionCount )
    {
        beginBufferUpload( buffer, "updateStructuredBufferRegions" );
        updateStructuredBufferRegionsNative( buffer, pBaseSource, pRegions, regionCount );
    }

    void IRHIResourceFactory::updateGeometryBufferRegions( RHIBufferHandle buffer, const void* pBaseSource, const RHIBufferCopyRegion* pRegions, uint32 regionCount )
    {
        beginBufferUpload( buffer, "updateGeometryBufferRegions" );
        updateGeometryBufferRegionsNative( buffer, pBaseSource, pRegions, regionCount );
    }

    RHITransferSubmit IRHIResourceFactory::submitTextureUpload( RHITextureHandle texture, const RHITextureUploadDesc& desc )
    {
        if ( _pUploadDevice != nullptr )
            _pUploadDevice->beginTextureUpload( texture, "submitTextureUpload" );
        return submitTextureUploadNative( texture, desc );
    }

    RHITransferSubmit IRHIResourceFactory::submitTextureRegionUpload( RHITextureHandle texture, const RHITextureRegionUploadDesc& desc )
    {
        if ( _pUploadDevice != nullptr )
            _pUploadDevice->beginTextureUpload( texture, "submitTextureRegionUpload" );
        return submitTextureRegionUploadNative( texture, desc );
    }

    RHITransferSubmit IRHIResourceFactory::submitTextureReadback( RHITextureHandle texture, uint32 mip, uint32 arraySlice, RHITextureMipSpan& outLayout )
    {
        if ( _pUploadDevice != nullptr )
            _pUploadDevice->beginTextureReadback( texture, "submitTextureReadback" );
        return submitTextureReadbackNative( texture, mip, arraySlice, outLayout );
    }

    RHIPipelineStateHandle IRHIResourceFactory::createPipelineState( RHIResourceCreateKey, const RHIPipelineStateDesc& desc )
    {
        if ( _pStartupCapabilities == nullptr )
        {
            SW_LOG_ERROR_ID( "RHI.PipelineFrontUnbound", "createPipelineState before the device bound the factory (IRHIDevice::initialize) - '%#'", desc._vertexShaderPath );
            return 0;
        }
        BusyScope                 busy{ engine::getSubsystem<ShaderCache>().getCompileBusyClock() };
        RHICompiledGraphicsStages stages;
        if ( RHIShaderRequest::buildGraphicsStages( desc, _shaderTargetFormat, *_pStartupCapabilities, stages ) == false )
            return 0;
        const RHIPipelineStateHandle pso = createPipelineStateNative( desc, stages );
        if ( stages._bHasComputeShader != SW_FALSE )
            recordThreadGroupSize( pso, stages._compute._groupSize );
        return pso;
    }

    RHIPipelineStateHandle IRHIResourceFactory::createComputePipelineState( RHIResourceCreateKey, string_view shaderPath, string_view entryPoint, const vector<string>& listDefine )
    {
        if ( _pStartupCapabilities == nullptr )
        {
            SW_LOG_ERROR_ID( "RHI.PipelineFrontUnbound", "createComputePipelineState before the device bound the factory (IRHIDevice::initialize) - '%#'", shaderPath );
            return 0;
        }
        if ( shaderPath.empty() ) // 빈 레코드를 등록하지 않는다(예전 다섯 백엔드와 같은 답)
            return 0;
        BusyScope              busy{ engine::getSubsystem<ShaderCache>().getCompileBusyClock() };
        RHICompiledShaderStage computeStage;
        if ( RHIShaderRequest::buildComputeStage( shaderPath, entryPoint, listDefine, _shaderTargetFormat, computeStage ) == false )
            return 0;
        const RHIPipelineStateHandle pso = createComputePipelineStateNative( computeStage, shaderPath );
        recordThreadGroupSize( pso, computeStage._groupSize );
        return pso;
    }

    RHIThreadGroupSize IRHIResourceFactory::getThreadGroupSize( RHIPipelineStateHandle pso ) const
    {
        ScopedLock<mutex> lock{ _groupSizeMutex };
        const auto        it = _mapGroupSize.find( pso.getPacked() );
        return it != _mapGroupSize.end() ? it->second : RHIThreadGroupSize{};
    }

    void IRHIResourceFactory::recordThreadGroupSize( RHIPipelineStateHandle pso, const RHIThreadGroupSize& groupSize )
    {
        if ( pso == 0 )
            return;
        ScopedLock<mutex> lock{ _groupSizeMutex };
        _mapGroupSize[pso.getPacked()] = groupSize;
    }

    void IRHIResourceFactory::removeThreadGroupSize( RHIPipelineStateHandle pso )
    {
        ScopedLock<mutex> lock{ _groupSizeMutex };
        _mapGroupSize.erase( pso.getPacked() );
    }
} // namespace sw

#include "pch.h"

#include "Engine/Graphics/RHI/IRHIResourceFactory.h"

#include "Core/Diagnostics/CallStackCapture.h"
#include "Core/Foundation/EnumUtil.h"
#include "Core/Time/BusyClock.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/RHI/Backend/RHIBufferSizeUtil.h"
#include "Engine/Graphics/RHI/Diagnostics/RHIMemoryTracker.h"
#include "Engine/Graphics/RHI/Diagnostics/RHIUploadLog.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/RHICapabilities.h"
#include "Engine/Graphics/RHI/RHICompiledShaderStages.h"
#include "Engine/Graphics/RHI/RHIFormatInfo.h"
#include "Engine/Graphics/RHI/RHIPipelineStateDesc.h"
#include "Engine/Graphics/Shader/Compile/RHIShaderRequest.h"
#include "Engine/Graphics/Shader/Compile/ShaderCache.h"

namespace sw
{
    namespace
    {
        struct RHIResourceFactoryInternal
        {
            static bool isValidBytes( uint64 sizeBytes, const utf8* pCaller )
            {
                if ( sizeBytes != 0 )
                    return true;
                SW_LOG_ERROR_ID( "RHI.BufferSizeZero", "%#: a resource of zero bytes cannot be created", pCaller );
                return false;
            }
            /** @brief 원소 크기 × 개수가 0 이 아니고 32비트에 담기는지 봅니다(`RHIBufferSizeUtil` 이 넘침을 알린다). */
            static bool isValidElements( uint32 elementSize, uint32 elementCount, const utf8* pCaller )
            {
                uint32 totalBytes{ 0 };
                return RHIBufferSizeUtil::computeStructuredBytes( elementSize, elementCount, totalBytes ) && isValidBytes( totalBytes, pCaller );
            }
            static bool isValidContent( const void* pData, uint32 sizeBytes, const utf8* pCaller )
            {
                if ( pData != nullptr )
                    return isValidBytes( sizeBytes, pCaller );
                SW_LOG_ERROR_ID( "RHI.BufferContentMissing", "%#: this buffer is created with its content, but none was given (%# bytes)", pCaller, sizeBytes );
                return false;
            }
            /** @brief 검사한 자원의 등록이 무효 번호면 알리고, 번호를 그대로 돌려줍니다. */
            template <typename IndexType>
            static IndexType validateIndex( IndexType index, const utf8* pCaller, uint64 handle )
            {
                if ( index == kInvalidDescriptorIndex )
                    SW_LOG_ERROR_ID( "RHI.BindlessRegisterFailed", "%#: no index for resource %# (unknown handle, full table, or the backend could not create the view)", pCaller, handle );
                return index;
            }
            static void reportIndexNotOwned( bool bReleased, const utf8* pCaller, uint32 index )
            {
                if ( bReleased == false )
                    SW_LOG_ERROR_ID( "RHI.BindlessIndexNotOwned", "%#: index %# is not a live index of this table - released twice, or an index of another kind", pCaller, index );
            }
        };
    } // namespace

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

    RHIBufferHandle IRHIResourceFactory::createBufferNative( const RHIBufferDesc& desc )
    {
        const RHIBufferHandle buffer = createStructuredBufferNative( desc._elementSize, desc._elementCount );
        if ( buffer != 0 && desc._pInitialData != nullptr )
            updateStructuredBuffer( buffer, desc._pInitialData, desc._sizeBytes );
        return buffer;
    }

    RHIBufferHandle IRHIResourceFactory::createBuffer( RHIResourceCreateKey, const RHIBufferDesc& desc )
    {
        return isValidBufferDesc( desc, "createBuffer" ) ? recordContent( createBufferNative( desc ), desc._pInitialData ) : RHIBufferHandle{ 0 };
    }

    RHIBufferHandle IRHIResourceFactory::createConstantBuffer( RHIResourceCreateKey, uint32 size )
    {
        return RHIResourceFactoryInternal::isValidBytes( size, "createConstantBuffer" ) ? createConstantBufferNative( size ) : RHIBufferHandle{ 0 };
    }

    RHIBufferHandle IRHIResourceFactory::createConstantBufferPage( RHIResourceCreateKey, uint32 size )
    {
        return RHIResourceFactoryInternal::isValidBytes( size, "createConstantBufferPage" ) ? createConstantBufferPageNative( size ) : RHIBufferHandle{ 0 };
    }

    RHIBufferHandle IRHIResourceFactory::createStructuredBuffer( RHIResourceCreateKey, uint32 elementSize, uint32 elementCount )
    {
        return RHIResourceFactoryInternal::isValidElements( elementSize, elementCount, "createStructuredBuffer" ) ? createStructuredBufferNative( elementSize, elementCount ) : RHIBufferHandle{ 0 };
    }

    RHIBufferHandle IRHIResourceFactory::createVertexBuffer( RHIResourceCreateKey, const void* pData, uint32 sizeBytes )
    {
        return RHIResourceFactoryInternal::isValidContent( pData, sizeBytes, "createVertexBuffer" ) ? recordContent( createVertexBufferNative( pData, sizeBytes ), pData ) : RHIBufferHandle{ 0 };
    }

    RHIBufferHandle IRHIResourceFactory::createGPUVertexBuffer( RHIResourceCreateKey, uint32 sizeBytes )
    {
        return RHIResourceFactoryInternal::isValidBytes( sizeBytes, "createGPUVertexBuffer" ) ? createGPUVertexBufferNative( sizeBytes ) : RHIBufferHandle{ 0 };
    }

    RHIBufferHandle IRHIResourceFactory::createIndexBuffer( RHIResourceCreateKey, const void* pData, uint32 sizeBytes )
    {
        return RHIResourceFactoryInternal::isValidContent( pData, sizeBytes, "createIndexBuffer" ) ? recordContent( createIndexBufferNative( pData, sizeBytes ), pData ) : RHIBufferHandle{ 0 };
    }

    RHIBufferHandle IRHIResourceFactory::createReadbackBuffer( RHIResourceCreateKey, uint32 elementSize, uint32 elementCount )
    {
        return RHIResourceFactoryInternal::isValidElements( elementSize, elementCount, "createReadbackBuffer" ) ? createReadbackBufferNative( elementSize, elementCount ) : RHIBufferHandle{ 0 };
    }

    bool IRHIResourceFactory::readReadbackBuffer( RHIBufferHandle readback, uint32 sizeBytes, void* pDestination )
    {
        if ( pDestination == nullptr || sizeBytes == 0 )
        {
            SW_LOG_ERROR_ID( "RHI.ReadbackDestinationMissing", "readReadbackBuffer: buffer %# needs a destination and a size (%# bytes)", readback.getPacked(), sizeBytes );
            return false;
        }
        return readReadbackBufferNative( readback, sizeBytes, pDestination );
    }

    bool IRHIResourceFactory::validateTextureDesc( const RHITextureDesc& desc, const utf8* pCaller ) const
    {
        if ( isRHITextureShapeValid( desc ) == false )
        {
            SW_LOG_ERROR_ID( "RHI.TextureShapeInvalid", "%#: dimension %# with %# slices and %# mips (%#x%#) is not a valid texture shape", pCaller, static_cast<uint32>( desc._dimension ), desc._arraySize,
                             desc._mipLevels, desc._width, desc._height );
            return false;
        }
        // 이 장치가 샘플링하지 못하는 압축 형식(데스크톱의 ASTC · ETC2)은 드라이버에 넘기지 않는다 — 넘기면 D3D12 는 실패, Vulkan 드라이버는 0 나누기로 죽었다.
        const bool bUnsupported = RHIFormatUtil::isBlockCompressed( desc._format ) && _pUploadDevice != nullptr && _pUploadDevice->supportsTextureFormat( desc._format ) == false;
        if ( bUnsupported )
        {
            SW_LOG_ERROR_ID( "RHI.TextureFormatUnsupported", "%#: this device cannot sample format %# (%#x%#) - the texture is not created", pCaller, static_cast<uint32>( desc._format ), desc._width, desc._height );
            return false;
        }
        return true;
    }

    RHITextureHandle IRHIResourceFactory::createTexture2D( RHIResourceCreateKey, const RHITextureDesc& desc )
    {
        return validateTextureDesc( desc, "createTexture2D" ) ? createTexture2DNative( desc ) : RHITextureHandle{ 0 };
    }

    RHITextureAllocationInfo IRHIResourceFactory::getTextureAllocationInfo( const RHITextureDesc& desc )
    {
        return validateTextureDesc( desc, "getTextureAllocationInfo" ) ? getTextureAllocationInfoNative( desc ) : RHITextureAllocationInfo{};
    }

    RHITransientHeapHandle IRHIResourceFactory::createTransientHeap( RHIResourceCreateKey, uint64 sizeBytes )
    {
        return RHIResourceFactoryInternal::isValidBytes( sizeBytes, "createTransientHeap" ) ? createTransientHeapNative( sizeBytes ) : RHITransientHeapHandle{ 0 };
    }

    RHITextureHandle IRHIResourceFactory::createPlacedTexture2D( RHIResourceCreateKey, const RHITextureDesc& desc, RHITransientHeapHandle heap, uint64 offsetBytes )
    {
        return validateTextureDesc( desc, "createPlacedTexture2D" ) ? createPlacedTexture2DNative( desc, heap, offsetBytes ) : RHITextureHandle{ 0 };
    }

    void IRHIResourceFactory::setTextureDebugName( RHITextureHandle texture, const utf8* pName )
    {
        if ( texture == 0 || pName == nullptr )
            return;
        if ( _pUploadDevice != nullptr )
            _pUploadDevice->getMemoryTracker().setLabel( RHIMemoryKey::makeTexture( texture ), pName );
        setTextureDebugNameNative( texture, pName );
    }

    void IRHIResourceFactory::requireRegistryMutable( const utf8* pCaller ) const
    {
        if ( _pUploadDevice != nullptr )
            _pUploadDevice->requireRegistryMutableNow( pCaller );
    }

    bool IRHIResourceFactory::validateBindlessTexture( RHITextureHandle texture, const utf8* pCaller )
    {
        RHITextureBindingInfo info{};
        if ( describeTextureNative( texture, info ) == false )
        {
            SW_LOG_ERROR_ID( "RHI.BindlessTextureUnknown", "%#: %# is not a live texture", pCaller, texture.getPacked() );
            return false;
        }
        // 셰이더의 텍스처 표 · 슬롯은 Texture2D 로 선언돼 있다 — 배열 · 큐브 뷰를 넣으면 셰이더가 읽지 못한다(RHITextureDimension).
        if ( info._dimension != RHITextureDimension::Texture2D )
        {
            SW_LOG_ERROR_ID( "RHI.BindlessTextureNotTexture2D", "%#: texture %# is a %# texture - only Texture2D can be read through the texture table", pCaller, texture.getPacked(),
                             static_cast<uint32>( info._dimension ) );
            return false;
        }
        if ( info._bShaderResource == SW_FALSE )
        {
            SW_LOG_ERROR_ID( "RHI.BindlessTextureNotShaderResource", "%#: texture %# was created without _bIsShaderResource", pCaller, texture.getPacked() );
            return false;
        }
        if ( info._bFirstUploadComplete == SW_FALSE )
        {
            // 부른 자리를 함께 남긴다 — 텍스처를 넘긴 쪽(패널 · 캐시)이 곧 완료를 기다리지 않은 쪽이다.
            CallStack stack{};
            CallStackCapture::capture( stack, 2 );
            SW_LOG_ERROR_ID( "RHI.RegisterBeforeTransferDone", "%#: texture %# has an unfinished first upload: register only after pollTransfer returns Complete\n%#", pCaller, texture.getPacked(),
                             CallStackCapture::symbolize( stack ).c_str() );
            return false;
        }
        return true;
    }

    RHITextureSRVIndex IRHIResourceFactory::registerBindlessTexture( RHITextureHandle texture )
    {
        requireRegistryMutable( "registerBindlessTexture" );
        if ( validateBindlessTexture( texture, "registerBindlessTexture" ) == false )
            return kInvalidDescriptorIndex;
        return RHIResourceFactoryInternal::validateIndex( registerBindlessTextureNative( texture ), "registerBindlessTexture", texture.getPacked() );
    }

    bool IRHIResourceFactory::rebindBindlessTexture( RHITextureSRVIndex index, RHITextureHandle texture )
    {
        requireRegistryMutable( "rebindBindlessTexture" );
        return validateBindlessTexture( texture, "rebindBindlessTexture" ) && rebindBindlessTextureNative( index, texture );
    }

    RHITextureSRVIndex IRHIResourceFactory::reserveBindlessTextureIndex()
    {
        return RHIResourceFactoryInternal::validateIndex( reserveBindlessTextureIndexNative(), "reserveBindlessTextureIndex", 0 );
    }

    bool IRHIResourceFactory::activateReservedBindlessTexture( RHITextureSRVIndex index, RHITextureHandle texture )
    {
        requireRegistryMutable( "activateReservedBindlessTexture" );
        return validateBindlessTexture( texture, "activateReservedBindlessTexture" ) && activateReservedBindlessTextureNative( index, texture );
    }

    bool IRHIResourceFactory::isTextureReadyToSample( RHITextureHandle texture )
    {
        RHITextureBindingInfo info{};
        return describeTextureNative( texture, info ) && info._bFirstUploadComplete != SW_FALSE;
    }

    void IRHIResourceFactory::unregisterBindlessTexture( RHITextureSRVIndex index )
    {
        requireRegistryMutable( "unregisterBindlessTexture" );
        RHIResourceFactoryInternal::reportIndexNotOwned( unregisterBindlessTextureNative( index ), "unregisterBindlessTexture", index.getValue() );
    }

    RHIBufferSRVIndex IRHIResourceFactory::registerBindlessBufferSRV( RHIBufferHandle buffer )
    {
        requireRegistryMutable( "registerBindlessBufferSRV" );
        return RHIResourceFactoryInternal::validateIndex( registerBindlessBufferSRVNative( buffer ), "registerBindlessBufferSRV", buffer.getPacked() );
    }

    RHIBufferSRVIndex IRHIResourceFactory::registerBindlessBufferSRV( const RHIBufferSlice& slice )
    {
        requireRegistryMutable( "registerBindlessBufferSRV(slice)" );
        // 구간은 256 B 배수에서 시작하고 페이지(프레임 칸 하나) 안이다: DX12 CBV 주소 · Vulkan minUniformBufferOffsetAlignment 상한 · DX11 16 상수 · GL · WebGPU 균일 버퍼 오프셋이 모두 256 을 받는다.
        const uint32 pageBytes = findConstantPageBytesNative( slice._buffer );
        const bool   bInside   = static_cast<uint64>( slice._offset ) + slice._size <= pageBytes;
        const bool   bValid    = pageBytes != 0 && bInside && slice._size != 0 && slice._offset % constant::render::kConstantBufferAlignment == 0;
        if ( bValid == false )
        {
            SW_LOG_ERROR_ID( "RHI.BadConstantBufferSlice", "registerBindlessBufferSRV: slice (offset %#, size %#) is not a %#-aligned slice of a constant buffer page (%# bytes)", slice._offset, slice._size,
                             constant::render::kConstantBufferAlignment, pageBytes );
            return kInvalidDescriptorIndex;
        }
        return RHIResourceFactoryInternal::validateIndex( registerBindlessBufferSRVNative( slice ), "registerBindlessBufferSRV(slice)", slice._buffer.getPacked() );
    }

    void IRHIResourceFactory::unregisterBindlessResource( RHIBufferSRVIndex index )
    {
        requireRegistryMutable( "unregisterBindlessResource" );
        RHIResourceFactoryInternal::reportIndexNotOwned( unregisterBindlessResourceNative( index ), "unregisterBindlessResource", index.getValue() );
    }

    RHIUnorderedAccessIndex IRHIResourceFactory::registerBindlessBufferUAV( RHIBufferHandle buffer )
    {
        requireRegistryMutable( "registerBindlessBufferUAV" );
        return RHIResourceFactoryInternal::validateIndex( registerBindlessBufferUAVNative( buffer ), "registerBindlessBufferUAV", buffer.getPacked() );
    }

    RHIUnorderedAccessIndex IRHIResourceFactory::registerBindlessTextureUAV( RHITextureHandle texture )
    {
        requireRegistryMutable( "registerBindlessTextureUAV" );
        RHITextureBindingInfo info{};
        if ( describeTextureNative( texture, info ) == false )
        {
            SW_LOG_ERROR_ID( "RHI.BindlessTextureUnknown", "%#: %# is not a live texture", "registerBindlessTextureUAV", texture.getPacked() );
            return kInvalidDescriptorIndex;
        }
        if ( info._bUnorderedAccess == SW_FALSE )
        {
            SW_LOG_ERROR_ID( "RHI.BindlessTextureNotUnorderedAccess", "registerBindlessTextureUAV: texture %# was created without _bIsUnorderedAccess", texture.getPacked() );
            return kInvalidDescriptorIndex;
        }
        return RHIResourceFactoryInternal::validateIndex( registerBindlessTextureUAVNative( texture ), "registerBindlessTextureUAV", texture.getPacked() );
    }

    void IRHIResourceFactory::unregisterBindlessUAV( RHIUnorderedAccessIndex index )
    {
        requireRegistryMutable( "unregisterBindlessUAV" );
        RHIResourceFactoryInternal::reportIndexNotOwned( unregisterBindlessUAVNative( index ), "unregisterBindlessUAV", index.getValue() );
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

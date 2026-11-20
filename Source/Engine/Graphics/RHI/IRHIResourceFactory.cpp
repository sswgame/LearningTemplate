#include "pch.h"

#include "Engine/Graphics/RHI/IRHIResourceFactory.h"

#include "Core/Time/BusyClock.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/RHI/Backend/RHIShaderRequest.h"
#include "Engine/Graphics/RHI/RHICapabilities.h"
#include "Engine/Graphics/RHI/RHICompiledShaderStages.h"
#include "Engine/Graphics/RHI/RHIPipelineStateDesc.h"
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

    void IRHIResourceFactory::bindPipelineFront( ShaderTargetFormat shaderTargetFormat, const RHICapabilities& startupCapabilities )
    {
        _shaderTargetFormat   = shaderTargetFormat;
        _pStartupCapabilities = &startupCapabilities;
    }

    RHIPipelineStateHandle IRHIResourceFactory::createPipelineState( const RHIPipelineStateDesc& desc )
    {
        if ( _pStartupCapabilities == nullptr )
        {
            SW_LOG_ERROR_ID( "RHI.PipelineFrontUnbound", "createPipelineState before the device bound the pipeline front (IRHIDevice::initialize) - '%#'", desc._vertexShaderPath );
            return 0;
        }
        BusyScope                 busy{ engine::getSubsystem<ShaderCache>().getCompileBusyClock() };
        RHICompiledGraphicsStages stages;
        if ( RHIShaderRequest::buildGraphicsStages( desc, _shaderTargetFormat, *_pStartupCapabilities, stages ) == false )
            return 0;
        return createPipelineStateNative( desc, stages );
    }

    RHIPipelineStateHandle IRHIResourceFactory::createComputePipelineState( string_view shaderPath, string_view entryPoint, const vector<string>& listDefine )
    {
        if ( _pStartupCapabilities == nullptr )
        {
            SW_LOG_ERROR_ID( "RHI.PipelineFrontUnbound", "createComputePipelineState before the device bound the pipeline front (IRHIDevice::initialize) - '%#'", shaderPath );
            return 0;
        }
        if ( shaderPath.empty() ) // 빈 레코드를 등록하지 않는다(예전 다섯 백엔드와 같은 답)
            return 0;
        BusyScope              busy{ engine::getSubsystem<ShaderCache>().getCompileBusyClock() };
        RHICompiledShaderStage computeStage;
        if ( RHIShaderRequest::buildComputeStage( shaderPath, entryPoint, listDefine, _shaderTargetFormat, computeStage ) == false )
            return 0;
        return createComputePipelineStateNative( computeStage, shaderPath );
    }
} // namespace sw

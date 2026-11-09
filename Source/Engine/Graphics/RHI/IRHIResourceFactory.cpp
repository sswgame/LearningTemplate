#include "pch.h"

#include "Engine/Graphics/RHI/IRHIResourceFactory.h"

#include "Core/Time/BusyClock.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/Shader/Compile/ShaderCache.h"

namespace sw
{
    RHIPipelineStateHandle IRHIResourceFactory::createPipelineState( const RHIPipelineStateDesc& desc )
    {
        BusyScope busy{ engine::getSubsystem<ShaderCache>().getCompileBusyClock() };
        return createPipelineStateNative( desc );
    }

    RHIPipelineStateHandle IRHIResourceFactory::createComputePipelineState( string_view shaderPath, string_view entryPoint, const vector<string>& listDefine )
    {
        BusyScope busy{ engine::getSubsystem<ShaderCache>().getCompileBusyClock() };
        return createComputePipelineStateNative( shaderPath, entryPoint, listDefine );
    }
} // namespace sw

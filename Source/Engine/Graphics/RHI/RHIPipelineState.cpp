#include "pch.h"

#include "Engine/Graphics/RHI/RHIPipelineState.h"

#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"

namespace sw
{
    bool RHIPipelineState::createGraphics( IRHIDevice& device, const RHIPipelineStateDesc& desc )
    {
        release( &device );
        _handle = device.getResourceFactory()->createPipelineState( RHIResourceCreateKey{}, desc );
        return isValid();
    }

    bool RHIPipelineState::createCompute( IRHIDevice& device, string_view shaderPath, string_view entryPoint, const vector<string>& listDefine )
    {
        release( &device );
        _handle = device.getResourceFactory()->createComputePipelineState( RHIResourceCreateKey{}, shaderPath, entryPoint, listDefine );
        return isValid();
    }

    void RHIPipelineState::release( IRHIDevice* pDevice )
    {
        if ( pDevice != nullptr && _handle != 0 )
            pDevice->releaseHandle( _handle );
        _handle = 0;
    }
} // namespace sw

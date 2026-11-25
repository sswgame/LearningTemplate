#include "pch.h"

#include "Engine/Renderer/Frame/FramePassContext.h"

#include "Engine/Graphics/RHI/IRHICommandList.h"

namespace sw
{
    void FramePassContext::beginRenderPass( const RHIRenderPassBeginInfo& beginInfo )
    {
        _pCmd->beginRenderPass( beginInfo );
        _boundPSO    = 0;
        _lastBindPSO = 0;
    }

    void FramePassContext::setPipeline( RHIPipelineStateHandle pso )
    {
        _pCmd->setPipelineState( pso );
        if ( _boundPSO == pso )
            return;
        _boundPSO    = pso;
        _lastBindPSO = 0;
    }
} // namespace sw

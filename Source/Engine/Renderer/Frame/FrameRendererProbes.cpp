/**
 * @file FrameRendererProbes.cpp
 * @brief 실시간 라이트 프로브 갱신(`RenderPassType::ProbeUpdate`)의 준비입니다. 본체는 `RealtimeProbeUpdater` 이고, 디스패치는 패스 실행의 컴퓨트 프리패스(`FramePassExecutor::dispatchRealtimeProbes`)가 한다.
 */
#include "pch.h"

#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Renderer/Frame/FrameRenderer.h"
#include "Engine/Renderer/Pipeline/RenderPassTypeInfo.h"

namespace sw
{
    void FrameRenderer::prepareRealtimeProbes( IRHIDevice* pDevice )
    {
        const bool bPipelineReady = _psoManager.getEnginePSO( RenderPassType::ProbeUpdate ) != 0;
        _state._probeUpdater.prepare( pDevice, _state._listFrameRealtimeProbe, _state._lightBuffer, _state._frameSkySource, _state._bHasFrameSky == SW_TRUE, bPipelineReady );
    }
} // namespace sw

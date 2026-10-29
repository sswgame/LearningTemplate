/**
 * @file FrameRendererProbes.cpp
 * @brief 실시간 라이트 프로브 갱신(`RenderPassType::ProbeUpdate`)의 준비와 디스패치입니다. 본체는 `RealtimeProbeUpdater` 이고 여기는 프레임 렌더러와 이어 주는 얇은 층입니다.
 */
#include "pch.h"

#include "Engine/Graphics/RHI/IRHICommandList.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Renderer/Frame/FrameRenderer.h"
#include "Engine/Renderer/Pipeline/RenderPassTypeInfo.h"

namespace sw
{
    void FrameRenderer::prepareRealtimeProbes( IRHIDevice* pDevice )
    {
        const bool bPipelineReady = _psoManager.getEnginePSO( RenderPassType::ProbeUpdate ) != 0;
        _state._probeUpdater.prepare( pDevice, _state._listFrameRealtimeProbe, _state._lightBuffer, _state._frameSkySource, _state._bHasFrameSky == SW_TRUE, bPipelineReady );
    }

    void FrameRenderer::dispatchRealtimeProbes()
    {
        if ( _pCmd == nullptr || _state._probeUpdater.isActive() == false )
            return;
        SW_PROFILE_SCOPE( "RT.Probe.Update" );
        _state._probeUpdater.dispatch( *_pCmd, _psoManager.getEnginePSO( RenderPassType::ProbeUpdate ), _state._lightBuffer, _transparentRankPlaceholder._srv );
    }
} // namespace sw

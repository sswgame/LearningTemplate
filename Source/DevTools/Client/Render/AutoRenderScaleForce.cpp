#include "pch.h"

#include "DevTools/Client/Render/AutoRenderScaleForce.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"

#include "Engine/Foundation/EngineSubsystems.h"
#include "Engine/Renderer/Frame/FrameRendererSubsystem.h"
#include "Engine/Renderer/Frame/Packet/RenderPacketBuilder.h"

namespace sw
{
    /** @brief `-gv_autoRenderScaleForce=S` — 자동 렌더 배율을 S(0 초과 1 이하)로 고정합니다. 0 이면 끔(조정기 경로). 시나리오 game/playground/automation/renderscalestep 이 쓴다. */
    SW_TEST_GLOBAL_VARIABLE( float32, gv_autoRenderScaleForce, 0.0f,
                             "Pin the auto render scale of the main view to this value (0..1] regardless of GPU time - deterministic scale steps for scenarios and measurement, 0 = off" );

    void AutoRenderScaleForce::apply()
    {
        FrameRendererSubsystem* const pSubsystem = engine::findSubsystem<FrameRendererSubsystem>();
        if ( pSubsystem == nullptr )
            return;
        const float32 forced = static_cast<float32>( gv_autoRenderScaleForce );
        pSubsystem->getPacketBuilder().setForcedAutoRenderScale( forced > 0.0f ? forced : 0.0f );
    }
} // namespace sw

/**
 * @file FrameFogWatches.cpp
 * @brief 볼륨 안개의 자동화 감시 값 `Render.FogVolumeState` 입니다(`FogVolumeState` 번호 — 0 안개 없음 · 1 꺼짐 · 2 물속 · 3 디바이스 없음 · 4 컴퓨트 PSO 없음 · 5 버퍼 실패 · 6 켜짐).
 * @details 시나리오만 읽는 값이라 안개 TU 가 아니라 이 파일에 둔다(`RenderViewWatches.cpp` 와 같은 모양). 값은 주 렌더러가 프레임마다 올린 것이다(`FrameFog::publishMainVolumeState`).
 */
#include "pch.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Renderer/Frame/PostProcess/FrameFog.h"

namespace sw
{
    namespace
    {
        [[nodiscard]] bool readFogVolumeState( const GameObjectManager* pManager, float64& outValue )
        {
            (void)pManager;
            outValue = static_cast<float64>( FrameFog::getLastMainVolumeState() );
            return true;
        }
    } // namespace

    SW_AUTOMATION_WATCH( renderFogVolumeState, "Render.FogVolumeState",
                         "Volumetric fog state of the main view last frame (0 no fog, 1 off in the settings, 2 underwater, 3 no device, 4 no compute PSO, 5 buffer failed, 6 active)",
                         &readFogVolumeState );
} // namespace sw

/**
 * @file ImGuiTextureUpdate.h
 * @brief ImGui 텍스처(글꼴 아틀라스 등) 갱신을 백엔드에 넘길지 정하는 판정입니다(네 렌더러 백엔드 공통).
 */
#pragma once
#include "Core/Common/Types.h"

#include "Engine/Graphics/RHI/RHITypes.h"

#include <imgui.h>

namespace sw::editor
{
    /**
     * @brief `ImTextureStatus_WantDestroy` 텍스처를 파괴하기 전에 기다릴 프레임 수입니다.
     * @details UI 스레드가 낸 draw 스냅샷(최대 `kMaxFrameCountInFlight` 장)을 렌더 스레드가 그리고, 그 GPU 작업이 또 `kMaxFrameCountInFlight`
     *          프레임까지 겹친다. 그 사이의 스냅샷이 옛 아틀라스를 아직 그릴 수 있으므로, ImGui 가 세는 미사용 프레임(`UnusedFrames`)이
     *          그 둘을 넘은 뒤에 파괴한다.
     */
    inline constexpr int32 kImGuiTextureDestroyDelayFrames = static_cast<int32>( constant::kMaxFrameCountInFlight * 2u + 1u );

    /**
     * @brief 이 텍스처의 갱신(생성 · 갱신 · 파괴)을 지금 백엔드에 넘길지 판정합니다.
     * @details 파괴는 `kImGuiTextureDestroyDelayFrames` 만큼 미룬다 — 바로 넘기면 백엔드가 이미지 · 디스크립터를 즉시 놓아, 그것을 그리는
     *          스냅샷이 아직 도는 렌더 스레드 · GPU 가 해제된 자원을 읽는다. ImGui 는 그동안 `WantDestroy` 로 두고 `UnusedFrames` 를 센다.
     */
    inline bool isTextureUpdateDue( const ImTextureData& texture )
    {
        if ( texture.Status == ImTextureStatus_OK || texture.Status == ImTextureStatus_Destroyed )
            return false;
        if ( texture.Status == ImTextureStatus_WantDestroy )
            return texture.UnusedFrames >= kImGuiTextureDestroyDelayFrames;
        return true;
    }
} // namespace sw::editor

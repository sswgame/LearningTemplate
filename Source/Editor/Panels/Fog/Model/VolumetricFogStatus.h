/**
 * @file VolumetricFogStatus.h
 * @brief 볼륨 안개 창의 상태 풀이입니다 — 상태마다 한 문장 · 무게(보통 · 켜짐 · 문제), 진단 보기를 쓸 수 있는지. ImGui 를 모릅니다.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Renderer/Frame/PostProcess/FrameFog.h"

namespace sw::editor
{
    /** @brief 상태 줄의 무게입니다. 색은 상태에만 쓴다(켜짐은 Success, 문제는 Warning, 그 밖은 흐린 글). */
    enum class VolumetricFogSeverity : uint8
    {
        Neutral, ///< 안개가 없거나 설정이 껐다 — 고칠 것이 아니라 고른 것이다
        Active,  ///< 볼륨이 돈다
        Problem, ///< 켜려 했는데 못 켰다(파이프라인 · 버퍼 · 디바이스)
    };

    /**
     * @struct VolumetricFogStatus
     * @brief 상태 풀이 모음입니다.
     */
    struct VolumetricFogStatus
    {
        /** @brief 상태를 한 문장으로 풉니다(지역화 글). 꺼졌으면 왜인지와 다음 할 일입니다. */
        static const utf8*           describe( FogVolumeState state );
        static VolumetricFogSeverity getSeverity( FogVolumeState state );
        /** @brief froxel 단면 · 비교 보기를 쓸 수 있으면 true 입니다(볼륨이 도는 프레임만 — 꺼졌으면 안개 패스가 진단 상수를 끈다). */
        static bool canShowVolumeViews( FogVolumeState state ) { return state == FogVolumeState::Active; }
    };
} // namespace sw::editor

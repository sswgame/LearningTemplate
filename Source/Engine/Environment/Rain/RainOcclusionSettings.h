/**
 * @file RainOcclusionSettings.h
 * @brief 비 가림 맵 — 주 시점을 따라다니며 위에서 내려다본 정사영 깊이 한 장의 값과 투영입니다.
 * @details 젖음 · 웅덩이 · 눈 · 빗방울 링(material_functions.hlsli `computeRainExposure`)과 GPU 강수(`VFXCollisionMethod::RainOcclusion`)가 읽는다. 값은 씬의 `RainOcclusionComponent` 가 정하고
 *          렌더 패킷이 나른다 — 렌더러가 컴포넌트 헤더를 보지 않도록 값 타입만 따로 둔다(`SkyGradient` 와 같다).
 *          Godot `GPUParticlesCollisionHeightField3D`(카메라를 따르는 높이 맵 — 비 · 눈용)와 언리얼 · 유니티 게임이 위에서 찍은 깊이로 비를 가리는 것과 같은 자리다.
 *          게임플레이의 실내 판정(`WeatherShelter`: 캐릭터 젖음 · 소리)은 물리 광선 그대로라 GPU 깊이를 되읽지 않는다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"

namespace sw
{
    /** @struct RainOcclusionSettings @brief 비 가림 맵 한 벌의 값입니다. 해상도가 0 이면 맵이 없다(컴포넌트 없음 · 품질이 껐다). */
    struct SW_API RainOcclusionSettings
    {
        float32 _extent{ 64.0f };       ///< 덮는 정사각형의 한 변(m)
        float32 _heightAbove{ 60.0f };  ///< 시점 위로 덮는 높이(m) — 이보다 높은 지붕은 가리지 않는다
        float32 _depthBelow{ 40.0f };   ///< 시점 아래로 덮는 깊이(m)
        float32 _depthBias{ 0.15f };    ///< 이만큼(m)까지는 같은 면으로 본다
        float32 _normalOffset{ 0.25f }; ///< 면에서 노멀 쪽으로 띄워 읽는 거리(m) — 벽이 제 윗면에 가려지지 않는다
        uint32  _resolution{ 0 };       ///< 한 변 텍셀 수 — 품질(`RenderConfig::_rainOcclusionResolution`)이라 렌더 패킷이 채운다(저장하지 않는다)

        /** @brief 이번 프레임에 맵이 있으면 true 입니다. */
        [[nodiscard]] bool isEnabled() const { return _resolution > 0 && _extent > 0.0f && _heightAbove + _depthBelow > 0.0f; }
        /** @brief @p viewPosition 을 따르는 맵의 눈입니다. 가로는 텍셀 격자, 높이는 1 m 에 맞춘다 — 카메라가 조금 움직여도 맵이 떨리지 않는다(방향광 캐스케이드의 텍셀 스냅과 같다). */
        [[nodiscard]] float3 computeEye( const float3& viewPosition ) const;
        /** @brief @p eye 에서 곧장 내려다본 정사영 뷰-투영입니다. */
        [[nodiscard]] float4x4 computeViewProjection( const float3& eye ) const;
        /** @brief 패스 상수 `g_RainOcclusionParams`(깊이 바이어스 NDC, 노멀 오프셋 m, 1 / 한 변, 켬 1)입니다. */
        [[nodiscard]] float4 makeShaderParams() const;
    };
} // namespace sw

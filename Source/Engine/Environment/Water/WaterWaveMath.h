/**
 * @file WaterWaveMath.h
 * @brief 거스트너 파도의 CPU 판 — 셰이더(`Resource/engine/shaders/gerstner.hlsli`)와 같은 식입니다. 부력 · 수면 질의가 이것을 씁니다.
 * @details 파도 하나 = 방향(라디안, +x 에서 +z 쪽으로) · 파장 · 진폭 · 가파름(0..1). 파수 k = 2π / 파장, 위상 속도는 깊은 물 분산
 *          ω = √(g k) 이고 가파름 Q = 가파름 / (k × 진폭 × 파도 수) 라 가파름 1 에서도 고리가 생기지 않습니다(GPU Gems 1 장 1 의 식).
 *          변위는 수평 원점 (x, z) 의 점이 시간 t 에 옮겨 가는 양입니다 — 월드 (x, z) 의 수면 높이는 그 자리로 **옮겨 오는** 원점을
 *          고정점 반복으로 찾은 뒤의 높이입니다(`computeSurfaceHeight`).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Math/Math.h"

#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /** @brief 거스트너 파도 하나입니다. 머티리얼에는 (방향, 파장, 진폭, 가파름) float4 하나로 실립니다. */
    REFLECT()
    struct SW_API GerstnerWave
    {
        REFLECT_BODY();

        PROPERTY( Units = rad, Tooltip = "Travel direction, from +x towards +z" )
        float32 _direction{ 0.0f };
        PROPERTY( Min = 0.01, Units = m )
        float32 _wavelength{ 8.0f };
        PROPERTY( Min = 0.0, Units = m )
        float32 _amplitude{ 0.1f };
        PROPERTY( Min = 0.0, Max = 1.0, Tooltip = "0 = sine wave, 1 = sharpest crest without loops" )
        float32 _steepness{ 0.5f };

        /** @brief 머티리얼 값(방향, 파장, 진폭, 가파름)입니다. */
        float4 toVector() const { return float4{ _direction, _wavelength, _amplitude, _steepness }; }
    };
} // namespace sw

namespace sw
{
    /**
     * @struct WaterWaveMath
     * @brief 거스트너 변위 · 노멀 · 수면 높이입니다. 파도는 `shaderslot::kGerstnerWaveCount` 개까지이고 진폭 0 인 칸은 빈 칸입니다.
     */
    struct SW_API WaterWaveMath
    {
        static constexpr float32 kGravity = 9.81f;

        /** @brief 수평 원점 (x, z) 의 점이 시간 @p time 에 옮겨 가는 변위(x, y, z)입니다. */
        static float3 computeDisplacement( const float2& origin, float32 time, const float4 ( &arrWave )[shaderslot::kGerstnerWaveCount] );
        /** @brief 수평 원점 (x, z) 의 옮겨 간 점에서의 수면 노멀입니다. */
        static float3 computeNormal( const float2& origin, float32 time, const float4 ( &arrWave )[shaderslot::kGerstnerWaveCount] );
        /**
         * @brief 월드 (x, z) 의 수면 높이(기준면 0 에 대한 변위)입니다. 그 자리로 옮겨 오는 원점을 고정점 반복 @p iterationCount 번으로 찾습니다.
         * @param pOutOrigin 찾은 원점입니다(nullptr 이면 버립니다). 노멀은 이 원점으로 `computeNormal` 을 부릅니다.
         */
        static float32 computeSurfaceHeight( const float2& position, float32 time, const float4 ( &arrWave )[shaderslot::kGerstnerWaveCount], uint32 iterationCount = 4,
                                             float2* pOutOrigin = nullptr );
        /** @brief 파도 수(진폭 > 0 인 칸)입니다. 가파름 Q 를 나누는 수입니다. */
        static uint32 countActiveWaves( const float4 ( &arrWave )[shaderslot::kGerstnerWaveCount] );
    };
} // namespace sw

/**
 * @file VfxParticle.h
 * @brief 입자 하나의 렌더 입력 형식 `GPUParticle` 입니다. CPU 시뮬레이션과 GPU 시뮬레이션이 같은 형식을 냅니다.
 * @details float4 셋(48 바이트)이라 구조 버퍼의 한 원소가 됩니다. 좌표는 방출기 로컬입니다(월드 변환은 그리는 쪽이 방출기 행렬로 합니다).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Math/VectorMath.h"

namespace sw
{
    /** @brief 입자 하나의 렌더 입력입니다. 필드 순서가 셰이더 쪽 구조체(`particle.hlsl` 의 `GPUParticle`)와 같습니다. */
    struct GPUParticle
    {
        float4 _positionSize{}; ///< xyz = 방출기 로컬 위치, w = 크기
        float4 _color{};        ///< 선형 RGBA
        float4 _rotationLife{}; ///< x = 회전(라디안), y = 수명 비율(0 = 갓 태어남, 1 = 사라짐), zw = 비어 있음
    };

    static_assert( sizeof( GPUParticle ) == sizeof( float4 ) * 3, "GPUParticle must be exactly three float4 (structured buffer element)" );
} // namespace sw

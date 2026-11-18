/**
 * @file EditorColliderHandle.h
 * @brief 콜라이더 크기 손잡이 하나의 값입니다. 손잡이 도구 View 가 셰이프 헤더 없이 들 수 있게 계산(`EditorColliderHandleMath`)에서 떼어 둔다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"

namespace sw::editor
{
    /** @brief 손잡이가 바꾸는 크기 값입니다. */
    enum class ColliderHandleKind : uint8
    {
        BoxExtent,        ///< 상자 반 크기의 한 축
        SphereRadius,     ///< 구 반지름
        CapsuleRadius,    ///< 캡슐 반지름(로컬 X · Z 방향 손잡이)
        CapsuleHalfHeight ///< 캡슐 원기둥 반 높이(로컬 Y 방향 손잡이 — 끝 반구까지 포함한 거리로 끈다)
    };

    /** @brief 손잡이 하나입니다. 셰이프 가장자리에서 바깥으로 향하는 한 점입니다. */
    struct ColliderHandle
    {
        ColliderHandleKind _kind{ ColliderHandleKind::BoxExtent };
        uint32             _shapeIndex{ 0 }; ///< 바디의 셰이프 목록에서의 번호
        uint32             _axis{ 0 };       ///< 셰이프 로컬 축(0 X, 1 Y, 2 Z)
        float32            _sign{ 1.0f };    ///< 로컬 축의 양(+1)과 음(-1)
        float3             _center{};        ///< 셰이프 중심(월드)
        float3             _direction{};     ///< 중심에서 손잡이로 향하는 단위 벡터(월드)
        float3             _position{};      ///< 손잡이 위치(월드)
    };
} // namespace sw::editor

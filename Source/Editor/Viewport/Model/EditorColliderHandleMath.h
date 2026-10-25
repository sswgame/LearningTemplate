/**
 * @file EditorColliderHandleMath.h
 * @brief 콜라이더 크기 손잡이의 계산입니다 — 셰이프의 손잡이 위치와, 손잡이를 끈 위치에서 새 크기를 구하는 일(ImGui 없음, EditorTest 가 본다).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Physics/Collision/PhysicsShape.h"

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

namespace sw::editor
{
    /**
     * @struct EditorColliderHandleMath
     * @brief 유니티 Box Collider · Sphere Collider · Capsule Collider 의 편집 손잡이에 해당하는 계산입니다.
     * @details 셰이프는 바디의 월드 배율을 먹여(`PhysicsComponentUtil::makeScaledShape`) 월드 크기로 놓고, 끈 위치의 거리를 배율로 나눠 저장 값(배율 전)으로 되돌린다.
     *          그래서 보이는 손잡이가 곧 물리가 판정하는 면이다. 배율을 되돌리는 비율은 `makeScaledShape` 에 단위 셰이프를 먹여 구한다 — 배율 규칙이 한 곳에만 있다.
     *          크기는 중심을 지키며 양쪽으로 같이 변한다(손잡이를 끈 위치가 곧 새 가장자리).
     */
    struct EditorColliderHandleMath
    {
        /** @brief 손잡이를 끌 때 셰이프가 가질 수 있는 가장 작은 월드 크기(m)입니다. 0 이면 셰이프가 사라져 다시 잡을 수 없다. */
        static constexpr float32 kMinExtent = 0.01f;

        /**
         * @brief 셰이프의 손잡이를 모읍니다(상자 6, 구 6, 캡슐 6 — 로컬 ±X · ±Z 는 반지름, ±Y 는 반 높이). 볼록 껍질과 삼각형 메시는 손잡이가 없습니다.
         * @param outListHandle 뒤에 붙인다(비우지 않는다)
         */
        static void collectHandles( const PhysicsShapeDesc3D& shape, uint32 shapeIndex, const float3& bodyPosition, const quaternion& bodyRotation, const float3& bodyScale,
                                    vector<ColliderHandle>& outListHandle );

        /**
         * @brief 손잡이를 중심에서 바깥으로 @p outwardDistance(월드, m) 떨어진 위치로 끌었을 때의 셰이프를 돌려줍니다.
         * @details 거리는 `kMinExtent` 로 아래를 막는다. 캡슐의 반 높이는 거리에서 (월드) 반지름을 뺀 값이고 0 아래로 내려가지 않는다. 배율이 0 인 축은 바꾸지 않는다.
         */
        static PhysicsShapeDesc3D applyDrag( const PhysicsShapeDesc3D& shape, const ColliderHandle& handle, float32 outwardDistance, const float3& bodyScale );

        /**
         * @brief 레이와 가장 가까워지는 축 위의 점의 매개변수를 구합니다(축 위의 점 = @p axisOrigin + t × @p axisDirection, 둘 다 단위 벡터).
         * @return 레이와 축이 나란하면 0 입니다.
         */
        static float32 computeClosestAxisParameter( const float3& rayOrigin, const float3& rayDirection, const float3& axisOrigin, const float3& axisDirection );
    };
} // namespace sw::editor

/**
 * @file PhysicsShape.h
 * @brief 3D · 2D 셰이프 서술자입니다 — 데이터(컴포넌트 PROPERTY · 물리 에셋)와 런타임(바디 만들기)이 같은 타입을 씁니다.
 * @details 바디 하나에 셰이프가 둘 이상이면 컴파운드입니다(각 셰이프가 바디 기준 자리 · 회전을 든다). 차원마다 다른 것은 셰이프 종류와
 *          크기 칸뿐입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /** @brief 3D 셰이프 종류입니다(Jolt Box · Sphere · Capsule · ConvexHull · Mesh). */
    ENUM()
    enum class PhysicsShapeType3D : uint8
    {
        Box = 0,
        Sphere,
        Capsule,      ///< 로컬 Y 축으로 선 캡슐 — 원기둥 반 높이(`_halfHeight`) + 반지름
        ConvexHull,   ///< `_listPoint` 를 감싸는 볼록 껍질
        TriangleMesh, ///< `_listPoint` · `_listIndex` 삼각형 — Static · Kinematic 바디만(움직이는 메시 충돌은 백엔드가 막는다)
    };

    /** @brief 2D 셰이프 종류입니다(Box2D polygon · circle · capsule · chain). */
    ENUM()
    enum class PhysicsShapeType2D : uint8
    {
        Box = 0,
        Circle,
        Capsule, ///< 로컬 Y 축으로 선 캡슐
        Polygon, ///< `_listPoint` 의 볼록 다각형(최대 8 점, 넘거나 오목하면 볼록 껍질)
        Chain,   ///< `_listPoint` 를 잇는 선분 사슬(지형) — 한쪽 면만 막는다. `_bLoop` 면 닫는다
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 3D 셰이프 하나입니다.
     * @code
     *     <PhysicsShapeDesc3D _type="Capsule" _radius="0.12" _halfHeight="0.18" _localPosition="0,0.2,0" _material="Flesh" />
     * @endcode
     */
    REFLECT()
    struct SW_API PhysicsShapeDesc3D
    {
        REFLECT_BODY();

        PROPERTY( Tooltip = "Shape kind" )
        PhysicsShapeType3D _type{ PhysicsShapeType3D::Box };
        PROPERTY( Tooltip = "Box half size", Meta = "Units=m" )
        float3 _halfExtents{ 0.5f, 0.5f, 0.5f };
        PROPERTY( Min = 0.0, Tooltip = "Sphere / capsule radius", Meta = "Units=m" )
        float32 _radius{ 0.5f };
        PROPERTY( Min = 0.0, Tooltip = "Capsule: half the length of the cylinder part along local Y", Meta = "Units=m" )
        float32 _halfHeight{ 0.5f };
        PROPERTY( Tooltip = "Offset from the body origin", Meta = "Units=m" )
        float3 _localPosition{};
        PROPERTY( Tooltip = "Rotation relative to the body (pitch, yaw, roll)", Meta = "Units=rad" )
        float3 _localRotation{};
        PROPERTY( Tooltip = "Convex hull points / triangle mesh vertices (body local)" )
        vector<float3> _listPoint;
        PROPERTY( Tooltip = "Triangle mesh: three vertex indices per triangle" )
        vector<uint32> _listIndex;
        PROPERTY( Tooltip = "Physics material name (empty: the body's material)" )
        hashed_string _material{};
    };
} // namespace sw

namespace sw
{
    /** @brief 2D 셰이프 하나입니다. 2D 물리는 XY 평면이고 각은 Z 축 둘레입니다. */
    REFLECT()
    struct SW_API PhysicsShapeDesc2D
    {
        REFLECT_BODY();

        PROPERTY( Tooltip = "Shape kind" )
        PhysicsShapeType2D _type{ PhysicsShapeType2D::Box };
        PROPERTY( Tooltip = "Box half size", Meta = "Units=m" )
        float2 _halfExtents{ 0.5f, 0.5f };
        PROPERTY( Min = 0.0, Tooltip = "Circle / capsule radius", Meta = "Units=m" )
        float32 _radius{ 0.5f };
        PROPERTY( Min = 0.0, Tooltip = "Capsule: half the length of the straight part along local Y", Meta = "Units=m" )
        float32 _halfHeight{ 0.5f };
        PROPERTY( Tooltip = "Offset from the body origin", Meta = "Units=m" )
        float2 _localPosition{};
        PROPERTY( Tooltip = "Rotation relative to the body", Meta = "Units=rad" )
        float32 _localAngle{ 0.0f };
        PROPERTY( Tooltip = "Polygon / chain points (body local)" )
        vector<float2> _listPoint;
        PROPERTY( Tooltip = "Physics material name (empty: the body's material)" )
        hashed_string _material{};
        PROPERTY( Tooltip = "Chain: connect the last point back to the first" )
        bool _bLoop{ false };
    };
} // namespace sw

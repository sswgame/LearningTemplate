/**
 * @file MeshUtil.h
 * @brief 내장 도형 생성기 — 큐브 · 쿼드 · 구 · 실린더 · 캡슐 · 원뿔
 *
 * [왜 Mesh 가 아닌가]
 * `Mesh` 가 책임지는 것은 **정점 버퍼와 그 수명**이다. 어떤 기하를 만들지는 다른 일이고, 도형이
 * 늘어날 때마다 리소스 클래스가 커질 이유가 없다 — `MaterialUtil` 이 `Material` 옆에 따로 있는 것과
 * 같은 자리다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    class Mesh;

    /**
     * @struct MeshUtil
     * @brief 원점 중심 단위 도형을 만듭니다. 전부 삼각형 목록이고 인덱스는 쓰지 않습니다.
     * @note 감김은 **바깥을 향한다**(이 엔진의 앞면 규약). 뒤집히면 후면 컬링에 화면에서 사라지므로
     *       `MeshPrimitiveTest.PrimitivesAreClosedAndOutwardFacing` 이 그것을 고정한다.
     */
    struct SW_API MeshUtil
    {
        /** @brief 원점 중심 단위 큐브(범위 [-0.5,0.5], 면별 색). */
        static shared_ptr<Mesh> createUnitCube();
        /** @brief 원점 중심 단위 2D 쿼드(범위 [-0.5,0.5]). */
        static shared_ptr<Mesh> createRectMesh();
        /**
         * @brief 원점 중심 UV 구(지름 1).
         * @param stackCount 위아래 분할 수(최소 2), @param sliceCount 둘레 분할 수(최소 3).
         * @details 큐브 하나만으로는 검증이 얇다 — 삼각형이 12개뿐이고 면이 축에 정렬돼 있어서
         *          래스터화·보간·컬링의 어느 것도 제대로 흔들지 않는다.
         */
        static shared_ptr<Mesh> createSphere( uint32 stackCount = 12, uint32 sliceCount = 16 );
        /** @brief 원점 중심 실린더(지름 1 · 높이 1). 옆면 + 위아래 뚜껑. */
        static shared_ptr<Mesh> createCylinder( uint32 sliceCount = 16 );
        /** @brief 원점 중심 캡슐(지름 1 · 원통부 높이 1). 반구 + 옆면 + 반구. */
        static shared_ptr<Mesh> createCapsule( uint32 stackCount = 6, uint32 sliceCount = 16 );
        /** @brief 원점 중심 원뿔(밑지름 1 · 높이 1). 옆면 + 밑면. */
        static shared_ptr<Mesh> createCone( uint32 sliceCount = 16 );

        /**
         * @brief 프리미티브 id로 내장 도형을 만듭니다.
         * @details 비었거나 "Cube" 면 큐브, "Quad"/"Rect" 면 쿼드,
         *          "Sphere" · "Cylinder" · "Capsule" · "Cone" 은 각각의 곡면 도형. 모르면 nullptr.
         *          씬 XML 의 `_meshId` 와 벤치의 도형 섞기가 같은 이름을 쓴다.
         */
        static shared_ptr<Mesh> createPrimitive( string_view meshId );
    };
} // namespace sw

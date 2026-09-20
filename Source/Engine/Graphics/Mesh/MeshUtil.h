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
        /** @brief 원점 중심 단위 2D 쿼드(범위 [-0.5,0.5]). XY 평면이라 화면을 마주 본다. */
        static shared_ptr<Mesh> createRectMesh();
        /**
         * @brief 위를 향한 바닥 평면(XZ, 한 변 1). 그림자를 **받아 보이게 하는** 면이다.
         * @param segmentCount 한 변의 분할 수(최소 1). 나누는 이유는 정점 색·조명이 정점 단위로
         *        보간되기 때문이다 — 한 장짜리 쿼드는 네 꼭짓점 사이가 선형으로 늘어나 빛이 뭉갠다.
         * @note 면은 로컬 `y = 0` 이고 노멀은 +Y 다. 한때 `y = +0.5`(큐브 윗면 자리)에 두어야 했는데,
         *       정점에 노멀이 없어 셰이더가 위치로 노멀을 지어냈기 때문이다(`y = 0` 이면 `|y|` 가 0 이라
         *       ±X/±Z 를 받아 바닥이 옆을 보는 것처럼 칠해졌다). 정점이 노멀을 들고 다니는 지금은
         *       그 회피가 필요 없다.
         */
        static shared_ptr<Mesh> createPlane( uint32 segmentCount = 8 );
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

        /**
         * @brief 프리미티브 id 로 **공유되는** 내장 도형을 돌려줍니다. 같은 id 면 같은 객체입니다.
         *
         * @details **돌려받은 메시를 고치지 마세요** — 씬 전체가 그 하나를 나눠 씁니다. 자기만의
         *          기하가 필요하면 `createPrimitive`(매번 새로 만든다) 나 개별 `createXxx` 를 쓰세요.
         *
         *          씬에서 온 `MeshComponent` 는 전부 이쪽을 쓴다. 예전에는 컴포넌트마다
         *          `createPrimitive` 로 **자기 Mesh 객체를 따로** 만들었는데, 배치 키가 메시 포인터라
         *          같은 큐브 8000 개가 배치 8000 개로 갈렸다(벤치는 하나를 나눠 써서 배치 2 개였고,
         *          그래서 이 결함이 벤치에는 한 번도 안 보였다). GPU 정점 버퍼도 8000 벌이었다.
         *
         *          캐시는 `weak_ptr` 이라 아무도 안 쓰면 알아서 사라진다 — 수명을 따로 관리하지
         *          않으므로 디바이스가 내려갈 때 붙들고 있는 것이 없다.
         */
        static shared_ptr<Mesh> acquirePrimitive( string_view meshId );
    };
} // namespace sw

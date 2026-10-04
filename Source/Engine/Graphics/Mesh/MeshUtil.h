/**
 * @file MeshUtil.h
 * @brief 내장 도형 생성기입니다: 큐브 · 쿼드 · 평면 · 구 · 실린더 · 캡슐 · 원뿔.
 *
 * [왜 Mesh 가 아닌가]
 * `Mesh` 가 책임지는 것은 **정점 버퍼와 그 수명**입니다. 어떤 기하를 만들지는 다른 일이고, 도형이
 * 늘어날 때마다 리소스 클래스가 커질 이유가 없습니다. `MaterialUtil` 이 `Material` 옆에 따로 있는 것과
 * 같은 자리입니다.
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
     * @enum PrimitiveVertexColor
     * @brief 내장 도형의 정점 색입니다. 셰이더는 정점 색에 머티리얼 색을 곱합니다.
     */
    enum class PrimitiveVertexColor : uint8
    {
        White,      ///< 모두 1 — 머티리얼 색이 그대로 보입니다. 게임 · 씬 · 에디터가 쓰는 도형입니다(상용 엔진의 기본 도형과 같다)
        Diagnostic, ///< 면마다 다른 색 · 노멀에서 뽑은 음영 · 평면의 바둑판 — 어느 면 · 어느 칸인지 그림에서 읽히게 하는 검증용(벤치 · 렌더 시험)
    };

    /**
     * @struct MeshUtil
     * @brief 원점 중심 단위 도형을 만듭니다. 모두 삼각형 목록이고 인덱스는 쓰지 않습니다.
     * @note 감김은 **바깥을 향합니다**(이 엔진의 앞면 규약). 뒤집히면 후면 컬링에 화면에서 사라지므로
     *       `MeshPrimitiveTest.PrimitivesAreClosedAndOutwardFacing` 이 그것을 고정합니다.
     * @note 개별 `createXxx` 는 **검증용 색**(`PrimitiveVertexColor::Diagnostic`)으로 만듭니다. 게임 · 씬이 쓰는 이름 창구
     *       (`createPrimitive` · `acquirePrimitive`)는 기본이 흰색입니다. 검증 색이 게임에 새면 회색으로 칠한 벽이 면마다 초록 · 주황이 된다.
     */
    struct SW_API MeshUtil
    {
        /** @brief 원점 중심 단위 큐브입니다(범위 [-0.5,0.5], 면별 색). */
        static shared_ptr<Mesh> createUnitCube();
        /** @brief 원점 중심 단위 2D 쿼드입니다(범위 [-0.5,0.5]). XY 평면이라 화면을 마주 봅니다. */
        static shared_ptr<Mesh> createRectMesh();
        /**
         * @brief 스프라이트 사각형입니다(XY 평면, 한 변 1). **양면**이고 어느 쪽에서 봐도 텍스처가 바로 읽힙니다.
         * @details 앞면은 -Z 를 향합니다 — 카메라의 기본 방향(+Z 를 본다, 화면 오른쪽 = +X · 위 = +Y)이 마주 보는 면이고 2D 게임의 화면입니다.
         *          그 면의 u 는 +X 로 늡니다. 뒷면(+Z 를 향함, -Z 를 보는 카메라 — 씬의 기본 GameCamera 가 그렇다)은 u 가 -X 로 늘어 글자가
         *          뒤집히지 않습니다. v 는 두 면 모두 위가 0 입니다.
         *          주의: 스프라이트에 3D 쿼드(`createRectMesh` — +Z 한 면, u 가 +X)를 쓰지 마십시오. 그 면은 -Z 를 보는 카메라에서만 보이는데
         *          그 카메라의 화면 오른쪽은 -X 라 **모든 스프라이트가 좌우로 뒤집혀** 그려지고, 2D 카메라(+Z 를 봄)에서는 후면 컬링으로
         *          아예 사라집니다. 유니티 스프라이트도 양면입니다(Cull Off).
         *          투명 패스는 후면을 거르므로 한 시점에서는 한 면만 그려집니다.
         */
        static shared_ptr<Mesh> createSpriteQuad();
        /**
         * @brief 위를 향한 바닥 평면입니다(XZ, 한 변 1). 그림자를 **받아 보이게 하는** 면입니다.
         * @param segmentCount 한 변의 분할 수(최소 1). 나누는 이유는 정점 색 · 조명이 정점 단위로
         *        보간되기 때문입니다. 한 장짜리 쿼드는 네 꼭짓점 사이가 선형으로 늘어나 빛이 뭉개집니다.
         * @note 면은 로컬 `y = 0` 이고 노멀은 +Y 입니다. 노멀은 정점이 들고 다닙니다.
         */
        static shared_ptr<Mesh> createPlane( uint32 segmentCount = 8 );
        /**
         * @brief 원점 중심 UV 구입니다(지름 1).
         * @param stackCount 위아래 분할 수(최소 2), @param sliceCount 둘레 분할 수(최소 3).
         * @details 큐브 하나만으로는 검증이 얇습니다. 삼각형이 12개뿐이고 면이 축에 정렬돼 있어서
         *          래스터화 · 보간 · 컬링의 어느 것도 제대로 흔들지 않습니다.
         */
        static shared_ptr<Mesh> createSphere( uint32 stackCount = 12, uint32 sliceCount = 16 );
        /** @brief 원점 중심 실린더입니다(지름 1 · 높이 1). 옆면 + 위아래 뚜껑. */
        static shared_ptr<Mesh> createCylinder( uint32 sliceCount = 16 );
        /** @brief 원점 중심 캡슐입니다(지름 1 · 원통부 높이 1). 반구 + 옆면 + 반구. */
        static shared_ptr<Mesh> createCapsule( uint32 stackCount = 6, uint32 sliceCount = 16 );
        /** @brief 원점 중심 원뿔입니다(밑지름 1 · 높이 1). 옆면 + 밑면. */
        static shared_ptr<Mesh> createCone( uint32 sliceCount = 16 );

        /**
         * @brief 프리미티브 id 로 내장 도형을 새로 만듭니다.
         * @details 비었거나 "Cube" 면 큐브, "Quad"/"Rect" 면 쿼드, "Sprite" 면 양면 스프라이트 사각형, "Plane"/"Ground" 면 바닥 평면,
         *          "Sphere" · "Cylinder" · "Capsule" · "Cone" 은 각각의 곡면 도형입니다. 모르면 nullptr 입니다.
         *          씬 XML 의 `_meshId` 와 벤치의 도형 섞기가 같은 이름을 씁니다. 정점 색은 @p vertexColor 입니다(기본 흰색 — 게임이 쓰는 색).
         */
        static shared_ptr<Mesh> createPrimitive( string_view meshId, PrimitiveVertexColor vertexColor = PrimitiveVertexColor::White );

        /**
         * @brief 프리미티브 id 로 **공유되는** 내장 도형을 반환합니다. 같은 id 면 같은 객체입니다.
         *
         * @details **반환받은 메시를 고치지 마십시오.** 씬 전체가 그 하나를 나눠 씁니다. 자기만의
         *          기하가 필요하면 `createPrimitive`(매번 새로 만듭니다)나 개별 `createXxx` 를 쓰십시오.
         *
         *          씬에서 온 `MeshComponent` 는 모두 이쪽을 씁니다. 주의: 컴포넌트마다 `createPrimitive` 로
         *          **자기 Mesh 객체를 따로** 만들면, 배치 키가 메시 포인터라 같은 큐브 8000 개가 배치 8000 개로
         *          갈리고 GPU 정점 버퍼도 8000 벌이 됩니다.
         *
         *          캐시는 `weak_ptr` 이라 아무도 안 쓰면 알아서 사라집니다. 수명을 따로 관리하지
         *          않으므로 디바이스가 내려갈 때 붙들고 있는 것이 없습니다. 정점 색은 흰색입니다(`PrimitiveVertexColor::White`).
         */
        static shared_ptr<Mesh> acquirePrimitive( string_view meshId );
    };
} // namespace sw

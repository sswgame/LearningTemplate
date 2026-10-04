/**
 * @file SpriteMeshBuilder.h
 * @brief 9-슬라이스(Sliced) · 타일(Tiled) 스프라이트 메시를 짓고 나눠 줍니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"
#include "Core/Memory/Memory.h"

#include "Engine/Graphics/RHI/RHITypes.h"

namespace sw
{
    class Mesh;

    /**
     * @struct SlicedSpriteDesc
     * @brief 9-슬라이스 · 타일 메시 하나를 정하는 값입니다. 같은 값이면 같은 메시를 나눠 씁니다(`SpriteMeshBuilder::acquireSlicedMesh`).
     * @details 테두리는 **프레임에 대한 비율**(0..1) (왼쪽, 아래, 오른쪽, 위)입니다. 스프라이트의 자연 크기는 단위 사각형(1 × 1)이라 테두리의 월드
     *          크기도 그 비율 그대로입니다 — 크기를 바꿔도 모서리는 자연 크기에서 보이던 크기로 남고 가운데만 늘거나(Sliced) 되풀이됩니다(Tiled).
     */
    struct SlicedSpriteDesc
    {
        float2 _size{ 1.0f, 1.0f };               ///< 월드(로컬) 크기 — 폭, 높이
        float4 _border{ 0.0f, 0.0f, 0.0f, 0.0f }; ///< (왼쪽, 아래, 오른쪽, 위) 프레임 비율
        uint8  _bTiled{ SW_FALSE };               ///< 가운데 · 변을 늘리지 않고 자연 크기로 되풀이합니다

        bool operator==( const SlicedSpriteDesc& other ) const;
    };
} // namespace sw

namespace sw
{
    /**
     * @struct SpriteMeshBuilder
     * @brief 9-슬라이스 · 타일 스프라이트의 정점을 짓습니다. 유니티 `SpriteRenderer.drawMode`(Sliced · Tiled) · Godot `NinePatchRect` 의 자리입니다.
     * @details 정점은 `MeshUtil::createSpriteQuad` 와 같은 규약입니다 — XY 평면, 앞면(-Z 노멀)과 그것을 X 로 거울에 비친 뒷면(+Z 노멀)이 같은 UV 를 써서
     *          어느 쪽에서 봐도 뒤집히지 않고, v 는 위가 0 입니다. UV 는 **프레임 안의 0..1** 이라 셰이더가 인스턴스의 아틀라스 프레임으로 옮깁니다(sprite2d.hlsl).
     *          크기가 테두리 합보다 작으면 테두리를 비율대로 줄입니다(유니티와 같음). 타일은 마지막 칸을 잘라 UV 도 그만큼만 씁니다.
     */
    struct SW_API SpriteMeshBuilder
    {
        /** @brief 한 축의 타일 칸 수 상한입니다. 넘으면 칸을 늘리지 않고 마지막 칸을 늘려 채웁니다(정점 수를 묶습니다). */
        static constexpr uint32 kMaxTileCountPerAxis = 64;

        /** @brief 앞면 · 뒷면 삼각형 목록(인덱스 없음)을 짓습니다. 앞면이 먼저이고 사각형마다 정점 여섯입니다. */
        static void buildSlicedVertices( const SlicedSpriteDesc& desc, vector<RHIVertex>& outListVertex );

        /**
         * @brief 같은 값의 메시를 나눠 줍니다. 없으면 짓습니다. 여러 스레드에서 불러도 됩니다(잠급니다).
         * @details 표는 약한 참조입니다(`MeshUtil::acquirePrimitive` 와 같은 모양) — 마지막 스프라이트가 놓으면 메시도 사라집니다. 같은 크기 · 테두리의
         *          패널은 한 메시라 한 배치로 묶입니다.
         */
        static shared_ptr<Mesh> acquireSlicedMesh( const SlicedSpriteDesc& desc );
    };
} // namespace sw

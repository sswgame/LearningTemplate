/**
 * @file LODView.h
 * @brief LOD 판정에 쓰는 뷰(뷰-투영 · 눈 위치 · 절두체)와 화면 크기 식입니다. 애니메이션 LOD 와 메시 LOD 가 같은 식을 씁니다.
 * @details 언리얼 `ComputeBoundsScreenSize` 처럼 화면 크기는 뷰의 일이라 그래픽스 층에 둔다 — 메시 LOD(오브젝트 층)가 애니메이션(기능 층)을 include 하지 않게.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Frustum.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"

namespace sw
{
    /**
     * @struct LODView
     * @brief LOD 판정에 쓰는 뷰 하나입니다(뷰-투영 · 눈 위치 · 그 행렬에서 뽑은 절두체). `make` 로 만듭니다.
     */
    struct LODView
    {
        float4x4 _viewProj{};
        float3   _position{};
        Frustum  _frustum{};

        /** @brief 뷰-투영과 눈 위치로 만듭니다(절두체를 한 번 뽑아 둡니다). */
        static LODView make( const float4x4& viewProj, const float3& position )
        {
            LODView view{};
            view._viewProj = viewProj;
            view._position = position;
            view._frustum  = Frustum::fromViewProjection( viewProj );
            return view;
        }
    };
} // namespace sw

namespace sw
{
    /**
     * @struct LODViewUtil
     * @brief 화면 크기의 순수 식입니다(테스트가 그대로 부릅니다).
     */
    struct SW_API LODViewUtil
    {
        /**
         * @brief 경계 구가 뷰에 차지하는 화면 크기(지름 / 화면 높이)입니다. 절두체 밖이면 0 입니다.
         * @details 행벡터 규약이라 클립 y = dot( p, 열 1 ), w = dot( p, 열 3 ) 입니다. 강체 뷰 행렬이면 열 1 의 xyz 길이가 투영의 y 배율(cot( fov / 2 ))이고,
         *          화면 크기 = 반지름 × 배율 / w 입니다(직교 투영이면 w = 1). 눈이 구 안에 있으면 1 로 봅니다.
         */
        static float32 computeScreenSize( const LODView& view, const float3& center, float32 radius );
        /**
         * @brief 화면 크기 식(`computeScreenSize`)이 읽는 투영의 두 항입니다. GPU 컬링의 인스턴스 LOD 선택(`gpucull.hlsl`)이 같은 식을 쓰도록 한 곳에서 뽑는다.
         * @param outArrDepthRow 클립 w 를 만드는 열(m14, m24, m34, m44) — `w = dot( center, xyz ) + w`
         * @param outScaleY      투영의 y 배율(열 1 의 xyz 길이)
         */
        static void computeProjectionTerms( const float4x4& viewProj, float32 ( &outArrDepthRow )[4], float32& outScaleY );
    };
} // namespace sw

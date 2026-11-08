/**
 * @file FramePostGrade.h
 * @brief 컬러 그레이딩의 셰이더 상수(화이트 밸런스 행렬 · 범위별 보정 · LUT 주소)를 뷰의 후처리 값에서 만듭니다. post_grade.hlsli 가 같은 배치를 읽습니다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"

namespace sw
{
    struct PostProcessValues;

    /**
     * @struct PostGradeConstants
     * @brief 컬러 그레이딩 상수 한 벌입니다. 패스 상수 `g_Grade*`(binding.hlsli)와 같은 배치입니다.
     * @details 언리얼은 그레이딩 · 화이트 밸런스 · 톤맵 곡선을 LUT 하나로 굽고(PostProcessCombineLUTs) 톤맵이 그 LUT 를 읽는다. 여기서는 뷰마다 바뀌는 값이 적고
     *          LUT 굽기 패스 하나가 왕복 하나라, 톤맵하는 셰이더가 같은 식을 픽셀마다 바로 푼다(값이 중립이면 `_params.z` 가 0 이라 셰이더가 건너뛴다).
     *          작업 색 공간은 선형 Rec.709 다(언리얼은 ACES AP1) — 같은 값이 언리얼과 조금 다른 색을 낸다.
     */
    struct SW_API PostGradeConstants
    {
        /// @brief 범위 하나의 보정 칸 수입니다 — 채도 · 대비 · 감마 · 게인 · 오프셋.
        static constexpr uint32 kRangeFieldCount = 5;
        /// @brief 범위 수입니다 — 그림자 · 중간 · 밝은 부분(전역은 셋에 미리 합친다).
        static constexpr uint32 kRangeCount = 3;

        float4 _arrWhiteBalance[3];                       ///< 화이트 밸런스 3x3 행렬의 행(xyz). 중립이면 단위 행렬
        float4 _arrRange[kRangeCount * kRangeFieldCount]; ///< 범위마다 (채도, 대비, 감마, 게인, 오프셋)의 rgb(w 는 0) — 전역 · 범위 값과 w 를 미리 곱하고 더했다
        float4 _params;                                   ///< x 그림자 끝 휘도, y 밝은 부분 시작 휘도, z 켬 1 · 끔 0(모든 값이 중립이면 0), w 0
        float4 _lut;                                      ///< x LUT 한 변 N(0 = 없음), y 세기, zw 0

        /**
         * @brief 후처리 값 @p values 의 그레이딩 상수입니다.
         * @param lutSize 읽힌 LUT 의 한 변 N(텍스처 세로 크기). LUT 가 없거나 아직 읽는 중이면 0 — 세기도 0 이 된다
         */
        static PostGradeConstants make( const PostProcessValues& values, uint32 lutSize );
        /**
         * @brief 색 온도 @p temperature(K) · 틴트 @p tint 의 장면 빛을 6500 K 흰색으로 돌리는 화이트 밸런스 행렬(선형 Rec.709, 행 셋)입니다.
         * @details 플랑크 궤적 근사(Kim 외 3차식)로 흰 점의 xy 를 구하고, 틴트는 1960 UCS 의 v 를 옮긴다. 두 흰 점 사이를 Bradford 적응으로 옮긴다(언리얼 WhiteBalance 와 같은 길).
         *          6500 K · 틴트 0 은 기준 흰 점과 같은 식이라 단위 행렬이다.
         */
        static void computeWhiteBalance( float32 temperature, float32 tint, float4 ( &outArrRow )[3] );
        /**
         * @brief LUT 띠(가로 N² × 세로 N)에서 색 @p color([0,1])의 파랑 조각 @p slice 안 텍셀 중심 UV 입니다. post_grade.hlsli 의 `CameraGrade::computeLUTUV` 와 같은 식입니다.
         * @details 조각 하나가 가로 N 칸이고 빨강이 그 안의 가로, 초록이 세로다. 끝 텍셀의 반 칸 안쪽을 짚어 이웃 조각과 섞이지 않는다.
         */
        static float2 computeLUTUV( const float3& color, uint32 lutSize, uint32 slice );
        /** @brief 텍스처 크기 @p width × @p height 가 LUT 띠(가로 = 세로², 세로 2 이상)인지 봅니다. */
        static bool isLUTStrip( uint32 width, uint32 height ) { return height >= 2 && width == height * height; }
    };
} // namespace sw

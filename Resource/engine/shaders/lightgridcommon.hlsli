/**
 * lightgridcommon.hlsli — 라이트 그리드(클러스터드 라이팅)의 칸 식. 읽는 쪽(lighting.hlsli 의 커서)과 만드는 쪽(컴퓨트 lightgrid.hlsl)이 같은 식을 쓴다.
 *
 * C++ `LightGrid`(CPU 생산자)도 같은 식이다(LightGridInternal::computeTile · LightGrid::computeSlicePosition). 식이 둘로 갈리면 경계의 픽셀이
 * 자기를 비추는 라이트가 없는 이웃 클러스터를 읽어 빛이 끊긴다. 단어 배치와 칸 수의 원본은 bindingslots.hlsli 10 절이다.
 */

#ifndef SW_ENGINE_LIGHTGRIDCOMMON_HLSLI
#define SW_ENGINE_LIGHTGRIDCOMMON_HLSLI

#include "common.hlsli"

/** @brief NDC 한 축의 값을 칸 번호로 바꿉니다(양 끝은 자른다). */
uint swComputeLightGridTile( float ndc, uint dimension )
{
    return min( (uint)( saturate( ndc * 0.5f + 0.5f ) * (float)dimension ), dimension - 1u );
}

/**
 * @brief 깊이 값을 조각 위치(실수)로 바꿉니다. 원근은 클립 w 의 로그 조각, 직교는 NDC z 의 선형 조각이다.
 * @param depth 원근이면 클립 w, 직교면 NDC z
 */
float swComputeLightGridSlicePosition( float depth, bool bPerspective, float nearDepth, float sliceScale )
{
    return bPerspective ? log2( max( depth, nearDepth ) / nearDepth ) * sliceScale : saturate( depth ) * sliceScale;
}

#endif // SW_ENGINE_LIGHTGRIDCOMMON_HLSLI

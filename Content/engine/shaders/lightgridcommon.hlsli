/**
 * lightgridcommon.hlsli — 라이트 그리드(클러스터드 라이팅)의 칸 식. 읽는 쪽(lighting.hlsli 의 커서)과 만드는 쪽(컴퓨트 lightgrid.hlsl)이 같은 식을 쓴다.
 *
 * C++ `LightGrid`(CPU 생산자)도 같은 식이다(LightGridInternal::computeTile · LightGrid::computeSlicePosition). 식이 둘로 갈리면 경계의 픽셀이
 * 자기를 비추는 라이트가 없는 이웃 클러스터를 읽어 빛이 끊긴다. 단어 배치와 칸 수의 원본은 bindingslots.hlsli 10 절이다.
 */

#ifndef SW_ENGINE_LIGHTGRIDCOMMON_HLSLI
#define SW_ENGINE_LIGHTGRIDCOMMON_HLSLI

#include "common.hlsli"

/**
 * @brief 라이트 그리드 — 칸 식(이 파일)과 지금 뷰의 그리드 읽기(binding.hlsli — 버퍼 g_SwLightGrid 를 아는 곳)를 한 이름 아래 둔다.
 * @details 그리드를 만드는 컴퓨트(lightgrid.hlsl)는 칸 식만 쓴다. 읽기 함수는 binding.hlsli 를 include 한 셰이더에만 정의가 있다.
 */
struct LightGrid
{
    static uint computeTile( float ndc, uint dimension );
    static float computeSlicePosition( float depth, bool bPerspective, float nearDepth, float sliceScale );
    static uint computeLightCount();
    static uint loadWord( uint wordIndex );
    static bool hasGrid();
    static uint computeCluster( float3 worldPosition );
    static uint computeVisitCount( float3 worldPosition );
};

/** @brief NDC 한 축의 값을 칸 번호로 바꿉니다(양 끝은 자른다). */
static uint LightGrid::computeTile( float ndc, uint dimension )
{
    return min( (uint)( saturate( ndc * 0.5f + 0.5f ) * (float)dimension ), dimension - 1u );
}

/**
 * @brief 깊이 값을 조각 위치(실수)로 바꿉니다. 원근은 클립 w 의 로그 조각, 직교는 NDC z 의 선형 조각이다.
 * @param depth 원근이면 클립 w, 직교면 NDC z
 */
static float LightGrid::computeSlicePosition( float depth, bool bPerspective, float nearDepth, float sliceScale )
{
    return bPerspective ? log2( max( depth, nearDepth ) / nearDepth ) * sliceScale : saturate( depth ) * sliceScale;
}

#endif // SW_ENGINE_LIGHTGRIDCOMMON_HLSLI

/**
 * post_process_material_graph_main.hlsli — 후처리 그래프 셰이더의 2단: 그래프 평가(`evaluateMaterialGraph`)를 후처리 뼈대의 훅으로 잇는다. 생성된 셰이더의 맨 끝에서 include 한다.
 */

#ifndef SW_ENGINE_POSTPROCESSMATERIALGRAPHMAIN_HLSLI
#define SW_ENGINE_POSTPROCESSMATERIALGRAPHMAIN_HLSLI

float4 computePostProcessMaterial( PixelContext context )
{
    const PostProcessGraphOutput graph = evaluateMaterialGraph( context );
    return float4( graph._color, graph._opacity );
}

#include "post_process_material_main.hlsli"

#endif // SW_ENGINE_POSTPROCESSMATERIALGRAPHMAIN_HLSLI

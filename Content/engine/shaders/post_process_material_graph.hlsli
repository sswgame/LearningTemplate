/**
 * post_process_material_graph.hlsli — 머티리얼 그래프가 낸 후처리 셰이더(출력 노드 `PostProcessOutput`)의 1단: 풀스크린 뼈대와 그래프 출력 형식.
 * 생성된 셰이더는 칸 선언 뒤에 이 파일을, 평가 함수 뒤에 post_process_material_graph_main.hlsli 를 include 한다(표면 그래프의 material_graph.hlsli 와 같은 자리).
 * 그래프 식이 읽는 문맥은 post_process_material.hlsli 의 PixelContext(UV · 원본 색 · G버퍼 노멀 · 복원한 월드 위치)와 "Post Process" 묶음 노드(PostProcessInput::*)다.
 */

#ifndef SW_ENGINE_POSTPROCESSMATERIALGRAPH_HLSLI
#define SW_ENGINE_POSTPROCESSMATERIALGRAPH_HLSLI

#include "post_process_material.hlsli"
#include "normal_map.hlsli"
#include "material_functions.hlsli" // 함수 묶음 노드(SurfaceFunction::*)도 화면 UV · 월드 위치로 쓸 수 있다

/** @brief 후처리 그래프 출력 노드의 칸입니다. 이름은 노드 핀(`Color` → `_color`, `MaterialGraphNodes.cpp`)과 같다. */
struct PostProcessGraphOutput
{
    float3 _color;   ///< 결과 화면 색
    float  _opacity; ///< 덮임(블렌더블 무게에 곱한다)
};

#endif // SW_ENGINE_POSTPROCESSMATERIALGRAPH_HLSLI

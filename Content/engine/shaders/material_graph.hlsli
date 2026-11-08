/**
 * material_graph.hlsli — 머티리얼 그래프가 낸 셰이더의 1단: 뼈대 선택과 그래프 출력 형식. 생성된 셰이더는 칸 선언 뒤에 이 파일을, 평가 함수 뒤에 material_graph_main.hlsli 를 include 한다.
 *
 * 조명은 pbr_lit 과 같은 코드다(기본 PBR — material_shader_main.hlsli 의 `computeSelectedLighting`). 그래프가 적는 것은 표면 값뿐이고 패스 · 깊이 · 순열은 뼈대가 정한다.
 * 스위치: SW_MATERIAL_GRAPH_EMISSIVE — 자체 발광을 쓴다. 디퍼드에서는 G버퍼 패스가 장면 색에 미리 적는다(G버퍼 명세 판 3 — 포워드 전용이 아니다).
 */

#ifndef SW_ENGINE_MATERIALGRAPH_HLSLI
#define SW_ENGINE_MATERIALGRAPH_HLSLI

#define SW_MATERIAL_DEFAULT_LIGHTING
#define SW_MATERIAL_RIM_LIGHT
#define SW_MATERIAL_DEFAULT_VARYINGS
#if !defined( SW_PASS_SHADOW ) && !defined( SW_PASS_DEPTH_PREPASS )
    #define SW_MATERIAL_USES_TANGENT
#endif
#if defined( SW_MATERIAL_GRAPH_EMISSIVE )
    #define SW_MATERIAL_EMISSIVE
#endif
#include "material_shader.hlsli"
#include "normal_map.hlsli"

// 알파 컷오프의 깊이 패스에는 탄젠트 칸이 없다 — 그 패스는 표면의 알파만 읽어 이 값의 영향을 받지 않는다.
#if defined( SW_MATERIAL_USES_TANGENT )
    #define SW_MATERIAL_GRAPH_TANGENT( pixelContext ) pixelContext._tangent
#else
    #define SW_MATERIAL_GRAPH_TANGENT( pixelContext ) float4( 1.0f, 0.0f, 0.0f, 1.0f )
#endif

/** @brief 그래프 출력 노드의 칸입니다. 이름은 노드 핀(`BaseColor` → `_baseColor`, `MaterialGraphNodes.cpp`)과 같다. */
struct MaterialGraphOutput
{
    float3 _baseColor;
    float  _metallic;
    float  _roughness;
    float3 _normal;
    float3 _emissive;
    float  _alpha;
};

#endif // SW_ENGINE_MATERIALGRAPH_HLSLI

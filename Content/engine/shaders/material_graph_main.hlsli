/**
 * material_graph_main.hlsli — 머티리얼 그래프가 낸 셰이더의 2단: 그래프 평가(`evaluateMaterialGraph`)를 뼈대의 훅으로 잇는다. 생성된 셰이더의 맨 끝에서 include 한다.
 */

#ifndef SW_ENGINE_MATERIALGRAPHMAIN_HLSLI
#define SW_ENGINE_MATERIALGRAPHMAIN_HLSLI

MaterialSurface computeMaterialSurface( PixelContext context )
{
    const MaterialGraphOutput graph = evaluateMaterialGraph( context );
    MaterialSurface           surface;
    surface._albedo    = float4( graph._baseColor, graph._alpha );
    surface._normal    = graph._normal;
    surface._roughness = saturate( graph._roughness );
    surface._metallic  = saturate( graph._metallic );
    surface.setAlphaCutoff( context._material._alphaCutoff ); // 알파 컷오프 머티리얼만 싣는다 — 깊이 · 본 패스가 같은 값으로 오린다
    return surface;
}

#if defined( SW_MATERIAL_EMISSIVE )
float3 computeMaterialEmissive( PixelContext context )
{
    const MaterialGraphOutput graph = evaluateMaterialGraph( context );
    return graph._emissive;
}
#endif

#include "material_shader_main.hlsli"

#endif // SW_ENGINE_MATERIALGRAPHMAIN_HLSLI

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

#if defined( SW_MATERIAL_GRAPH_HAIR )
    #include "hair_shading.hlsli"
    // 깊이 패스에는 탄젠트 칸이 없고 조명 훅도 부르지 않는다 — 가닥 방향을 읽는 몸통은 색 패스에만 둔다.
    #if !defined( SW_MATERIAL_USES_TANGENT )
float4 computeMaterialLighting( PixelContext context, MaterialSurface surface )
{
    return surface._albedo;
}
    #elif SW_VIEWMODE_SKIPS_LIGHTING
float4 computeMaterialLighting( PixelContext context, MaterialSurface surface )
{
    return surface._albedo;
}
    #else
/** @brief 셰이딩 모델 Hair 의 조명 — hair.hlsl 과 같은 빛 고리(앰비언트 한 번 + 빛마다 R · TT · TRT + 다중 산란)다. 자체 발광 핀이 있으면 더한다. */
float4 computeMaterialLighting( PixelContext context, MaterialSurface surface )
{
    const float3 albedo   = ViewMode::overrideLightingAlbedo( surface._albedo.rgb );
    const float3 tangentU = context._tangent.xyz;
    const float3 tangentV = -cross( context._normal, tangentU ) * context._tangent.w; // 메시 규약: cross( N, T ) × w = −dP/dv

    HairSurface hair;
    hair._baseColor = albedo;
    hair._roughness = surface._roughness;
    hair._shift     = context._material._hairShift;
    hair._specular  = 1.0f;
    hair._scatter   = context._material._hairScatter;
    hair._tangent   = normalize( context._material._hairStrandAxis > 0.5f ? tangentV : tangentU );

    float3 color = HairShading::shadeLights( hair, context._worldPosition, surface._normal );
        #if defined( SW_MATERIAL_GRAPH_EMISSIVE )
    color += computeMaterialEmissive( context );
        #endif
    return float4( color, surface._albedo.a );
}
    #endif
#endif

#include "material_shader_main.hlsli"

#endif // SW_ENGINE_MATERIALGRAPHMAIN_HLSLI

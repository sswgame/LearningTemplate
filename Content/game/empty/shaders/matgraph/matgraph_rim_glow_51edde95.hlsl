// clang-format off
// Generated from game/empty/materials/matgraph/rim_glow.matgraph.xml by MaterialGraphCompiler - edit the graph, not this file.
#include "lighting.hlsli"

SW_MATERIAL_BEGIN
{
    float4 _tint;
    float4 _rimColor;
    float _alphaCutoff;
    uint _albedo;
    float _roughness;
    float _rimPower;
}
SW_MATERIAL_END

#define SW_MATERIAL_GRAPH_EMISSIVE
#include "material_graph.hlsli"

MaterialGraphOutput evaluateMaterialGraph( PixelContext context )
{
    // Tint (ColorParameter)
    const float4 n1_RGBA = context._material._tint;
    const float3 n1_RGB = n1_RGBA.rgb;
    const float n1_A = n1_RGBA.a;
    // Albedo (TextureSample)
    const float4 n2_RGBA = MaterialTexture::sampleIndex( context._material._albedo, context._uv );
    const float3 n2_RGB = n2_RGBA.rgb;
    const float n2_R = n2_RGBA.r;
    const float n2_G = n2_RGBA.g;
    const float n2_B = n2_RGBA.b;
    const float n2_A = n2_RGBA.a;
    // Base (Multiply)
    const float3 n3_Out = n1_RGB * n2_RGB;
    // Rough (ScalarParameter)
    const float n4_Out = context._material._roughness;
    // RimColor (ColorParameter)
    const float4 n5_RGBA = context._material._rimColor;
    const float3 n5_RGB = n5_RGBA.rgb;
    const float n5_A = n5_RGBA.a;
    // RimPower (ScalarParameter)
    const float n6_Out = context._material._rimPower;
    // Rim/Fresnel (Fresnel)
    const float n7_Out = pow( saturate( 1.0f - dot( normalize( context._normal ), View::computeToView( context._worldPosition ) ) ), n6_Out );
    // Glow (Multiply)
    const float3 n8_Out = n5_RGB * float3( n7_Out, n7_Out, n7_Out );
    MaterialGraphOutput output;
    output._baseColor = n3_Out;
    output._metallic = 0.0f;
    output._roughness = n4_Out;
    output._normal = context._normal;
    output._emissive = n8_Out;
    output._alpha = 1.0f;
    return output;
}

#include "material_graph_main.hlsli"

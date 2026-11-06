// clang-format off
// Generated from game/empty/materials/matgraph/layered_stone.matgraph.xml by MaterialGraphCompiler - edit the graph, not this file.
#include "lighting.hlsli"

SW_MATERIAL_BEGIN
{
    float4 _tint;
    float4 _Dirt_color;
    float _alphaCutoff;
    float _DirtWeight;
    float _Dirt_roughness;
    uint _swPad0;
}
SW_MATERIAL_END

#include "material_graph.hlsli"

MaterialGraphOutput evaluateMaterialGraph( PixelContext context )
{
    // Tint (ColorParameter)
    const float4 n1_RGBA = context._material._tint;
    const float3 n1_RGB = n1_RGBA.rgb;
    const float n1_A = n1_RGBA.a;
    MaterialGraphOutput output;
    output._baseColor = n1_RGB;
    output._metallic = 0.0f;
    output._roughness = 0.5f;
    output._normal = context._normal;
    output._emissive = float3( 0.0f, 0.0f, 0.0f );
    output._alpha = 1.0f;
    // Up (WorldNormal)
    const float3 n2_Out = context._normal;
    // UpY (Split3)
    const float n3_X = n2_Out.x;
    const float n3_Y = n2_Out.y;
    const float n3_Z = n2_Out.z;
    // Dirt/Color (ColorParameter)
    const float4 n4_RGBA = context._material._Dirt_color;
    const float3 n4_RGB = n4_RGBA.rgb;
    const float n4_A = n4_RGBA.a;
    // Dirt/Rough (ScalarParameter)
    const float n5_Out = context._material._Dirt_roughness;
    // Dirt (Layer game/empty/materials/matgraph/layer_dirt.matgraph.xml)
    const float n6_Mask = saturate( n3_Y * context._material._DirtWeight );
    output._baseColor = lerp( output._baseColor, n4_RGB, n6_Mask );
    output._roughness = lerp( output._roughness, n5_Out, n6_Mask );
    return output;
}

#include "material_graph_main.hlsli"

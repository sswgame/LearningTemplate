#include "binding.hlsli"

struct PSInput
{
    float4 _position : SV_POSITION;
    float2 _uv : TEXCOORD0;
};

PSInput VSMain( VertexInput input, uint vertexID : SV_VertexID )
{
    PSInput output;
    input._position     = input._position;
    float2 clipPosition = float2( ( vertexID == 1 ) ? 3.0f : -1.0f, ( vertexID == 2 ) ? 3.0f : -1.0f );
    output._position    = float4( clipPosition, 0.0f, 1.0f );
    output._uv          = clipPosition * float2( 0.5f, -0.5f ) + 0.5f;
    return output;
}

float4 PSMain( PSInput input ) : SV_TARGET
{
    float     depth        = PassTexture::sampleDepth( input._uv ).r;
    float3    normal       = PassTexture::sampleNormal( input._uv );
    float2    texel        = g_OutlineParams.yz;
    float     occlusion    = 0.0f;
    const int kSampleCount = 4;
    float2    arrOffset[4] = {
        float2( -texel.x, 0 ), float2( texel.x, 0 ), float2( 0, -texel.y ), float2( 0, texel.y ) };
    SW_UNROLL for ( int sampleIndex = 0; sampleIndex < kSampleCount; ++sampleIndex )
    {
        float  sampleDepth     = PassTexture::sampleDepth( input._uv + arrOffset[sampleIndex] * 2.0f ).r;
        float3 sampleNormal    = PassTexture::sampleNormal( input._uv + arrOffset[sampleIndex] * 2.0f );
        float  depthDifference = saturate( ( sampleDepth - depth ) * 40.0f );
        float  normalAgreement = saturate( dot( normal, sampleNormal ) );
        occlusion += ( 1.0f - depthDifference ) * ( 0.5f + 0.5f * normalAgreement );
    }
    occlusion = saturate( occlusion / kSampleCount );
    return float4( occlusion, occlusion, occlusion, 1.0f );
}

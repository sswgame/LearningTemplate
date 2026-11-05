#include "binding.hlsli"
#include "fullscreen.hlsli"

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

#include "binding.hlsli"

#include "post_outline.hlsli"

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
    // 본문은 post_outline.hlsli 하나다 — 합친 체인(post_chain.hlsl)과 **같은 코드**를 쓴다.
    float2 texel = g_OutlineParams.yz;
    float3 color = PassTexture::sampleSourcePoint( input._uv ).rgb;
    // 후처리를 끈 뷰(CCTV · 표면 값 보기 모드) · 품질 미달이면 이 패스를 기록하지 않고 출력 이름이 원본을 가리킨다(RenderPassTraitFlag::kBypassesToSource).
    return float4( Outline::apply( input._uv, texel, color ), 1.0f );
}

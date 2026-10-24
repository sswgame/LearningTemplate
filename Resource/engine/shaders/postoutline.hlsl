#include "binding.hlsli"

#include "postoutline.hlsli"

struct PSInput
{
    float4 _position : SV_POSITION;
    float2 _uv : TEXCOORD0;
};

PSInput VSMain( VertexInput input, uint vertexId : SV_VertexID )
{
    PSInput output;
    input._position     = input._position;
    float2 clipPosition = float2( ( vertexId == 1 ) ? 3.0f : -1.0f, ( vertexId == 2 ) ? 3.0f : -1.0f );
    output._position    = float4( clipPosition, 0.0f, 1.0f );
    output._uv          = clipPosition * float2( 0.5f, -0.5f ) + 0.5f;
    return output;
}

float4 PSMain( PSInput input ) : SV_TARGET
{
    // 본문은 postoutline.hlsli 하나다 — 합친 체인(postchain.hlsl)과 **같은 코드**를 쓴다.
    float2 texel = g_OutlineParams.yz;
    float3 color = PassTexture::sampleSourcePoint( input._uv ).rgb;
    // 후처리를 끈 뷰(CCTV · 표면 값 보기 모드)는 원본을 낸다 — 포맷이 달라 복사할 수 없을 때 렌더러가 이 셰이더로 원본을 옮긴다. 값 선택이다(postchain 과 같은 이유).
    const bool bSkipPost = ( g_Flags & SW_PASS_FLAG_SKIP_POST ) != 0u;
    return float4( bSkipPost ? color : Outline::apply( input._uv, texel, color ), 1.0f );
}

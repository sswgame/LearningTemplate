#include "binding.hlsli"
#include "fullscreen.hlsli"

#include "post_outline.hlsli"

float4 PSMain( PSInput input ) : SV_TARGET
{
    // 본문은 post_outline.hlsli 하나다 — 합친 체인(post_chain.hlsl)과 **같은 코드**를 쓴다.
    float2 texel = g_OutlineParams.yz;
    float3 color = PassTexture::sampleSourcePoint( input._uv ).rgb;
    // 후처리를 끈 뷰(CCTV · 표면 값 보기 모드) · 품질 미달이면 이 패스를 기록하지 않고 출력 이름이 원본을 가리킨다(RenderPassTraitFlag::kBypassesToSource).
    return float4( Outline::apply( input._uv, texel, color ), 1.0f );
}

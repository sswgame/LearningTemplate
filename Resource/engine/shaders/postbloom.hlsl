#include "binding.hlsli"

#include "postbloom.hlsli"

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

#if defined( SW_PASS_BLOOM_DOWNSAMPLE )
float4 PSMain( PSInput input ) : SV_TARGET
{
    // BloomDownsample 패스 — 반해상도 출력에 밝은 부분만 낸다(합성은 Bloom 패스가 한다). 원본은 원래 해상도라 g_SourceTexel 이 원본 텍셀이다.
    // 블룸이 꺼진 뷰에서는 읽는 패스가 없어 이 패스를 기록하지 않는다(FrameRenderer::applyViewPassSelection).
    return float4( Bloom::computeBrightDownsampled( input._uv, g_SourceTexel.xy ), 1.0f );
}
#else
float4 PSMain( PSInput input ) : SV_TARGET
{
    // 본문은 postbloom.hlsli 하나다 — 합친 체인(postchain.hlsl)과 **같은 코드**를 쓴다.
    // 블룸은 원본을 텍셀 반 칸 비켜 읽는다 — 원본(반해상도일 수 있다)의 텍셀이다(g_SourceTexel).
    float2 texel = g_SourceTexel.xy;
    float3 color = PassTexture::sampleSourcePoint( input._uv ).rgb;
    // 후처리를 끈 뷰(CCTV · 표면 값 보기 모드) · 품질 미달이면 이 패스를 기록하지 않고 출력 이름이 원본을 가리킨다(RenderPassTraitFlag::kBypassesToSource).
    // 파이프라인이 반해상도 밝은 부분(BloomBright)을 입력으로 선언했으면 그것을 업샘플해 더하고, 아니면 여기서 구한다. 인덱스는 패스 상수라 분기가 고르다.
    const bool   bHasDownsampledBright = g_BloomBrightIndex != kInvalidIndex;
    const float3 bloomed               = bHasDownsampledBright ? Bloom::applyBright( input._uv, color ) : Bloom::apply( input._uv, texel, color );
    return float4( bloomed, 1.0f );
}
#endif

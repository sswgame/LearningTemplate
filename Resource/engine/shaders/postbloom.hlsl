#include "binding.hlsli"

#include "postbloom.hlsli"

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

PSInput VSMain( VertexInput input, uint vertexId : SV_VertexID )
{
    PSInput output;
    input.position      = input.position;
    float2 clipPosition = float2( ( vertexId == 1 ) ? 3.0f : -1.0f, ( vertexId == 2 ) ? 3.0f : -1.0f );
    output.position     = float4( clipPosition, 0.0f, 1.0f );
    output.uv           = clipPosition * float2( 0.5f, -0.5f ) + 0.5f;
    return output;
}

#if defined( SW_PASS_BLOOM_DOWNSAMPLE )
float4 PSMain( PSInput input ) : SV_TARGET
{
    // BloomDownsample 패스 — 반해상도 출력에 밝은 부분만 낸다(합성은 Bloom 패스가 한다). 원본은 원래 해상도라 g_SourceTexel 이 원본 텍셀이다.
    // 후처리를 끈 뷰에서는 더할 것이 없다(0). 크기가 다른 출력이라 원본을 복사할 수 없다.
    const bool   bSkipPost = ( g_Flags & SW_PASS_FLAG_SKIP_POST ) != 0u;
    const float3 bright    = bSkipPost ? float3( 0.0f, 0.0f, 0.0f ) : Bloom::computeBrightDownsampled( input.uv, g_SourceTexel.xy );
    return float4( bright, 1.0f );
}
#else
float4 PSMain( PSInput input ) : SV_TARGET
{
    // 본문은 postbloom.hlsli 하나다 — 합친 체인(postchain.hlsl)과 **같은 코드**를 쓴다.
    // 블룸은 원본을 텍셀 반 칸 비켜 읽는다 — 원본(반해상도일 수 있다)의 텍셀이다(g_SourceTexel).
    float2 texel = g_SourceTexel.xy;
    float3 color = PassTexture::sampleSourcePoint( input.uv ).rgb;
    // 후처리를 끈 뷰(CCTV · 표면 값 보기 모드)는 원본을 낸다 — 포맷이 달라 복사할 수 없을 때 렌더러가 이 셰이더로 원본을 옮긴다. 값 선택이다(postchain 과 같은 이유).
    const bool bSkipPost = ( g_Flags & SW_PASS_FLAG_SKIP_POST ) != 0u;
    // 파이프라인이 반해상도 밝은 부분(BloomBright)을 입력으로 선언했으면 그것을 업샘플해 더하고, 아니면 여기서 구한다. 인덱스는 패스 상수라 분기가 고르다.
    const bool   bHasDownsampledBright = g_BloomBrightIndex != kInvalidIndex;
    const float3 bloomed               = bHasDownsampledBright ? Bloom::applyBright( input.uv, color ) : Bloom::apply( input.uv, texel, color );
    return float4( bSkipPost ? color : bloomed, 1.0f );
}
#endif

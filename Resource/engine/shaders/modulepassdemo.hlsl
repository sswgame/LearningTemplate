// 모듈 렌더 패스 시연 셰이더(ModuleRenderPass M2) — 게임 모듈이 등록한 패스(`Empty.ModulePassDemo`)가 넘긴 패스 상수로 원본 색을 물들인다.
// 모듈 상수의 이름은 엔진 패스 상수 칸(binding.hlsli 의 PassCB)의 이름이다. 이 셰이더는 다른 패스가 이 패스 동안 쓰지 않는 칸 `g_OutlineColor` 를 틴트로 빌린다 —
// rgb 가 곱할 색, a 가 섞는 비율(0 이면 원본 그대로). 렌더러는 이 패스를 그린 뒤 그 칸을 원래 값으로 되돌린다(executeModulePass).
// 입력은 파이프라인이 선언한 SourceColor 역할 첨부, 출력은 패스의 첫 출력이다. 톤맵 전의 HDR 색을 그대로 물들인다.
#include "binding.hlsli"

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

float4 PSMain( PSInput input ) : SV_TARGET
{
    const float3 source = PassTexture::sampleSourcePoint( input.uv ).rgb;
    const float3 tinted = source * g_OutlineColor.rgb;
    return float4( lerp( source, tinted, saturate( g_OutlineColor.a ) ), 1.0f );
}

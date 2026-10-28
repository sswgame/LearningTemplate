// 모듈 렌더 확장 시연 셰이더(ModuleRenderPass M3) — 게임 모듈(EmptyGame)의 확장 패스 `Empty.TintDemo` 가 프로그램으로 쓴다(파이프라인 `_listModuleShader` 에 선언).
// 풀스크린 삼각형을 장면 색 위에 프리멀티플라이 알파로 블렌드해 물들인다(색 = 틴트 × 비율, 알파 = 비율 — 장면 × (1 - 비율) + 틴트 × 비율). 원본을 읽지 않으므로 입력 첨부가 없다.
// 모듈 상수의 이름은 엔진 패스 상수 칸(binding.hlsli 의 PassCB)의 이름이다. 이 셰이더는 이 패스 동안 다른 곳이 쓰지 않는 칸 `g_OutlineColor` 를 틴트로 빌린다 —
// rgb 가 틴트 색, a 가 섞는 비율(0 이면 원본 그대로). 렌더러는 그린 뒤 그 칸을 원래 값으로 되돌린다(ModulePassContext 의 상수 범위).
#include "binding.hlsli"

struct PSInput
{
    float4 _position : SV_POSITION;
};

PSInput VSMain( VertexInput input, uint vertexId : SV_VertexID )
{
    PSInput output;
    input._position     = input._position;
    float2 clipPosition = float2( ( vertexId == 1 ) ? 3.0f : -1.0f, ( vertexId == 2 ) ? 3.0f : -1.0f );
    output._position    = float4( clipPosition, 0.0f, 1.0f );
    return output;
}

float4 PSMain( PSInput input ) : SV_TARGET
{
    const float amount = saturate( g_OutlineColor.a );
    return float4( g_OutlineColor.rgb * amount, amount );
}

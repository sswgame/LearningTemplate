// 모듈 렌더 확장 시연 셰이더(ExtensionRenderPass M4) — 게임 모듈(EmptyGame)의 프록시가 낸 메시 요소를 확장 패스 `Empty.DrawDemo` 가 그린다(파이프라인 `_listExtensionShader` 에 선언).
// 요소의 월드 행렬은 PassCB 의 g_World 로 오고(렌더러가 요소마다 새 상수 슬롯에 쓴다), 요소의 색은 패스 상수 칸 `g_OutlineColor` 의 rgb 를 빌린다(이 패스 동안 다른 곳이 쓰지 않는 칸).
// 인스턴스가 둘 이상인 요소는 SV_InstanceID 로 X 축으로 펼친다(요소 하나의 월드 행렬 · 상수를 모든 인스턴스가 본다). 조명은 없고 고정 방향의 단순 명암이다.
#include "binding.hlsli"

struct PSInput
{
    float4 _position : SV_POSITION;
    float3 _normal : TEXCOORD0;
};

PSInput VSMain( VertexInput input, uint instanceId : SV_InstanceID )
{
    PSInput      output;
    const float3 spread        = float3( (float)instanceId * 1.2f, 0.0f, 0.0f );
    const float4 worldPosition = mul( float4( input._position + spread, 1.0f ), g_World );
    output._position           = mul( worldPosition, g_ViewProj );
    output._normal             = mul( input._normal, (float3x3)g_World );
    return output;
}

float4 PSMain( PSInput input ) : SV_TARGET
{
    const float3 lightDirection = normalize( float3( 0.4f, 0.8f, -0.5f ) );
    const float  diffuse        = saturate( dot( normalize( input._normal ), lightDirection ) );
    return float4( g_OutlineColor.rgb * ( 0.25f + 0.75f * diffuse ), 1.0f );
}

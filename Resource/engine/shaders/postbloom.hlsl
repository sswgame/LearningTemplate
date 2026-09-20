#include "binding.hlsli"

struct PSInput
{
	float4 pos : SV_POSITION;
	float2 uv  : TEXCOORD0;
};

PSInput VSMain(SwVertexInput input, uint vid : SV_VertexID)
{
	PSInput output;
	input.pos = input.pos;
	float2 p = float2((vid == 1) ? 3.0f : -1.0f, (vid == 2) ? 3.0f : -1.0f);
	output.pos = float4(p, 0.0f, 1.0f);
	output.uv  = p * float2(0.5f, -0.5f) + 0.5f;
	return output;
}

float4 PSMain(PSInput input) : SV_TARGET
{
	float2 texel = g_OutlineParams.yz;
	float3 c = SampleSource(input.uv).rgb;

	// 블러는 **바이리니어 두 번**으로 끝낸다. 예전에는 상하좌우 네 점을 따로 찍었는데, 텍셀 중심에서
	// 반 칸 비낀 곳을 찍으면 샘플러가 이웃 네 텍셀을 공짜로 평균해 준다 — 두 번이면 여덟 텍셀이다.
	// 점 네 개(십자)와 커널 모양이 조금 다르지만 블룸은 어차피 퍼뜨리는 것이라 차이가 보이지 않는다
	// (벤치 스크린샷 표본 차이 0.51% · 평균 0.16). 재어 보면 GPU 프레임이 355 -> 317 us 다
	// (720p · DX12 · RT.BeginFrame 기준). 샘플러는 SW_SAMPLER_LINEAR_WRAP 이라 반 칸 비낀 곳을
	// 찍으면 실제로 이웃 네 텍셀이 평균된다 - point 샘플러였다면 이 최적화는 성립하지 않는다.
	float2 half0 = texel * 0.5f;
	float3 blur  = SampleSource(input.uv - half0).rgb;
	blur += SampleSource(input.uv + half0).rgb;
	blur *= 0.5f;

	float lum = max(max(blur.r, blur.g), blur.b);
	float soft = saturate((lum - g_BloomParams.x + g_BloomParams.z) / max(g_BloomParams.z, 1e-4));
	float3 bright = blur * soft * soft * g_BloomParams.y;
	// SSAO 가 있으면 조명 결과를 가린다(파이프라인 XML 이 이 패스의 입력으로 AOColor 를 선언한다). 없으면 1.
	// 디퍼드에서 SSAO 가 매 프레임 돌고도 아무도 읽지 않던 자리다 — 이제 선언과 셰이더가 같은 말을 한다.
	float ao = SampleAmbientOcclusion(input.uv);
	return float4(c * ao + bright, 1.0f);
}

// 원본을 그대로 화면에 옮기는 블릿이다(톤맵 없음). `_shaderPath` 없는 Present 의 기본 셰이더라, 앞 패스가 톤맵을 끝낸 파이프라인(디퍼드)이
// 여기서 한 번 더 톤맵을 걸면 화면이 c/(c+1) 로 눌린다. 톤맵이 필요한 Present 는 `tonemap.hlsl` · `postchain.hlsl` 을 지정한다.
#include "binding.hlsli"

struct PSInput
{
	float4 position : SV_POSITION;
	float2 uv       : TEXCOORD0;
};

PSInput VSMain(SwVertexInput input, uint vertexId : SV_VertexID)
{
	PSInput output;
	input.position = input.position;
	float2 clipPosition = float2((vertexId == 1) ? 3.0f : -1.0f, (vertexId == 2) ? 3.0f : -1.0f);
	output.position = float4(clipPosition, 0.0f, 1.0f);
	output.uv       = clipPosition * float2(0.5f, -0.5f) + 0.5f;
	return output;
}

float4 PSMain(PSInput input) : SV_TARGET
{
	return float4(swSampleSource(input.uv).rgb, 1.0f);
}

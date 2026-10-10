#include "binding.hlsli"

#include "postbloom.hlsli"

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
	// 본문은 postbloom.hlsli 하나다 — 합친 체인(postchain.hlsl)과 **같은 코드**를 쓴다.
	// 블룸은 원본을 텍셀 반 칸 비켜 읽는다 — 원본(반해상도일 수 있다)의 텍셀이다(g_SourceTexel).
	float2 texel = g_SourceTexel.xy;
	float3 color = swSampleSourcePoint(input.uv).rgb;
	// 후처리를 끈 뷰(CCTV · 표면 값 보기 모드)는 원본을 낸다 — 포맷이 달라 복사할 수 없을 때 렌더러가 이 셰이더로 원본을 옮긴다. 값 선택이다(postchain 과 같은 이유).
	const bool bSkipPost = (g_Flags & SW_PASS_FLAG_SKIP_POST) != 0u;
	return float4(bSkipPost ? color : swApplyBloom(input.uv, texel, color), 1.0f);
}

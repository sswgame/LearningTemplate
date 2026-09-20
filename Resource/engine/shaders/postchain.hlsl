/**
 * @file postchain.hlsl
 * @brief 전체화면 후처리를 **한 패스**로 합친다 — 무엇을 적용할지는 파이프라인 XML 의 퍼뮤테이션이 정한다.
 * @details 전체화면 패스는 픽셀당 비용이 지배적이고(해상도를 1/4 로 줄이면 비용도 1/4), 그 비용의
 *          대부분은 계산이 아니라 **중간 타깃에 쓰고 다시 읽는 왕복**이다. 720p 왕복 하나가 약
 *          57 us 였다 — 블룸·외곽선·톤맵을 따로 두면 그 왕복을 두 번 더 낸다.
 *
 *          그렇다고 셰이더 하나에 효과를 박아 넣으면 파이프라인 XML 로 패스를 끼우고 빼던 조립이
 *          깨진다. 그래서 효과는 각자 `.hlsli` 의 **함수**로 두고, 이 셰이더는 퍼뮤테이션으로 부를
 *          것만 부른다 — 끄고 켜는 일은 여전히 XML 의 `_listPermutation` 한 줄이다.
 *
 *          `SW_POST_*` 를 하나도 안 켜면 이 셰이더는 `fullscreenblit.hlsl` 과 같다(원본을 그대로 낸다).
 */
#include "binding.hlsli"

#if defined( SW_POST_BLOOM )
	#include "postbloom.hlsli"
#endif
#if defined( SW_POST_OUTLINE )
	#include "postoutline.hlsli"
#endif

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
	// 화면과 1:1 이라 UV 가 텍셀 중심에 정확히 떨어진다 — 섞을 것이 없으니 점 샘플러로 읽는다.
	float3 color = SampleSourcePoint(input.uv).rgb;

#if defined( SW_POST_BLOOM )
	color = SwApplyBloom(input.uv, texel, color);
	// **여기서 자르는 이유.** 패스를 나눠 두었을 때는 블룸 결과가 `R8G8B8A8_UNORM` 중간 타깃에
	// 쓰이면서 [0,1] 로 잘렸다. 합치면 그 자름이 사라져 밝은 부분이 달라 보인다 — 합치기는
	// **성능 변경이지 룩 변경이 아니어야** 하므로 같은 자리에서 똑같이 자른다.
	// (톤맵까지 HDR 로 가져가고 싶다면 이 줄을 지우면 된다. 그건 의도적인 룩 변경이다.)
	color = saturate(color);
#endif
#if defined( SW_POST_OUTLINE )
	color = SwApplyOutline(input.uv, texel, color);
#endif
#if defined( SW_POST_TONEMAP )
	color = color / (color + 1.0f);
#endif
	return float4(color, 1.0f);
}

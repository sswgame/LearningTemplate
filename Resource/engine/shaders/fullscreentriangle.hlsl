#include "common.hlsli"

struct VSInput
{
	float3 pos : POSITION;
	float4 col : COLOR;
};

struct PSInput
{
	float4 pos : SV_POSITION;
	float4 col : COLOR;
};

// b1(MaterialCB)을 네 백엔드에서 모두 실제 상수 버퍼로 쓸 수 있는지 지키는 픽스처다.
// Vulkan 이 이 슬롯을 푸시 상수로 우회하던 시절에는 파이프라인 레이아웃에 없는 디스크립터를
// 참조해 RHITest 가 segfault 했다.
SW_DECLARE_CBUFFER( MaterialCB, 1 )
{
	float4 g_MaterialColor;
};

PSInput VSMain(VSInput input)
{
	PSInput output;
	output.pos = float4(input.pos, 1.0f);
	output.col = input.col;
	return output;
}

float4 PSMain(PSInput input) : SV_TARGET
{
	return input.col * g_MaterialColor;
}

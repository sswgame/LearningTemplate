#include "common.hlsli"

// 프로보킹 정점 프로브 — 삼각형의 세 정점이 서로 다른 nointerpolation 값을 내면, 래스터라이저가 어느
// 정점의 값을 삼각형 전체에 쓰는지(FIRST / LAST)가 픽셀 색으로 드러난다. DX·Vulkan 은 FIRST 가 규약이고
// OpenGL 기본은 LAST 라 엔진이 glProvokingVertex( FIRST ) 로 맞춘다 — 이 셰이더가 그 한 줄의 검증 대상이다
// (RHIDeviceTest.ProvokingVertexIsFirstOnAllBackends). 엔진 셰이더의 flat 값(materialIndex)은 배치 안에서 전부
// 같아서 그림으로는 확인할 수 없다.
struct PSInput
{
	float4 position               : SV_POSITION;
	nointerpolation uint vertexId : TEXCOORD1;
};

PSInput VSMain(SwVertexInput input, uint vertexId : SV_VertexID)
{
	PSInput output;
	float2 clipPosition = float2((vertexId == 1) ? 3.0f : -1.0f, (vertexId == 2) ? 3.0f : -1.0f);
	output.position = float4(clipPosition, 0.0f, 1.0f);
	output.vertexId = vertexId;
	return output;
}

float4 PSMain(PSInput input) : SV_TARGET
{
	// 정점 0 = 빨강, 1 = 초록, 2 = 파랑. FIRST 면 화면 전체가 빨강, LAST 면 파랑이다.
	if (input.vertexId == 0)
		return float4(1.0f, 0.0f, 0.0f, 1.0f);
	if (input.vertexId == 1)
		return float4(0.0f, 1.0f, 0.0f, 1.0f);
	return float4(0.0f, 0.0f, 1.0f, 1.0f);
}

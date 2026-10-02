// 간접 드로우 시험용 지오메트리(삼각형 둘 = 사각형 하나, sampleindirect.hlsl 이 정점 6 개를 적는다) — SV_VertexID 로 위치 · 색을 고른다.
// 정점 입력은 쓰지 않지만 SwVertexInput 을 받는다(Vulkan · GL 의 location 계약, common.hlsli 6). 그 include 가 빠져 있어
// 1a49d645 부터 이 파일은 컴파일되지 않았고, 커밋된 바이너리는 그 전 선언(VSInput)으로 구운 것이었다.
#include "common.hlsli"

struct PSInput
{
	float4 position : SV_POSITION;
	float4 color    : COLOR;
};

PSInput VSMain(SwVertexInput input, uint vertexId : SV_VertexID)
{
	PSInput output;

	
	float2 arrPosition[6] = {
		float2(-0.8f,  0.8f), 
		float2( 0.8f, -0.8f), 
		float2(-0.8f, -0.8f), 

		float2(-0.8f,  0.8f), 
		float2( 0.8f,  0.8f), 
		float2( 0.8f, -0.8f)  
	};

	float4 arrColor[6] = {
		float4(1.0f, 0.0f, 0.0f, 1.0f),
		float4(0.0f, 1.0f, 0.0f, 1.0f),
		float4(0.0f, 0.0f, 1.0f, 1.0f),
		
		float4(1.0f, 0.0f, 0.0f, 1.0f),
		float4(1.0f, 1.0f, 0.0f, 1.0f),
		float4(0.0f, 1.0f, 0.0f, 1.0f)
	};

	output.position = float4(arrPosition[vertexId % 6], 0.0f, 1.0f);
	output.color = arrColor[vertexId % 6];
	
	return output;
}

float4 PSMain(PSInput input) : SV_TARGET
{
	return input.color;
}

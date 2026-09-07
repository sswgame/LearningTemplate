#include "common.hlsli"

struct ComputeData
{
	float4 color;
	float  factor;
};

// 바인딩 계약(bindingslots.hlsli): 컴퓨트 UAV u0. C++: bindComputeUAV( index, 0 ).
SW_DECLARE_RW_STRUCTURED_BUFFER( ComputeData, g_OutputBuffer, 0 );

[numthreads( 64, 1, 1 )]
void CSMain( uint3 dispatchThreadID : SV_DispatchThreadID )
{
	uint index = dispatchThreadID.x;
	g_OutputBuffer[index].color = float4( 0.2f, 0.8f, 0.4f, 1.0f );
	g_OutputBuffer[index].factor = index * 1.5f;
}

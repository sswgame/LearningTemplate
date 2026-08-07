






struct ComputeData
{
	float4 color;
	float  factor;
};

RWStructuredBuffer<ComputeData> g_OutputBuffer : register( u0 );

[numthreads( 64, 1, 1 )]
void CSMain( uint3 dispatchThreadID : SV_DispatchThreadID )
{
	uint index = dispatchThreadID.x;
	g_OutputBuffer[index].color = float4( 0.2f, 0.8f, 0.4f, 1.0f );
	g_OutputBuffer[index].factor = index * 1.5f;
}

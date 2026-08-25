




struct PSInput
{
	float4 pos : SV_POSITION;
	float4 col : COLOR;
};

struct VSInput
{
	float3 pos : POSITION;
	float4 col : COLOR;
};

PSInput VSMain(VSInput dummy, uint vertexID : SV_VertexID)
{
	PSInput output;

	
	float2 positions[6] = {
		float2(-0.8f,  0.8f), 
		float2( 0.8f, -0.8f), 
		float2(-0.8f, -0.8f), 

		float2(-0.8f,  0.8f), 
		float2( 0.8f,  0.8f), 
		float2( 0.8f, -0.8f)  
	};

	float4 colors[6] = {
		float4(1.0f, 0.0f, 0.0f, 1.0f),
		float4(0.0f, 1.0f, 0.0f, 1.0f),
		float4(0.0f, 0.0f, 1.0f, 1.0f),
		
		float4(1.0f, 0.0f, 0.0f, 1.0f),
		float4(1.0f, 1.0f, 0.0f, 1.0f),
		float4(0.0f, 1.0f, 0.0f, 1.0f)
	};

	output.pos = float4(positions[vertexID % 6], 0.0f, 1.0f);
	output.col = colors[vertexID % 6];
	
	return output;
}

float4 PSMain(PSInput input) : SV_TARGET
{
	return input.col;
}

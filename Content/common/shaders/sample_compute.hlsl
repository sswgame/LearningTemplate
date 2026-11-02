#include "common.hlsli"

struct ComputeData
{
    float4 _color;
    float  _factor;
    uint   _swPad0; // 원소 크기를 16 의 배수로 — DX 의 촘촘한 간격과 GL std430 배열 간격이 같아야 한다(ShaderBindingValidator)
    uint   _swPad1;
    uint   _swPad2;
};

// 바인딩 계약(binding_slots.hlsli): 컴퓨트 UAV u0. C++: bindComputeUAV( index, 0 ).
SW_DECLARE_RW_STRUCTURED_BUFFER( ComputeData, g_OutputBuffer, 0 );

SW_NUMTHREADS( 64, 1, 1 )
void CSMain( uint3 dispatchThreadID : SV_DispatchThreadID )
{
    uint index                    = dispatchThreadID.x;
    g_OutputBuffer[index]._color  = float4( 0.2f, 0.8f, 0.4f, 1.0f );
    g_OutputBuffer[index]._factor = index * 1.5f;
}

#include "common.hlsli"

struct PSInput
{
    float4 _position : SV_POSITION;
    float4 _color : COLOR;
};

// b1(MaterialCB)을 네 백엔드에서 모두 실제 상수 버퍼로 쓸 수 있는지 지키는 픽스처다 (GPUScene 을 거치지 않는 드로우).
// 한 백엔드가 이 슬롯을 푸시 상수 등으로 우회하면 파이프라인 레이아웃에 없는 디스크립터를 참조해 RHIDeviceTest 가 죽는다.
SW_DECLARE_CBUFFER( MaterialCB, SW_SLOT_MATERIAL_CB )
{
    float4 g_MaterialColor;
};

// 정점 입력 없이 SV_VertexID 로 NDC 큰 삼각형을 만든다 — 정점 버퍼를 걸지 않는 드로우(PSO 의 입력 레이아웃이 비어 있다).
PSInput VSMain( uint vertexID : SV_VertexID )
{
    PSInput output;
    output._position = float4( ( vertexID == 1u ) ? 3.0f : -1.0f, ( vertexID == 2u ) ? 3.0f : -1.0f, 0.0f, 1.0f );
    output._color    = float4( 1.0f, 1.0f, 1.0f, 1.0f );
    return output;
}

float4 PSMain( PSInput input ) : SV_TARGET
{
    return input._color * g_MaterialColor;
}

#include "common.hlsli"

// 인스턴스 슬롯 스트림 프로브 — 정점 셰이더가 정점 슬롯 1(인스턴스 스텝)의 uint 를 읽어 색으로 낸다. 스트림이 걸렸으면 7 이
// 들어와 화면이 초록이고, 안 걸렸으면(0 이나 정의되지 않은 값) 빨강이다. 엔진은 drawIndexedIndirect 를 부르지 않아서, 그
// 진입점만 슬롯 1 을 빠뜨려도 드러나지 않았다(RHIDeviceTest.IndexedIndirectDrawReadsInstanceSlotStream 이 이 셰이더로 잡는다).
struct PSInput
{
	float4 pos : SV_POSITION;
	nointerpolation uint slot : TEXCOORD1;
};

PSInput VSMain(SwVertexInput input, uint vid : SV_VertexID)
{
	PSInput output;
	float2 p = float2((vid == 1) ? 3.0f : -1.0f, (vid == 2) ? 3.0f : -1.0f);
	output.pos = float4(p, 0.0f, 1.0f);
	output.slot = input.instanceSlot;
	return output;
}

float4 PSMain(PSInput input) : SV_TARGET
{
	return (input.slot == 7u) ? float4(0.0f, 1.0f, 0.0f, 1.0f) : float4(1.0f, 0.0f, 0.0f, 1.0f);
}

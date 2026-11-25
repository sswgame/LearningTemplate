// rhi_contract.hlsl — RHI 계약 검사(C++ RHIContractCheck)의 풀스크린 그리기. 루트 상수의 색과 t11 구조 버퍼의 첫 원소를 더해 쓴다 — 어느 값이 걸렸는지가 픽셀에 남는다.
#define SW_OWN_ROOT_CONSTANTS 1
#include "binding.hlsli"
#include "fullscreen.hlsli"

SW_DECLARE_STRUCTURED_BUFFER( float4, g_RHIContractValues, SW_SLOT_PASS_SRV );

SW_ROOT_CONSTANTS_BEGIN
{
    float4 g_RHIContractColor;
};
SW_ROOT_CONSTANTS_END

float4 PSMain( PSInput input ) : SV_TARGET
{
    return SW_ROOT( g_RHIContractColor ) + g_RHIContractValues[0];
}

// 컴퓨트 RW 텍스처 쓰기 — RHITest.ComputeTextureUavWriteIsReadable 이 네 백엔드에서 결과를 읽어 비교한다.
// 바인딩 계약(bindingslots.hlsli): 대상 텍스처는 registerBindlessTextureUAV 인덱스(DX12/Vulkan) 또는
// SW_SLOT_COMPUTE_TEXUAV0 슬롯 서수(DX11/GL)로 고르고, 그 값은 루트 상수 g_TargetIndex 로 온다.
#include "binding.hlsli"

SW_ROOT_CONSTANTS_BEGIN
	uint g_TargetIndex;
	uint g_Width;
	uint g_Height;
	uint g_Pad0;
SW_ROOT_CONSTANTS_END

[numthreads( 8, 8, 1 )]
void CSMain( uint3 id : SV_DispatchThreadID )
{
	if ( id.x >= SW_ROOT( g_Width ) || id.y >= SW_ROOT( g_Height ) )
		return;
	// r = x, g = y (8비트 UNORM 에서 정확히 복원), b = 0, a = 1
	SW_StoreTex2D( SW_ROOT( g_TargetIndex ), id.xy, float4( id.x / 255.0f, id.y / 255.0f, 0.0f, 1.0f ) );
}

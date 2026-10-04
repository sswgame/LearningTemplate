// 거스트너 파도 프로브 — RenderPassGpuTest.WaterWaveShaderMatchesCpu 가 네 백엔드에서 결과를 읽어 CPU(WaterWaveMath)와 대조한다.
// water.hlsl 이 정점을 옮기는 **같은 함수**(gerstner.hlsli 의 swComputeGerstnerDisplacement)를 표본 자리마다 부르고, 변위 (x, y, z) 의 float 비트를
// RGBA8 텍스처에 그대로 싣는다 — 표본 하나 = 한 행의 텍셀 여섯(성분마다 16 비트 둘). 16 비트는 G(위 바이트) · A(아래 바이트)에 싣는다:
// 백엔드가 RGBA 를 BGRA 로 돌려줘도 G · A 는 자리가 같다.
// 바인딩 계약: 대상 텍스처는 registerBindlessTextureUav 인덱스(DX12/Vulkan) 또는 SW_SLOT_COMPUTE_TEXUAV0 슬롯 서수(DX11/GL)를 루트 상수로 받는다.
#include "binding.hlsli"
#include "gerstner.hlsli"

SW_ROOT_CONSTANTS_BEGIN
	uint   g_TargetIndex;
	uint   g_SampleCount;
	float  g_Time;
	uint   g_Pad0;
	float4 g_Wave0;
	float4 g_Wave1;
	float4 g_Wave2;
SW_ROOT_CONSTANTS_END

/** @brief 16 비트 하나를 G(위 바이트) · A(아래 바이트)에 싣는다. */
float4 encodeHalfWord( uint word )
{
	return float4( 0.0f, (float)( ( word >> 8 ) & 0xFFu ) / 255.0f, 0.0f, (float)( word & 0xFFu ) / 255.0f );
}

[numthreads( 64, 1, 1 )]
void CSMain( uint3 dispatchThreadId : SV_DispatchThreadID )
{
	const uint sampleIndex = dispatchThreadId.x;
	if ( sampleIndex >= SW_ROOT( g_SampleCount ) )
		return;
	// 표본 자리 — 시험의 CPU 쪽과 같은 식이다.
	const float2 origin = float2( -20.0f + (float)( sampleIndex % 8u ) * 5.25f, -15.0f + (float)( sampleIndex / 8u ) * 4.125f );
	float4       arrWave[SW_GERSTNER_WAVE_COUNT] = { SW_ROOT( g_Wave0 ), SW_ROOT( g_Wave1 ), SW_ROOT( g_Wave2 ), float4( 0.0f, 1.0f, 0.0f, 0.0f ) };
	const float3 displacement = swComputeGerstnerDisplacement( origin, SW_ROOT( g_Time ), arrWave );
	const uint3  bits         = asuint( displacement );
	swStoreRwTexture2D( SW_ROOT( g_TargetIndex ), uint2( 0u, sampleIndex ), encodeHalfWord( bits.x & 0xFFFFu ) );
	swStoreRwTexture2D( SW_ROOT( g_TargetIndex ), uint2( 1u, sampleIndex ), encodeHalfWord( bits.x >> 16 ) );
	swStoreRwTexture2D( SW_ROOT( g_TargetIndex ), uint2( 2u, sampleIndex ), encodeHalfWord( bits.y & 0xFFFFu ) );
	swStoreRwTexture2D( SW_ROOT( g_TargetIndex ), uint2( 3u, sampleIndex ), encodeHalfWord( bits.y >> 16 ) );
	swStoreRwTexture2D( SW_ROOT( g_TargetIndex ), uint2( 4u, sampleIndex ), encodeHalfWord( bits.z & 0xFFFFu ) );
	swStoreRwTexture2D( SW_ROOT( g_TargetIndex ), uint2( 5u, sampleIndex ), encodeHalfWord( bits.z >> 16 ) );
}

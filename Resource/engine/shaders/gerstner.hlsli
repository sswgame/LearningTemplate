/**
 * gerstner.hlsli — 거스트너 파도 변위 · 노멀. C++ `WaterWaveMath`(Source/Engine/Environment/Water)와 **같은 식 · 같은 계산 순서**다 — 부력 ·
 * 수면 질의가 CPU 에서 이 값을 다시 계산하므로 한쪽만 고치면 배가 수면에서 떠오르거나 가라앉는다.
 * RenderPassGPUTest.WaterWaveShaderMatchesCpu 가 컴퓨트(common/shaders/waterwaveprobe.hlsl)로 이 함수를 돌려 CPU 값과 대조한다.
 *
 * 파도 하나 = float4( 방향(라디안, +x 에서 +z 쪽), 파장, 진폭, 가파름 0..1 ). 진폭이나 파장이 0 이하면 빈 칸이다.
 * 파수 k = 2π / 파장, 각진동수 ω = √(g k)(깊은 물 분산 — g 는 설정된 물리 중력을 머티리얼 · 루트 상수로 받는다, 숫자를 두지 않는다), Q = 가파름 / (k × 진폭 × 파도 수) — 가파름 1 에서도 물마루가 고리를 만들지 않는다.
 */

#ifndef SW_ENGINE_GERSTNER_HLSLI
#define SW_ENGINE_GERSTNER_HLSLI

#include "common.hlsli"

/** @brief 진폭 · 파장이 0 보다 큰 파도 수 — Q 를 나누는 수다. */
uint swCountGerstnerWaves( float4 arrWave[SW_GERSTNER_WAVE_COUNT] )
{
	uint count = 0u;
	for ( uint waveIndex = 0u; waveIndex < SW_GERSTNER_WAVE_COUNT; ++waveIndex )
		count += ( arrWave[waveIndex].z > 0.0f && arrWave[waveIndex].y > 0.0f ) ? 1u : 0u;
	return count;
}

/**
 * @brief 파도 하나의 항 — 방향 · 파수 · Q · 위상. 빈 칸이면 false.
 */
bool swComputeGerstnerTerm( float4 wave, uint activeCount, float2 origin, float time, float gravity, out float2 outDirection, out float outWaveNumber, out float outSharpness,
                            out float outPhase )
{
	outDirection  = float2( 0.0f, 0.0f );
	outWaveNumber = 0.0f;
	outSharpness  = 0.0f;
	outPhase      = 0.0f;
	if ( wave.z <= 0.0f || wave.y <= 0.0f || activeCount == 0u )
		return false;
	outDirection        = float2( cos( wave.x ), sin( wave.x ) );
	outWaveNumber       = kTwoPi / wave.y;
	outSharpness        = wave.w / ( outWaveNumber * wave.z * (float)activeCount );
	const float speed   = sqrt( gravity * outWaveNumber );
	outPhase            = outWaveNumber * ( outDirection.x * origin.x + outDirection.y * origin.y ) - speed * time;
	return true;
}

/** @brief 수평 원점 (x, z) 의 점이 시간 @p time 에 옮겨 가는 변위(x, y, z). */
float3 swComputeGerstnerDisplacement( float2 origin, float time, float gravity, float4 arrWave[SW_GERSTNER_WAVE_COUNT] )
{
	const uint activeCount  = swCountGerstnerWaves( arrWave );
	float3     displacement = float3( 0.0f, 0.0f, 0.0f );
	for ( uint waveIndex = 0u; waveIndex < SW_GERSTNER_WAVE_COUNT; ++waveIndex )
	{
		float2 direction;
		float  waveNumber;
		float  sharpness;
		float  phase;
		if ( swComputeGerstnerTerm( arrWave[waveIndex], activeCount, origin, time, gravity, direction, waveNumber, sharpness, phase ) == false )
			continue;
		const float amplitude = arrWave[waveIndex].z;
		const float cosine    = cos( phase );
		const float sine      = sin( phase );
		displacement.x += sharpness * amplitude * direction.x * cosine;
		displacement.y += amplitude * sine;
		displacement.z += sharpness * amplitude * direction.y * cosine;
	}
	return displacement;
}

/** @brief 수평 원점 (x, z) 의 옮겨 간 점의 수면 노멀(정규화) — 옮겨 간 면의 두 접선(원점 x · z 편미분)의 외적이다. */
float3 swComputeGerstnerNormal( float2 origin, float time, float gravity, float4 arrWave[SW_GERSTNER_WAVE_COUNT] )
{
	const uint activeCount = swCountGerstnerWaves( arrWave );
	float3     tangentX    = float3( 1.0f, 0.0f, 0.0f );
	float3     tangentZ    = float3( 0.0f, 0.0f, 1.0f );
	for ( uint waveIndex = 0u; waveIndex < SW_GERSTNER_WAVE_COUNT; ++waveIndex )
	{
		float2 direction;
		float  waveNumber;
		float  sharpness;
		float  phase;
		if ( swComputeGerstnerTerm( arrWave[waveIndex], activeCount, origin, time, gravity, direction, waveNumber, sharpness, phase ) == false )
			continue;
		const float slope = waveNumber * arrWave[waveIndex].z;
		const float crest = sharpness * slope * sin( phase );
		const float cosine = cos( phase );
		tangentX += float3( -crest * direction.x * direction.x, slope * direction.x * cosine, -crest * direction.x * direction.y );
		tangentZ += float3( -crest * direction.x * direction.y, slope * direction.y * cosine, -crest * direction.y * direction.y );
	}
	return normalize( cross( tangentZ, tangentX ) );
}

#endif // SW_ENGINE_GERSTNER_HLSLI

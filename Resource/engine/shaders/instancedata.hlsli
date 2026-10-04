/**
 * instancedata.hlsli — GPUScene 인스턴스 원소 하나(SwInstanceData). C++ `GpuInstance`(GpuSceneSnapshot.h)와 바이트까지 같다(128 바이트).
 *
 * 그래픽스(binding.hlsli 가 g_SwInstances 로 건다)와 컴퓨트 셋(gpucull · instancesort 는 읽고, instanceanim 은 고쳐 쓴다)이
 * **이 파일 하나**를 include 한다. 구조체를 셰이더마다 베끼면 한쪽만 고쳤을 때 원소가 아무 말 없이 어긋난다.
 * ShaderBindingValidatorTest.InstanceElementLayoutMatchesCpuStruct 가 세 이름을 모두 쿠킹된 바이너리로 대조한다.
 *
 * 스프라이트 칸(uvStart · uvEnd · tint)은 **머티리얼 인스턴스를 만들지 않고** 인스턴스마다 다른 값을 싣는 자리다(언리얼 Custom Primitive
 * Data · 유니티 MaterialPropertyBlock 의 자리). 배치 키는 머티리얼 인스턴스라, 프레임마다 바뀌는 아틀라스 프레임 · 색을 인스턴스로
 * 바꾸면 스프라이트마다 배치가 갈린다. 여기 실으면 같은 텍스처의 스프라이트는 프레임 · 색이 달라도 한 드로우다.
 * 지금 읽는 셰이더는 sprite2d.hlsl 하나다(다른 셰이더에서는 그냥 실려 다니는 16 바이트 — 프레임 · 색 · 픽셀 스냅).
 */

#ifndef SW_ENGINE_INSTANCEDATA_HLSLI
#define SW_ENGINE_INSTANCEDATA_HLSLI

struct SwInstanceData
{
	float4x4 world;
	float3   boundsCenter;
	float    boundsRadius;
	uint     meshBatchIndex;
	uint     materialIndex;
	uint     blendMode;
	uint     spinSeed;  // GPU 회전 시드 — 0 이면 instanceanim.hlsl 이 건드리지 않는다
	uint     uvStart;   // 사각형의 왼쪽 위 꼭짓점이 읽는 UV (u, v) — unorm16 둘(u 가 하위 16비트)
	uint     uvEnd;     // 오른쪽 아래 꼭짓점이 읽는 UV — unorm16 둘. uvEnd.u < uvStart.u 면 좌우가 뒤집힌다
	uint     tint;      // 인스턴스 색 RGBA8 unorm(r 이 하위 바이트). 머티리얼 색 · 텍스처에 곱한다
	float    pixelSnap; // 픽셀 스냅 단위(자산 픽셀 하나의 월드 길이 = 1 / PPU). 0 이면 끈다 — sprite2d.hlsl 이 인스턴스 원점을 이 격자에 붙인다
	float    vertexAnimationPhase; // 정점 애니메이션(VAT) 시각 오프셋(초) — VAT 시계에 더해 인스턴스마다 다른 프레임을 고른다(binding.hlsli)
	uint     reserved0; // 원소를 128 바이트(16 의 배수)로 맞춘다 — 스칼라 셋이라 std430 에서도 오프셋이 C++ 과 같다
	uint     reserved1;
	uint     reserved2;
};

/** @brief unorm16 둘을 [0, 1] 실수 둘로 푼다(하위 16비트가 x). C++ `GpuSpriteInstanceData::makeUnorm16x2` 의 역이다. */
float2 swUnpackUnorm16x2( uint packed )
{
	return float2( packed & 0xFFFFu, packed >> 16 ) * ( 1.0f / 65535.0f );
}

/**
 * @brief 인스턴스의 UV 사각형 (u, v, 폭, 높이) — 머티리얼 uvRect 와 같은 꼴이다. 기본값(0, 0)–(1, 1)은 (0, 0, 1, 1) 이다.
 * @details 폭 · 높이는 음수일 수 있다(뒤집힌 프레임). 사각형 UV 에 아핀으로 적용하므로 정점에서 해도 픽셀에서 한 것과 같다.
 */
float4 swComputeInstanceUvRect( SwInstanceData instance )
{
	const float2 uvStart = swUnpackUnorm16x2( instance.uvStart );
	const float2 uvEnd   = swUnpackUnorm16x2( instance.uvEnd );
	return float4( uvStart, uvEnd - uvStart );
}

/** @brief 인스턴스 색 (r, g, b, a) — RGBA8 을 [0, 1] 로 푼다. 기본값은 흰색 불투명이다. */
float4 swComputeInstanceTint( SwInstanceData instance )
{
	const uint packed = instance.tint;
	return float4( packed & 0xFFu, ( packed >> 8 ) & 0xFFu, ( packed >> 16 ) & 0xFFu, packed >> 24 ) * ( 1.0f / 255.0f );
}

#endif // SW_ENGINE_INSTANCEDATA_HLSLI

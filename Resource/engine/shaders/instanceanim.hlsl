#include "common.hlsli"

/**
 * instanceanim.hlsl — GPUScene 인스턴스 애니메이션 (컴퓨트).
 *
 * CPU 가 매 프레임 오브젝트마다 회전을 계산해 트랜스폼을 다시 쓰던 것을 GPU 로 옮긴다. 인스턴스마다
 * **각속도가 다르다** — 시드를 해시해 속도를 뽑으므로 전부 같은 속도로 도는 일이 없다.
 *
 * 각도는 누적하지 않고 **절대 시간에서 매번 다시 만든다**. 누적하면 프레임 시간 흔들림이 그대로 쌓여
 * 백엔드마다 다른 각도가 나오고, 정지·재개가 불가능해진다.
 *
 * 바인딩 계약(bindingslots.hlsli): 컴퓨트 CB 는 b0, 인스턴스 읽기·쓰기 버퍼는 u0(RW).
 * C++: bindComputeConstantBuffer( cb, 0 ) / bindComputeUAV( instanceUav, 0 ).
 */

struct GpuInstance
{
	float4x4 world;
	float3	 boundsCenter;
	float	 boundsRadius;
	uint	 meshBatchIndex;
	uint	 materialIndex;
	uint	 blendMode;
	uint	 spinSeed; // 0 이면 이 인스턴스는 건드리지 않는다
};

SW_DECLARE_CBUFFER( AnimParams, SW_SLOT_COMPUTE_CB )
{
	float g_Time;          // 씬 시작부터의 절대 시간 (초)
	float g_SpinBaseSpeed; // 기준 각속도 (라디안/초)
	float g_SpinSpeedRange;// 시드가 만드는 속도 편차의 폭 (라디안/초)
	uint  g_AnimInstanceCount;
};

SW_DECLARE_RW_STRUCTURED_BUFFER( GpuInstance, g_InstancesRW, 0 );

/**
 * @brief 시드를 32비트 정수 해시로 섞는다 (Wang hash).
 * @details 시드가 1,2,3... 처럼 이어진 값이어도 결과가 골고루 흩어져야 한다. 곱셈만 쓰면 이웃한 시드가
 *          이웃한 속도를 받아 "줄줄이 같은 속도"가 되는데, 그게 정확히 피하려는 그림이다.
 */
uint SwHashSeed(uint seed)
{
	seed = (seed ^ 61u) ^ (seed >> 16);
	seed *= 9u;
	seed = seed ^ (seed >> 4);
	seed *= 0x27d4eb2du;
	seed = seed ^ (seed >> 15);
	return seed;
}

/** @brief 해시를 [0,1) 실수로. 상위 24비트만 쓴다 (하위 비트는 해시 품질이 낮다). */
float SwHashToUnit(uint hash)
{
	return float(hash >> 8) * (1.0f / 16777216.0f);
}

[numthreads(64, 1, 1)]
void CSMain(uint3 dtid : SV_DispatchThreadID)
{
	const uint idx = dtid.x;
	if (idx >= g_AnimInstanceCount)
		return;

	GpuInstance inst = g_InstancesRW[idx];
	if (inst.spinSeed == 0)
		return; // GPU 회전을 요청하지 않은 인스턴스는 CPU 가 올린 트랜스폼 그대로 둔다

	const uint  hash = SwHashSeed(inst.spinSeed);
	const float unit = SwHashToUnit(hash);

	// 속도는 기준 ± 폭. 시드의 최하위 비트로 **방향**도 가른다 — 속도만 다르면 전부 같은 쪽으로 돌아
	// 멀리서 보면 여전히 한 덩어리로 보인다.
	const float speed = g_SpinBaseSpeed + unit * g_SpinSpeedRange;
	const float dir   = (hash & 1u) != 0u ? -1.0f : 1.0f;

	// 위상도 시드마다 어긋나게 — 속도가 달라도 t=0 에서 전부 같은 각도로 출발하면 처음 몇 초가 어색하다.
	const float phase = SwHashToUnit(SwHashSeed(hash)) * 6.2831853f;
	const float angle = dir * speed * g_Time + phase;

	// CPU 가 올린 월드는 스케일 x 이동으로 본다(회전은 GPU 몫). 행벡터 규약(mul(v, M))이라 0~2행이
	// 축이고 3행이 이동이다. 축의 길이가 곧 스케일이므로 길이를 뽑아 회전을 새로 조립한다.
	const float3 scale = float3(length(inst.world[0].xyz), length(inst.world[1].xyz), length(inst.world[2].xyz));
	const float3 trans = inst.world[3].xyz;

	float s, c;
	sincos(angle, s, c);

	// world = Scale * RotY * Translate (행벡터 규약이라 왼쪽부터 적용된다).
	inst.world[0] = float4(scale.x * c, 0.0f, scale.x * -s, 0.0f);
	inst.world[1] = float4(0.0f, scale.y, 0.0f, 0.0f);
	inst.world[2] = float4(scale.z * s, 0.0f, scale.z * c, 0.0f);
	inst.world[3] = float4(trans, 1.0f);

	// 바운드 중심은 이동 성분이다 — 회전만 바꿨으므로 그대로지만, 컬링이 읽는 값이라 맞춰 둔다.
	inst.boundsCenter = trans;

	g_InstancesRW[idx] = inst;
}

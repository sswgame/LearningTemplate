#include "common.hlsli"

/**
 * gpucull.hlsl — GPU 컬링 + **드로우 커맨드 생성** (언리얼 FInstanceCullingContext 와 같은 자리).
 *
 * 스레드 하나가 인스턴스 하나를 맡는다. 절두체를 통과하면
 *   1) 자기 배치의 `instanceCount` 를 InterlockedAdd 로 하나 올려 **자리 번호를 받고**,
 *   2) 그 자리에 자기 인스턴스 번호를 적는다 (`g_VisibleInstanceIds`).
 * 그래서 간접 인자의 개수와 인스턴스 목록이 **함께** 만들어진다. 정점 셰이더는
 * `g_SwVisibleInstanceIds[g_InstanceBase + SV_InstanceID]` 로 자기 인스턴스를 찾는다.
 *
 * 예전 버전은 배치마다 스레드 하나를 두고 보이는 **개수만** 세어 `instanceCount` 에 넣었다. 그러면
 * 드로우는 늘 배치 앞쪽 N 개를 그린다 — 앞이 안 보이고 뒤가 보이는 상황에서 **보이는 쪽이 사라지고
 * 안 보이는 쪽이 그려졌다**. 개수만으로는 무엇을 그릴지 고를 수가 없다. 압축 목록이 그 답이다.
 *
 * 개수는 디스패치 전에 0 이어야 한다 — GpuScene 이 컬링이 켜져 있을 때 0 을 올린다.
 *
 * 바인딩 계약(bindingslots.hlsli): CB b0, 인스턴스 읽기 t0, 배치 구간 읽기 t1, 간접 인자 쓰기 u0,
 * 가시 ID 쓰기 u1.
 */

struct GpuInstance
{
	float4x4 world;
	float3	 boundsCenter;
	float	 boundsRadius;
	uint	 meshBatchIndex;
	uint	 materialIndex;
	uint	 blendMode;
	uint	 spinSeed;
};

struct DrawIndirectCommand
{
	uint vertexCount;
	uint instanceCount;
	uint startVertex;
	uint startInstance;
};

/**
 * 배치의 인스턴스 구간. 간접 인자의 startInstance 는 **0 이어야 해서**(Vulkan 의 InstanceIndex 가
 * firstInstance 를 포함하므로 셰이더가 루트 상수로 더한다) 컬링이 그 값을 배치 시작점으로 쓸 수 없다.
 * 그래서 시작점은 이 버퍼가 따로 알려준다 — 언리얼이 드로우 커맨드마다 인스턴스 구간을 들고 있는 것과 같다.
 */
struct GpuBatchInfo
{
	uint instanceBase;
	uint instanceCount;
	uint2 pad;
};

SW_DECLARE_CBUFFER( CullParams, SW_SLOT_COMPUTE_CB )
{
	float4 g_FrustumPlanes[6];
	uint   g_InstanceCount;
	uint   g_BatchCount;
	uint2  g_Pad;
};

SW_DECLARE_STRUCTURED_BUFFER( GpuInstance, g_Instances, 0 );
SW_DECLARE_STRUCTURED_BUFFER( GpuBatchInfo, g_BatchInfo, 1 );
SW_DECLARE_RW_STRUCTURED_BUFFER( DrawIndirectCommand, g_IndirectArgs, 0 );
SW_DECLARE_RW_STRUCTURED_BUFFER( uint, g_VisibleInstanceIds, 1 );

bool IsVisible(float3 center, float radius)
{
	[unroll]
	for (uint i = 0; i < 6; ++i)
	{
		float d = dot(g_FrustumPlanes[i].xyz, center) + g_FrustumPlanes[i].w;
		if (d < -radius)
			return false;
	}
	return true;
}

[numthreads(64, 1, 1)]
void CSMain(uint3 dtid : SV_DispatchThreadID)
{
	const uint instId = dtid.x;
	if (instId >= g_InstanceCount)
		return;

	const GpuInstance inst = g_Instances[instId];
	const uint batchIndex = inst.meshBatchIndex;
	if (batchIndex >= g_BatchCount)
		return;

	// 바운드 중심은 이미 월드 공간이다 (GpuScene 이 월드 행렬의 이동 성분으로 채우고, instanceanim 도
	// 회전 뒤에 다시 맞춘다). 여기서 또 world 를 곱하면 이동이 두 번 들어간다.
	if (IsVisible(inst.boundsCenter, inst.boundsRadius) == false)
		return;

	// 자리 하나를 예약하고 그 자리에 자기 번호를 적는다. slot 은 배치 안에서의 순서라 배치 시작
	// 오프셋(startInstance)을 더해야 전역 자리가 된다.
	uint slot = 0;
	InterlockedAdd(g_IndirectArgs[batchIndex].instanceCount, 1u, slot);

	const uint writeAt = g_BatchInfo[batchIndex].instanceBase + slot;
	if (writeAt < g_InstanceCount)
		g_VisibleInstanceIds[writeAt] = instId;
}

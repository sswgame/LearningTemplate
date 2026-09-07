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
	uint sortMode; // 0 = 없음(불투명), 1 = CPU 순서 유지, 2 = 압축 뒤 GPU 깊이 정렬
	uint pad;
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

	// sortMode 1 = 압축을 포기하는 배치. 인스턴스는 배치마다 연속으로 놓이므로 instId 가 곧 자기 자리다 —
	// 제자리 매핑을 적고 개수는 CPU 가 채운 값을 그대로 둔다. GPU 정렬 한계(SW_SORT_MAX_ELEMENTS)를 넘는
	// 큰 투명 배치만 여기로 온다. 나머지 투명(sortMode 2)은 압축한 뒤 instancesort.hlsl 이 깊이순으로 되돌린다.
	if (g_BatchInfo[batchIndex].sortMode == 1u)
	{
		g_VisibleInstanceIds[instId] = instId;
		return;
	}

	// 자리 하나를 예약하고 그 자리에 자기 번호를 적는다. slot 은 배치 안에서의 순서라 배치 시작
	// 오프셋을 더해야 전역 자리가 된다.
	uint slot = 0;
#if defined( DX11 )
	// SM5.0 에는 웨이브 인트린식이 없다 — 스레드마다 원자 연산을 한다.
	// (DX11 은 간접 인자 제약으로 컬링 자체를 돌리지 않으므로 이 경로는 실제로 쓰이지 않는다.)
	InterlockedAdd(g_IndirectArgs[batchIndex].instanceCount, 1u, slot);
#else
	// 웨이브 안에서 먼저 모으고 **웨이브당 한 번만** 원자 연산을 한다. 같은 배치에 인스턴스가 많을수록
	// (인스턴스드 렌더링에서는 그게 정상이다) 같은 주소에 대한 경합이 스레드 수만큼 쌓이기 때문이다.
	// 언리얼의 인스턴스 컬링도 같은 이유로 웨이브 단위로 접는다.
	//
	// 한 웨이브 안에 **여러 배치**가 섞일 수 있으므로 batchIndex 가 같은 레인끼리만 묶는다.
	// WaveMatch 는 SM6.5+ 라 쓰지 않고, 같은 값끼리 모으는 표준 관용구를 쓴다.
	const uint firstBatch = WaveReadLaneFirst(batchIndex);
	if (firstBatch == batchIndex)
	{
		// 이 웨이브에서 batchIndex 가 첫 레인과 같은 레인들만 여기 들어온다.
		const uint laneCount = WaveActiveCountBits(true);
		const uint laneRank  = WavePrefixCountBits(true);
		uint       waveBase  = 0;
		if (WaveIsFirstLane())
			InterlockedAdd(g_IndirectArgs[batchIndex].instanceCount, laneCount, waveBase);
		slot = WaveReadLaneFirst(waveBase) + laneRank;
	}
	else
	{
		// 첫 레인과 배치가 다른 레인들 — 드문 경우라 그냥 각자 원자 연산을 한다.
		InterlockedAdd(g_IndirectArgs[batchIndex].instanceCount, 1u, slot);
	}
#endif

	const uint writeAt = g_BatchInfo[batchIndex].instanceBase + slot;
	if (writeAt < g_InstanceCount)
		g_VisibleInstanceIds[writeAt] = instId;
}

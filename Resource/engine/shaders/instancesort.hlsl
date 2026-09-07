#include "common.hlsli"

/**
 * instancesort.hlsl — 배치 안의 **가시 인스턴스를 깊이순으로 정렬**한다 (컴퓨트).
 *
 * 컬링이 압축을 하면 자리 번호가 원자 연산의 **완료 순서**로 정해진다. 불투명은 상관없지만 투명은 그
 * 순서가 곧 블렌딩 순서라 그림이 틀린다. 그래서 예전에는 투명 배치를 아예 압축하지 않고 CPU 가 정렬해 둔
 * 제자리 매핑을 썼다 — 순서는 지켜지지만 **투명은 컬링 이득을 통째로 포기**했다.
 *
 * 여기서는 압축을 그대로 두고 **정렬을 GPU 로 옮긴다**. 컬링 뒤에 배치마다 워크그룹 하나가 붙어 그 배치의
 * 가시 목록을 카메라에서 먼 것부터 정렬한다. 그러면 투명도 컬링을 받으면서 순서가 맞는다.
 *
 * 정렬은 그룹공유 메모리 안의 **바이토닉 정렬**이다. 워크그룹 하나에 담기는 만큼(SW_SORT_MAX_ELEMENTS)만
 * 다룰 수 있다 — 그보다 큰 투명 배치는 CPU 가 정렬한 제자리 매핑을 그대로 쓴다(GpuScene 이 그런 배치에
 * sortMode = Preserve 를 준다). 배치 하나에 투명 인스턴스가 수백 개를 넘는 일은 드물고, 넘으면 정확성을
 * 포기하는 대신 컬링을 포기한다.
 *
 * 바인딩 계약(bindingslots.hlsli): CB b0, 인스턴스 t0, 배치 구간 t1, 간접 인자 u0, 가시 ID u1.
 * (컬링과 같은 자리라 바인딩을 갈아 끼우지 않고 PSO 만 바꿔 디스패치한다.)
 */

/// @brief 한 배치에서 GPU 정렬로 다룰 수 있는 최대 인스턴스 수. 바이토닉이라 2의 거듭제곱이어야 한다.
#define SW_SORT_MAX_ELEMENTS 512
#define SW_SORT_THREADS      256 // = SW_SORT_MAX_ELEMENTS / 2 (스레드마다 비교·교환 한 쌍)

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

struct GpuBatchInfo
{
	uint instanceBase;
	uint instanceCount;
	uint sortMode; // 0 = 정렬 없음(불투명), 1 = CPU 순서 유지, 2 = 여기서 깊이 정렬
	uint pad;
};

SW_DECLARE_CBUFFER( SortParams, SW_SLOT_COMPUTE_CB )
{
	float4 g_CameraPos;   // xyz = 월드 카메라 위치
	uint   g_InstanceCount;
	uint   g_BatchCount;
	uint2  g_SortPad;
};

SW_DECLARE_STRUCTURED_BUFFER( GpuInstance, g_Instances, 0 );
SW_DECLARE_STRUCTURED_BUFFER( GpuBatchInfo, g_BatchInfo, 1 );
SW_DECLARE_RW_STRUCTURED_BUFFER( DrawIndirectCommand, g_IndirectArgs, 0 );
SW_DECLARE_RW_STRUCTURED_BUFFER( uint, g_VisibleInstanceIds, 1 );

// 키와 값이 같이 움직여야 하므로 둘을 나란히 둔다. 키는 카메라까지의 거리 제곱(뒤에서 앞으로).
groupshared float s_key[SW_SORT_MAX_ELEMENTS];
groupshared uint  s_id[SW_SORT_MAX_ELEMENTS];

[numthreads(SW_SORT_THREADS, 1, 1)]
void CSMain(uint3 gid : SV_GroupID, uint3 gtid : SV_GroupThreadID)
{
	const uint batchIndex = gid.x;
	if (batchIndex >= g_BatchCount)
		return;

	const GpuBatchInfo info = g_BatchInfo[batchIndex];
	if (info.sortMode != 2u) // 이 배치는 여기서 정렬하지 않는다
		return;

	const uint count = min(g_IndirectArgs[batchIndex].instanceCount, (uint)SW_SORT_MAX_ELEMENTS);
	const uint base  = info.instanceBase;

	// 그룹공유로 올린다. 남는 자리는 키를 -1 로 둬 뒤로 밀어 두고, 되쓸 때 count 까지만 쓴다.
	// (바이토닉은 2의 거듭제곱 길이를 요구하므로 패딩이 필요하다.)
	for (uint load = gtid.x; load < (uint)SW_SORT_MAX_ELEMENTS; load += SW_SORT_THREADS)
	{
		if (load < count)
		{
			const uint instId = g_VisibleInstanceIds[base + load];
			s_id[load]        = instId;
			// 인스턴스 번호가 범위를 벗어나면(있어선 안 되지만) 맨 뒤로 보낸다.
			if (instId < g_InstanceCount)
			{
				const float3 d = g_Instances[instId].boundsCenter - g_CameraPos.xyz;
				s_key[load]    = dot(d, d);
			}
			else
				s_key[load] = -1.0f;
		}
		else
		{
			s_id[load]  = 0u;
			s_key[load] = -1.0f;
		}
	}
	GroupMemoryBarrierWithGroupSync();

	// 바이토닉 정렬 — **내림차순**(먼 것이 앞). 투명은 뒤에서 앞으로 그려야 블렌딩이 맞는다.
	for (uint k = 2u; k <= (uint)SW_SORT_MAX_ELEMENTS; k <<= 1u)
	{
		for (uint j = k >> 1u; j > 0u; j >>= 1u)
		{
			for (uint i = gtid.x; i < (uint)SW_SORT_MAX_ELEMENTS; i += SW_SORT_THREADS)
			{
				const uint partner = i ^ j;
				if (partner > i)
				{
					// (i & k) == 0 이면 이 구간은 내림차순으로 맞춘다.
					const bool bDescending = ((i & k) == 0u);
					const bool bSwap       = bDescending ? (s_key[i] < s_key[partner]) : (s_key[i] > s_key[partner]);
					if (bSwap)
					{
						const float tmpKey = s_key[i];
						s_key[i]           = s_key[partner];
						s_key[partner]     = tmpKey;
						const uint tmpId   = s_id[i];
						s_id[i]            = s_id[partner];
						s_id[partner]      = tmpId;
					}
				}
			}
			GroupMemoryBarrierWithGroupSync();
		}
	}

	for (uint store = gtid.x; store < count; store += SW_SORT_THREADS)
		g_VisibleInstanceIds[base + store] = s_id[store];
}

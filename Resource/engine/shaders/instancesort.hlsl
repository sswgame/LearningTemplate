#include "common.hlsli"

/**
 * instancesort.hlsl — 배치 안의 **가시 인스턴스를 CPU 정렬 순서로 되돌린다** (컴퓨트).
 *
 * 컬링이 압축을 하면 자리 번호가 원자 연산의 **완료 순서**로 정해진다. 불투명은 상관없지만 투명은 그
 * 순서가 곧 블렌딩 순서라 그림이 틀린다. 투명을 압축하지 않고 CPU 정렬 순서를 쓰면 순서는 지켜지지만 **투명은 컬링
 * 이득을 통째로 포기**한다.
 *
 * 여기서는 압축을 그대로 두고 **순서를 GPU 에서 되돌린다**. 투명 순서는 CPU 가 이미 정했다(GPUSceneBuilder::sortTransparent —
 * 정렬 레이어 키 → 깊이 → 후보 번호의 전순서)이고, 배치 안의 인스턴스는 그 순서대로 연속으로 놓인다. 그래서 **인스턴스 번호가
 * 곧 그리는 순서**다. 컬링 뒤에 배치마다 워크그룹 하나가 그 배치의 가시 목록을 인스턴스 번호 오름차순으로 정렬한다.
 * 깊이를 여기서 다시 재면 (1) 정렬 레이어를 모르고 (2) 같은 깊이를 불안정하게 가르며 (3) 직교 카메라의 시선 축 깊이를 모른다 —
 * CPU 와 GPU 가 다른 순서를 낸다. 정렬 기준은 CPU 한 곳뿐이다. 추가 뷰(CCTV · PiP)는 그 뷰의 눈으로 CPU 가 다시 정한 순번(t2,
 * GPUSceneBuilder::buildViewTransparentOrders)으로 정렬한다 — 키는 (순번 << 9) | 배치 안 번호.
 *
 * 정렬은 그룹공유 메모리 안의 **바이토닉 정렬**이다. 워크그룹 하나에 담기는 만큼(SW_SORT_MAX_ELEMENTS)만
 * 다룰 수 있다 — 그보다 큰 투명 배치는 CPU 가 정렬한 제자리 매핑을 그대로 쓴다(GPUScene 이 그런 배치에
 * sortMode = Preserve 를 준다). 배치 하나에 투명 인스턴스가 수백 개를 넘는 일은 드물고, 넘으면 정확성을
 * 포기하는 대신 컬링을 포기한다.
 *
 * 바인딩 계약(bindingslots.hlsli): CB b0, 인스턴스 t0, 배치 구간 t1, 뷰 순번 t2(추가 뷰 — 다른 뷰는 자리표), 간접 인자 u0, 가시 ID u1.
 * (컬링과 같은 자리라 바인딩을 갈아 끼우지 않고 PSO 만 바꿔 디스패치한다.)
 */

/// @brief 한 배치에서 GPU 정렬로 다룰 수 있는 최대 인스턴스 수. 바이토닉이라 2의 거듭제곱이어야 한다.
#define SW_SORT_MAX_ELEMENTS 512
#define SW_SORT_THREADS      256 // = SW_SORT_MAX_ELEMENTS / 2 (스레드마다 비교·교환 한 쌍)

// 인스턴스 원소(SwInstanceData)는 그래픽스와 같은 정의 하나를 쓴다(베낀 구조체는 계약 검사가 보지 않는다).
#include "instancedata.hlsli"

// C++ RHIDrawIndirectCommand 와 레이아웃 일치. 여기서 쓰는 것은 instanceCount 뿐이다 — startVertexLocation 는 정점 풀 시작,
// startInstanceLocation 는 배치의 인스턴스 시작(인스턴스 슬롯 스트림의 원소를 그만큼 건너뛴다).
struct RHIDrawIndirectCommand
{
	uint vertexCount;
	uint instanceCount;
	uint startVertexLocation;
	uint startInstanceLocation;
};

struct GPUBatchInfo
{
	uint instanceBase;
	uint instanceCount;
	uint sortMode;        // 0 = 없음(불투명), 1 = CPU 순서 유지, 2 = 압축 뒤 CPU 순서로 되돌림
	uint morphVertexBase; // 정점 셰이더용 — 여기서는 안 읽는다 (binding.hlsli SwBatchData 와 같은 표)
	uint firstVertex;     // 정점 셰이더용 — 여기서는 안 읽는다
	uint pad0;
	uint pad1;
	uint pad2;
};

SW_DECLARE_CBUFFER( SortParams, SW_SLOT_COMPUTE_CB )
{
	float4 g_CameraPos;           // xyz = 월드 카메라 위치
	uint   g_InstanceCount;
	uint   g_BatchCount;
	uint   g_UseViewRank;         // 1 = 추가 뷰 — t2 의 뷰 순번으로 정렬한다, 0 = 인스턴스 번호
	uint   g_TransparentTailBase; // 순번 표 0 번의 인스턴스 번호
};

SW_DECLARE_STRUCTURED_BUFFER( SwInstanceData, g_Instances, 0 );
SW_DECLARE_STRUCTURED_BUFFER( GPUBatchInfo, g_BatchInfo, 1 );
SW_DECLARE_STRUCTURED_BUFFER( uint, g_ViewRank, 2 );
SW_DECLARE_RW_STRUCTURED_BUFFER( RHIDrawIndirectCommand, g_IndirectArgs, 0 );
SW_DECLARE_RW_STRUCTURED_BUFFER( uint, g_VisibleInstanceIDs, 1 );

// 정렬할 값은 인스턴스 번호 하나다 — 번호가 곧 CPU 가 정한 그리는 순서다(위 주석).
groupshared uint s_arrId[SW_SORT_MAX_ELEMENTS];

[numthreads(SW_SORT_THREADS, 1, 1)]
void CSMain(uint3 groupId : SV_GroupID, uint3 groupThreadId : SV_GroupThreadID)
{
	const uint batchIndex = groupId.x;
	if (batchIndex >= g_BatchCount)
		return;

	const GPUBatchInfo info = g_BatchInfo[batchIndex];
	if (info.sortMode != 2u) // 이 배치는 여기서 정렬하지 않는다
		return;

	const uint count = min(g_IndirectArgs[batchIndex].instanceCount, (uint)SW_SORT_MAX_ELEMENTS);
	const uint base  = info.instanceBase;

	// **실제 개수의 다음 2의 거듭제곱까지만 일한다.** 늘 512 칸을 채우고 비교하면 인스턴스가 열 개인 배치도 512 칸
	// 분량이고, 작은 씬에서 컬링·정렬 프리패스 비용의 대부분이 이 패딩이 된다.
	// (바이토닉은 2의 거듭제곱 길이를 요구하므로 count 까지가 아니라 그 위 거듭제곱까지다.)
	//
	// **루프 경계는 그대로 상수다.** `count` 는 UAV 에서 읽은 값이라 컴파일러(FXC)에게는 "스레드마다
	// 다를 수 있는 값" 이고, 그 값에 걸린 흐름 안의 배리어는 X4026 으로 거부된다 — 실제로는 그룹 안에서
	// 같은 값이지만 증명할 수 없다. 그래서 배리어는 상수 루프에 두고 **비교·교환만** sortLength 안으로 줄인다.
	// 배리어 45 번은 남지만 그것은 싸고, 비싼 것은 칸마다의 그룹공유 읽기·쓰기다.
	uint sortLength = 2u;
	while (sortLength < count)
		sortLength <<= 1u;

	// 그룹공유로 올린다. count 밖의 자리는 가장 큰 값으로 둬 뒤로 밀어 두고, 되쓸 때 count 까지만 쓴다.
	// 인스턴스 번호가 범위를 벗어나도(있어선 안 되지만) 값 그대로 정렬된다 — 그리는 쪽이 g_SwInstanceCount 로 막는다.
	// 추가 뷰는 키 = (그 뷰의 순번 << 9) | 배치 안 번호다(배치 안 번호는 SW_SORT_MAX_ELEMENTS 미만 — 9 비트). 되쓸 때 아래 9 비트로 번호를 되찾는다.
	// 정렬하는 배치(sortMode 2)는 투명 배치뿐이고 그 인스턴스는 모두 꼬리라 순번이 있다.
	for (uint loadIndex = groupThreadId.x; loadIndex < (uint)SW_SORT_MAX_ELEMENTS; loadIndex += SW_SORT_THREADS)
	{
		uint key = 0xFFFFFFFFu;
		if (loadIndex < count)
		{
			const uint instanceId = g_VisibleInstanceIDs[base + loadIndex];
			if (g_UseViewRank != 0u)
			{
				const uint viewRank = (instanceId >= g_TransparentTailBase) ? min(g_ViewRank[instanceId - g_TransparentTailBase], 0x7FFFFFu) : 0x7FFFFFu;
				key = (viewRank << 9u) | ((instanceId - base) & 0x1FFu);
			}
			else
			{
				key = instanceId;
			}
		}
		s_arrId[loadIndex] = key;
	}
	GroupMemoryBarrierWithGroupSync();

	// 바이토닉 정렬 — **오름차순**(번호가 작은 것 = CPU 순서에서 먼저 그릴 것이 앞).
	// blockSize 는 이 단계에서 정렬되는 구간의 길이, compareDistance 는 그 안에서 짝을 짓는 거리다.
	for (uint blockSize = 2u; blockSize <= (uint)SW_SORT_MAX_ELEMENTS; blockSize <<= 1u)
	{
		for (uint compareDistance = blockSize >> 1u; compareDistance > 0u; compareDistance >>= 1u)
		{
			// sortLength 를 넘는 단계는 할 일이 없다 — 분기 안에 배리어가 없으므로 가변 값으로 걸러도 된다.
			for (uint elementIndex = groupThreadId.x; elementIndex < sortLength && blockSize <= sortLength; elementIndex += SW_SORT_THREADS)
			{
				const uint partner = elementIndex ^ compareDistance;
				if (partner > elementIndex)
				{
					// (elementIndex & blockSize) == 0 이면 이 구간은 오름차순으로 맞춘다.
					const bool bAscending = ((elementIndex & blockSize) == 0u);
					const bool bSwap      = bAscending ? (s_arrId[elementIndex] > s_arrId[partner]) : (s_arrId[elementIndex] < s_arrId[partner]);
					if (bSwap)
					{
						const uint swapId     = s_arrId[elementIndex];
						s_arrId[elementIndex] = s_arrId[partner];
						s_arrId[partner]      = swapId;
					}
				}
			}
			GroupMemoryBarrierWithGroupSync();
		}
	}

	for (uint storeIndex = groupThreadId.x; storeIndex < count; storeIndex += SW_SORT_THREADS)
	{
		const uint key = s_arrId[storeIndex];
		g_VisibleInstanceIDs[base + storeIndex] = (g_UseViewRank != 0u) ? (base + (key & 0x1FFu)) : key;
	}
}

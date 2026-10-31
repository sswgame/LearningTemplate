/**
 * batch_data.hlsli — 씬 배치 표 원소 하나(BatchData)의 정의. 그래픽스(binding.hlsli 의 g_SwBatches)와 컴퓨트(gpu_cull · instance_sort 의 g_BatchInfo)가 이 파일 하나를 쓴다.
 *
 * C++ `GPUBatchInfo`(Engine/Renderer/Scene/GPUScene.h)와 바이트 배치가 같아야 한다(4 바이트 열 칸, 40 바이트). 구조체를 셰이더마다 베끼면 칸을 더할 때 한 곳만 고쳐
 * 원소 1 부터 어긋난다(라이트 원소 light_data.hlsli 와 같은 이유).
 */

#ifndef SW_ENGINE_BATCHDATA_HLSLI
#define SW_ENGINE_BATCHDATA_HLSLI

struct BatchData
{
    uint _instanceBase;        // 인스턴스 버퍼(또는 가시 목록)에서 이 배치의 시작
    uint _instanceCount;       // CPU 가 센 개수 (컬링은 간접 인자에 자기 개수를 따로 만든다)
    uint _sortMode;            // 컬링 전용 (GPUBatchSortMode) — 0 없음(불투명), 1 CPU 순서 유지, 2 압축 뒤 CPU 순서로 되돌림
    uint _morphVertexBase;     // 모프 정점 풀에서 이 배치(메시 구간)의 시작. kInvalidIndex = 모프 안 함
    uint _firstVertex;         // 정점 풀에서 이 배치(메시 구간)의 시작 (간접 인자의 startVertex). SV_VertexID 가 이 값을 포함하는지는 API 마다 다르다 — MorphPool::computeElement 참고
    uint _vertexAnimationBase; // 정점 애니메이션(VAT) 표에서 이 메시의 머리 원소. kInvalidIndex = VAT 없음 (InstanceData::loadAnimatedVertex)
    uint _sectionVertexOffset; // 이 배치가 그리는 메시 구간의 첫 정점(메시 안 번호). VAT 표는 메시 전체 순서라 배치 안 번호에 이 값을 더한다
    uint _visibleBase;         // 가시 목록(g_VisibleInstanceIDs) 안에서 이 배치 구간의 시작. 인스턴스 구간(_instanceBase)과 다른 축이다 — LOD 그룹이 없으면 둘이 같다

    uint  _lodLevelCount; // LOD 그룹의 머리 배치(단계 0)의 단계 수. 0 = 그룹이 아니다 — 컬링이 인스턴스의 머리 배치 행에서 읽는다
    float _lodScreenSize; // LOD 그룹의 낮은 단계 배치 행: 이 단계로 넘어가는 화면 크기 임계값 — 컬링이 머리 + 단계 번호 행에서 읽는다

    static BatchData load( uint batchIndex ); // 정의는 binding.hlsli(배치 표 g_SwBatches 를 읽는다)
};

#endif // SW_ENGINE_BATCHDATA_HLSLI

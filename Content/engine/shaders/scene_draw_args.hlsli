/**
 * scene_draw_args.hlsli — 씬 배치의 간접 드로우 인자 하나(SceneDrawArgs)의 정의. 컬링(gpu_cull)과 투명 정렬(instance_sort)이 이 파일 하나를 쓴다.
 *
 * C++ `RHIDrawIndexedIndirectCommand`(Engine/Graphics/RHI/RHIDescriptors.h)와 바이트 배치가 같아야 한다(4 바이트 다섯 칸, 20 바이트 — D3D12 · Vulkan 의 인덱스 간접 인자).
 * 컴퓨트가 쓰는 것은 instanceCount 뿐이다 — startIndexLocation 은 인덱스 풀 안 배치의 시작, baseVertexLocation 은 메시의 정점 풀 시작,
 * startInstanceLocation 은 배치의 가시 구간 시작(인스턴스 슬롯 스트림의 원소를 그만큼 건너뛴다).
 */

#ifndef SW_ENGINE_SCENEDRAWARGS_HLSLI
#define SW_ENGINE_SCENEDRAWARGS_HLSLI

struct SceneDrawArgs
{
    uint _indexCountPerInstance;
    uint _instanceCount;
    uint _startIndexLocation;
    int  _baseVertexLocation;
    uint _startInstanceLocation;
};

#endif // SW_ENGINE_SCENEDRAWARGS_HLSLI

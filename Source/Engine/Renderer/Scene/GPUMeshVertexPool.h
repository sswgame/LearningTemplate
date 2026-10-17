/**
 * @file GPUMeshVertexPool.h
 * @brief 씬의 모든 메시 정점을 **한 정점 버퍼**에 모으는 풀입니다. 언리얼의 통합 정점 버퍼 풀에 해당합니다.
 *
 * [무엇을 푸는가]
 * 배치마다 메시가 다르면 드로우마다 정점 버퍼를 갈아 끼워야 하고, 그러면 같은 PSO 의 배치들도 멀티 드로우
 * 하나로 낼 수 없습니다(멀티 드로우 안에서는 바인딩이 바뀌지 않습니다). 배치당 비용은 상태 변경이 아니라
 * **호출 자체**입니다(871 배치에서 드로우 루프가 렌더 스레드 프레임의 41%). 호출 수를 줄이려면 정점 버퍼가
 * 하나여야 합니다.
 *
 * [어떻게]
 * 메시마다 풀 안의 구간을 내주고(`SpanAllocator`) 그 시작을 기억합니다. 배치의 간접 인자는 `startVertex = 시작 오프셋` 이고,
 * 입력 어셈블러가 그 구간을 읽습니다. 정점 셰이더의 SV_VertexID 가 그 오프셋을 포함하는지는 API 마다 다릅니다(Vulkan, GL 은 포함,
 * D3D 는 0 기반. binding.hlsli swComputeMorphElement). 정점 데이터는 CPU 사본(`Mesh::getVertices`)에서 옵니다.
 *
 * [부분 갱신 — C-43]
 * 메시 집합이 바뀌면 **바뀐 메시의 구간만** 씁니다(`IRHIResourceFactory::updateVertexBufferRegions`). 들어온 메시는 빈 구간을 받아 그
 * 구간만 올리고, 나간 메시의 구간은 돌려받습니다. 예전에는 집합이 바뀔 때마다 모든 메시를 다시 이어 붙여 버퍼 전체를 올렸습니다 — 월드
 * 스트리밍(D1-7)의 셀 하나, 지형 LOD 교체 하나가 수십 MB 업로드였습니다. 언리얼 GPUScene 이 프리미티브 구간을 `FSpanAllocator` 로 나눠
 * 바뀐 것만 올리는 것과 같은 구조입니다.
 * - **돌려받은 구간은 곧바로 다시 주지 않습니다.** DX12 · Vulkan 의 정점 버퍼는 CPU 가 직접 쓰는 메모리라 GPU 와 순서가 맞춰지지
 *   않습니다. 방금 나간 메시의 구간을 새 메시에 주면 아직 끝나지 않은 앞 프레임이 읽는 자리를 덮어씁니다. 그래서 `update` 를 부른 수
 *   (= 제출한 프레임 수, FrameRenderer::uploadSceneAndSubmit 가 프레임마다 한 번)로 번호를 매겨 `kMaxFrameCountInFlight` 프레임 뒤에 돌려받습니다.
 * - **용량이 모자라거나 조각나 들어갈 자리가 없으면 다시 채웁니다.** 더 큰 새 버퍼를 만들어 살아 있는 메시를 앞에서부터 다시 이어 붙입니다
 *   (`repack`). 새 버퍼는 아무 프레임도 읽지 않으므로 대기 중인 반납도 버립니다. 용량은 1.5 배씩 늘어 드물게만 일어납니다.
 */
#pragma once
#include "Core/Common/Defines.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/SpanAllocator.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"

#include "Engine/Graphics/RHI/RHITypes.h"

namespace sw
{
    class IRHIDevice;
    class Mesh;

    /**
     * @class GPUMeshVertexPool
     * @brief 씬 메시들의 정점을 한 버퍼에 모읍니다. 렌더 스레드가 소유합니다(GPUScene 의 GPU 상태와 같은 규칙).
     */
    class SW_API GPUMeshVertexPool
    {
    public:
        /** @brief 풀에 들어가지 못한 메시가 받는 값입니다. 그 배치는 자기 정점 버퍼로 그립니다. */
        static constexpr uint32 kInvalidBase = invalid_index::kUint32;

        GPUMeshVertexPool();
        ~GPUMeshVertexPool()                                     = default;
        GPUMeshVertexPool( const GPUMeshVertexPool& )            = delete;
        GPUMeshVertexPool& operator=( const GPUMeshVertexPool& ) = delete;
        /// GPUScene 의 이동 연산이 이 멤버를 옮길 수 있어야 해서 이동은 허용합니다(핸들만 옮깁니다).
        GPUMeshVertexPool( GPUMeshVertexPool&& ) noexcept            = default;
        GPUMeshVertexPool& operator=( GPUMeshVertexPool&& ) noexcept = default;

        /**
         * @brief 이번 프레임의 메시 집합에 풀을 맞춥니다. 프레임마다 **한 번** 부릅니다(돌려받은 구간의 대기 번호가 이 호출 수입니다).
         * @details 순서가 아니라 **집합**을 봅니다. 배치 정렬이 바뀌어도 메시가 같으면 아무것도 올리지 않습니다. 포인터와 내용 번호
         *          (`Mesh::getContentID`)를 함께 봐서, 지워진 자리에 새 메시가 생기거나 정점이 바뀐 메시는 나간 메시 + 들어온 메시로 다룹니다.
         *          예산(`kMaxPoolVertices`)을 넘는 메시는 들어가지 못하고 `kInvalidBase` 를 받습니다.
         * @param listMesh 소유하지 않는 포인터들. 스냅샷 배치가 소유권을 보관하는 동안에만 유효합니다.
         * @return 어느 메시의 시작 오프셋이라도 바뀌었으면(들어옴, 나감, 다시 채움) true 입니다. 배치 테이블을 다시 올려야 합니다.
         */
        bool update( IRHIDevice* pDevice, const vector<Mesh*>& listMesh );

        /** @brief 메시의 풀 시작 오프셋(정점 단위)을 반환합니다. 풀에 없으면 `kInvalidBase` 입니다. */
        uint32 baseOf( const Mesh* pMesh ) const;

        /** @brief 풀 정점 버퍼를 반환합니다. 0 이면 풀이 비어 있습니다. */
        RHIBufferHandle getVertexBuffer() const { return _vertexBuffer; }
        /** @brief 풀에 든(구간을 받은) 메시의 정점 수 합을 반환합니다. */
        uint32 getVertexCount() const { return _liveVertexCount; }
        /** @brief 풀 버퍼의 용량(정점 수)을 반환합니다. */
        uint32 getCapacity() const { return _allocator.getCapacity(); }
        /** @brief 버퍼를 다시 만들어 모든 메시를 다시 올린 횟수를 반환합니다. 부분 갱신이 통째로 올리기로 새지 않는지 테스트가 봅니다. */
        uint32 getRepackCount() const { return _repackCount; }
        /** @brief 지난 `update` 가 올린 바이트 수를 반환합니다(다시 채웠으면 버퍼 전체입니다). */
        uint32 getLastUploadBytes() const { return _lastUploadBytes; }

        /** @brief 버퍼를 놓습니다. 디바이스가 바뀌거나 내려갈 때 부릅니다. */
        void release( IRHIDevice* pDevice );

    private:
        /** @brief 풀에 든 메시 하나의 기록입니다. */
        struct Entry
        {
            uint64 _contentID{ 0 };
            uint64 _seenStamp{ 0 };       ///< 이 메시를 마지막으로 본 `update` 의 번호
            uint32 _base{ kInvalidBase }; ///< 구간 시작. 못 들어갔으면 kInvalidBase
            uint32 _count{ 0 };
        };

        /** @brief 더 큰 새 버퍼를 만들고 살아 있는 메시를 모두 앞에서부터 다시 이어 붙입니다. */
        void repack( IRHIDevice* pDevice );

        /// @brief 풀 상한(정점 수)입니다. 4M 정점 × 48 바이트 = 192MB 로, 이 저장소의 씬에는 넉넉합니다.
        static constexpr uint32 kMaxPoolVertices = 4u * 1024u * 1024u;
        /// @brief 처음 만드는 풀의 최소 용량(정점 수)입니다. 작은 씬에서 메시 하나 들어올 때마다 다시 채우지 않게 합니다.
        static constexpr uint32 kMinPoolVertices = 64u * 1024u;

        SpanAllocator   _allocator;
        RHIBufferHandle _vertexBuffer;
        /// @brief 메시 → 기록입니다. 지난 프레임에 본 메시들입니다(포인터는 키로만 쓰며 따라가지 않습니다 — 이미 죽었을 수 있습니다).
        unordered_map<const Mesh*, Entry> _mapEntry;
        /// @brief 이번 `update` 에 새로 구간이 필요한 메시입니다(살아 있는 포인터). 프레임마다 할당하지 않습니다.
        vector<const Mesh*> _listScratchAdded;
        /// @brief 예산 때문에 못 들어간 채 남은 메시(집합이 바뀐 프레임에만 다시 시도)와 이번에 나간 메시입니다.
        vector<const Mesh*> _listScratchRetry;
        vector<const Mesh*> _listScratchRemoved;
        /// @brief 올릴 정점과 조각입니다. 프레임마다 할당하지 않습니다.
        vector<RHIVertex>           _listScratchVertex;
        vector<RHIBufferCopyRegion> _listScratchRegion;
        uint64                      _updateCount;
        uint32                      _liveVertexCount;
        uint32                      _repackCount;
        uint32                      _lastUploadBytes;
    };
} // namespace sw

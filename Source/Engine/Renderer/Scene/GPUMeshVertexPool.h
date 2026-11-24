/**
 * @file GPUMeshVertexPool.h
 * @brief 씬의 모든 메시 정점 · 인덱스를 **한 정점 버퍼 · 한 인덱스 버퍼**에 모으는 풀입니다. 언리얼의 통합 정점 · 인덱스 버퍼 풀에 해당합니다.
 *
 * [무엇을 푸는가]
 * 배치마다 메시가 다르면 드로우마다 정점 버퍼를 갈아 끼워야 하고, 그러면 같은 PSO 의 배치들도 멀티 드로우
 * 하나로 낼 수 없습니다(멀티 드로우 안에서는 바인딩이 바뀌지 않습니다). 배치당 비용은 상태 변경이 아니라
 * **호출 자체**입니다(871 배치에서 드로우 루프가 렌더 스레드 프레임의 41%). 호출 수를 줄이려면 정점 · 인덱스 버퍼가
 * 하나여야 합니다.
 *
 * [어떻게]
 * 메시마다 정점 풀과 인덱스 풀에 구간을 하나씩 내주고(`SpanAllocator` 둘) 그 시작을 기억합니다. 정점 버퍼는 [위치 구간][속성 구간]이고
 * (`RHIMeshVertexStreams` — 메시 하나의 정점 버퍼와 같은 모양) 정점 번호가 두 구간에 같은 칸을 가리킵니다. 속성 원소 크기가 형식마다 달라
 * 풀은 정점 형식마다 하나입니다(`GPUScene` 이 형식마다 둔다 — 멀티 드로우는 정점 버퍼가 다르면 어차피 끊긴다). 인덱스는 메시 안 번호 그대로 올리고,
 * 배치의 간접 인자가 `baseVertex = 정점 시작` · `startIndex = 인덱스 시작 + 구간` 입니다. 정점 셰이더의 SV_VertexID 가 baseVertex 를
 * 포함하는지는 API 마다 다릅니다(Vulkan, GL 은 포함, D3D 는 인덱스 값 그대로. binding.hlsli MorphPool::computeElement).
 * 정점 · 인덱스는 CPU 사본(`Mesh::getVertices` · `getIndices` 와 낮은 단계)에서 옵니다.
 *
 * [부분 갱신 — C-43]
 * 메시 집합이 바뀌면 **바뀐 메시의 구간만** 씁니다(`IRHIResourceFactory::updateGeometryBufferRegions` — 정점 · 인덱스 버퍼 같은 길). 들어온 메시는 빈 구간을 받아 그
 * 구간만 올리고, 나간 메시의 구간은 돌려받습니다. 예전에는 집합이 바뀔 때마다 모든 메시를 다시 이어 붙여 버퍼 전체를 올렸습니다 — 월드
 * 스트리밍(D1-7)의 셀 하나, 지형 LOD 교체 하나가 수십 MB 업로드였습니다. 언리얼 GPUScene 이 프리미티브 구간을 `FSpanAllocator` 로 나눠
 * 바뀐 것만 올리는 것과 같은 구조입니다.
 * - **돌려받은 구간은 곧바로 다시 주지 않습니다.** DX12 · Vulkan 의 정점 버퍼는 CPU 가 직접 쓰는 메모리라 GPU 와 순서가 맞춰지지
 *   않습니다. 방금 나간 메시의 구간을 새 메시에 주면 아직 끝나지 않은 앞 프레임이 읽는 자리를 덮어씁니다. 그래서 `update` 마다 펜스 값을 내고
 *   (`RHIFence::signal`) 그 값을 키로 두었다가, GPU 가 그 펜스를 끝냈다고 디바이스가 알린 뒤에 돌려받습니다(프레임 수로 짐작하지 않는다).
 * - **용량이 모자라거나 조각나 들어갈 자리가 없으면 다시 채웁니다.** 더 큰 새 버퍼를 만들어 살아 있는 메시를 앞에서부터 다시 이어 붙입니다
 *   (`repack`). 새 버퍼는 아무 프레임도 읽지 않으므로 대기 중인 반납도 버립니다. 용량은 1.5 배씩 늘어 드물게만 일어납니다.
 */
#pragma once
#include "Core/Container/SpanAllocator.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/IdentityToken.h"

#include "Engine/Graphics/Mesh/MeshVertex.h"
#include "Engine/Graphics/RHI/RHIDescriptors.h"
#include "Engine/Graphics/RHI/RHIFence.h"
#include "Engine/Graphics/RHI/RHIGeometryBuffer.h"

namespace sw
{
    class IRHICommandList;
    class IRHIDevice;
    class Mesh;

    /**
     * @class GPUMeshVertexPool
     * @brief 씬 메시들의 정점 · 인덱스를 한 버퍼씩에 모읍니다. 렌더 스레드가 소유합니다(GPUScene 의 GPU 상태와 같은 규칙).
     */
    class SW_API GPUMeshVertexPool
    {
    public:
        /** @brief 풀에 들어가지 못한 메시가 받는 값입니다. 그 배치는 자기 정점 · 인덱스 버퍼로 그립니다. */
        static constexpr uint32 kInvalidBase = constant::invalid_index::kUint32;

        /** @brief 풀 안 메시 하나의 자리입니다. 못 들어간 메시는 둘 다 `kInvalidBase` 입니다. */
        struct Placement
        {
            uint32 _baseVertex{ kInvalidBase }; ///< 정점 풀 시작(간접 인자의 baseVertex)
            uint32 _firstIndex{ kInvalidBase }; ///< 인덱스 풀 시작(구간 · LOD 단계의 첫 인덱스를 더해 startIndex)
        };

        /** @brief @p format 형식의 메시만 담는 풀을 만듭니다. */
        explicit GPUMeshVertexPool( RHIMeshVertexFormat format = RHIMeshVertexFormat::Packed );
        ~GPUMeshVertexPool()                                     = default;
        GPUMeshVertexPool( const GPUMeshVertexPool& )            = delete;
        GPUMeshVertexPool& operator=( const GPUMeshVertexPool& ) = delete;
        /// GPUScene 의 이동 연산이 이 멤버를 옮길 수 있어야 해서 이동은 허용합니다(핸들만 옮깁니다).
        GPUMeshVertexPool( GPUMeshVertexPool&& ) noexcept            = default;
        GPUMeshVertexPool& operator=( GPUMeshVertexPool&& ) noexcept = default;

        /**
         * @brief 이번 프레임의 메시 집합에 풀을 맞춥니다. 프레임마다 **한 번** 부릅니다(돌려받은 구간의 대기 번호가 이 호출 수입니다).
         * @details 순서가 아니라 **집합**을 봅니다. 배치 정렬이 바뀌어도 메시가 같으면 아무것도 올리지 않습니다. 항목의 키가 주소가 아니라
         *          내용 토큰(`Mesh::getContentID`)이라, 지워진 자리에 새 메시가 생기거나 정점이 바뀐 메시는 저절로 나간 메시 + 들어온 메시입니다.
         *          예산(`kMaxPoolVertices` · `kMaxPoolIndices`)을 넘는 메시는 들어가지 못하고 `kInvalidBase` 를 받습니다.
         * @param listMesh 소유하지 않는 포인터들. 스냅샷 배치가 소유권을 보관하는 동안에만 유효합니다.
         * @return 어느 메시의 시작 오프셋이라도 바뀌었으면(들어옴, 나감, 다시 채움) true 입니다. 배치 테이블을 다시 올려야 합니다.
         */
        bool update( IRHIDevice* pDevice, const vector<Mesh*>& listMesh );

        /** @brief 메시의 풀 자리(정점 · 인덱스 시작)를 반환합니다. 풀에 없으면 둘 다 `kInvalidBase` 입니다. */
        Placement getPlacementOf( const Mesh* pMesh ) const;

        /** @brief 풀 정점 버퍼를 반환합니다. 0 이면 풀이 비어 있습니다. */
        RHIBufferHandle getVertexBuffer() const { return _vertexBuffer._buffer; }
        /** @brief 풀 정점 버퍼에서 속성 구간이 시작하는 바이트입니다(용량 × 12 B). 슬롯 2 를 여기서 건다. */
        uint32 getAttributeOffset() const;
        /** @brief 이 풀이 담는 정점 형식입니다. */
        RHIMeshVertexFormat getFormat() const { return _format; }
        /** @brief 풀 인덱스 버퍼(uint32)를 반환합니다. 정점 버퍼와 함께 만들고 함께 놓습니다. */
        RHIBufferHandle getIndexBuffer() const { return _indexBuffer._buffer; }
        /** @brief 풀에 든(구간을 받은) 메시의 정점 수 합을 반환합니다. */
        uint32 getVertexCount() const { return _liveVertexCount; }
        /** @brief 풀 정점 버퍼의 용량(정점 수)을 반환합니다. */
        uint32 getCapacity() const { return _allocator.getCapacity(); }

        /** @brief 버퍼를 놓습니다. 디바이스가 바뀌거나 내려갈 때 부릅니다. */
        void release( IRHIDevice* pDevice );

        /**
         * @brief 쌓인 GPU 복사(렌더 프록시 → 풀, 옛 풀 → 새 풀)를 @p cmd 에 기록하고 다 쓴 옛 풀 버퍼를 지연 해제합니다 — 프레임 업로드 단계(실행기가 첫 리스트 앞에 고정으로 부른다).
         * @details 원본은 CopySource, 도착은 CopyDest 로 옮겨 복사하고 다시 VertexOrConstant 로 돌린다(R-15 — 풀은 CPU 정점 대신 GPU 원본에서 채운다). 쌓인 순서대로 기록한다 —
         *          다시 꾸린 옛 풀이 아직 받지 못한 복사가 새 풀 복사보다 먼저 들어간다.
         */
        void recordPendingCopies( IRHICommandList& cmd, IRHIDevice& device );

    private:
        friend struct GPUMeshVertexPoolTestAccess; ///< 테스트만 안을 본다 — 배포 공개 API 에 진단 조회를 두지 않는다
        /** @brief 풀에 든 메시 하나의 기록입니다. */
        struct Entry
        {
            const Mesh* _pMesh{ nullptr };          ///< 이번 `update` 에서 본 메시(그 호출 동안만 따라간다 — 쓸기가 못 본 항목을 지운다)
            uint64      _seenStamp{ 0 };            ///< 이 메시를 마지막으로 본 `update` 의 번호
            uint32      _base{ kInvalidBase };      ///< 정점 구간 시작. 못 들어갔으면 kInvalidBase(인덱스 구간도 없다)
            uint32      _count{ 0 };                ///< 정점 수(`Mesh::getDrawVertexCount`)
            uint32      _indexBase{ kInvalidBase }; ///< 인덱스 구간 시작
            uint32      _indexCount{ 0 };           ///< 인덱스 수(`Mesh::getDrawIndexCount`)
        };

        /// @brief 풀 상한(정점 수)입니다. 4M 정점 × 44 바이트(Packed — 위치 12 + 속성 32) = 176MB 로, 이 저장소의 씬에는 넉넉합니다.
        static constexpr uint32 kMaxPoolVertices = 4u * 1024u * 1024u;
        /// @brief 처음 만드는 풀의 최소 용량(정점 수)입니다. 작은 씬에서 메시 하나 들어올 때마다 다시 채우지 않게 합니다.
        static constexpr uint32 kMinPoolVertices = 64u * 1024u;
        /// @brief 인덱스 풀 상한 · 최소 용량입니다. 삼각형 목록 메시는 인덱스가 정점 수와 같고 나눠 쓰는 메시는 대략 그 1.5 ~ 2 배라 정점의 3 배를 잡는다(12M × 4 바이트 = 48MB).
        static constexpr uint32 kMaxPoolIndices = 3u * kMaxPoolVertices;
        static constexpr uint32 kMinPoolIndices = 3u * kMinPoolVertices;

        /**
         * @brief 항목의 정점 · 인덱스 구간을 늦게 돌려주고, 그 구간에 쌓인 채 기록하지 않은 복사를 버립니다(구간을 받은 항목만).
         * @details 그리지 않는 프레임(`FrameRenderer::executePacketWithoutDrawing`)과 시작하지 못한 프레임은 복사를 기록하지 않는다 — 그 사이 구간이 다른 메시에 다시 나가면
         *          뒤늦은 옛 복사가 새 내용(동적 메시의 곧바로 올린 정점)을 덮는다. 남은 복사의 원본 메시는 늘 풀에 든 살아 있는 메시다.
         */
        void retireEntry( Entry& inoutEntry, uint64 retireStamp );
        /** @brief 더 큰 새 버퍼를 만들고 살아 있는 메시를 모두 앞에서부터 다시 이어 붙입니다. */
        void repack( IRHIDevice* pDevice );
        /** @brief 메시의 그릴 인덱스 흐름(LOD 0 뒤에 낮은 단계, `Mesh::getDrawIndexCount`)을 올릴 목록 끝에 붙입니다. */
        void appendDrawIndices( const Mesh& mesh );
        /** @brief 이번 목록의 메시를 표시합니다. 처음 보는 내용 토큰은 들어올 목록에, 예산 때문에 못 들어간 메시는 다시 시도할 목록에 넣습니다. 바뀐 것이 있으면 true 입니다. */
        [[nodiscard]] bool markListedMeshes( const vector<Mesh*>& listMesh );
        /** @brief 이번 목록에 없는 메시의 구간을 늦게 돌려주고 항목을 지웁니다(포인터는 따라가지 않는다). 지운 것이 있으면 true 입니다. */
        [[nodiscard]] bool sweepUnlistedMeshes( uint64 retireStamp );
        /** @brief 들어온 메시에 구간을 주고 그 정점만 한 번에 올립니다. 자리가 없는 메시가 있으면 false 이고, 부르는 쪽이 다시 채운다(`repack`). */
        [[nodiscard]] bool placeAddedMeshes( IRHIDevice* pDevice );

        /** @brief 메시의 그릴 정점 흐름(LOD 0 뒤에 낮은 단계)을 `_listScratchByte`([위치 구간 @p vertexCapacity 칸][속성 구간])의 @p firstVertex 칸부터 원본에서 바로 씁니다. */
        void writeDrawVertices( const Mesh& mesh, uint32 firstVertex, uint32 vertexCapacity );
        /**
         * @brief 메시 하나를 풀의 @p firstVertex 칸에 채울 일을 쌓습니다 — 렌더 프록시 정점 버퍼가 있으면 GPU 복사(위치 · 속성 두 구간), 없으면(동적 메시) CPU 정점을 스테이징으로 올린다.
         * @return 채울 원본이 없으면(프록시도 CPU 정점도 없다) false 입니다.
         */
        [[nodiscard]] bool queueMeshFill( IRHIDevice& device, const Mesh& mesh, uint32 firstVertex, uint32 vertexCapacity );

        /** @brief 쌓인 GPU 복사 하나입니다(도착은 그때의 풀 버퍼 — 다시 꾸리면 바뀐다). */
        struct PendingCopy
        {
            RHIBufferHandle _destination{ 0 };
            uint64          _destinationOffset{ 0 };
            RHIBufferHandle _source{ 0 };
            uint64          _sourceOffset{ 0 };
            uint64          _sizeBytes{ 0 };
            uint32          _base{ kInvalidBase }; ///< 도착 구간의 시작 정점 — 구간을 돌려줄 때 아직 기록하지 않은 복사를 이 값으로 찾아 버린다
        };

    private:
        RHIMeshVertexFormat _format;
        SpanAllocator       _allocator;
        SpanAllocator       _indexAllocator;
        RHIGeometryBuffer   _vertexBuffer;
        RHIGeometryBuffer   _indexBuffer;
        /// @brief 메시 내용 토큰 → 기록입니다. 키가 토큰을 들어 지운 메시의 주소가 새 메시의 키로 다시 쓰이지 않는다(언리얼 `FObjectKey`, Godot `RID` 의 세대와 같은 자리).
        unordered_map<IdentityToken, Entry> _mapEntry;
        /// @brief 이번 `update` 에 새로 구간이 필요한 메시입니다. 프레임마다 할당하지 않습니다.
        vector<IdentityToken> _listScratchAdded;
        /// @brief 예산 때문에 못 들어간 채 남은 메시(집합이 바뀐 프레임에만 다시 시도)와 이번에 나간 메시입니다.
        vector<IdentityToken> _listScratchRetry;
        vector<IdentityToken> _listScratchRemoved;
        /// @brief 올릴 정점 · 인덱스와 조각입니다. 프레임마다 할당하지 않습니다.
        vector<uint8>               _listScratchByte; ///< 올릴 정점 — GPU 스트림 모양([위치 구간][속성 구간])
        vector<uint32>              _listScratchIndex;
        vector<RHIBufferCopyRegion> _listScratchRegion;
        vector<RHIBufferCopyRegion> _listScratchIndexRegion;
        uint64                      _updateCount; ///< `update` 를 부른 수 — 이번 목록에서 본 메시의 표시(`Entry::_seenStamp`)다
        RHIFence                    _gpuFence;    ///< 돌려받은 구간의 대기 키(펜스 값)를 내고 GPU 가 끝낸 값을 안다
        uint32                      _liveVertexCount;
        vector<PendingCopy>         _listPendingCopy;   ///< 다음 프레임 업로드 단계가 기록할 GPU 복사(쌓인 순서)
        vector<RHIGeometryBuffer>   _listRetiredBuffer; ///< 다시 꾸려 비운 옛 풀 정점 버퍼 — 그것을 읽는 복사를 기록한 뒤 지연 해제한다
    };
} // namespace sw

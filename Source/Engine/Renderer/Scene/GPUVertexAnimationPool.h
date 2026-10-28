/**
 * @file GPUVertexAnimationPool.h
 * @brief 정점 애니메이션(VAT) 테이블들을 구조 버퍼 하나(g_SwVertexAnimation, t10)에 모으는 풀입니다. 렌더 스레드가 소유합니다.
 * @details 테이블은 베이크하고 나면 변하지 않아(`MeshVertexAnimation`) 집합이 바뀔 때만 올립니다 — 구간 할당기(`SpanAllocator`)로 테이블마다 구간을 내주어 새로 들어온 테이블의 구간만 올리고, 빠진 테이블의 구간은 미뤄 돌려받습니다(다 차면 크게 다시 만들어 전부 올립니다). 메시마다 헤더 원소(프레임 수, 프레임율, 정점 수,
 *          반복) 하나와 프레임 × 정점 원소가 이어지고, 배치는 헤더 원소 번호를 배치 테이블에 싣습니다(`GPUMeshBatch::_vertexAnimationBase`).
 *          같은 테이블을 나누는 메시들은 한 구간을 씁니다.
 */
#pragma once
#include "Core/Container/SpanAllocator.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Defines.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Graphics/RHI/RHIStructuredBufferSlot.h"

namespace sw
{
    struct MeshVertexAnimation;

    class IRHIDevice;
    class Mesh;

    /**
     * @class GPUVertexAnimationPool
     * @brief VAT 가 걸린 메시들의 테이블을 한 버퍼에 이어 붙입니다.
     */
    class SW_API GPUVertexAnimationPool
    {
    public:
        /** @brief 풀에 없는 메시가 받는 값입니다(셰이더의 kInvalidIndex). */
        static constexpr uint32 kInvalidBase = invalid_index::kUint32;

        GPUVertexAnimationPool()                                           = default;
        ~GPUVertexAnimationPool()                                          = default;
        GPUVertexAnimationPool( const GPUVertexAnimationPool& )            = delete;
        GPUVertexAnimationPool& operator=( const GPUVertexAnimationPool& ) = delete;

        /**
         * @brief VAT 가 걸린 메시 목록으로 풀을 맞춥니다. 목록(포인터와 내용 번호)이 그대로면 아무것도 하지 않습니다.
         * @param listMesh 소유하지 않는 포인터들입니다. 스냅샷 배치가 소유권을 보관하는 동안만 유효합니다. VAT 가 없는 메시는 건너뜁니다.
         */
        void rebuild( IRHIDevice* pDevice, const vector<Mesh*>& listMesh );
        /** @brief 메시의 헤더 원소 번호입니다. 풀에 없으면 `kInvalidBase` 입니다. */
        uint32 baseOf( const Mesh* pMesh ) const;
        /** @brief 테이블 버퍼입니다. 정점 셰이더가 SRV 로 읽습니다. */
        const RHIStructuredBufferSlot& getBuffer() const { return _table; }
        /** @brief 테이블의 단어(uint32) 수입니다. 셰이더의 범위 검사 값입니다. */
        uint32 getWordCount() const { return _wordCount; }
        /** @brief 업로드한 테이블 수입니다(같은 테이블을 나누는 메시는 하나로 셉니다). */
        uint32 getAnimationCount() const { return static_cast<uint32>( _mapEntry.size() ); }
        /** @brief 버퍼를 놓습니다. */
        void release( IRHIDevice* pDevice );

    private:
        /// @brief 풀 상한(단어 수, 단어 하나 = 4바이트이며 256MB)입니다. 넘는 테이블은 싣지 않고 오류를 남기며 그 메시는 그리지 않습니다(바인드 포즈로 대신 그리지 않는다).
        static constexpr uint32 kMaxWordCount = 64u * 1024u * 1024u;
        /// @brief 반납한 구간을 이 횟수만큼의 재구성이 지난 뒤에야 다시 내줍니다(앞 프레임들이 아직 읽는 구간을 덮어쓰지 않게 한다 — 재구성은 프레임보다 드물다).
        static constexpr uint64 kRetireLag = 4;

        /// @brief 테이블 하나의 구간입니다. 같은 테이블을 나누는 메시는 `_refCount` 로 센다.
        struct Entry
        {
            uint32 _base{ kInvalidBase };
            uint32 _wordCount{ 0 };
            uint32 _refCount{ 0 };
        };
        /// @brief 메시 하나가 어느 테이블을 어느 내용 번호로 올렸는지입니다(내용 번호가 달라지면 새 테이블로 본다).
        struct MeshRecord
        {
            const MeshVertexAnimation* _pAnimation{ nullptr };
            uint64                     _contentID{ 0 };
        };

        /// @brief 이번 목록에서 VAT 가 있는 메시 하나입니다.
        struct Wanted
        {
            const Mesh*                _pMesh{ nullptr };
            const MeshVertexAnimation* _pAnimation{ nullptr };
            uint64                     _contentID{ 0 };
        };

        /** @brief 테이블 하나(머리 단어 + 원소)를 @p base 에 올립니다. */
        void uploadEntry( IRHIDevice* pDevice, const MeshVertexAnimation& animation, uint32 base );
        /**
         * @brief @p listWanted 의 @p listIndex 번째들을 풀에 둡니다 — 이미 있는 테이블은 나누고, 없으면 구간을 받아 올립니다.
         * @return 구간이 모자라면 false 입니다(그때까지 둔 것은 남는다 — 부르는 쪽이 크게 다시 만든다). 상한을 넘는 테이블은 오류를 남기고 그 메시만 뺀다.
         */
        [[nodiscard]] bool placeWanted( IRHIDevice* pDevice, const vector<Wanted>& listWanted, const vector<size_t>& listIndex );
        /** @brief 메시 → 머리 단어 번호 표를 기록에서 다시 만든다. */
        void refreshBases();
        /** @brief 버퍼 · 구간 할당기를 @p capacity 단어로 다시 만들고 모든 기록을 비웁니다. 못 만들면 풀을 비우고 false 입니다. */
        [[nodiscard]] bool resetStorage( IRHIDevice* pDevice, uint32 capacity );

        RHIStructuredBufferSlot                          _table;
        SpanAllocator                                    _allocator;
        unordered_map<const MeshVertexAnimation*, Entry> _mapEntry;
        unordered_map<const Mesh*, MeshRecord>           _mapRecord;
        unordered_map<const Mesh*, uint32>               _mapBase;
        vector<const Mesh*>                              _listBuilt;
        vector<uint64>                                   _listBuiltContentID;
        uint64                                           _rebuildSerial{ 0 };
        uint32                                           _wordCount{ 0 }; ///< 버퍼가 담는 단어 수(구간 할당기 용량)입니다. 셰이더 범위 검사 값이다
    };
} // namespace sw

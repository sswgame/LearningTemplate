/**
 * @file GPUVertexAnimationPool.h
 * @brief 정점 애니메이션(VAT) 표들을 구조버퍼 하나(g_SwVertexAnimation, t14)에 모으는 풀입니다. 렌더 스레드가 소유합니다.
 * @details 표는 굽고 나면 변하지 않아(`MeshVertexAnimation`) 집합이 바뀔 때만 한 번 올립니다. 메시마다 머리 원소(프레임 수 · 프레임율 · 정점 수 ·
 *          반복) 하나와 프레임 × 정점 원소가 이어지고, 배치는 머리 원소 번호를 배치 표에 싣습니다(`GPUMeshBatch::_vertexAnimationBase`).
 *          같은 표를 나누는 메시들은 한 구간을 씁니다.
 */
#pragma once
#include "Core/Common/Defines.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"

#include "Engine/Graphics/RHI/RHIStructuredBufferSlot.h"

namespace sw
{
    struct MeshVertexAnimation;

    class IRHIDevice;
    class Mesh;

    /**
     * @class GPUVertexAnimationPool
     * @brief VAT 가 걸린 메시들의 표를 한 버퍼에 잇습니다.
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
         * @brief VAT 가 걸린 메시 목록으로 풀을 맞춥니다. 목록(포인터 · 내용 번호)이 그대로면 아무것도 하지 않습니다.
         * @param listMesh 소유하지 않는 포인터들 — 스냅샷 배치가 소유를 들고 있는 동안만 유효합니다. VAT 가 없는 메시는 건너뜁니다.
         */
        void build( IRHIDevice* pDevice, const vector<Mesh*>& listMesh );
        /** @brief 메시의 머리 원소 번호입니다. 풀에 없으면 `kInvalidBase` 입니다. */
        uint32 baseOf( const Mesh* pMesh ) const;
        /** @brief 표 버퍼입니다. 정점 셰이더가 SRV 로 읽습니다. */
        const RHIStructuredBufferSlot& getBuffer() const { return _table; }
        /** @brief 표의 원소(float4) 수입니다. 셰이더의 범위 검사 값입니다. */
        uint32 getElementCount() const { return _elementCount; }
        /** @brief 올린 표 수입니다(같은 표를 나누는 메시는 하나로 셉니다). */
        uint32 getAnimationCount() const { return static_cast<uint32>( _mapBaseByAnimation.size() ); }
        /** @brief 버퍼를 놓습니다. */
        void release( IRHIDevice* pDevice );

    private:
        /// @brief 풀 상한(원소 수, 원소 하나 = 16 바이트 — 256 MB)입니다. 넘는 표는 싣지 않고 그 메시는 바인드 포즈로 그립니다.
        static constexpr uint32 kMaxElementCount = 16u * 1024u * 1024u;

        RHIStructuredBufferSlot                           _table;
        unordered_map<const MeshVertexAnimation*, uint32> _mapBaseByAnimation;
        unordered_map<const Mesh*, uint32>                _mapBase;
        vector<const Mesh*>                               _listBuilt;
        vector<uint64>                                    _listBuiltContentId;
        uint32                                            _elementCount{ 0 };
    };
} // namespace sw

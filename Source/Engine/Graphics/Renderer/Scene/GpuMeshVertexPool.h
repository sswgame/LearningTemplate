/**
 * @file GpuMeshVertexPool.h
 * @brief 씬의 모든 메시 정점을 **한 정점 버퍼**에 모으는 풀입니다. 언리얼의 통합 정점 버퍼 풀이 있는 자리입니다.
 *
 * [무엇을 푸는가]
 * 배치마다 메시가 다르면 드로우마다 정점 버퍼를 갈아 끼워야 하고, 그러면 같은 PSO 의 배치들도 멀티 드로우
 * 하나로 낼 수 없습니다(멀티 드로우 안에서는 바인딩이 바뀌지 않습니다). 2026-09-13 벤치에서 871 배치의 드로우
 * 루프가 렌더 스레드 프레임의 41% 였고, 배치당 비용은 상태 변경이 아니라 **호출 자체**였습니다. 호출 수를
 * 줄이려면 정점 버퍼가 하나여야 합니다.
 *
 * [어떻게]
 * 메시들의 정점을 이어 붙여 정점 버퍼 하나를 만들고 메시마다 시작 오프셋을 기억합니다. 배치의 간접 인자는
 * `startVertex = 시작 오프셋` 이고, 입력 어셈블러가 그 구간을 읽습니다. 정점 셰이더의 SV_VertexID 가 그 오프셋을
 * 포함하는지는 API 마다 다릅니다(Vulkan · GL 은 포함, D3D 는 0 기반. binding.hlsli SwMorphElementOf).
 * 모프 풀(`GpuMeshMorphPool`)이 이미 같은 모양입니다.
 * 메시 집합이 바뀔 때만 다시 만듭니다(장면 로드 · 메시 추가). 정점 데이터는 CPU 사본(`Mesh::getVertices`)에서 옵니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"

#include "Engine/Graphics/RHI/RHITypes.h"

namespace sw
{
    class IRHIDevice;
    class Mesh;

    /**
     * @class GpuMeshVertexPool
     * @brief 씬 메시들의 정점을 한 버퍼에 모읍니다. 렌더 스레드가 소유합니다(GpuScene 의 GPU 상태와 같은 규칙).
     */
    class SW_API GpuMeshVertexPool
    {
    public:
        /** @brief 풀에 들어가지 못한 메시가 받는 값입니다. 그 배치는 자기 정점 버퍼로 그립니다. */
        static constexpr uint32 kInvalidBase = 0xFFFFFFFFu;

        GpuMeshVertexPool()                                      = default;
        ~GpuMeshVertexPool()                                     = default;
        GpuMeshVertexPool( const GpuMeshVertexPool& )            = delete;
        GpuMeshVertexPool& operator=( const GpuMeshVertexPool& ) = delete;
        /// GpuScene 의 이동 연산이 이 멤버를 옮길 수 있어야 해서 이동은 허용합니다(핸들만 옮깁니다).
        GpuMeshVertexPool( GpuMeshVertexPool&& ) noexcept            = default;
        GpuMeshVertexPool& operator=( GpuMeshVertexPool&& ) noexcept = default;

        /**
         * @brief 메시 집합에 맞춰 풀을 만듭니다. 집합이 지난번과 같으면 아무것도 하지 않습니다.
         * @details 순서가 아니라 **집합**을 비교합니다. 배치 정렬이 바뀌어도 메시가 같으면 다시 올리지 않습니다.
         *          예산(`kMaxPoolVertices`)을 넘는 메시는 들어가지 못하고 `kInvalidBase` 를 받습니다.
         * @param listMesh 소유하지 않는 포인터들. 스냅샷 배치가 소유를 들고 있는 동안에만 유효합니다.
         * @return 풀을 다시 만들었으면 true. 배치의 시작 오프셋이 바뀌었으니 표를 다시 올려야 합니다.
         */
        bool build( IRHIDevice* pDevice, const vector<Mesh*>& listMesh );

        /** @brief 메시의 풀 시작 오프셋(정점 단위)을 반환합니다. 풀에 없으면 `kInvalidBase` 입니다. */
        uint32 baseOf( const Mesh* pMesh ) const;

        /** @brief 풀 정점 버퍼를 반환합니다. 0 이면 풀이 비어 있습니다. */
        RHIBufferHandle getVertexBuffer() const { return _vertexBuffer; }
        /** @brief 풀에 든 정점 수를 반환합니다. */
        uint32 getVertexCount() const { return _vertexCount; }

        /** @brief 버퍼를 놓습니다. 디바이스가 바뀌거나 내려갈 때 부릅니다. */
        void release( IRHIDevice* pDevice );

    private:
        /// @brief 풀 상한(정점 수)입니다. 4M 정점 × 48 바이트 = 192MB 로, 이 저장소의 씬에는 넉넉합니다.
        static constexpr uint32 kMaxPoolVertices = 4u * 1024u * 1024u;

        RHIBufferHandle _vertexBuffer{ 0 };
        /// @brief 메시 → 풀 시작 오프셋(정점 단위)입니다.
        unordered_map<const Mesh*, uint32> _mapBase;
        /// @brief 지난 build 의 메시 집합(포인터 오름차순)입니다. 같으면 다시 만들지 않습니다.
        vector<const Mesh*> _listBuilt;
        /// @brief build 가 집합을 정렬해 두는 스크래치입니다. 프레임마다 할당하지 않습니다.
        vector<const Mesh*> _listScratchSorted;
        uint32              _vertexCount{ 0 };
    };
} // namespace sw

/**
 * @file GpuMeshMorphPool.h
 * @brief GPU 가 변형한 정점을 담는 풀입니다. 언리얼 GPU Skin Cache 가 있는 자리입니다.
 *
 * [무엇을 푸는가]
 * 정점을 실시간으로 바꾸려면 CPU 가 매 프레임 다시 올리는 수밖에 없었습니다. 그런데 `Mesh::setVertices`
 * 는 정점 버퍼를 **파괴하고 다시 만듭니다**. 메시마다, 프레임마다. 게다가 그 호출은 게임 스레드라
 * OpenGL 에서는 컨텍스트가 없습니다(`_bThreadSafeResourceCreation == false`). 그래서 GPU 가 합니다.
 *
 * [왜 메시마다 버퍼가 아니라 풀인가]
 * 이 엔진의 규약은 "드로우 사이에 바인딩이 바뀌지 않는다" 입니다. 인스턴스 · 머티리얼 · 가시 목록이 모두
 * 큰 버퍼 하나이고 드로우는 **인덱스로 읽습니다**. 메시마다 버퍼를 두면 드로우마다 SRV 를 갈아 끼워야
 * 해서 그 규약이 깨집니다. 풀 하나에 구간을 나눠 주면 SRV 는 패스당 한 번 걸리고, 배치는 시작
 * 오프셋만 배치 표(`g_SwBatches`, t13)에 싣습니다. 언리얼 스킨 캐시도 캐시 버퍼를 할당해 섹션마다 나눠 씁니다.
 *
 * [왜 정점 버퍼가 아니라 구조버퍼인가]
 * 언리얼은 결과를 정점 스트림으로 물리고(`FGPUSkinPassthroughVertexFactory`), 유니티는 정점 버퍼를
 * `Raw` 로 열어 컴퓨트가 직접 씁니다. 둘 다 "정점 버퍼이면서 UAV" 를 요구하는데 이 엔진에서는 그것이 가장
 * 비싼 길입니다. `createBuffer` 는 `Vertex` 플래그를 보면 `UnorderedAccess` 를 버리고, DX12 의
 * `createVertexBuffer` 는 UPLOAD 힙이라 UAV 가 될 수 없으며, DX11 은 구조버퍼와 정점 버퍼를 겸할 수
 * 없습니다. 그래서 결과를 **구조버퍼**에 두고 정점 셰이더가 `SV_VertexID` 로 읽습니다(정점 풀링).
 * 이미 네 백엔드에서 도는 `RHIStructuredBufferSlot` 을 그대로 쓰므로 백엔드 분기가 없습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

#include "Engine/Graphics/RHI/RHIStructuredBufferSlot.h"

namespace sw
{
    class IRHIDevice;
    class Mesh;

    /**
     * @struct GpuMorphVertex
     * @brief 모프 풀의 정점 하나입니다. GPU 에서는 **float4 둘**로 보입니다(`g_SwMorphVertices`, binding.hlsli).
     * @details 셰이더 쪽은 구조체가 아니라 `StructuredBuffer<float4>` 입니다. 처음에는 구조체였고 레이아웃도
     *          네 백엔드가 같았는데 OpenGL 만 같은 원소의 두 멤버를 **다른 원소**에서 읽었습니다(사연은
     *          binding.hlsli). 그래서 버퍼의 원소는 float4 이고 정점당 `kMorphFloat4PerVertex` 개입니다.
     *          이 구조체는 CPU 가 채우는 모양일 뿐이며, 바이트 배치는 float4 둘과 같습니다.
     */
    struct GpuMorphVertex
    {
        float4 _position{}; ///< w 는 쓰지 않습니다(정렬용).
        float4 _normal{};   ///< 변형된 노멀입니다. 컴퓨트가 위치와 **같이** 다시 만듭니다. w 는 쓰지 않습니다.
    };

    static_assert( sizeof( GpuMorphVertex ) == 2 * sizeof( float4 ), "모프 풀 정점은 float4 둘(32바이트)이어야 한다 — 셰이더가 [2i], [2i+1] 로 읽는다" );

    /// @brief 정점 하나가 차지하는 버퍼 원소(float4) 수입니다. 셰이더의 `SW_MORPH_FLOAT4_PER_VERTEX` 와 같아야 합니다.
    inline constexpr uint32 kMorphFloat4PerVertex = 2;

    /**
     * @class GpuMeshMorphPool
     * @brief 모프를 요청한 메시들의 레스트 · 결과 정점을 한 쌍의 구조버퍼에 모읍니다. 렌더 스레드가 소유합니다.
     */
    class SW_API GpuMeshMorphPool
    {
    public:
        /** @brief 풀에 들어가지 못한 메시가 받는 값입니다. 셰이더의 폴백 조건과 같은 뜻입니다. */
        static constexpr uint32 kInvalidBase = 0xFFFFFFFFu;

        GpuMeshMorphPool()                                     = default;
        ~GpuMeshMorphPool()                                    = default;
        GpuMeshMorphPool( const GpuMeshMorphPool& )            = delete;
        GpuMeshMorphPool& operator=( const GpuMeshMorphPool& ) = delete;

        /**
         * @brief 이번 프레임에 모프할 메시 목록을 받아 풀을 맞춥니다.
         * @details 목록이 지난 프레임과 같으면 아무것도 하지 않습니다(레스트는 한 번만 올립니다).
         *          예산(`kMaxPoolVertices`)을 넘는 메시는 **들어가지 못하고** 레스트 포즈로 그려집니다.
         *          언리얼 스킨 캐시가 가득 차면 일반 경로로 되돌리는 것과 같습니다.
         * @param listMesh 소유하지 않는 포인터들. 스냅샷 배치가 소유를 들고 있는 동안에만 유효합니다.
         */
        void build( IRHIDevice* pDevice, const vector<Mesh*>& listMesh );

        /** @brief 메시의 풀 시작 오프셋(정점 단위)을 반환합니다. 풀에 없으면 `kInvalidBase` 입니다. */
        uint32 baseOf( const Mesh* pMesh ) const;

        /** @brief 결과 버퍼입니다. 정점 셰이더가 SRV 로 읽습니다. */
        const RHIStructuredBufferSlot& getMorphBuffer() const { return _morph; }
        /** @brief 레스트 버퍼입니다. 컴퓨트가 SRV 로 읽습니다. */
        const RHIStructuredBufferSlot& getRestBuffer() const { return _rest; }
        /** @brief 풀에 든 정점 수입니다. 0 이면 디스패치할 것이 없습니다. */
        uint32 getVertexCount() const { return _vertexCount; }
        /** @brief 컴퓨트가 쓸 수 있는 상태인지(레스트 SRV 와 결과 UAV 가 둘 다 있는지) 확인합니다. */
        bool isDispatchable() const;

        /** @brief 버퍼를 놓습니다. 디바이스가 바뀌거나 내려갈 때 부릅니다. */
        void release( IRHIDevice* pDevice );

    private:
        /// @brief 풀 상한(정점 수)입니다. 언리얼의 `r.SkinCache.SceneMemoryLimitInMB` 자리이며, 여기서는 고정값입니다.
        static constexpr uint32 kMaxPoolVertices = 4u * 1024u * 1024u;

        RHIStructuredBufferSlot _rest;
        RHIStructuredBufferSlot _morph;
        /// @brief 메시 → 풀 시작 오프셋(정점 단위)입니다.
        unordered_map<const Mesh*, uint32> _mapBase;
        /// @brief 지난 build 의 메시 목록입니다. 같으면 다시 만들지 않습니다.
        vector<const Mesh*> _listBuilt;
        uint32              _vertexCount{ 0 };
    };
} // namespace sw

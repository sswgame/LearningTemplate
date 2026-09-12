/**
 * @file GpuMeshMorphPool.h
 * @brief GPU 가 변형한 정점을 담는 풀 — 언리얼 GPU Skin Cache 가 있는 자리
 *
 * [무엇을 푸는가]
 * 정점을 실시간으로 바꾸려면 CPU 가 매 프레임 다시 올리는 수밖에 없었다. 그런데 `Mesh::setVertices`
 * 는 정점 버퍼를 **파괴하고 다시 만든다** — 메시마다, 프레임마다. 게다가 그 호출은 게임 스레드라
 * OpenGL 에서는 컨텍스트가 없다(`_bThreadSafeResourceCreation == false`). 그래서 GPU 가 한다.
 *
 * [왜 메시마다 버퍼가 아니라 풀인가]
 * 이 엔진의 규약은 "드로우 사이에 바인딩이 바뀌지 않는다" 다 — 인스턴스·머티리얼·가시 목록이 전부
 * 큰 버퍼 하나이고 드로우는 **인덱스로 읽는다**. 메시마다 버퍼를 두면 드로우마다 SRV 를 갈아 끼워야
 * 해서 그 규약이 깨진다. 풀 하나에 구간을 나눠 주면 SRV 는 패스당 한 번 걸리고, 배치는 시작
 * 오프셋만 루트 상수로 싣는다. 언리얼 스킨 캐시도 캐시 버퍼를 할당해 섹션마다 나눠 쓴다.
 *
 * [왜 정점 버퍼가 아니라 구조버퍼인가]
 * 언리얼은 결과를 정점 스트림으로 물리고(`FGPUSkinPassthroughVertexFactory`), 유니티는 정점 버퍼를
 * `Raw` 로 열어 컴퓨트가 직접 쓴다. 둘 다 "정점 버퍼이면서 UAV" 를 요구하는데 이 엔진에서는 그게 가장
 * 비싼 길이다 — `createBuffer` 가 `Vertex` 플래그를 보면 `UnorderedAccess` 를 버리고, DX12 의
 * `createVertexBuffer` 는 UPLOAD 힙이라 UAV 가 될 수 없으며, DX11 은 구조버퍼와 정점 버퍼를 겸할 수
 * 없다. 그래서 결과를 **구조버퍼**에 두고 정점 셰이더가 `SV_VertexID` 로 읽는다(정점 풀링).
 * 이미 네 백엔드에서 도는 `RHIStructuredBufferSlot` 을 그대로 쓰므로 백엔드 분기가 없다.
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
     * @brief 모프 풀의 원소 — 셰이더의 `SwVertexData`(binding.hlsli) 와 레이아웃이 같아야 합니다.
     * @details `RHIVertex` 를 그대로 쓰지 않는 이유는 **정렬**이다. std430 은 vec4 를 16 바이트 경계에
     *          맞추므로 `float3` 뒤의 `float4` 가 어긋난다 — DX/Vulkan 은 DXC 가 명시 오프셋을 적어
     *          넘어가지만 OpenGL 에서는 기하가 무너진다(실제로 GL 만 그랬다). 그래서 전부 `float4` 다.
     */
    struct GpuMorphVertex
    {
        float4 _position{}; ///< w 는 쓰지 않는다(정렬용).
        float4 _normal{};   ///< 변형된 노멀 — 컴퓨트가 위치와 **같이** 다시 만든다. w 는 쓰지 않는다.
    };

    static_assert( sizeof( GpuMorphVertex ) == 32, "모프 풀 원소는 float4 둘(32바이트)이어야 한다 — 셰이더 SwVertexData 와 같은 크기" );

    /**
     * @class GpuMeshMorphPool
     * @brief 모프를 요청한 메시들의 레스트·결과 정점을 한 쌍의 구조버퍼에 모읍니다. 렌더 스레드 소유.
     */
    class SW_API GpuMeshMorphPool
    {
    public:
        /** @brief 풀에 들어가지 못한 메시가 받는 값. 셰이더의 폴백 조건과 같은 뜻이다. */
        static constexpr uint32 kInvalidBase = 0xFFFFFFFFu;

        GpuMeshMorphPool()                                     = default;
        ~GpuMeshMorphPool()                                    = default;
        GpuMeshMorphPool( const GpuMeshMorphPool& )            = delete;
        GpuMeshMorphPool& operator=( const GpuMeshMorphPool& ) = delete;

        /**
         * @brief 이번 프레임에 모프할 메시 목록을 받아 풀을 맞춥니다.
         * @details 목록이 지난 프레임과 같으면 아무것도 하지 않는다(레스트는 한 번만 올린다).
         *          예산(`kMaxPoolVertices`)을 넘는 메시는 **들어가지 못하고** 레스트 포즈로 그려진다 —
         *          언리얼 스킨 캐시가 가득 차면 일반 경로로 되돌리는 것과 같다.
         * @param listMesh 소유하지 않는 포인터들. 스냅샷 배치가 소유를 들고 있는 동안에만 유효하다.
         */
        void build( IRHIDevice* pDevice, const vector<Mesh*>& listMesh );

        /** @brief 메시의 풀 시작 오프셋(정점 단위). 풀에 없으면 `kInvalidBase`. */
        uint32 baseOf( const Mesh* pMesh ) const;

        /** @brief 결과 버퍼 — 정점 셰이더가 읽는다(SRV). */
        const RHIStructuredBufferSlot& getMorphBuffer() const { return _morph; }
        /** @brief 레스트 버퍼 — 컴퓨트가 읽는다(SRV). */
        const RHIStructuredBufferSlot& getRestBuffer() const { return _rest; }
        /** @brief 풀에 든 정점 수. 0 이면 디스패치할 것이 없다. */
        uint32 getVertexCount() const { return _vertexCount; }
        /** @brief 컴퓨트가 쓸 수 있는 상태인가(레스트 SRV 와 결과 UAV 가 둘 다 있는가). */
        bool isDispatchable() const;

        /** @brief 버퍼를 놓습니다. 디바이스가 바뀌거나 내려갈 때. */
        void release( IRHIDevice* pDevice );

    private:
        /// @brief 풀 상한(정점 수). 언리얼의 `r.SkinCache.SceneMemoryLimitInMB` 자리 — 여기서는 고정값이다.
        static constexpr uint32 kMaxPoolVertices = 4u * 1024u * 1024u;

        RHIStructuredBufferSlot _rest;
        RHIStructuredBufferSlot _morph;
        /// @brief 메시 → 풀 시작 오프셋(정점 단위).
        unordered_map<const Mesh*, uint32> _mapBase;
        /// @brief 지난 build 의 메시 목록 — 같으면 다시 만들지 않는다.
        vector<const Mesh*> _listBuilt;
        uint32              _vertexCount{ 0 };
    };
} // namespace sw

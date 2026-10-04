/**
 * @file GpuMeshMorphPool.h
 * @brief GPU 가 변형한 정점을 담는 풀입니다. 언리얼 GPU Skin Cache 가 있는 자리입니다.
 *
 * [무엇을 푸는가]
 * CPU 에서 정점을 실시간으로 바꾸면 매 프레임 다시 올려야 합니다. 그런데 `Mesh::setVertices`
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
 *
 * [스키닝]
 * 풀은 두 구간입니다: [모프 메시들][스킨드 메시들]. 모프 컴퓨트(meshmorph.hlsl)는 앞 구간을, 스키닝 컴퓨트(meshskin.hlsl)는 뒤 구간을
 * 씁니다. 스킨 구간에는 정점마다 가중치 · 팔레트 행 번호(float4 둘, `_skinWeight`)가 한 번 올라가고, 팔레트(`_skinPalette`, 본 하나 =
 * float4 셋)는 프레임마다 올라갑니다. 행 번호에는 그 메시의 팔레트 시작이 미리 더해져 있어 컴퓨트 디스패치는 하나입니다.
 * 결과를 읽는 쪽(정점 셰이더)은 모프와 똑같아 새 그리기 코드가 없습니다.
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
    struct GpuSkinPalette;

    class IRHIDevice;
    class Mesh;

    /**
     * @struct GpuMorphVertex
     * @brief 모프 풀의 정점 하나입니다. GPU 에서는 **float4 둘**로 보입니다(`g_SwMorphVertices`, binding.hlsli).
     * @details 셰이더 쪽은 구조체가 아니라 `StructuredBuffer<float4>` 입니다. 주의: 구조체로 선언하면 레이아웃이 네 백엔드에서
     *          같아도 OpenGL 만 같은 원소의 두 멤버를 **다른 원소**에서 읽습니다. 그래서 버퍼의 원소는 float4 이고 정점당
     *          `kMorphFloat4PerVertex` 개입니다.
     *          이 구조체는 CPU 가 채우는 모양일 뿐이며, 바이트 배치는 float4 둘과 같습니다.
     */
    struct GpuMorphVertex
    {
        float4 _position{}; ///< w 는 쓰지 않습니다(정렬용).
        float4 _normal{};   ///< 변형된 노멀입니다. 컴퓨트가 위치와 **같이** 다시 만듭니다. w 는 쓰지 않습니다.
    };
} // namespace sw

namespace sw
{
    static_assert( sizeof( GpuMorphVertex ) == 2 * sizeof( float4 ), "모프 풀 정점은 float4 둘(32바이트)이어야 한다 — 셰이더가 [2i], [2i+1] 로 읽는다" );

    /// @brief 정점 하나가 차지하는 버퍼 원소(float4) 수입니다. 셰이더의 `SW_MORPH_FLOAT4_PER_VERTEX` 와 같아야 합니다.
    inline constexpr uint32 kMorphFloat4PerVertex = 2;
    /// @brief 스킨 팔레트의 본 하나가 차지하는 버퍼 원소(float4) 수입니다(행벡터 4x4 의 0 · 1 · 2 열). 셰이더의 `SW_SKIN_FLOAT4_PER_BONE` 과 같아야 합니다.
    inline constexpr uint32 kSkinFloat4PerBone = 3;

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
        /**
         * @brief 모프 메시와 스킨드 메시를 함께 받아 풀을 맞춥니다. 모프가 앞 구간, 스킨이 뒤 구간입니다.
         * @details 목록 둘이 지난 프레임과 같으면(포인터 · 내용 번호) 아무것도 하지 않습니다. 스킨 가중치도 한 번만 올립니다.
         */
        void build( IRHIDevice* pDevice, const vector<Mesh*>& listMorphMesh, const vector<Mesh*>& listSkinMesh );
        /**
         * @brief 이번 프레임 팔레트를 풀의 스킨 구간 순서로 올립니다. 팔레트가 없는 메시는 단위 행렬(바인드 포즈)입니다.
         * @param pListRow 스냅샷의 팔레트 행(본 하나 = float4 셋). nullptr 이면 모두 단위입니다.
         */
        void uploadSkinPalettes( IRHIDevice* pDevice, const vector<GpuSkinPalette>& listPalette, const vector<float4>* pListRow );

        /** @brief 메시의 풀 시작 오프셋(정점 단위)을 반환합니다. 풀에 없으면 `kInvalidBase` 입니다. */
        uint32 baseOf( const Mesh* pMesh ) const;

        /** @brief 결과 버퍼입니다. 정점 셰이더가 SRV 로 읽습니다. */
        const RHIStructuredBufferSlot& getMorphBuffer() const { return _morph; }
        /** @brief 레스트 버퍼입니다. 컴퓨트가 SRV 로 읽습니다. */
        const RHIStructuredBufferSlot& getRestBuffer() const { return _rest; }
        /** @brief 풀에 든 정점 수(모프 + 스킨)입니다. 정점 셰이더의 범위 검사 값입니다. */
        uint32 getVertexCount() const { return _vertexCount; }
        /** @brief 모프 구간(앞)의 정점 수입니다. 모프 컴퓨트가 이만큼 돕니다. */
        uint32 getMorphVertexCount() const { return _skinVertexBase; }
        /** @brief 스킨 구간이 시작하는 정점입니다. */
        uint32 getSkinVertexBase() const { return _skinVertexBase; }
        /** @brief 스킨 구간의 정점 수입니다. */
        uint32 getSkinVertexCount() const { return _vertexCount - _skinVertexBase; }
        /** @brief 스킨 팔레트의 본 수(모든 스킨드 메시의 합)입니다. */
        uint32 getSkinBoneCount() const { return _skinBoneCount; }
        /** @brief 스킨 가중치 버퍼입니다(스킨 정점 하나 = float4 둘). */
        const RHIStructuredBufferSlot& getSkinWeightBuffer() const { return _skinWeight; }
        /** @brief 스킨 팔레트 버퍼입니다(본 하나 = float4 셋). */
        const RHIStructuredBufferSlot& getSkinPaletteBuffer() const { return _skinPalette; }
        /** @brief 컴퓨트가 쓸 수 있는 상태인지(레스트 SRV 와 결과 UAV 가 둘 다 있는지) 확인합니다. */
        bool isDispatchable() const;
        /** @brief 스키닝 컴퓨트가 쓸 수 있는 상태인지(스킨 정점 · 가중치 · 팔레트 SRV 가 있는지) 확인합니다. */
        bool isSkinDispatchable() const;

        /** @brief 버퍼를 놓습니다. 디바이스가 바뀌거나 내려갈 때 부릅니다. */
        void release( IRHIDevice* pDevice );

    private:
        /// @brief 풀 상한(정점 수)입니다. 언리얼의 `r.SkinCache.SceneMemoryLimitInMB` 자리이며, 여기서는 고정값입니다.
        static constexpr uint32 kMaxPoolVertices = 4u * 1024u * 1024u;

        RHIStructuredBufferSlot _rest;
        RHIStructuredBufferSlot _morph;
        RHIStructuredBufferSlot _skinWeight;
        RHIStructuredBufferSlot _skinPalette;
        /// @brief 메시 → 풀 시작 오프셋(정점 단위)입니다.
        unordered_map<const Mesh*, uint32> _mapBase;
        /// @brief 지난 build 의 메시 목록입니다. 같으면 다시 만들지 않습니다.
        vector<const Mesh*> _listBuilt;
        /// @brief `_listBuilt` 와 같은 순서의 내용 번호(`Mesh::getContentId`)입니다. 포인터가 같아도 이것이 다르면 다른 메시다.
        vector<uint64> _listBuiltContentId;
        /// @brief 스킨 구간의 메시들(풀 순서)과 그 팔레트 시작 본입니다.
        vector<const Mesh*> _listSkinMesh;
        vector<uint32>      _listSkinBoneBase;
        /// @brief 팔레트를 풀 순서로 다시 모으는 자리입니다(프레임마다 재사용).
        vector<float4> _listScratchPaletteRow;
        uint32         _vertexCount{ 0 };
        uint32         _skinVertexBase{ 0 };
        uint32         _skinBoneCount{ 0 };
    };
} // namespace sw

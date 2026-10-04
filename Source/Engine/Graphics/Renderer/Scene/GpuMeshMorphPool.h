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
 * 결과 버퍼는 두 구간입니다: [모프 메시][스킨 인스턴스]. 모프 컴퓨트(meshmorph.hlsl)는 앞 구간을, 스키닝 컴퓨트(meshskin.hlsl)는 뒤 구간을
 * 씁니다. 스킨 데이터는 **원본**(`Mesh::getSkinDataId` 가 같은 메시들)마다 한 번만 올라갑니다 — 레스트 정점(`_skinRest`)과 정점마다 가중치 ·
 * 본 번호(`_skinWeight`, float4 둘). 스킨 **인스턴스**(그리는 메시 객체 — 캐릭터 · 군중 묶음 · 사본)는 결과 구간 · 원본 구간 · 정점 수 ·
 * 팔레트 시작을 인스턴스 표(`_skinInstance`, uint4 둘)에 한 줄씩 갖고, 팔레트(`_skinPalette`, 본 하나 = float4 셋)는 프레임마다 올라갑니다.
 * 컴퓨트는 디스패치 하나로 스킨 구간 전체를 돌며 정점마다 인스턴스를 이분 탐색해 찾습니다. 그래서 같은 캐릭터의 사본이 여럿이어도 레스트 ·
 * 가중치는 한 벌이고, 인스턴스가 늘거나 줄어도 다시 올리는 것은 인스턴스 표뿐입니다(언리얼 스킨 캐시가 같은 스켈레탈 메시의 정점 데이터를 나누는
 * 것과 같은 자리). 결과를 읽는 쪽(정점 셰이더)은 모프와 똑같아 새 그리기 코드가 없습니다.
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
    /// @brief 스킨 인스턴스 표의 한 줄이 차지하는 원소(uint4) 수입니다. 셰이더의 `SW_SKIN_UINT4_PER_INSTANCE` 와 같아야 합니다.
    inline constexpr uint32 kSkinUint4PerInstance = 2;
} // namespace sw

namespace sw
{
    /**
     * @struct GpuSkinInstanceRow
     * @brief 스킨 인스턴스 표의 한 줄(uint4 둘)입니다. 셰이더 meshskin.hlsl 이 `g_SkinInstances[2i]` · `[2i + 1]` 로 읽습니다.
     */
    struct GpuSkinInstanceRow
    {
        uint32 _resultOffset{ 0 };            ///< 스킨 구간 안에서 이 인스턴스의 결과 시작(정점)
        uint32 _sourceBase{ 0 };              ///< 원본 레스트 · 가중치 버퍼에서의 시작(정점)
        uint32 _vertexCount{ 0 };             ///< 정점 수
        uint32 _paletteBase{ 0 };             ///< 팔레트에서의 시작 본
        uint32 _arrReserved[4]{ 0, 0, 0, 0 }; ///< 둘째 uint4 — 모프 타깃 가중치 자리(지금은 0)
    };
} // namespace sw

namespace sw
{
    static_assert( sizeof( GpuSkinInstanceRow ) == kSkinUint4PerInstance * 16, "스킨 인스턴스 줄은 uint4 둘이어야 한다(meshskin.hlsl)" );

    /**
     * @class GpuMeshMorphPool
     * @brief 모프를 요청한 메시들의 레스트 · 결과 정점과 스킨 원본 · 인스턴스를 구조버퍼에 모읍니다. 렌더 스레드가 소유합니다.
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
         * @brief 모프 메시와 스킨드 메시를 함께 받아 풀을 맞춥니다. 모프가 앞 구간, 스킨 인스턴스가 뒤 구간입니다.
         * @details 세 가지를 따로 봅니다 — 모프 집합이 바뀌면 모프 레스트를, 스킨 원본(스킨 데이터 번호) 집합이 바뀌면 원본 레스트 · 가중치를,
         *          스킨 인스턴스 목록이 바뀌면 인스턴스 표만 다시 올립니다. 결과 버퍼는 컴퓨트가 프레임마다 채우므로 배치가 바뀌어도 올릴 것이 없습니다.
         */
        void build( IRHIDevice* pDevice, const vector<Mesh*>& listMorphMesh, const vector<Mesh*>& listSkinMesh );
        /**
         * @brief 이번 프레임 팔레트를 풀의 스킨 인스턴스 순서로 올립니다. 팔레트가 없는 메시는 단위 행렬(바인드 포즈)입니다.
         * @param pListRow 스냅샷의 팔레트 행(본 하나 = float4 셋). nullptr 이면 모두 단위입니다.
         */
        void uploadSkinPalettes( IRHIDevice* pDevice, const vector<GpuSkinPalette>& listPalette, const vector<float4>* pListRow );

        /** @brief 메시의 풀 시작 오프셋(정점 단위)을 반환합니다. 풀에 없으면 `kInvalidBase` 입니다. */
        uint32 baseOf( const Mesh* pMesh ) const;

        /** @brief 결과 버퍼입니다. 정점 셰이더가 SRV 로 읽습니다. */
        const RHIStructuredBufferSlot& getMorphBuffer() const { return _morph; }
        /** @brief 모프 메시의 레스트 버퍼입니다. 모프 컴퓨트가 SRV 로 읽습니다(진단 2 · 3 은 정점 셰이더에 그대로 물린다). */
        const RHIStructuredBufferSlot& getRestBuffer() const { return _rest; }
        /** @brief 풀에 든 결과 정점 수(모프 + 스킨 인스턴스)입니다. 정점 셰이더의 범위 검사 값입니다. */
        uint32 getVertexCount() const { return _vertexCount; }
        /** @brief 모프 구간(앞)의 정점 수입니다. 모프 컴퓨트가 이만큼 돕니다. */
        uint32 getMorphVertexCount() const { return _skinVertexBase; }
        /** @brief 스킨 구간이 시작하는 정점입니다. */
        uint32 getSkinVertexBase() const { return _skinVertexBase; }
        /** @brief 스킨 구간의 정점 수(모든 인스턴스의 합)입니다. */
        uint32 getSkinVertexCount() const { return _vertexCount - _skinVertexBase; }
        /** @brief 스킨 팔레트의 본 수(모든 스킨 인스턴스의 합)입니다. */
        uint32 getSkinBoneCount() const { return _skinBoneCount; }
        /** @brief 결과 구간을 받은 스킨 인스턴스 수입니다. */
        uint32 getSkinInstanceCount() const { return static_cast<uint32>( _listSkinRow.size() ); }
        /** @brief 올린 스킨 원본(스킨 데이터 번호가 다른 메시) 수입니다. 같은 캐릭터의 사본이 레스트를 나누는지 시험이 봅니다. */
        uint32 getSkinSourceCount() const { return static_cast<uint32>( _listSourceDataId.size() ); }
        /** @brief 올린 스킨 원본의 정점 수 합입니다. */
        uint32 getSkinSourceVertexCount() const { return _skinSourceVertexCount; }
        /** @brief 스킨 원본의 레스트 정점 버퍼입니다(정점 하나 = float4 둘). */
        const RHIStructuredBufferSlot& getSkinRestBuffer() const { return _skinRest; }
        /** @brief 스킨 원본의 가중치 버퍼입니다(정점 하나 = float4 둘 — 가중치 넷, 원본 본 번호 넷). */
        const RHIStructuredBufferSlot& getSkinWeightBuffer() const { return _skinWeight; }
        /** @brief 스킨 인스턴스 표입니다(인스턴스 하나 = uint4 둘, `GpuSkinInstanceRow`). */
        const RHIStructuredBufferSlot& getSkinInstanceBuffer() const { return _skinInstance; }
        /** @brief 스킨 팔레트 버퍼입니다(본 하나 = float4 셋). */
        const RHIStructuredBufferSlot& getSkinPaletteBuffer() const { return _skinPalette; }
        /** @brief 컴퓨트가 쓸 수 있는 상태인지(레스트 SRV 와 결과 UAV 가 둘 다 있는지) 확인합니다. */
        bool isDispatchable() const;
        /** @brief 스키닝 컴퓨트가 쓸 수 있는 상태인지(결과 UAV · 원본 · 가중치 · 인스턴스 표 · 팔레트 SRV 가 있는지) 확인합니다. */
        bool isSkinDispatchable() const;

        /** @brief 버퍼를 놓습니다. 디바이스가 바뀌거나 내려갈 때 부릅니다. */
        void release( IRHIDevice* pDevice );

    private:
        /// @brief 풀 상한(결과 정점 수)입니다. 언리얼의 `r.SkinCache.SceneMemoryLimitInMB` 자리이며, 여기서는 고정값입니다.
        static constexpr uint32 kMaxPoolVertices = 4u * 1024u * 1024u;

        /** @brief 목록이 지난 build 와 같은지(포인터 · 내용 번호) 봅니다. */
        static bool isSameList( const vector<Mesh*>& listMesh, const vector<const Mesh*>& listBuilt, const vector<uint64>& listBuiltContentId );
        /** @brief 스킨 원본 집합을 맞춥니다. 바뀌었으면 레스트 · 가중치를 다시 올립니다. */
        void rebuildSkinSources( IRHIDevice* pDevice, const vector<Mesh*>& listSkinMesh );

        RHIStructuredBufferSlot _rest;
        RHIStructuredBufferSlot _morph;
        RHIStructuredBufferSlot _skinRest;
        RHIStructuredBufferSlot _skinWeight;
        RHIStructuredBufferSlot _skinInstance;
        RHIStructuredBufferSlot _skinPalette;
        /// @brief 메시 → 결과 시작 오프셋(정점 단위)입니다.
        unordered_map<const Mesh*, uint32> _mapBase;
        /// @brief 지난 build 의 모프 메시 목록 · 내용 번호입니다. 포인터가 같아도 내용 번호가 다르면 다른 메시다.
        vector<const Mesh*> _listBuiltMorph;
        vector<uint64>      _listBuiltMorphContentId;
        /// @brief 지난 build 의 스킨 메시 목록 · 내용 번호입니다(받은 그대로 — 풀에 못 든 것도).
        vector<const Mesh*> _listBuiltSkin;
        vector<uint64>      _listBuiltSkinContentId;
        /// @brief 결과 구간을 받은 스킨 인스턴스(풀 순서) · 그 표 줄입니다.
        vector<const Mesh*>        _listSkinMesh;
        vector<GpuSkinInstanceRow> _listSkinRow;
        /// @brief 올린 스킨 원본(스킨 데이터 번호)과 그 원본 버퍼 시작(정점)입니다.
        vector<uint64> _listSourceDataId;
        vector<uint32> _listSourceBase;
        /// @brief 메시 → 이번 프레임 팔레트 항목 자리입니다(`uploadSkinPalettes` 가 프레임마다 다시 짓는다 — 인스턴스마다 목록을 훑지 않게).
        unordered_map<const Mesh*, uint32> _mapScratchPaletteIndex;
        /// @brief 팔레트를 풀 순서로 다시 모으는 자리입니다(프레임마다 재사용).
        vector<float4> _listScratchPaletteRow;
        uint32         _vertexCount{ 0 };
        uint32         _skinVertexBase{ 0 };
        uint32         _skinBoneCount{ 0 };
        uint32         _skinSourceVertexCount{ 0 };
    };
} // namespace sw

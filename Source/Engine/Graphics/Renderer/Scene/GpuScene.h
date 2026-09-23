/**
 * @file GpuScene.h
 * @brief 렌더 스레드의 씬 GPU 데이터입니다. 스냅샷을 받아 인스턴스 · 배치 표 · 간접 인자 · 머티리얼 버퍼로 올립니다.
 * @details 씬(MeshComponent)을 보지 않습니다. 입력은 `GpuSceneSnapshot` 하나이고(`adoptCpuSnapshot`), 여기서 만드는
 *          값(GPU 핸들 · 컬 뷰 · 간접 개수)은 스냅샷 타입에 없어 게임 스레드로 되돌아갈 수 없습니다. 스냅샷을 만드는
 *          쪽은 `GpuSceneBuilder` 입니다.
 */
#pragma once
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"

#include "Engine/EngineMinimal.h"
#include "Engine/Graphics/RHI/RHIStructuredBufferSlot.h"
#include "Engine/Graphics/RHI/RHITypes.h"
#include "Engine/Graphics/Renderer/Frame/RenderView.h"
#include "Engine/Graphics/Renderer/Scene/GpuMeshVertexPool.h"
#include "Engine/Graphics/Renderer/Scene/GpuSceneSnapshot.h"

namespace sw
{
    class GpuMeshMorphPool;
    class IRHIDevice;
    class Mesh;

    /**
     * @brief 뷰 하나가 갖는 컬링 산출물(간접 인자 + 가시 인스턴스 목록)입니다.
     * @details 컬링 결과는 **절두체에 종속**입니다. 메인 카메라로 거른 목록을 그림자 패스가 쓰면, 화면
     *          밖에 있지만 화면 안으로 그림자를 드리우는 물체가 사라집니다. 그래서 뷰마다 하나씩입니다.
     *          뷰의 **입력**(행렬 · 절두체 · 상수버퍼)은 RenderView 가 갖습니다.
     */
    struct GpuCullViewResources
    {
        RHIStructuredBufferSlot _indirectArgs;
        RHIStructuredBufferSlot _visibleInstances;
    };

    /**
     * @brief 배치의 가시 목록 정렬 방식입니다.
     * @details 컬링이 압축을 하면 자리 번호가 원자 연산의 완료 순서로 정해집니다. 불투명은 상관없지만
     *          투명은 그 순서가 곧 블렌딩 순서입니다. 그래서 투명은 컬링 뒤에 **GPU 에서 깊이순으로 다시
     *          정렬**합니다(instancesort.hlsl). 한 워크그룹에 안 담기는 큰 배치만 압축을 포기하고 CPU 가
     *          정렬해 둔 제자리 매핑을 씁니다.
     */
    enum class GpuBatchSortMode : uint32
    {
        None     = 0, ///< 불투명. 압축만 하고 순서는 상관없음
        Preserve = 1, ///< 압축하지 않고 CPU 정렬 순서를 그대로 (GPU 정렬 한계를 넘는 투명 배치)
        DepthGpu = 2, ///< 압축한 뒤 GPU 가 깊이순으로 정렬 (투명 기본)
    };

    /// @brief 한 배치에서 GPU 정렬로 다룰 수 있는 최대 인스턴스 수입니다(instancesort.hlsl 의 SW_SORT_MAX_ELEMENTS).
    inline constexpr uint32 kGpuSortMaxElements = 512;

    /**
     * @brief 배치 표의 원소 하나입니다. 컬링 컴퓨트(t1)와 정점 셰이더(`g_SwBatches`, t13)가 같은 표를 읽습니다.
     * @details 컬링은 배치의 인스턴스 구간으로 "이 배치의 가시 목록은 어디서 시작하나" 를 알고, 정점 셰이더는
     *          모프 · 정점 풀 오프셋을 읽습니다. binding.hlsli 의 SwBatchData · gpucull.hlsl 의 GpuBatchInfo 와
     *          레이아웃이 같아야 합니다(uint 여덟, 32 바이트).
     */
    struct GpuBatchInfo
    {
        uint32 _instanceBase{ 0 };
        uint32 _instanceCount{ 0 };
        /**
         * @brief 이 배치의 가시 목록을 어떻게 다룰지입니다(GpuBatchSortMode). gpucull.hlsl · instancesort.hlsl 과 값이 같아야 합니다.
         */
        uint32 _sortMode{ 0 };
        /**
         * @brief 모프 정점 풀에서 이 배치 메시의 시작(정점 단위)입니다. 0xFFFFFFFF = 모프 안 함.
         * @details 예전에는 드로우마다 루트 상수로 실었습니다. 배치마다 다른 값이 표에 있어야 같은 PSO 의 배치들을 멀티 드로우
         *          하나로 낼 수 있습니다. 정점 셰이더는 자기 배치 번호로 이 표(g_SwBatches, t13)를 읽습니다.
         */
        uint32 _morphVertexBase{ 0xFFFFFFFFu };
        /// @brief 정점 풀(GpuMeshVertexPool)에서 이 배치 메시의 시작(정점 단위)입니다. 풀 밖 메시는 0 입니다.
        uint32 _firstVertex{ 0 };
        uint32 _pad0{ 0 };
        uint32 _pad1{ 0 };
        uint32 _pad2{ 0 };
    };
    static_assert( sizeof( GpuBatchInfo ) == 8 * sizeof( uint32 ), "GpuBatchInfo 는 uint 여덟(32바이트) — binding.hlsli SwBatchData · gpucull.hlsl GpuBatchInfo 와 같아야 한다" );

    /// @brief 머티리얼 그룹의 GPU 버퍼입니다. RT 가 소유하고, 셰이더 경로를 키로 스냅샷을 넘어 재사용합니다.
    struct GpuMaterialGpu
    {
        RHIStructuredBufferSlot _slot;
        /** @brief 마지막으로 올린 바이트입니다. 같으면 업로드를 건너뜁니다(언리얼처럼 바뀐 것만 올립니다). */
        vector<uint8> _lastBytes;
    };

    /**
     * @class GpuScene
     * @brief 렌더 스레드가 영속 소유하는 씬 GPU 상태입니다. 매 프레임 패킷의 스냅샷을 받아(`adoptCpuSnapshot`) 올립니다(`upload`).
     * @details GPU 버퍼 · 핸들은 프레임을 넘어 재사용합니다. GPU 상태까지 통째로 바꿔 끼우면 직전 프레임에 올린 버퍼를
     *          `releaseGpu` 없이 잃어 매 프레임 새로 만드는 리크가 됩니다(실제로 그랬습니다). 그래서 스냅샷만 갈아 끼우고 GPU 쪽은 여기 남깁니다.
     */
    class SW_API GpuScene
    {
    public:
        GpuScene() noexcept = default;
        ~GpuScene()         = default;

        GpuScene( GpuScene&& other ) noexcept            = default;
        GpuScene& operator=( GpuScene&& other ) noexcept = default;

        GpuScene( const GpuScene& )            = delete;
        GpuScene& operator=( const GpuScene& ) = delete;

        /** @brief 스냅샷(과 그것이 든 소유)을 놓고 개수를 0 으로 되돌립니다. GPU 버퍼는 `releaseGpu` 가 놓습니다. */
        void clear();
        /**
         * @brief 패킷의 스냅샷을 받아 갑니다. **바꿔치기**라서 받는 쪽의 지난 스냅샷이 패킷 자리로 돌아갑니다.
         *        GPU 핸들 · 용량 · 간접 개수 같은 RT 소유 상태는 타입이 달라 건드릴 수 없습니다.
         * @details 옮겨 오기(move)만 하면 패킷 자리의 저장소가 비어, 다음에 GT 가 그 자리를 다시 채울 때 모두 새로
         *          할당합니다. 바꿔치기면 저장소가 GT → 링 → RT → 링 → GT 로 돌아 용량이 남습니다(프레임당 할당 0).
         */
        void adoptCpuSnapshot( GpuSceneSnapshot& snapshot );
        /**
         * @brief 스냅샷을 GPU 에 올립니다(RT). 인스턴스 버퍼 · 배치 표 · 간접 인자 · 머티리얼 그룹 버퍼 · 정점 풀을 맞춥니다.
         *        인스턴스 버퍼와 메인 뷰 간접 인자가 모두 있으면 true 를 반환합니다.
         */
        bool upload( IRHIDevice* pDevice );
        /** @brief GPU 버퍼를 해제합니다. */
        void releaseGpu( IRHIDevice* pDevice );

        /** @brief 인스턴스 목록을 반환합니다. */
        const vector<GpuInstance>& getInstances() const { return _snapshot.getInstances(); }
        /**
         * @brief GPU 회전을 요청한(시드가 0 이 아닌) 인스턴스 수입니다.
         * @details 0 이면 애니메이션 디스패치를 통째로 건너뜁니다. 그러지 않으면 회전을 쓰지 않는 씬도 매 프레임
         *          인스턴스당 96 바이트를 읽고 아무 일도 하지 않습니다.
         */
        uint32 getSpinInstanceCount() const { return _snapshot._spinInstanceCount; }
        /** @brief 모든 배치(불투명 다음 투명)를 반환합니다. 간접 슬롯 순서와 같습니다. 모프 풀 구성이 이 목록을 봅니다. */
        const vector<GpuMeshBatch>& getAllBatches() const { return _snapshot._listAllBatch; }
        /** @brief 불투명 배치를 반환합니다. */
        const vector<GpuMeshBatch>& getOpaqueBatches() const { return _snapshot._listOpaqueBatch; }
        /** @brief 투명 배치를 반환합니다. */
        const vector<GpuMeshBatch>& getTransparentBatches() const { return _snapshot._listTransparentBatch; }
        /** @brief 셰이더 타입별 머티리얼 데이터 그룹(CPU 스냅샷)을 반환합니다. */
        const vector<GpuMaterialGroup>& getMaterialGroups() const { return _snapshot._listMaterialGroup; }
        /** @brief 퍼뮤테이션 하나를 얻습니다. 인덱스가 없으면 nullptr 입니다. */
        const GpuShaderPermutation* findShaderPermutation( uint32 index ) const
        {
            const vector<GpuShaderPermutation>* pList = _snapshot._pListShaderPermutation.get();
            return ( pList != nullptr && index < pList->size() ) ? &( *pList )[index] : nullptr;
        }
        /**
         * @brief 배치마다 모프 풀 구간을 적습니다(RT 전용).
         * @details GT 는 GPU 풀을 모릅니다(스냅샷 소유 규칙). 그래서 RT 가 프레임마다 채웁니다.
         *          세 목록(불투명 · 투명 · 전체)이 같은 배치를 따로 들고 있으므로 모두 채워야 합니다.
         */
        void assignMorphBases( const GpuMeshMorphPool& pool );

        /** @brief 인스턴스 버퍼 핸들을 반환합니다. */
        RHIBufferHandle getInstanceBuffer() const { return _instances._buffer; }
        /** @brief 인스턴스 SRV 인덱스를 반환합니다. */
        RHIDescriptorIndex getInstanceSrv() const { return _instances._srv; }
        /**
         * @brief 인스턴스 버퍼의 UAV 인덱스입니다. 컴퓨트가 월드 행렬을 **고쳐 쓰는** 통로입니다.
         * @details instanceanim.hlsl 이 인스턴스마다 다른 각속도로 회전을 얹습니다. 백엔드가 구조버퍼 UAV 를
         *          못 만들면 kInvalidDescriptorIndex 라 애니메이션 패스가 통째로 생략됩니다(그리기는 그대로).
         */
        RHIDescriptorIndex getInstanceUav() const { return _instances._uav; }
        /** @brief 뷰 하나의 컬링 산출물(간접 인자 + 가시 목록)을 반환합니다. */
        const GpuCullViewResources& getCullView( RenderViewType view ) const { return _arrCullView[static_cast<uint32>( view )]; }
        /** @brief 배치 표 버퍼의 SRV 를 반환합니다(컬링 컴퓨트 t1 · 그래픽스 t13 g_SwBatches). */
        RHIDescriptorIndex getBatchInfoSrv() const { return _batchInfo._srv; }
        /** @brief 배치 표 버퍼 핸들을 반환합니다. */
        RHIBufferHandle getBatchInfoBuffer() const { return _batchInfo._buffer; }
        /** @brief 씬 메시 정점 풀(RT 소유)을 반환합니다. */
        const GpuMeshVertexPool& getVertexPool() const { return _vertexPool; }
        /** @brief 인스턴스 슬롯 스트림 정점 버퍼(슬롯 1)를 반환합니다. 0 이면 아직 없습니다. */
        RHIBufferHandle getInstanceSlotStream() const { return _instanceSlotStream; }
        /**
         * @brief 정점 풀을 쓸지 정합니다(기본 켬). 끄면 배치가 자기 정점 버퍼로 그립니다(startVertex 0). 진단 · A/B 용입니다.
         * @details 풀을 켠 그림과 끈 그림은 같아야 합니다. 다르면 백엔드가 간접 인자의 startVertex 나 SV_VertexID 를 다르게
         *          다루는 것입니다(binding.hlsli SwMorphElementOf 의 API 차이 참고).
         */
        void setVertexPoolEnabled( bool bEnabled );
        /**
         * @brief 간접 인자의 인스턴스 개수를 CPU 가 채울지, 컴퓨트가 만들지 정합니다.
         * @details true 면 업로드 시점에 개수를 **0 으로** 올립니다. 컬링 컴퓨트가 InterlockedAdd 로 채우기
         *          때문입니다. 컬링이 없으면 CPU 가 채운 개수 그대로 그려야 하므로 false 여야 합니다.
         *          FrameRenderer 가 매 프레임 실제 컬링 가능 여부를 보고 정합니다.
         */
        void setIndirectCountsFilledByGpu( bool bByGpu );
        /**
         * @brief 간접 인자의 인스턴스 개수만 다시 올립니다(배치 구성이 그대로일 때).
         * @details 컬링 컴퓨트는 개수를 0 에서부터 셉니다. 그래서 씬이 하나도 안 바뀐 프레임에도 개수는
         *          되돌려 놓아야 합니다. 배치당 16 바이트뿐이라 모두 올려도 쌉니다.
         */
        void refreshIndirectCounts( IRHIDevice* pDevice );
        /** @brief 모든 컬링 뷰가 간접 인자와 가시 목록을 다 갖췄는지 확인합니다(하나라도 없으면 GPU 개수를 쓰면 안 됩니다). */
        bool hasAllCullViewBuffers() const;
        /** @brief 모든 컬링 뷰가 가시 목록 버퍼를 갖췄는지 확인합니다(간접 인자는 이 검사 뒤에 만들어집니다). */
        bool hasAllVisibleBuffers() const;
        /**
         * @brief 마지막 upload 가 실제로 개수를 컴퓨트에 맡겼는지 반환합니다.
         * @details "원한다" 와 "실제로 된다" 는 다릅니다. 가시 목록이나 배치 구간 버퍼를 못 만들었으면
         *          개수를 0 으로 올리지 않습니다. 컬링 디스패치와 가시 목록 바인딩은 **이 값**을 따라야
         *          합니다. 둘이 어긋나면 개수 0 짜리 인자로 그리거나(빈 화면), 갱신 안 된 목록을 읽습니다.
         */
        bool areIndirectCountsGpuFilled() const { return _bGpuFillsIndirectCounts != SW_FALSE; }
        /** @brief 메인 뷰의 간접 인자 버퍼를 반환합니다. 뷰를 가리지 않는 검사(upload 의 재업로드 판정 · 반환값)에 씁니다. */
        RHIBufferHandle getIndirectArgsBuffer() const { return _arrCullView[static_cast<uint32>( RenderViewType::Main )]._indirectArgs._buffer; }
        /** @brief 간접 커맨드 개수를 반환합니다. */
        uint32 getIndirectCommandCount() const { return _indirectCommandCount; }
        /** @brief 인스턴스 버퍼가 GPU 에 올라갔는지 반환합니다. */
        bool isUploaded() const { return _instances._buffer != 0; }

    private:
        /**
         * @brief 그룹마다 머티리얼 패킹 바이트를 원소 stride 로 이어 붙여 구조버퍼에 올리고 배치에 버퍼 · SRV 를 적습니다(RT).
         * @details 바이트가 지난 업로드와 같으면 건너뜁니다. 값이 바뀐 그룹만 올립니다(언리얼의 더티 업로드).
         */
        void uploadMaterialGroups( IRHIDevice* pDevice );

        /** @brief 패킷에서 받은 스냅샷입니다. 이 안의 것만 GT 가 만든 값입니다. */
        GpuSceneSnapshot _snapshot;
        /// @brief 셰이더 경로 → 머티리얼 데이터 GPU 버퍼입니다(RT 영속, 스냅샷 교체와 무관).
        unordered_map<string, GpuMaterialGpu> _mapMaterialGpu;
        vector<uint8>                         _listMaterialScratch;
        /// @brief 그룹 → (버퍼, SRV, 원소 수) 표입니다. 배치 루프가 조회 대신 읽습니다. 프레임마다 다시 채웁니다.
        struct ResolvedGroup
        {
            RHIBufferHandle    _buffer{ 0 };
            RHIDescriptorIndex _srv{ kInvalidDescriptorIndex };
            uint32             _elementCount{ 0 };
        };
        vector<ResolvedGroup>          _listResolvedGroupScratch;
        vector<RHIDrawIndirectCommand> _listScratchIndirectCmd;
        vector<GpuBatchInfo>           _listScratchBatchInfo;
        /// @brief 부분 인스턴스 업로드용 영역 목록입니다. 프레임마다 다시 채워 씁니다(할당을 되풀이하지 않습니다).
        vector<RHIBufferCopyRegion> _listScratchCopyRegion;

        RHIStructuredBufferSlot _instances;
        /**
         * @brief 뷰별 컬링 산출물입니다. 언리얼 FInstanceCullingContext 가 뷰마다 있는 것과 같은 자리입니다.
         * @details 컬링 컴퓨트가 살아남은 인스턴스의 **원본 인덱스**를 배치 구간에 압축해 넣고, 정점 셰이더는
         *          입력 어셈블러가 준 인스턴스 슬롯(간접 인자의 startInstance + 인스턴스 서수)으로
         *          `g_SwVisibleInstanceIds[슬롯]` 을 읽습니다(binding.hlsli SwResolveInstanceId). 이것이 없으면 컬링이
         *          개수만 줄일 수 있어 **뒤쪽 인스턴스가 통째로 사라집니다**(보이는 것을 고를 수가 없습니다).
         *          목록은 절두체에 종속이므로 메인 카메라와 그림자 라이트가 **각자** 갖습니다.
         */
        GpuCullViewResources    _arrCullView[static_cast<uint32>( RenderViewType::Count )];
        RHIStructuredBufferSlot _batchInfo;
        /**
         * @brief 씬 메시 정점을 모은 풀입니다(RT 소유). 배치는 이 버퍼와 자기 시작 오프셋으로 그립니다.
         * @details 메시 집합이 바뀔 때만 다시 만듭니다. 풀에 못 든 메시는 자기 정점 버퍼로 그립니다(멀티 드로우에는 못 묶입니다).
         */
        GpuMeshVertexPool _vertexPool;
        /// @brief 풀에 넣을 메시 목록 스크래치입니다. 프레임마다 할당하지 않습니다.
        vector<Mesh*> _listScratchPoolMesh;
        /// @brief setVertexPoolEnabled 의 값입니다. 0 이면 풀을 비우고 배치가 자기 정점 버퍼로 그립니다.
        uint8 _bVertexPoolEnabled{ SW_TRUE };
        /**
         * @brief 인스턴스 슬롯 스트림입니다. `0,1,2,…` 를 담은 정점 버퍼(슬롯 1, 인스턴스 스텝)이며 RT 가 소유합니다.
         * @details 간접 인자의 startInstance 가 배치 시작이므로 입력 어셈블러가 인스턴스마다 `startInstance + i` 를 줍니다. 그것이
         *          곧 가시 목록 슬롯입니다. 인스턴스 수만큼 커지면 다시 만듭니다(내용은 항등이라 그대로 늘리기만 합니다).
         */
        RHIBufferHandle _instanceSlotStream{ 0 };
        uint32          _instanceSlotStreamCapacity{ 0 };
        /**
         * @brief 배치 표 · 간접 인자를 다시 올려야 하는지입니다. 스냅샷은 그대로인데 풀 오프셋(정점 · 모프)이 바뀌었을 때 켜집니다.
         * @details 스냅샷 dirty 와 별개입니다. 모프 풀은 메시가 모프를 켜고 끌 때, 정점 풀은 메시 집합이 바뀔 때 다시 만들어지는데
         *          둘 다 씬(스냅샷)이 그대로인 채로 일어날 수 있습니다.
         */
        uint8 _bBatchTablesDirty{ SW_TRUE };
        /// @brief 부르는 쪽이 원한 값입니다(setIndirectCountsFilledByGpu).
        uint8 _bWantGpuIndirectCounts{ SW_FALSE };
        /// @brief 마지막 upload 가 실제로 그렇게 했는지입니다(버퍼가 다 있어야 1).
        uint8 _bGpuFillsIndirectCounts{ SW_FALSE };
        /// @brief 마지막 upload() 가 올린 간접 인자 개수입니다. 스냅샷에 없으므로 GT 가 덮어쓸 수 없습니다.
        uint32 _indirectCommandCount{ 0 };
    };
} // namespace sw

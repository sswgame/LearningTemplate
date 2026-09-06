/**
 * @file GpuScene.h
 * @brief GPU 컬·간접 드로우용 MeshComponent CPU 스냅샷.
 */
#pragma once
#include "Core/Container/unordered_set.h"
#include "Core/Memory/Memory.h"
#include "Core/Task/TaskTypes.h"

#include "Engine/EngineMinimal.h"
#include "Engine/Graphics/RHI/RHITypes.h"
#include "Engine/Graphics/Shader/ShaderBindingSlots.h"

namespace sw
{
    class IRHIDevice;
    class Material;
    class MaterialInstance;
    class Mesh;
    class MeshComponent;
    class Scene;
    class TaskManager;

    /// @brief GPU 인스턴스 (월드 행렬 + 메시/머티리얼 인덱스)
    struct GpuInstance
    {
        float4x4 _world{};
        float3   _boundsCenter{};
        float32  _boundsRadius{ 1.0f };
        uint32   _meshBatchIndex{ 0 };
        uint32   _materialIndex{ 0 };
        uint32   _blendMode{ 0 }; ///< RHIBlendMode
        uint32   _pad{ 0 };
    };

    /// @brief 같은 메시/머티리얼의 인스턴스 배치
    struct GpuMeshBatch
    {
        Mesh*              _pMesh{ nullptr };
        RHIBufferHandle    _vertexBuffer{ 0 };
        uint32             _vertexCount{ 0 };
        uint32             _instanceBase{ 0 };
        uint32             _instanceCount{ 0 };
        uint32             _materialIndex{ 0 };
        RHIBlendMode       _blendMode  = RHIBlendMode::Opaque;
        RHIDescriptorIndex _materialCb = kInvalidDescriptorIndex;
        /**
         * @brief 머티리얼 텍스처의 백엔드 SRV 인덱스(서수 순). 비네이티브 bindless 백엔드에서만 쓴다.
         * @details DX11/GL 은 셰이더가 전역 인덱스를 못 풀어서 엔진이 t5..t8 에 직접 바인딩해야 한다.
         *          렌더 스레드는 씬을 못 보므로(Material* 를 따라갈 수 없다) 값으로 실어 나른다.
         */
        RHIDescriptorIndex _arrMaterialTexSrv[shaderslot::kMaterialTextureCount] = {
            kInvalidDescriptorIndex, kInvalidDescriptorIndex, kInvalidDescriptorIndex, kInvalidDescriptorIndex };
        /** @brief RT가 draw 직전에 applyToGpu. 수명은 GpuScene pin/retire 큐로 관리. */
        MaterialInstance* _pMaterialInstance{ nullptr };
    };

    /**
     * @class GpuMaterialRetireQueue
     * @brief GT→RT 교차 MaterialInstance 수명 정책.
     * @details build 배치에 실린 인스턴스는 pin. 배치에서 빠지면 retire(프레임 지연).
     *          GT는 isPinned이면 파괴하지 말고, flushAfterGpu 이후에 파괴합니다.
     */
    class SW_API GpuMaterialRetireQueue
    {
    public:
        /** @brief RenderThread 패킷 링 깊이(constant::kRenderFrameQueueDepth)와 같아야 안전합니다 —
         *         그보다 짧으면 아직 큐잉된(미소비) 패킷이 참조 중인 MaterialInstance를 조기 파괴할 수 있습니다. */
        static constexpr uint32 kRetireFrameDelay = constant::kRenderFrameQueueDepth;

        /** @brief 현재 배치에 실린 인스턴스를 pin하고, 빠진 것은 retire 큐로 옮깁니다. */
        void syncFromBatches( const vector<GpuMeshBatch>& listOpaque, const vector<GpuMeshBatch>& listTransparent );
        /** @brief RT 프레임 종료 시 호출 — retire 카운트를 줄입니다 (waitIdle 없음). */
        void advanceFrame();
        /** @brief device.waitIdle() 후 pin/retire를 모두 비웁니다. */
        void flushAfterGpu( IRHIDevice* pDevice );
        /** @brief GPU 경로가 아직 참조 중이면 true. */
        bool isPinned( const MaterialInstance* pInstance ) const;
        /** @brief pin·retire를 즉시 비웁니다 (GPU sync 없음). */
        void clear();

    private:
        struct RetireEntry
        {
            MaterialInstance* _pInstance{ nullptr };
            uint32            _framesLeft{ 0 };
        };

        unordered_set<MaterialInstance*> _uniquePinned;
        vector<RetireEntry>              _listRetiring;
    };

    struct GpuSceneDrawCandidate
    {
        float4x4          _world{};
        float3            _boundsCenter{};
        float32           _boundsRadius{ 1.0f };
        Mesh*             _pMesh{ nullptr };
        Material*         _pMaterial{ nullptr };
        MaterialInstance* _pInstance{ nullptr };
        uint32            _blendMode{ 0 };
    };

    struct GpuSceneSortKey
    {
        Mesh*             _pMesh{ nullptr };
        Material*         _pMaterial{ nullptr };
        MaterialInstance* _pInstance{ nullptr };
        /** @brief 메시·머티리얼·인스턴스가 같은지 비교합니다. */
        bool operator==( const GpuSceneSortKey& o ) const { return _pMesh == o._pMesh && _pMaterial == o._pMaterial && _pInstance == o._pInstance; }
    };

    struct GpuSceneSortEntry
    {
        GpuSceneSortKey _key;
        uint32          _srcIdx{ 0 };
    };

    /**
     * @class GpuScene
     * @brief 게임 스레드에서 구축(선택적 TaskManager)하고 렌더 스레드에서 소비합니다.
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

        /** @brief CPU/GPU 스냅샷을 비웁니다. */
        void clear();
        /**
         * @brief MeshComponent를 수집합니다. 개수가 많으면 TaskManager 병렬 샤드를 씁니다.
         * @param pTaskManager 선택적 병렬 구축. null이면 단일 스레드 수집
         * @details 내용·카메라가 이전과 같으면 재구축을 건너뜁니다. 카메라만 바뀌면
         *          transparent 재정렬 + 배치 재구성만 합니다.
         */
        void buildFromScene( Scene* pScene, const float3& cameraPos,
                             TaskManager* pTaskManager = nullptr );
        /** @brief 인스턴스 SRV와 배치별 간접 인자를 업로드합니다 (RT/디바이스 스레드). */
        bool upload( IRHIDevice* pDevice );
        /** @brief GPU 버퍼를 해제합니다. */
        void releaseGpu( IRHIDevice* pDevice );

        /**
         * @brief CPU 스냅샷(인스턴스/배치 목록)만 outSnapshot으로 복사합니다. GPU 핸들은 건드리지 않습니다.
         * @details GT가 프레임마다 영속 GpuScene에서 RenderFramePacket으로 넘길 스냅샷을 뽑을 때 씁니다.
         *          호출 후 *this의 dirty 플래그는 소비된 것으로 보고 0으로 리셋합니다(다음 buildFromScene이
         *          다시 바뀌었다고 판단할 때까지 유지 — upload()의 재업로드 스킵과 대칭되는 GT 쪽 소비 시점).
         */
        void exportCpuSnapshot( GpuScene& outSnapshot );
        /**
         * @brief snapshot의 CPU 스냅샷만 *this로 옮깁니다. *this의 GPU 핸들/용량/MaterialRetireQueue는 보존합니다.
         * @details RT(FrameRenderer)가 영속 소유한 GpuScene에 매 프레임 패킷의 스냅샷을 반영할 때 씁니다.
         */
        void adoptCpuSnapshot( GpuScene&& snapshot );

        /** @brief 인스턴스 목록을 반환합니다. */
        const vector<GpuInstance>& getInstances() const { return _listInstance; }
        /** @brief 불투명 배치를 반환합니다. */
        const vector<GpuMeshBatch>& getOpaqueBatches() const { return _listOpaqueBatch; }
        /** @brief 투명 배치를 반환합니다. */
        const vector<GpuMeshBatch>& getTransparentBatches() const { return _listTransparentBatch; }
        /** @brief 인스턴스 버퍼 핸들을 반환합니다. */
        RHIBufferHandle getInstanceBuffer() const { return _instanceBuffer; }
        /** @brief 인스턴스 SRV 인덱스를 반환합니다. */
        RHIDescriptorIndex getInstanceSrv() const { return _instanceSrv; }
        /** @brief 간접 인자 버퍼 핸들을 반환합니다. */
        RHIBufferHandle getIndirectArgsBuffer() const { return _indirectArgsBuffer; }
        /** @brief 간접 인자 UAV 인덱스를 반환합니다. */
        RHIDescriptorIndex getIndirectArgsUav() const { return _indirectArgsUav; }
        /** @brief 간접 커맨드 개수를 반환합니다. */
        uint32 getIndirectCommandCount() const { return _indirectCommandCount; }
        /** @brief GPU에 올라갔는지 반환합니다. */
        bool isUploaded() const { return _instanceBuffer != 0; }
        /** @brief 마지막 buildFromScene이 CPU 스냅샷을 바꿨으면 true. */
        bool isCpuSnapshotDirty() const { return _bCpuDirty != 0; }

        /** @brief 배치 MaterialInstance pin/retire 정책. */
        GpuMaterialRetireQueue& getMaterialRetireQueue() { return _materialRetire; }
        /** @brief 배치 MaterialInstance pin/retire 정책. */
        const GpuMaterialRetireQueue& getMaterialRetireQueue() const { return _materialRetire; }
        /** @brief 현재 배치로 pin을 맞추고, RT 프레임 끝에서 advanceFrame을 호출하세요. */
        void syncMaterialPins();
        /** @brief RT 프레임 종료 — retire 지연 카운트. */
        void advanceMaterialRetireFrame() { _materialRetire.advanceFrame(); }
        /** @brief waitIdle 후 pin/retire 전부 해제 (핫스왑·셧다운). */
        void flushMaterialRetire( IRHIDevice* pDevice ) { _materialRetire.flushAfterGpu( pDevice ); }

    private:
        /** @brief 후보를 GpuInstance scratch로 채웁니다. ParallelBlockDelegate 시그니처입니다. */
        void fillScratchRange( uint32 start, uint32 end );
        /** @brief 수집된 인스턴스를 배치로 묶습니다. */
        void buildBatches();
        /** @brief 투명 인덱스를 카메라 거리순(먼→가까운)으로 정렬합니다. */
        void sortTransparent( const float32* pCameraPos );
        /** @brief opaque/transparent 인덱스 테이블을 후보에서 다시 만듭니다. */
        void rebuildPartitionTables();
        /** @brief 직전 후보와 배치 키가 모두 같은지(= 트랜스폼만 달라졌는지) 확인합니다. */
        bool hasSameBatchKeysAsBuilt() const;
        /** @brief 캐시 무효화. */
        void invalidateBuildCache();

        vector<GpuInstance>  _listInstance;
        vector<GpuMeshBatch> _listOpaqueBatch;
        vector<GpuMeshBatch> _listTransparentBatch;
        vector<GpuMeshBatch> _listAllBatch; ///< 불투명 다음 투명. 간접 슬롯과 일치

        /// buildFromScene에서 재사용해 프레임당 힙 할당을 줄입니다.
        struct DrawCandidate
        {
            float4x4          _world{};
            float3            _boundsCenter{};
            float32           _boundsRadius{ 1.0f };
            Mesh*             _pMesh{ nullptr };
            Material*         _pMaterial{ nullptr };
            MaterialInstance* _pInstance{ nullptr };
            uint32            _blendMode{ 0 };

            /**
             * @brief 재구축이 필요한지 판단하기 위한 필드 단위 비교입니다.
             * @details 예전엔 후보마다 100여 바이트를 FNV 로 섞어 64비트 지문을 만들어 비교했다.
             *          그건 (1) 바이트마다 곱셈이 들어가 이 비교 자체가 수집 비용에 맞먹었고,
             *          (2) 해시가 충돌하면 바뀐 씬을 "그대로"로 보고 화면이 멈추는, 재현이 사실상
             *          불가능한 버그를 남겼다. 어차피 같은 바이트를 다 읽어야 한다면 지문을 만들지 말고
             *          **그냥 비교**하는 게 더 싸고 정확하다 — 다르면 즉시 빠져나올 수도 있다.
             * @note 부동소수는 float3/float4x4 의 `operator==` (nearEqual, 엡실론 비교)가 아니라
             *       **비트 그대로** 비교한다. 엡실론 비교는 매 프레임 엡실론 미만으로 움직이는 물체를
             *       영원히 "안 바뀜"으로 보고 화면에 오차를 누적시킨다. 반대로 비트 비교가 틀리는
             *       방향(-0.0 과 0.0 을 다르게 봄)은 불필요한 재구축일 뿐이라 안전하다.
             *       비교 대상 블록은 모두 float 연속이라 패딩이 끼지 않는다.
             */
            bool operator==( const DrawCandidate& o ) const
            {
                return _pMesh == o._pMesh && _pMaterial == o._pMaterial && _pInstance == o._pInstance &&
                       _blendMode == o._blendMode &&
                       Memory::compare( &_world, &o._world, sizeof( _world ) ) == 0 &&
                       Memory::compare( &_boundsCenter, &o._boundsCenter, sizeof( _boundsCenter ) ) == 0 &&
                       Memory::compare( &_boundsRadius, &o._boundsRadius, sizeof( _boundsRadius ) ) == 0;
            }
            /** @brief operator== 의 부정입니다. */
            bool operator!=( const DrawCandidate& o ) const { return ( *this == o ) == false; }

            /**
             * @brief 배치가 묶이는 기준(메시·머티리얼·인스턴스·블렌드)이 같은지. 트랜스폼은 보지 않습니다.
             * @details 불투명 배치는 이 네 가지로만 나뉘고 정렬된다. 물체가 **움직이기만** 했다면
             *          배치 구성은 한 글자도 바뀌지 않으므로 다시 나누고 다시 정렬할 이유가 없다.
             *          움직이는 씬에서 남는 유일한 O(N log N) 이 그 정렬이다.
             */
            bool hasSameBatchKey( const DrawCandidate& o ) const
            {
                return _pMesh == o._pMesh && _pMaterial == o._pMaterial && _pInstance == o._pInstance &&
                       _blendMode == o._blendMode;
            }
        };

        vector<DrawCandidate> _listScratchCandidate;
        /** @brief 마지막으로 반영된 후보 집합. 다음 프레임의 변경 판단 기준이자 scratch 버퍼의 재활용처입니다. */
        vector<DrawCandidate> _listBuiltCandidate;
        vector<GpuInstance>   _listScratchRaw;
        /** @brief 병렬 채우기 구간에서 쓰는 버퍼 주소. 디스패치 직전에 한 번만 확정합니다. */
        const DrawCandidate* _pScratchCandidateBase{ nullptr };
        GpuInstance*         _pScratchRawBase{ nullptr };

        struct SortKey
        {
            Mesh*             _pMesh{ nullptr };
            Material*         _pMaterial{ nullptr };
            MaterialInstance* _pInstance{ nullptr };
            /** @brief 메시·머티리얼·인스턴스가 같은지 비교합니다. */
            bool operator==( const SortKey& o ) const { return _pMesh == o._pMesh && _pMaterial == o._pMaterial && _pInstance == o._pInstance; }
        };

        struct SortEntry
        {
            SortKey _key;
            uint32  _srcIdx{ 0 };
        };

        vector<SortEntry>              _listScratchOpaqueEntry;
        vector<uint32>                 _listScratchTransparentIdx;
        vector<RHIDrawIndirectCommand> _listScratchIndirectCmd;

        GpuMaterialRetireQueue _materialRetire;
        TaskStageHandle        _snapshotStage;
        RHIBufferHandle        _instanceBuffer{ 0 };
        RHIBufferHandle        _indirectArgsBuffer{ 0 };
        float3                 _lastCameraPos{};
        /** @brief 마지막으로 반영한 프리미티브 집합 세대. 달라졌으면 등록부가 바뀐 것. */
        uint64             _lastPrimitiveSetGeneration{ 0 };
        RHIDescriptorIndex _instanceSrv     = kInvalidDescriptorIndex;
        RHIDescriptorIndex _indirectArgsUav = kInvalidDescriptorIndex;
        uint32             _indirectCommandCount{ 0 };
        uint32             _instanceCapacity{ 0 };
        uint32             _argsCapacity{ 0 };
        uint8              _bCpuDirty{ 1 };
    };
} // namespace sw

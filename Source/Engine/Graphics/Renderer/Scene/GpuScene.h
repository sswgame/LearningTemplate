/**
 * @file GpuScene.h
 * @brief GPU 컬·간접 드로우용 MeshComponent CPU 스냅샷.
 */
#pragma once
#include "Core/Container/pair.h"
#include "Core/Container/unordered_map.h"
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
        /**
         * @brief GPU 인스턴스 애니메이션 시드 — 0 이면 애니메이션 없음.
         * @details instanceanim.hlsl 이 이 값을 해시해 **인스턴스마다 다른 각속도**를 만든다. 예전엔 여기가
         *          정렬용 `_pad` 였다 — 자리를 새로 만들지 않고 그 빈칸을 쓴다(셰이더 구조체 레이아웃 불변).
         *          CPU 가 매 프레임 회전을 계산해 올리던 것을 GPU 로 옮기는 통로다.
         */
        uint32 _spinSeed{ 0 };
    };

    /**
     * @brief 컬링을 따로 도는 뷰. 언리얼이 뷰마다 `FInstanceCullingContext` 를 두는 자리와 같다.
     * @details 컬링 결과(간접 인자 개수 + 가시 인스턴스 목록)는 **절두체에 종속**이다. 메인 카메라로 거른
     *          목록을 그림자 패스가 쓰면, 화면 밖에 있지만 화면 안으로 그림자를 드리우는 물체가 사라진다.
     *          그래서 뷰마다 자기 인자·목록을 갖는다.
     */
    enum class GpuCullView : uint32
    {
        Main   = 0, ///< 게임 카메라
        Shadow = 1, ///< 그림자 라이트
        Count  = 2
    };

    /** @brief 뷰 하나가 갖는 컬링 산출물 (간접 인자 + 가시 인스턴스 목록). */
    struct GpuCullViewResources
    {
        RHIBufferHandle    _indirectArgsBuffer{ 0 };
        RHIDescriptorIndex _indirectArgsUav = kInvalidDescriptorIndex;
        RHIBufferHandle    _visibleInstanceBuffer{ 0 };
        RHIDescriptorIndex _visibleInstanceSrv = kInvalidDescriptorIndex;
        RHIDescriptorIndex _visibleInstanceUav = kInvalidDescriptorIndex;
        uint32             _argsCapacity{ 0 };
        uint32             _visibleCapacity{ 0 };
    };

    /**
     * @brief 배치의 인스턴스 구간 — 컬링 컴퓨트에게 "이 배치는 어디서 시작하나"를 알려준다.
     * @details 간접 인자의 `startInstance` 는 0 이어야 해서(Vulkan 의 InstanceIndex 가 firstInstance 를
     *          포함하므로 셰이더가 루트 상수로 더한다) 컬링이 그 값을 시작점으로 쓸 수 없다. gpucull.hlsl 의
     *          GpuBatchInfo 와 레이아웃이 같아야 한다.
     */
    struct GpuBatchInfo
    {
        uint32 _instanceBase{ 0 };
        uint32 _instanceCount{ 0 };
        /**
         * @brief 이 배치는 인스턴스 **순서를 지켜야 하는가** (투명 배치는 1).
         * @details 불투명은 살아남은 것을 앞에서부터 채워도 되지만(InterlockedAdd 로 자리를 받는다),
         *          투명은 CPU 가 뒤에서 앞으로 정렬해 둔 순서가 곧 블렌딩 순서다. 원자 연산이 주는 자리
         *          번호는 완료 순서라 그 정렬을 부순다. 그래서 순서를 지켜야 하는 배치는 압축하지 않고
         *          제자리 매핑(g_VisibleInstanceIds[instId] = instId)을 쓰고 개수는 CPU 가 채운다.
         */
        uint32 _bPreserveOrder{ 0 };
        uint32 _pad{ 0 };
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
        /** @brief 배치가 쓰는 부모 머티리얼 — 셰이더 타입(머티리얼 데이터 그룹)과 텍스처 슬롯의 소유자. */
        Material* _pMaterial{ nullptr };
        /** @brief 머티리얼 데이터 그룹(셰이더 타입) 인덱스 — _listMaterialGroup. 없으면 kInvalidMaterialGroup. */
        uint32 _materialGroup{ 0xFFFFFFFFu };
        /**
         * @brief 그룹의 GPU 구조버퍼(g_SwMaterials, t9)와 SRV 인덱스 — upload 가 채운다.
         * @details 인스턴스의 _materialIndex 가 이 버퍼의 원소를 고른다 (언리얼 GPUScene 방식). 드로우마다 CB 를 갈아
         *          끼우지 않고, 배치마다 이 버퍼를 리플렉션 슬롯에 한 번 건다.
         */
        RHIBufferHandle    _materialBuffer{ 0 };
        RHIDescriptorIndex _materialSrv = kInvalidDescriptorIndex;
        /** @brief 그룹 버퍼의 원소 수 — PassCB g_SwMaterialCount 로 넘겨 셰이더가 인덱스를 클램프한다. */
        uint32 _materialCount{ 0 };
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
     * @struct GpuMaterialGroup
     * @brief 셰이더 타입(머티리얼 셰이더 경로)별 머티리얼 데이터 원소 목록 — CPU 스냅샷의 일부.
     * @details 원소 순서가 곧 materialIndex 다. 같은 셰이더를 쓰는 머티리얼/인스턴스는 구조체 레이아웃이 같아 한 버퍼에 쌓인다.
     */
    /**
     * @struct GpuMaterialElementKey
     * @brief 머티리얼 데이터 원소 하나를 가리키는 키 — (머티리얼, 인스턴스) 쌍.
     * @details 인스턴스가 없으면 머티리얼 자신이 원소다. 인스턴스는 CB 값만 덮어쓰므로 부모 머티리얼과 함께 봐야 한다.
     */
    struct GpuMaterialElementKey
    {
        Material*         _pMaterial{ nullptr };
        MaterialInstance* _pInstance{ nullptr };
        /** @brief 같으면 true를 반환합니다. */
        bool operator==( const GpuMaterialElementKey& other ) const
        {
            return _pMaterial == other._pMaterial && _pInstance == other._pInstance;
        }
    };

    /// @brief GpuMaterialElementKey 해시 — 포인터 둘을 섞는다.
    struct GpuMaterialElementKeyHash
    {
        /** @brief 호출 연산자입니다. */
        size_t operator()( const GpuMaterialElementKey& key ) const
        {
            size_t h = reinterpret_cast<size_t>( key._pMaterial ) * 1315423911u;
            h ^= reinterpret_cast<size_t>( key._pInstance ) + 0x9e3779b9u + ( h << 6 ) + ( h >> 2 );
            return h;
        }
    };

    struct GpuMaterialGroup
    {
        string                                     _shaderPath;
        vector<pair<Material*, MaterialInstance*>> _listEntry;
        /**
         * @brief 원소 키 → `_listEntry` 인덱스.
         * @details 예전엔 인스턴스마다 `_listEntry` 를 처음부터 훑어 같은 쌍을 찾았다 — 인스턴스 N 개와 머티리얼 M 종에
         *          O(N·M) 이라 머티리얼이 늘수록 빌드가 제곱으로 느려졌다(벤치는 머티리얼이 하나라 안 보였다).
         *          언리얼은 등록 시점에 영속 ID 를 주고 더티만 갱신한다 — 여기서는 최소한 조회를 상수 시간으로 만든다.
         */
        unordered_map<GpuMaterialElementKey, uint32, GpuMaterialElementKeyHash> _mapEntryToIndex;
        /// @brief 원소별 마지막으로 쓰인 빌드 번호 — 오래 안 쓰인 원소를 회수하는 기준.
        vector<uint64> _listEntryLastSeenBuild;
        /// @brief 회수된 원소 자리. **인덱스를 옮기지 않고** 재사용한다 — 옮기면 영속 ID 가 아니게 된다.
        vector<uint32> _listFreeEntry;
        /**
         * @brief 직전 조회 결과 — 배치 안의 인스턴스는 정렬돼 있어 대부분 같은 원소를 연속으로 묻는다.
         * @details 맵만 두면 머티리얼이 하나뿐인 흔한 경우가 오히려 느려진다(포인터 비교 한 번 → 해시+탐색).
         *          실측으로 확인했다: 큐브 2000 개·머티리얼 1 종에서 배치 구성이 665us → 901us 로 늘었다.
         */
        GpuMaterialElementKey _lastKey{};
        uint32                _lastIndex{ 0 };
        uint8                 _bHasLast{ 0 };
    };

    /// @brief 그룹의 GPU 버퍼 — RT 소유, 셰이더 경로로 스냅샷을 넘어 재사용한다.
    struct GpuMaterialGpu
    {
        RHIBufferHandle    _buffer{ 0 };
        RHIDescriptorIndex _srv{ kInvalidDescriptorIndex };
        uint32             _capacityBytes{ 0 };
        uint32             _stride{ 0 };
        /** @brief 마지막으로 올린 바이트 — 같으면 업로드를 건너뛴다 (언리얼처럼 더티만 올린다). */
        vector<uint8> _lastBytes;
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

        /**
         * @brief 현재 배치와 머티리얼 그룹에 실린 인스턴스를 pin하고, 빠진 것은 retire 큐로 옮깁니다.
         * @details 배치를 셰이더 타입으로 합치면 배치의 _pMaterialInstance 는 대표 하나뿐이라, 그룹 원소(머티리얼·인스턴스 쌍)도 본다.
         */
        void syncFromBatches( const vector<GpuMeshBatch>& listOpaque, const vector<GpuMeshBatch>& listTransparent, const vector<GpuMaterialGroup>& listGroup );
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
        /**
         * @brief GPU 회전을 요청한(시드가 0 이 아닌) 인스턴스 수.
         * @details 0 이면 애니메이션 디스패치를 통째로 건너뛴다. 안 그러면 회전을 쓰지 않는 씬도 매 프레임
         *          인스턴스당 96 바이트를 읽고 아무 일도 하지 않는다.
         */
        uint32 getSpinInstanceCount() const { return _spinInstanceCount; }
        /** @brief 불투명 배치를 반환합니다. */
        const vector<GpuMeshBatch>& getOpaqueBatches() const { return _listOpaqueBatch; }
        /** @brief 투명 배치를 반환합니다. */
        const vector<GpuMeshBatch>& getTransparentBatches() const { return _listTransparentBatch; }
        /** @brief 인스턴스 버퍼 핸들을 반환합니다. */
        RHIBufferHandle getInstanceBuffer() const { return _instanceBuffer; }
        /** @brief 인스턴스 SRV 인덱스를 반환합니다. */
        RHIDescriptorIndex getInstanceSrv() const { return _instanceSrv; }
        /**
         * @brief 인스턴스 버퍼의 UAV 인덱스 — 컴퓨트가 월드 행렬을 **고쳐 쓰는** 통로.
         * @details instanceanim.hlsl 이 인스턴스마다 다른 각속도로 회전을 얹는다. 백엔드가 구조버퍼 UAV 를
         *          못 만들면 kInvalidDescriptorIndex 라 애니메이션 패스가 통째로 생략된다(그리기는 그대로).
         */
        RHIDescriptorIndex getInstanceUav() const { return _instanceUav; }
        /** @brief 뷰 하나의 컬링 산출물 (간접 인자 + 가시 목록). */
        const GpuCullViewResources& getCullView( GpuCullView view ) const { return _arrCullView[static_cast<uint32>( view )]; }
        /** @brief 배치 구간 버퍼의 SRV (컬링 컴퓨트 t1). */
        RHIDescriptorIndex getBatchInfoSrv() const { return _batchInfoSrv; }
        /**
         * @brief 간접 인자의 인스턴스 개수를 CPU 가 채울지, 컴퓨트가 만들지 정합니다.
         * @details true 면 업로드 시점에 개수를 **0 으로** 올린다 — 컬링 컴퓨트가 InterlockedAdd 로 채우기
         *          때문이다. 컬링이 없으면 CPU 가 채운 개수 그대로 그려야 하므로 false 여야 한다.
         *          FrameRenderer 가 매 프레임 실제 컬링 가능 여부를 보고 정한다.
         */
        void setIndirectCountsFilledByGpu( bool bByGpu );
        /**
         * @brief 간접 인자의 인스턴스 개수만 다시 올립니다 (배치 구성이 그대로일 때).
         * @details 컬링 컴퓨트는 개수를 0 에서부터 센다. 그래서 씬이 하나도 안 바뀐 프레임에도 개수는
         *          되돌려 놓아야 한다. 배치당 16 바이트뿐이라 전량 업로드해도 싸다.
         */
        void refreshIndirectCounts( IRHIDevice* pDevice );
        /** @brief 모든 컬링 뷰가 간접 인자와 가시 목록을 다 갖췄는가 (하나라도 없으면 GPU 개수를 쓰면 안 된다). */
        bool hasAllCullViewBuffers() const;
        /**
         * @brief 마지막 upload 가 실제로 개수를 컴퓨트에 맡겼는가.
         * @details "원한다"와 "실제로 된다"는 다르다 — 가시 목록이나 배치 구간 버퍼를 못 만들었으면
         *          개수를 0 으로 올리지 않는다. 컬링 디스패치와 가시 목록 바인딩은 **이 값**을 따라야
         *          한다. 둘이 어긋나면 개수 0 짜리 인자로 그리거나(빈 화면), 갱신 안 된 목록을 읽는다.
         */
        bool areIndirectCountsGpuFilled() const { return _bGpuFillsIndirectCounts != 0; }
        /** @brief 메인 뷰의 간접 인자 버퍼 (isUploaded 등 뷰를 가리지 않는 검사용). */
        RHIBufferHandle getIndirectArgsBuffer() const { return _arrCullView[static_cast<uint32>( GpuCullView::Main )]._indirectArgsBuffer; }
        /** @brief 간접 커맨드 개수를 반환합니다. */
        uint32 getIndirectCommandCount() const { return _indirectCommandCount; }
        /** @brief GPU에 올라갔는지 반환합니다. */
        bool isUploaded() const { return _instanceBuffer != 0; }
        /** @brief 마지막 buildFromScene이 CPU 스냅샷을 바꿨으면 true. */
        bool isCpuSnapshotDirty() const { return _bCpuDirty != 0; }
        /** @brief 셰이더 타입별 머티리얼 데이터 그룹 (CPU 스냅샷). */
        const vector<GpuMaterialGroup>& getMaterialGroups() const { return _listMaterialGroup; }
        /**
         * @brief 불투명 배치를 머티리얼이 아니라 **셰이더 타입**(머티리얼 셰이더 경로)으로 묶을지 정합니다 (언리얼 GPUScene).
         * @details 머티리얼 파라미터는 인스턴스의 materialIndex 로 버퍼에서 읽으므로, 텍스처를 인덱스로 고를 수 있는 백엔드
         *          (DX12/Vulkan 텍스처 배열)에서는 같은 메시·같은 셰이더면 머티리얼이 달라도 한 드로우다. DX11/GL 은 머티리얼
         *          텍스처를 t5..t8 슬롯에 걸어야 해서 배치가 머티리얼 단위로 남는다. FrameRenderer/EngineLoop 가 디바이스
         *          caps(supportsNativeBindlessSampling)로 정한다. 바꾸면 다음 buildFromScene 이 다시 묶는다.
         */
        void setMergeBatchesAcrossMaterials( bool bMerge );
        /** @brief setMergeBatchesAcrossMaterials 로 정한 값. */
        bool isMergingBatchesAcrossMaterials() const { return _bMergeAcrossMaterials != 0; }

        static constexpr uint32 kInvalidMaterialGroup = 0xFFFFFFFFu;

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
        /** @brief 머티리얼의 셰이더 타입 그룹 인덱스를 찾거나 만듭니다 (GT, buildBatches 안). 머티리얼이 없으면 kInvalidMaterialGroup. */
        /** @brief 오래 안 쓰인 머티리얼 원소를 회수해 자리를 프리리스트로 돌립니다 (인덱스는 옮기지 않는다). */
        void retireUnusedMaterialElements();
        /** @brief 머티리얼 원소 레지스트리를 통째로 비웁니다 (그룹 기준이 바뀌었을 때). */
        void   resetMaterialRegistry();
        uint32 materialGroupFor( const Material* pMaterial );
        /**
         * @brief (머티리얼, 인스턴스) 쌍을 그룹에 넣고 원소 인덱스(materialIndex)를 돌려줍니다 (GT, buildBatches 안).
         * @details 같은 쌍은 같은 원소를 공유한다. 인스턴스마다 부른다 — 배치를 셰이더 타입으로 합치면 한 배치 안에 여러 원소가 산다.
         */
        uint32 assignMaterialElement( Material* pMaterial, MaterialInstance* pInstance, uint32 groupIndex );
        /**
         * @brief 배치 키에 쓸 머티리얼 — 합치기가 켜져 있으면 같은 셰이더 경로의 대표 머티리얼, 아니면 그 머티리얼 자신.
         * @details 대표는 재구축마다 처음 만난 머티리얼이다(_mapShaderRepresentative). 재구축 여부 판단은 후보 자체를 비교하므로 대표가 바뀌어도 무관하다.
         */
        Material* batchKeyMaterial( Material* pMaterial );
        /**
         * @brief 그룹마다 머티리얼 패킹 바이트를 원소 stride 로 이어 붙여 구조버퍼에 올리고 배치에 버퍼/SRV 를 적습니다 (RT).
         * @details 바이트가 지난 업로드와 같으면 건너뛴다 — 값이 바뀐 그룹만 올린다(언리얼의 더티 업로드).
         */
        void uploadMaterialGroups( IRHIDevice* pDevice );
        /** @brief 투명 인덱스를 카메라 거리순(먼→가까운)으로 정렬합니다. */
        void sortTransparent( const float32* pCameraPos );
        /** @brief opaque/transparent 인덱스 테이블을 후보에서 다시 만듭니다. */
        void rebuildPartitionTables();
        /** @brief 직전 후보와 배치 키가 모두 같은지(= 트랜스폼만 달라졌는지) 확인합니다. */
        bool hasSameBatchKeysAsBuilt() const;
        /** @brief 캐시 무효화. */
        void invalidateBuildCache();

        vector<GpuInstance>      _listInstance;
        vector<GpuMeshBatch>     _listOpaqueBatch;
        vector<GpuMeshBatch>     _listTransparentBatch;
        vector<GpuMeshBatch>     _listAllBatch;      ///< 불투명 다음 투명. 간접 슬롯과 일치
        vector<GpuMaterialGroup> _listMaterialGroup; ///< 셰이더 타입별 머티리얼 원소 (CPU 스냅샷)
        /// @brief 셰이더 경로 → `_listMaterialGroup` 인덱스. 예전엔 배치마다 그룹 목록을 string 비교로 훑었다.
        unordered_map<string, uint32> _mapShaderPathToGroup;
        /// @brief 빌드 번호. 원소가 마지막으로 쓰인 시점을 재는 데만 쓴다(회수 판정).
        uint64 _buildCounter{ 0 };
        /// @brief 셰이더 경로 → 머티리얼 데이터 GPU 버퍼 (RT 영속, 스냅샷 교체와 무관)
        unordered_map<string, GpuMaterialGpu> _mapMaterialGpu;
        /// @brief 재구축 중 셰이더 경로 → 대표 머티리얼 (배치 키 합치기용, GT).
        unordered_map<string, Material*> _mapShaderRepresentative;
        vector<uint8>                    _listMaterialScratch;

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
            /// @brief GPU 회전 애니메이션 시드 (0 = 없음). MeshComponent 가 준다 → GpuInstance::_spinSeed.
            uint32 _spinSeed{ 0 };

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
                       _blendMode == o._blendMode && _spinSeed == o._spinSeed &&
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
        vector<GpuBatchInfo>           _listScratchBatchInfo;

        GpuMaterialRetireQueue _materialRetire;
        TaskStageHandle        _snapshotStage;
        RHIBufferHandle        _instanceBuffer{ 0 };
        float3                 _lastCameraPos{};
        /** @brief 마지막으로 반영한 프리미티브 집합 세대. 달라졌으면 등록부가 바뀐 것. */
        uint64             _lastPrimitiveSetGeneration{ 0 };
        RHIDescriptorIndex _instanceSrv = kInvalidDescriptorIndex;
        RHIDescriptorIndex _instanceUav = kInvalidDescriptorIndex;
        /**
         * @brief 뷰별 컬링 산출물 — 언리얼 FInstanceCullingContext 가 뷰마다 있는 것과 같은 자리.
         * @details 컬링 컴퓨트가 살아남은 인스턴스의 **원본 인덱스**를 배치 구간에 압축해 넣고, 정점 셰이더는
         *          `g_SwVisibleInstanceIds[g_InstanceBase + SV_InstanceID]` 로 읽는다. 이게 없으면 컬링이
         *          개수만 줄일 수 있어 **뒤쪽 인스턴스가 통째로 사라진다**(보이는 것을 고를 수가 없다).
         *          목록은 절두체에 종속이므로 메인 카메라와 그림자 라이트가 **각자** 갖는다.
         */
        GpuCullViewResources _arrCullView[static_cast<uint32>( GpuCullView::Count )];
        RHIBufferHandle      _batchInfoBuffer{ 0 };
        RHIDescriptorIndex   _batchInfoSrv = kInvalidDescriptorIndex;
        uint32               _batchInfoCapacity{ 0 };
        /// @brief 호출자가 원한 값 (setIndirectCountsFilledByGpu).
        uint8 _bWantGpuIndirectCounts{ 0 };
        /// @brief 마지막 upload 가 실제로 그렇게 했는가 (버퍼가 다 있어야 1).
        uint8 _bGpuFillsIndirectCounts{ 0 };
        /// @brief GPU 회전을 요청한 인스턴스 수 (0 이면 애니메이션 디스패치를 건너뛴다).
        uint32 _spinInstanceCount{ 0 };
        uint32 _indirectCommandCount{ 0 };
        uint32 _instanceCapacity{ 0 };
        uint8  _bCpuDirty{ 1 };
        uint8  _bMergeAcrossMaterials{ 0 };
    };
} // namespace sw

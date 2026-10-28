/**
 * @file GPUSceneBuilder.h
 * @brief 게임 스레드가 씬의 프리미티브(MeshComponent, 인스턴스 배치 항목)를 모아 `GPUSceneSnapshot` 을 만듭니다.
 * @details 렌더 스레드 쪽(`GPUScene`)과는 스냅샷 타입으로만 만난다. 여기에는 GPU 핸들이 하나도 없고, 저쪽에는 씬이 없다.
 *          프레임 간에 유지되는 것은 재구축 여부를 판단하는 캐시(후보 집합, 카메라, 세대)와 **머티리얼 원소 레지스트리**
 *          (언리얼 GPUScene 의 영속 ID 에 해당, `GPUMaterialRegistry`)다. 스냅샷은 매 프레임 복사해 내보내고, 기준 데이터는 여기에 남는다.
 *          투명 정렬 규칙(`GPUTransparentSorter`), 머티리얼 레지스트리, 스킨 팔레트 옮기기(`GPUSkinPaletteCollector`)는 값 멤버 클래스로 두고 직접 부른다.
 */
#pragma once
#include "Core/Common/Defines.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

#include "Engine/EngineMinimal.h"
#include "Engine/Renderer/Scene/GPUInstanceRing.h"
#include "Engine/Renderer/Scene/GPUMaterialRegistry.h"
#include "Engine/Renderer/Scene/GPUSceneSnapshot.h"
#include "Engine/Renderer/Scene/GPUSkinPaletteCollector.h"
#include "Engine/Renderer/Scene/GPUTransparentSorter.h"

namespace sw
{
    struct PrimitiveInstanceEntry;
    struct RenderViewRequest;

    class GameObjectManager;
    class GPUUploadQueue;
    class Material;
    class MaterialInstance;
    class Mesh;
    class PrimitiveRegistry;
    class Scene;
    class ScenePrimitive;
    class VfxComponent;

    /**
     * @class GPUSceneBuilder
     * @brief 씬에서 스냅샷을 만드는 클래스입니다. 게임 스레드 전용이며 GPU 를 알지 못합니다.
     */
    class SW_API GPUSceneBuilder
    {
    public:
        GPUSceneBuilder() noexcept = default;
        ~GPUSceneBuilder()         = default;

        GPUSceneBuilder( GPUSceneBuilder&& other ) noexcept            = default;
        GPUSceneBuilder& operator=( GPUSceneBuilder&& other ) noexcept = default;

        GPUSceneBuilder( const GPUSceneBuilder& )            = delete;
        GPUSceneBuilder& operator=( const GPUSceneBuilder& ) = delete;

        /**
         * @brief 프리미티브가 이 수 이상이면 전체 수집의 채우기를 잡으로 나눕니다.
         * @details 채우기는 프리미티브당 약 12ns 라 8000개가 직렬로 100µs 다. 잡 디스패치 바닥(약 50µs)과 같은
         *          자릿수라 지금은 병렬이 86µs 로 비기는 수준이다. 프리미티브당 일이 늘면(LOD 선택, 스키닝 바운드)
         *          바로 이득이 나는 구조라 임계값만 두고 유지한다. 부분 수집(더티가 1/4 미만)은 일이 작으므로 직렬로 돈다.
         */
        static constexpr uint32 kParallelCollectPrimitiveCount = 4096;
        /**
         * @brief 월드 행렬만 바뀐 프리미티브가 이 수 이상이면 행렬 복사(`copyCandidateTransforms`)를 워커에 나눕니다.
         * @details 한 건이 행렬 하나 복사라 싸지만, 그 행렬은 방금 틱이 끝난 뒤 다른 코어들이 쓴 것이다. 한 스레드가 8000줄을 원격 캐시에서
         *          가져오면 그 지연이 쌓인다.
         */
        static constexpr uint32 kParallelTransformCopyCount = 2048;

        /** @brief 스냅샷, 캐시, 머티리얼 레지스트리를 비웁니다. 스냅샷이 가진 소유권도 여기서 해제됩니다. */
        void clear();
        /**
         * @brief 프리미티브(MeshComponent, 인스턴스 배치 항목)를 모아 CPU 스냅샷을 만듭니다(게임 스레드).
         * @details 내용과 카메라가 이전과 같으면 재구축을 건너뛴다. 카메라만 바뀌면
         *          투명 재정렬과 배치 재구성만 한다. 입자 그리기(`populateParticleBatches`)는 건너뛰기와 무관하게 매번 다시 모은다.
         * @note 인스턴스 채우기(`GT.GPUScene.build.fill`)는 이 스레드에서 한다. 워커로 나눠 봤지만 **모든 크기에서 졌다**.
         *       원소당 일이 필드 몇 개 복사라서 디스패치와 대기 비용이 일 자체보다 훨씬 크다
         *       (400개 358µs → 1µs, 20,000개 219µs → 99µs). 다시 나누자고 제안하기 전에 이 측정값을 확인한다.
         *       수집과 전체 제자리 갱신은 임계값을 넘으면 병렬로 돈다(`kParallelCollectPrimitiveCount`, `refreshInstancesInPlace`).
         */
        void populateFromScene( Scene* pScene, const float3& cameraPos );
        /**
         * @brief 투명 정렬의 깊이를 측정할 축입니다. 영벡터(기본)면 카메라까지의 거리를 쓰고, 아니면 카메라에서 그 축 방향으로 측정한 깊이를 씁니다.
         * @details 직교 2D 카메라에서 거리로 측정하면 같은 Z 의 두 스프라이트가 카메라의 XY 위치에 따라 앞뒤가 바뀐다. `Engine` 가
         *          `Render2DSettings::computeTransparentSortAxis` 로 값을 정해 넘긴다(유니티 Transparency Sort Mode). 값이 바뀌면 다음 빌드가 다시 정렬한다.
         */
        void setTransparentSortAxis( const float3& axis );
        /**
         * @brief 추가 뷰마다 투명 꼬리를 그 뷰의 시점으로 다시 정렬해 스냅샷에 싣습니다. `populateFromScene` **뒤**, 내보내기 전에 부릅니다.
         * @details 씬이 그대로인 프레임에도 부른다. 뷰 카메라만 움직여도 순서가 바뀌기 때문이다. 그리지 않는 뷰(`_bRender` 0)는 건너뛴다.
         *          정렬 키는 주 뷰와 같은 규칙(`GPUTransparentSorter::makeTransparentSortKey`, `isDrawnBefore`)을 쓴다. 입자 그리기도 그 뷰의 시점으로 정렬해
         *          그 뷰의 배치 순서 안에 자리를 준다(`GPUViewTransparentOrder::_listParticleSlot`). 메시 투명이 없으면 입자 자리만 싣는다.
         */
        void computeViewTransparentOrders( const vector<RenderViewRequest>& listView );
        /** @brief 투명 정렬의 깊이 축입니다. 영벡터면 거리를 씁니다. */
        const float3& getTransparentSortAxis() const { return _transparentSorter.getAxis(); }
        /**
         * @brief 스냅샷(GT 에서 RT 로 넘어가는 모든 데이터)을 outSnapshot 으로 복사합니다. GPU 쪽 데이터는 타입상 담을 수 없습니다.
         * @details GT 가 프레임마다 RenderFramePacket 에 담을 때 쓴다. 부른 뒤 dirty 플래그는 소비된 것으로 보고
         *          0 으로 되돌린다(GPUScene::upload 가 재업로드를 생략하는 것과 짝을 이루는 GT 쪽 소비 시점).
         */
        void exportCPUSnapshot( GPUSceneSnapshot& outSnapshot );
        /**
         * @brief 이번 빌드가 그릴 메시 중 아직 업로드되지 않은 것을 업로드 큐에 올립니다(게임 스레드).
         * @details 렌더 스레드가 처음 그릴 때 만들 것을 **그리기 전에** 만들어 두려는 것이다. 큐가 미리
         *          만들어 두면 RT 의 `Mesh::initRHI` 호출은 핸들을 읽는 일이 되고, 큐가 처리하지 못한 것은 RT 가
         *          그 자리에서 만든다. 앞당기는 장치일 뿐 유일한 경로가 아니다.
         */
        void requestGPUUploads( GPUUploadQueue& queue ) const;
        /**
         * @brief 불투명 배치를 머티리얼이 아니라 **셰이더 퍼뮤테이션**(셰이더 경로 + 정적 define)으로 묶을지 정합니다(언리얼 GPUScene 방식).
         * @details 머티리얼 파라미터는 인스턴스의 materialIndex 로 버퍼에서 읽는다. 그래서 텍스처를 인덱스로 고를 수 있는 백엔드
         *          (DX12, Vulkan 의 네이티브 bindless)에서는 같은 메시와 같은 퍼뮤테이션이면 머티리얼이 달라도 드로우 하나로 그린다.
         *          DX11 과 GL 은 머티리얼 텍스처를 t16..t31 슬롯에 바인딩해야 해서 배치가 머티리얼 단위로 남는다. FrameRenderer 와 Engine 가
         *          디바이스 caps(supportsNativeBindlessSampling)로 값을 정한다. 값이 바뀌면 다음 populateFromScene 이 다시 묶는다.
         */
        void setMergeBatchesAcrossMaterials( bool bMerge );
        /** @brief 불투명 배치를 셰이더 퍼뮤테이션 단위로 묶고 있으면 true 입니다(`setMergeBatchesAcrossMaterials`). */
        bool isMergeBatchesAcrossMaterials() const { return _bMergeAcrossMaterials == SW_TRUE; }

        /** @brief 인스턴스 목록을 반환합니다. */
        const vector<GPUInstance>& getInstances() const { return _snapshot.getInstances(); }
        /** @brief 불투명 배치 구간을 반환합니다. */
        vector_reference<const GPUMeshBatch> getOpaqueBatches() const { return _snapshot.getOpaqueBatches(); }
        /** @brief 투명 배치 구간을 반환합니다. */
        vector_reference<const GPUMeshBatch> getTransparentBatches() const { return _snapshot.getTransparentBatches(); }
        /** @brief 방출기마다의 입자 그리기(주 시점에서 먼 것부터)를 반환합니다. */
        const vector<GPUParticleBatch>& getParticleBatches() const { return _snapshot._listParticleBatch; }
        /** @brief 주 시점에서 입자 그리기의 메시 투명 배치 사이 자리(`getParticleBatches` 와 같은 순서)를 반환합니다. */
        const vector<GPUTransparentParticleSlot>& getParticleSlots() const { return _snapshot._listParticleSlot; }
        /** @brief GPU 로 시뮬레이션하는 메시 방출기들(배치 항목마다의 인스턴스 칸)을 반환합니다. */
        const vector<GPUVfxMeshEmitter>& getVfxMeshEmitters() const { return _snapshot._listVfxMeshEmitter; }
        /** @brief 마지막 `computeViewTransparentOrders` 가 만든 추가 뷰마다의 투명 순서입니다. */
        const vector<GPUViewTransparentOrder>& getViewTransparentOrders() const { return _snapshot._listViewTransparentOrder; }
        /** @brief 셰이더 타입별 머티리얼 데이터 그룹(CPU 스냅샷)을 반환합니다. */
        const vector<GPUMaterialGroup>& getMaterialGroups() const { return _snapshot._listMaterialGroup; }
        /** @brief 퍼뮤테이션 하나를 반환합니다. 인덱스가 없으면 nullptr 입니다. */
        const GPUShaderPermutation* findShaderPermutation( uint32 index ) const
        {
            const vector<GPUShaderPermutation>* pList = _snapshot._pListShaderPermutation.get();
            return ( pList != nullptr && index < pList->size() ) ? &( *pList )[index] : nullptr;
        }
        /** @brief 마지막 populateFromScene 이 CPU 스냅샷을 바꿨으면 true 를 반환합니다. 내보내면 다시 false 가 됩니다. */
        bool isCPUSnapshotDirty() const { return _snapshot._bCPUDirty != SW_FALSE; }

    private:
        /** @brief `populateFromScene` 의 메시 쪽입니다(프리미티브 수집 · 나누기 · 투명 정렬). 바뀐 것이 없거나 후보가 없으면 일찍 끝난다. */
        void populateMeshesFromScene( Scene* pScene, const float3& cameraPos );

        /** @brief 이번 프레임 수집의 입력입니다(레지스트리 목록과 지난 프레임과의 같음 여부). `populateMeshesFromScene` 이 한 번 정합니다. */
        struct MeshCollectInput
        {
            Scene*                                _pScene{ nullptr };
            const PrimitiveRegistry*              _pPrimitives{ nullptr };
            const vector<ScenePrimitive*>*        _pListPrimitive{ nullptr };
            const vector<PrimitiveInstanceEntry>* _pListInstanceEntry{ nullptr };
            uint32                                _meshCount{ 0 }; ///< 슬롯 공간에서 메시 컴포넌트 수(그 뒤가 인스턴스 배치 항목)
            uint32                                _slotCount{ 0 };
            bool                                  _bHasCache{ false };        ///< 지난 프레임 후보가 있다
            bool                                  _bSetSame{ false };         ///< 프리미티브 집합 세대가 같다
            bool                                  _bPermutationSame{ false }; ///< 퍼뮤테이션 세대가 같다
        };
        /** @brief 수집 결과입니다. 두 수집(부분 · 전체)이 같은 뜻으로 채웁니다. */
        struct MeshCollectResult
        {
            size_t _candidateCount{ 0 };
            bool   _bPartialDone{ false }; ///< 부분 수집으로 끝났다(false 면 전체 수집이 필요하거나 전체 수집의 결과)
            bool   _bKeysSame{ false };    ///< 배치 키가 지난 프레임과 같다(부분: 바뀐 후보 기준, 전체: 모든 후보의 키와 자리)
            bool   _bContentSame{ false }; ///< 내용이 지난 프레임과 같다(부분: 바뀐 후보가 없다, 전체: 모든 후보의 내용과 자리)
        };
        /** @brief 더티 슬롯만 제자리에서 다시 채웁니다. 실릴지가 바뀌는 등 조건이 어긋나면 되돌리고 `_bPartialDone == false` 입니다. */
        [[nodiscard]] MeshCollectResult collectDirtyCandidatesInPlace( const MeshCollectInput& input );
        /** @brief 모든 슬롯을 다시 채우고(임계값을 넘으면 병렬) 지난 프레임과 슬롯마다 비교한 뒤 앞으로 당깁니다. */
        [[nodiscard]] MeshCollectResult collectAllCandidates( const MeshCollectInput& input );
        /** @brief 전체 수집이 남긴 슬롯 표시로 실린 후보를 앞으로 당기고, 지난 프레임과 같은지를 하나로 줄입니다(직렬 구간). */
        [[nodiscard]] MeshCollectResult compactCollectedCandidates( uint32 primitiveCount, bool bPrevAligned );
        /** @brief 후보의 raw 페이로드를 채우고(@p bOnlyDirtySlots 면 더티 슬롯만), 배치 키가 바뀌었으면 분할 테이블을 다시 짓습니다. */
        void fillPayloadsAndPartition( bool bOnlyDirtySlots, bool bBatchKeysSame );
        /** @brief 투명을 정렬하고 배치를 제자리 갱신하거나 다시 지은 뒤 쓰지 않는 머티리얼 원소를 놓습니다. */
        void rebuildBatches( const float3& cameraPos, bool bBatchKeysSame, bool bPartialCollect );
        /** @brief 수집한 인스턴스를 배치로 묶습니다. */
        void populateBatches();
        /**
         * @brief 정렬된 투명 후보를 배치로 방출합니다(populateBatches 의 뒷부분). 불투명 배치 뒤에 이어 붙입니다.
         * @details 전체 재구축과 **투명 꼬리만 다시 만드는 경로**(`rebuildTransparentTail`)가 이 함수를 함께 쓴다.
         */
        void emitTransparentBatches();
        /**
         * @brief 투명 정렬 순서만 바뀐 프레임에 불투명 접두부는 그대로 두고 투명 꼬리만 다시 방출합니다.
         * @details 투명 인스턴스는 불투명 인스턴스 뒤에 연속으로 놓인다(`GPUSceneSnapshot::getOpaqueInstanceCount`). 접두부는 제자리 갱신이 맡고,
         *          여기서는 꼬리를 잘라 내고 새 순서로 다시 붙인다. 투명 순서만 바뀐 프레임(투명 물체가 흔들리는 씬에서는 거의
         *          매 프레임)에 **불투명까지** 통째로 다시 만들지 않으려는 것이다.
         * @note **접두부 갱신과 같은 병렬 구간에서 돈다**(`refreshInstancesInPlace` 의 블록 0). 워커에서 실행될 수도 있다.
         *       쓰기 슬롯의 [0, 불투명 인스턴스 수) 는 워커들이 포인터로 쓰는 중이라 건드리지 않는다. 자르고 뒤에 붙일 뿐이고,
         *       붙일 공간은 호출하는 쪽이 미리 확보해 둔다(재할당이 없다). 접두부 청크가 읽는 것(발행본, raw, 원천 인덱스의 접두부)도
         *       쓰지 않는다. 여기에 접두부를 건드리는 코드를 추가하면 레이스가 생긴다.
         */
        void rebuildTransparentTail();
        /**
         * @brief 배치 키에 쓸 머티리얼입니다. 합치기가 켜져 있으면 같은 퍼뮤테이션의 대표 머티리얼을, 아니면 그 머티리얼 자신을 반환합니다.
         * @details 대표는 재구축할 때마다 처음 만난 머티리얼이다(_mapShaderRepresentative). 재구축 여부는 후보 자체를 비교해 판단하므로 대표가 바뀌어도 상관없다.
         *          불투명 나누기만 이 함수를 부른다. 투명 방출은 해시가 같다는 것까지 확인한 뒤라 맵 없이 같은 답을 낸다(`emitTransparentBatches`).
         *          대표는 **퍼뮤테이션 단위**다. 같은 .hlsl 이라도 정적 스위치가 다르면 다른 셰이더이므로 합칠 수 없다.
         */
        Material* batchKeyMaterial( Material* pMaterial, uint64 permutationHash );
        /**
         * @brief 투명 인덱스를 카메라 거리순(먼 것에서 가까운 것 순서)으로 정렬합니다.
         * @details 후보에서 키를 채우는 것만 여기서 하고, 정렬은 `GPUTransparentSorter::sortKeys` 가 한다. 지난 프레임의 순서에서
         *          삽입 정렬로 시작하고, 이동 횟수가 원소당 `GPUTransparentSorter::kTransparentInsertionMovesPerElement` 를 넘으면
         *          std::sort 로 넘어간다. 순서는 전순서이므로 어느 쪽이든 결과가 같다.
         */
        void sortTransparent( const float3& cameraPos );
        /** @brief 불투명 인덱스 테이블과 투명 인덱스 테이블을 후보에서 다시 만듭니다. */
        void rebuildPartitionTables();
        /**
         * @brief 씬의 `VfxComponent` 가 낸 입자로 방출기마다 그리기 하나(`GPUParticleBatch`)를 채우고 주 시점에서 먼 것부터 놓습니다.
         * @details 정렬 규칙은 메시 투명 후보와 같다(`GPUTransparentSorter::makeTransparentSortKey` · `isDrawnBefore`, 깊이는 방출기 위치). 입자가 없는 방출기는 싣지 않는다.
         *          메시 투명 배치(발행된 꼬리)와 비교해 입자마다 주 투명 순서 안의 자리(`GPUSceneSnapshot::_listParticleSlot`)도 정한다. 메시 쪽이 끝난 **뒤** 부른다.
         */
        void populateParticleBatches( const GameObjectManager& objects, const float3& cameraPos );
        /**
         * @brief GPU 메시 방출기 하나(`GPUVfxMeshEmitter`)를 싣습니다 — 배치 항목마다 이번에 발행된 인스턴스 칸을 찾는다(불투명 구간만). 재생 중이 아니거나 배치가 없으면 싣지 않습니다.
         */
        void appendVfxMeshEmitter( const VfxComponent& vfx, uint32 emitterIndex, const PrimitiveRegistry& primitives );
        /** @brief 입자 그리기마다 @p eye · @p axis 의 정렬 키를 채우고 그리는 순서로 정렬합니다. 키의 후보 번호 칸은 `_listParticleBatch` 의 번호입니다. */
        void fillParticleKeys( const float3& eye, const float3& axis, vector<GPUTransparentSortKey>& outListKey ) const;
        /**
         * @brief 주 투명 배치마다 첫 인스턴스(주 순서에서 가장 먼저 그리는 것)의 키를 채웁니다. 발행된 인스턴스와 꼬리 순서(`_listBuiltTransparentIdx`)를 읽습니다.
         * @details 꼬리 안에서 찾지 못하는 배치는 `GPUTransparentSorter::kNoBatchHeadLayer` 키를 받는다(모든 입자가 그 앞에 선다).
         */
        void fillMainBatchHeadKeys( const float3& cameraPos, vector<GPUTransparentSortKey>& outListKey ) const;
        /** @brief 투명 꼬리 원소 @p tailIndex(꼬리 시작 기준)의 정렬 레이어 키입니다. 기본 키 표시는 @p defaultSortKey 로 풉니다. */
        uint32 resolveTailSortLayer( uint32 tailIndex, uint32 defaultSortKey ) const;
        /**
         * @brief 배치를 다시 나누지 않고 인스턴스 값만 제자리에서 갱신합니다.
         * @details 배치 키가 그대로일 때 쓴다. 그러면 불투명 접두부의 배치와 각 원소의
         *          `_meshBatchIndex`, `_materialIndex` 가 그대로이므로 바뀐 것은 트랜스폼, 바운드, 회전 시드뿐이다.
         *          투명 정렬 순서가 바뀌었으면 투명 꼬리만 다시 만든다(`rebuildTransparentTail`).
         * @return 갱신했으면 true 입니다. 조건이 맞지 않아 전체 재구축이 필요하면 false 입니다.
         */
        bool refreshInstancesInPlace( bool bPartialCollect );
        /** @brief 전체 제자리 갱신입니다. 접두부를 청크로 나눠 병렬로 고치고(바뀐 투명 순서면 블록 0 이 꼬리) 청크의 더티 구간을 잇습니다. 청크가 실패하면 false 입니다. */
        [[nodiscard]] bool refreshAllInstancesParallel( const vector<GPUInstance>& listPublishedInstance, size_t slotCount, bool bTransparentOrderChanged, bool& ioTooManyRuns );
        /** @brief 다시 만든 투명 꼬리를 더티 구간 하나로 이어 붙입니다(구간 상한을 넘으면 @p ioTooManyRuns). */
        void appendTransparentTailRun( uint32 opaqueInstanceCount, bool& ioTooManyRuns );
        /** @brief 부분 제자리 갱신입니다. 더티 프리미티브가 가리키는 인스턴스만 고치고 회전 인스턴스 수를 증감합니다. 매핑이 어긋나면 false 입니다. */
        [[nodiscard]] bool refreshDirtyInstances( const vector<GPUInstance>& listPublishedInstance, size_t slotCount, bool& ioTooManyRuns );
        /**
         * @brief 부분 수집에서 메시만 바뀐 불투명 후보(`_listMeshSwapCandidate`)의 배치를 다시 나누지 않고 그 배치의 메시만 바꿉니다.
         * @details 후보마다 그 배치가 인스턴스 하나뿐이어야 한다(지형 청크는 청크마다 인스턴스 배치 하나다). 같은 메시를 여럿이 쓰는 배치는
         *          새 메시로 갈라야 하므로 나누기가 맡는다. 불투명 인스턴스 슬롯은 정렬된 나누기 항목의 슬롯과 같다(`populateBatches` 가 항목 순서로 방출한다) —
         *          그 슬롯의 나누기 키 메시도 함께 바꿔 다음 전체 방출(`populateBatches`)이 낡은 메시 포인터로 묶지 않게 한다.
         *          인접한 같은 키끼리 묶는 규칙이라 정렬 순서가 흐트러져도 배치는 맞다(묶음이 하나 덜 합쳐질 수 있을 뿐이다).
         *          렌더 스레드는 배치의 메시에서 정점 풀 구간 · 모프 · VAT 를 매 프레임 다시 구하므로 GT 는 메시와 정점 수만 바꾼다.
         * @return 모든 후보를 바꿨으면 true 입니다. 하나라도 조건이 맞지 않으면 아무것도 바꾸지 않고 false 이며, 부르는 쪽이 배치를 다시 나눈다.
         */
        [[nodiscard]] bool swapSingleInstanceBatchMeshes();
        /** @brief 재구축 판단 캐시를 무효로 만듭니다. */
        void invalidateBuildCache();
        /**
         * @brief 텍스처가 다시 업로드되었으면(`TextureCache::getReloadGeneration`) 지난 빌드의 머티리얼이 새 SRV 인덱스를 받게 합니다.
         * @details 슬롯 바인딩 백엔드(DX11, GL)의 배치는 SRV 를 값으로 가지고 있으므로(`GPUMeshBatch::_arrMaterialTexSrv`) 바뀐 머티리얼이 있으면
         *          퍼뮤테이션 세대를 올려 배치를 다시 만들게 한다. 네이티브 bindless 는 머티리얼 바이트가 바뀌어 매 프레임 업로드가 처리한다.
         */
        void refreshReloadedTextures();

        /** @brief 기준 스냅샷입니다. 매 프레임 `exportCPUSnapshot` 이 복사해 내보냅니다. 퍼뮤테이션 테이블과 머티리얼 그룹은 여기서 계속 자랍니다. */
        GPUSceneSnapshot _snapshot;
        /** @brief 머티리얼 그룹, 원소의 영속 인덱스, 퍼뮤테이션 표입니다. RT 가 읽는 부분은 `_snapshot` 에 쓰고 찾는 맵과 회수 시계는 자기가 가집니다. */
        GPUMaterialRegistry _materialRegistry;
        /** @brief 투명 정렬 규칙, 정렬 축, 정렬 작업 배열입니다. */
        GPUTransparentSorter _transparentSorter;
        /** @brief 스킨 팔레트를 `_snapshot` 으로 옮깁니다(`populateFromScene` 이 매 프레임 부릅니다). */
        GPUSkinPaletteCollector _skinPaletteCollector;
        /**
         * @brief 마지막 수집 때 본 퍼뮤테이션 세대입니다(`MaterialUtil::getPermutationGeneration`).
         * @details 정지한 씬에서 머티리얼의 정적 스위치나 키워드를 바꾸면 프리미티브는 하나도 더러워지지
         *          않는다. 이 값이 다르면 "아무도 안 움직였다" 는 건너뛰기를 하지 않는다.
         */
        uint64 _lastPermutationGeneration{ 0 };
        /**
         * @brief 재구축 중 **퍼뮤테이션 해시**에서 대표 머티리얼을 찾는 맵입니다(배치 키 합치기용).
         * @details 셰이더 **경로**를 키로 쓰면 forwardlit.hlsl 을 쓰는 유리 머티리얼과 불투명 머티리얼이 한 대표로 합쳐져
         *          같은 배치가 된다. 배치는 PSO 하나로 그리므로 한쪽 퍼뮤테이션이 통째로 사라진다.
         */
        unordered_map<uint64, Material*> _mapShaderRepresentative;

        /// 그릴 후보 하나(트랜스폼, 바운드, 메시, 머티리얼, 인스턴스, 블렌드)입니다. populateFromScene 이 배열을 재사용해 프레임당 힙 할당을 줄입니다.
        struct DrawCandidate
        {
            float4x4 _world{};
            float3   _boundsCenter{};
            float32  _boundsRadius{ 1.0f };      ///< 월드 반지름(메시 반지름 × 월드의 최대 축 스케일). 컬링이 읽는다.
            float32  _localBoundsRadius{ 1.0f }; ///< 메시 공간 반지름. 트랜스폼만 바뀐 프레임에는 이 값으로 월드 반지름을 다시 만든다.
            uint32   _blendMode{ 0 };
            /**
             * @brief 그리는 메시 구간(`MeshSection`)의 첫 정점과 정점 수입니다. 수가 0 이면 메시 전체입니다. **배치 키**입니다 — 같은 메시의 다른 구간은
             *        머티리얼이 같아도 다른 배치(다른 정점 범위)다(`MeshComponent::findDrawSection`).
             */
            uint32                       _sectionFirstVertex{ 0 };
            uint32                       _sectionVertexCount{ 0 };
            shared_ptr<Mesh>             _mesh;
            shared_ptr<Material>         _material;
            shared_ptr<MaterialInstance> _instance;
            /// @brief GPU 회전 애니메이션 시드입니다(0이면 없음). MeshComponent 가 주고 GPUInstance::_spinSeed 로 전달됩니다.
            uint32 _spinSeed{ 0 };
            /**
             * @brief 투명 큐의 정렬 키입니다(`Render2DSettings::makeSortKey`: 정렬 레이어와 레이어 안 순서, 0이면 기본). **내용**이며 배치 키가 아닙니다.
             * @details 투명 정렬(`sortTransparent`)의 첫 번째 키다. 바뀌면 그 프레임의 정렬이 순서를 바꾸고 투명 꼬리를 다시 방출한다.
             */
            uint32 _sortKey{ 0 };
            /// @brief 강조(`GPUInstanceHighlight::pack`)입니다. MeshComponent 가 주고 GPUInstance::_highlight 로 전달됩니다(배치 키가 아니라 내용).
            uint32 _highlight{ 0 };
            /**
             * @brief 등장 비율(0 ~ 1)입니다. MeshComponent 가 주고 `GPUInstance::_appearance` 로 전달됩니다(내용). 1 보다 작으면 디더 변형이라 퍼뮤테이션 해시가 달라진다.
             * @details 배치 키에는 따로 넣지 않는다 — 디더인지는 퍼뮤테이션 해시에 들어 있다(`usesAppearanceDither`, `stampPermutationHash`).
             */
            float32 _appearance{ 1.0f };
            /**
             * @brief 스프라이트 프레임과 색입니다(GPUInstance::_sprite 로 전달됩니다). **내용**이며 배치 키가 아닙니다.
             * @details `operator==` 에는 포함되고 `hasSameBatchKey` 에는 포함되지 않는다. 프레임만 넘긴 스프라이트는 배치를 다시 나누지 않고
             *          제자리 갱신(더티 구간 하나)으로 끝난다. 키에 넣으면 프레임마다 정렬과 나누기를 다시 하게 된다.
             */
            GPUSpriteInstanceData _sprite{};
            /**
             * @brief (셰이더 경로 + define) 해시입니다. **어느 PSO 로 그릴지**를 정하는 값입니다.
             * @details 배치 키에 들어가야 한다. 머티리얼과 인스턴스 **포인터가 그대로여도** 인스턴스의
             *          키워드, 멀티컴파일, 품질을 바꾸면 이 값이 바뀌고, 그러면 다른 셰이더로 그려야 한다.
             *          이 값이 키에 없으면 `hasSameBatchKey` 가 "그대로" 라고 답해 배치가 다시 나뉘지 않고,
             *          **바뀐 퍼뮤테이션이 화면에 반영되지 않는다**(런타임에 정적 스위치를 바꾸는 경로가 조용히 죽는다). 수집에서 한 번 구해 두면 나누기와 정렬도 다시 구하지 않는다.
             */
            uint64 _permutationHash{ 0 };
            /**
             * @brief 월드 행렬식이 음수(거울 변환)인지 나타냅니다. 배치 키이며 `GPUMeshBatch::_bReverseCulling` 으로 전달됩니다.
             * @details 월드 행렬에서 나오는 값이라 트랜스폼만 바뀐 프레임에도 다시 구한다(`copyCandidateTransforms`). 부호가 바뀌면 배치를 다시 나눈다.
             */
            uint8 _bReverseCulling{ SW_FALSE };
            /// @brief 정점 애니메이션 시간 오프셋(초)입니다. MeshComponent 가 주고 GPUInstance::_vertexAnimationPhase 로 전달됩니다(배치 키가 아니라 내용).
            /// 컬 반전(1바이트) 뒤의 정렬 빈 공간에 둔다. 앞에 두면 구멍이 생긴다(아래 static_assert).
            float32 _vertexAnimationPhase{ 0.0f };

            /** @brief 등장 디더 변형으로 그리는지 반환합니다 — 등장 중(1 미만)이고 불투명일 때입니다. 투명은 정렬 · 블렌드가 있어 디더하지 않는다. */
            bool usesAppearanceDither() const { return _appearance < 1.0f && _blendMode != static_cast<uint32>( RHIBlendMode::Transparent ); }

            /**
             * @brief 재구축이 필요한지 판단하기 위한 필드 단위 비교입니다.
             * @details 지문(후보마다 100여 바이트를 FNV 로 섞은 64비트)을 쓰지 않는다. 지문에는 두 가지 문제가 있다.
             *          (1) 바이트마다 곱셈이 들어가 비교 자체가 수집 비용에 맞먹는다.
             *          (2) 해시가 충돌하면 바뀐 씬을 "그대로" 로 보고 화면이 멈추는데, 재현이 사실상 불가능한 버그가 된다.
             *          어차피 같은 바이트를 다 읽어야 한다면 지문을 만들지 말고
             *          **그냥 비교**하는 것이 더 싸고 정확하다. 다르면 즉시 빠져나올 수도 있다.
             * @note 부동소수는 float3 와 float4x4 의 `operator==` (nearEqual, 엡실론 비교)가 아니라
             *       **비트 단위로** 비교한다. 엡실론 비교는 매 프레임 엡실론 미만으로 움직이는 물체를
             *       영원히 "안 바뀜" 으로 보고 화면에 오차를 누적시킨다. 반대로 비트 비교가 틀리는
             *       경우(-0.0 과 0.0 을 다르게 봄)는 불필요한 재구축일 뿐이라 안전하다.
             *       비교 대상 블록은 모두 float 이 연속이라 패딩이 끼지 않는다.
             */
            bool operator==( const DrawCandidate& other ) const
            {
                return _mesh == other._mesh && _material == other._material && _instance == other._instance &&
                       _sectionFirstVertex == other._sectionFirstVertex && _sectionVertexCount == other._sectionVertexCount &&
                       _blendMode == other._blendMode && _spinSeed == other._spinSeed && _sprite == other._sprite && _highlight == other._highlight &&
                       Memory::compare( &_vertexAnimationPhase, &other._vertexAnimationPhase, sizeof( _vertexAnimationPhase ) ) == 0 &&
                       Memory::compare( &_appearance, &other._appearance, sizeof( _appearance ) ) == 0 &&
                       _permutationHash == other._permutationHash && _sortKey == other._sortKey && _bReverseCulling == other._bReverseCulling &&
                       Memory::compare( &_world, &other._world, sizeof( _world ) ) == 0 &&
                       Memory::compare( &_boundsCenter, &other._boundsCenter, sizeof( _boundsCenter ) ) == 0 &&
                       Memory::compare( &_boundsRadius, &other._boundsRadius, sizeof( _boundsRadius ) ) == 0;
            }
            /** @brief operator== 의 부정입니다. */
            bool operator!=( const DrawCandidate& other ) const { return ( *this == other ) == false; }

            /**
             * @brief 배치가 묶이는 기준(메시, 머티리얼, 인스턴스, 블렌드, 퍼뮤테이션 해시, 컬 반전)이 같은지 확인합니다. 트랜스폼은 보지 않습니다.
             * @details 불투명 배치는 이 여섯 가지로만 나뉘고 정렬된다. 컬 반전은 월드 행렬식의 부호뿐이라 이동, 회전, 양수 스케일로는 바뀌지 않는다. 물체가 **움직이기만** 했다면
             *          배치 구성은 한 글자도 바뀌지 않으므로 다시 나누고 다시 정렬할 이유가 없다.
             *          움직이는 씬에서 남는 유일한 O(N log N) 이 그 정렬이다.
             */
            bool hasSameBatchKey( const DrawCandidate& other ) const { return _mesh == other._mesh && hasSameBatchKeyExceptMesh( other ); }
            /**
             * @brief 메시 말고 배치 키(머티리얼, 인스턴스, 블렌드, 퍼뮤테이션 해시, 컬 반전)가 같은지 확인합니다.
             * @details 메시만 바뀐 후보(지형 청크의 LOD 교체 — 청크마다 새 메시)는 인스턴스 하나짜리 배치면 배치를 다시 나누지 않고 그 배치의 메시만
             *          바꾼다(`swapSingleInstanceBatchMeshes`). PSO · 머티리얼 버퍼를 정하는 값은 모두 여기 있고 메시는 정점 구간만 정한다.
             */
            bool hasSameBatchKeyExceptMesh( const DrawCandidate& other ) const
            {
                return _material == other._material && _instance == other._instance && _sectionFirstVertex == other._sectionFirstVertex &&
                       _sectionVertexCount == other._sectionVertexCount && _blendMode == other._blendMode && _permutationHash == other._permutationHash &&
                       _bReverseCulling == other._bReverseCulling;
            }
        };
        /// @brief 후보는 프리미티브마다 하나라 패딩이 곧 메모리 대역폭입니다. 필드 크기의 합을 정렬 단위로 올린 값을 넘으면(필드 사이에 구멍이 생기면) 빌드를 멈춥니다.
        static_assert( sizeof( DrawCandidate ) <= ( sizeof( float4x4 ) + sizeof( float3 ) + sizeof( float32 ) * 2 + sizeof( uint32 ) * 6 +
                                                    sizeof( shared_ptr<Mesh> ) * 3 + sizeof( GPUSpriteInstanceData ) + sizeof( uint64 ) +
                                                    sizeof( uint8 ) + sizeof( float32 ) * 2 + alignof( DrawCandidate ) - 1 ) /
                                                      alignof( DrawCandidate ) * alignof( DrawCandidate ),
                       "DrawCandidate has padding between fields (or a field was added without adding its size here)" );

        /**
         * @brief 프리미티브 하나를 후보로 채웁니다. 그릴 수 없으면(안 보임, 비활성, 메시 없음) false 를 반환합니다.
         * @details **전체 수집과 부분 수집이 이 함수를 함께 쓴다.** 채우는 규칙이 두 곳으로 갈리면
         *          부분 갱신만 낡은 필드를 남기는데, 그 화면은 대부분의 프레임에서 멀쩡해 보인다.
         */
        bool fillCandidateFromPrimitive( ScenePrimitive* pPrimitive, Scene* pScene, DrawCandidate& candidate );
        /** @brief 인스턴스 배치의 항목 하나를 후보로 채웁니다. 메시 컴포넌트 버전과 규칙이 같고, 소유권은 배치에 있습니다. */
        bool fillCandidateFromInstanceEntry( const PrimitiveInstanceEntry& entry, Scene* pScene, DrawCandidate& candidate );
        /**
         * @brief 후보의 머티리얼과 블렌드 모드를 정합니다. 메시 컴포넌트 버전과 인스턴스 배치 버전이 같은 규칙을 씁니다.
         * @details 머티리얼이 없으면 인스턴스의 부모 머티리얼을, 그것도 없으면 씬 기본 머티리얼을 쓴다(언리얼의 기본 머티리얼).
         *          인스턴스의 원소 바이트와 퍼뮤테이션은 부모의 레이아웃을 따르므로, 배치의 머티리얼, 그룹, 텍스처, stride 도 부모의
         *          것이어야 한다(씬 기본이 먼저면 인스턴스만 붙은 메시가 두 머티리얼이 섞인 배치가 된다).
         *          **블렌드 모드는 머티리얼의 속성이다.** 언리얼도 블렌드 모드가 머티리얼 에셋에 있고, 그 값이 셰이더
         *          퍼뮤테이션(불투명, 반투명)을 가른다. 메시가 값을 뒤집을 수 있게 두면 불투명으로 컴파일된 머티리얼을 블렌딩으로
         *          그리는 어긋난 상태가 만들어진다. 블렌드 모드는 후보의 머티리얼에서 읽고, 머티리얼이 없으면 `fallbackBlendMode`
         *          (메시 쪽 값)를 쓴다. 머티리얼이 없는 디버그 메시와 픽스처 메시가 그 경우다.
         */
        static void fillCandidateMaterial( DrawCandidate& candidate, Material* pMaterial, Scene* pScene, uint32 fallbackBlendMode );
        /** @brief 후보의 머티리얼이 GPU 에 아직 올라가지 않았으면(`MaterialGpuState` 가 `Ready` 가 아님) 세고 true 를 반환합니다. 워커에서 불립니다. */
        bool isMaterialNotReady( const DrawCandidate& candidate );
        /** @brief 후보의 퍼뮤테이션 해시를 기록합니다. 게임 스레드 전용입니다(머티리얼의 지연 캐시를 건드립니다). */
        static void stampPermutationHash( DrawCandidate& candidate );
        /** @brief 후보에서 GPU 인스턴스 페이로드(월드, 바운드, 블렌드, 시드, 스프라이트 프레임과 색)를 채웁니다. 배치와 머티리얼 인덱스는 건드리지 않습니다. */
        static void fillPayload( const DrawCandidate& candidate, GPUInstance& outInstance );
        /**
         * @brief 월드 행렬만 바뀐 프리미티브(`_listTransformDirtyPrimitive`)의 후보에 트랜스폼 저장소의 행렬과 바운드 중심을 옮깁니다.
         * @details 컴포넌트, 메시, 머티리얼은 읽지 않는다. 부분 수집 안에서만 부르고, 후보 슬롯은 지난 프레임 것 그대로다
         *          (후보에 실리지 않았던 것은 부르기 전에 렌더 상태 목록으로 넘긴다). 행렬식의 부호(컬 반전)도 다시 구한다.
         * @return 컬 반전이 바뀐 후보가 있으면 true 입니다. 배치 키가 바뀐 것이므로 호출하는 쪽이 배치를 다시 나눕니다.
         */
        [[nodiscard]] bool copyCandidateTransforms( const PrimitiveRegistry& primitives );
        /**
         * @brief 월드 행렬만 바뀐 항목 가운데 지난 프레임 후보에 없는 것(메시가 아직 없었거나 인스턴스 항목)을 렌더 상태 목록으로 넘깁니다.
         * @details 그런 항목은 다시 모아야 후보에 실릴지 알 수 있다. 남은 것만 `copyCandidateTransforms` 가 행렬을 옮긴다. 부분 수집 안에서만 부른다.
         */
        void moveTransformSlotsWithoutCandidate( uint32 meshCount );
        /**
         * @brief 후보 [begin,end) 를 배치 하나로 방출하고 인스턴스를 작업 배열에 붙입니다(populateBatches 안에서 부릅니다).
         * @details 불투명과 투명이 **같은 함수**를 쓴다. 따로 두면 한쪽에 넣은 수정(역매핑, 회전 수, 머티리얼 원소)이
         *          다른 쪽에 반영되지 않는다. 다른 점은 인자로 받는다.
         *          배치가 실을 머티리얼과 인스턴스(불투명은 합치기 대표, 투명은 머리 후보의 것)와 블렌드 모드가 그것이다.
         */
        void emitBatch( const uint32* pSrcIdx, uint32 begin, uint32 end, RHIBlendMode blendMode, const shared_ptr<Material>& material,
                        const shared_ptr<MaterialInstance>& instance );
        /**
         * @brief 후보 인덱스에서 마지막 방출이 그 후보에 준 머티리얼 원소 인덱스를 찾는 배열입니다.
         * @details 원소 인덱스는 (머티리얼, 인스턴스) 쌍에 영속적이라 배치 키가 그대로인 프레임에는 다시 묻지 않아도 된다.
         *          투명 꼬리를 다시 만들 때 인스턴스 2000개가 해시 테이블을 다시 조회하던 것(깊이순이라 원소가 번갈아 와서
         *          직전 조회 캐시도 맞지 않는다)을 배열 읽기 하나로 바꾼다. 전체 재구축이 채우고, 꼬리 재방출이 읽는다.
         */
        vector<uint32> _listCandidateMaterialElement;

        /**
         * @brief 전체 수집이 프리미티브 슬롯마다 남기는 표시(비트)입니다.
         * @details 워커가 자기 슬롯을 채우면서 **지난 후보와의 비교까지** 한 번에 끝낸다. 직렬 패스로
         *          해시 기록과 배치 키 비교를 하려고 같은 슬롯들을 다시 지나가지 않는다.
         *          해시는 (머티리얼, 인스턴스, 퍼뮤테이션 세대)의 함수라 셋이 같으면 지난 값이 그대로 맞다.
         *          Release 에서 큐브 8000개가 모두 이동하는 300프레임을 2회 측정한 결과, 빌드 p50 이 786/851 → 720/720µs, 배치 키 비교가
         *          65~81 → 0 이 되었고 수집은 늘지 않았다(245/262 → 229/245). raw 채우기는 여기 넣지 않는다(`populateFromScene` 본문 주석).
         */
        enum CollectFlag : uint8
        {
            kCollectIncluded    = 1u << 0, ///< 후보에 실린다.
            kCollectNeedsStamp  = 1u << 1, ///< 해시를 직렬 구간에서 기록해야 한다(머티리얼, 인스턴스, 세대 중 하나가 달라졌다).
            kCollectKeySame     = 1u << 2, ///< 지난 후보와 배치 키가 같다.
            kCollectContentSame = 1u << 3, ///< 지난 후보와 내용이 전부 같다.
        };

        /** @brief 후보 배열에 실리지 않은 프리미티브의 표시입니다. */
        static constexpr uint32 kInvalidCandidateIndex = invalid_index::kUint32;
        /**
         * @brief 이번 프레임에 "바뀌었다" 고 표시된 프리미티브의 레지스트리 인덱스입니다.
         * @details 레지스트리에서 받을 때는 **렌더 상태가** 바뀐 것만 들어 있고(다시 모은다), 부분 수집이 끝나면 `_listTransformDirtyPrimitive` 를
         *          뒤에 이어 붙인다. 채우기와 제자리 갱신은 둘을 구분하지 않는다.
         */
        vector<uint32> _listDirtyPrimitive;
        /**
         * @brief 이번 프레임에 **월드 행렬만** 바뀐 프리미티브의 레지스트리 인덱스입니다(`PrimitiveRegistry::markTransformDirty`).
         * @details 이 프리미티브들은 후보를 다시 채우지 않고 트랜스폼 저장소의 행렬만 옮긴다(`copyCandidateTransforms`). 부분 수집의 임계값(더티가
         *          번호 공간의 4분의 1 미만)에도 이들은 세지 않는다. 큐브 8000개가 모두 움직이는 프레임도 부분 수집이다.
         */
        vector<uint32> _listTransformDirtyPrimitive;
        /** @brief 이번 부분 수집에서 메시만 바뀐 불투명 후보의 인덱스입니다(`swapSingleInstanceBatchMeshes`). 수집마다 비웁니다. */
        vector<uint32> _listMeshSwapCandidate;
        /** @brief 레지스트리 인덱스에서 후보 인덱스를 찾는 배열입니다(`kInvalidCandidateIndex` 는 후보에 실리지 않았다는 뜻). */
        vector<uint32> _listPrimitiveToCandidate;
        /// @brief 전체 수집이 프리미티브 번호 위치에 남긴 `CollectFlag` 입니다. 앞으로 당겨 올 때 읽습니다.
        vector<uint8> _listCollectFlag;
        /** @brief 후보 인덱스에서 인스턴스 슬롯을 찾는 배열입니다(`kInvalidCandidateIndex` 는 인스턴스 없음). `_listInstanceSrcIndex` 의 역방향입니다. */
        vector<uint32> _listCandidateToInstance;
        /** @brief 마지막 수집이 만든 후보 수입니다. 부분 수집이 이 슬롯 수를 그대로 이어받습니다. */
        size_t _lastCandidateCount{ 0 };
        /**
         * @brief 마지막 수집에서 GPU 준비가 안 된(`MaterialGpuState` 가 `Ready` 가 아닌) 머티리얼 때문에 후보에서 뺀 프리미티브 수입니다(워커가 더하므로 원자).
         * @details 0 이 아니면 다음 프레임은 더티 신호가 없어도 전체 수집을 다시 합니다. 렌더 스레드가 머티리얼을 올리는 것은 씬에 아무 신호도 남기지 않기 때문입니다.
         *          이동 가능하게 두려고 값을 감싼 형식입니다.
         */
        struct NotReadyCounter
        {
            atomic<uint32> _count{ 0 };
            NotReadyCounter() = default;
            NotReadyCounter( NotReadyCounter&& other ) noexcept
                : _count{ other._count.load() }
            {
            }
            NotReadyCounter& operator=( NotReadyCounter&& other ) noexcept
            {
                _count.store( other._count.load() );
                return *this;
            }
        };
        NotReadyCounter _notReadySkipped;
        /** @brief 부분 수집이 프리미티브 하나를 채워 볼 임시 후보입니다. 프레임마다 할당하지 않습니다. */
        DrawCandidate _candidateProbe;

        vector<DrawCandidate> _listScratchCandidate;
        /** @brief 마지막으로 반영한 후보 집합입니다. 다음 프레임의 변경 판단 기준이며 scratch 버퍼를 재활용하는 곳이기도 합니다. */
        vector<DrawCandidate> _listBuiltCandidate;
        vector<GPUInstance>   _listScratchRaw;

        struct SortKey
        {
            Mesh*             _pMesh{ nullptr };
            uint32            _sectionFirstVertex{ 0 }; ///< 같은 메시의 다른 구간은 다른 배치다(DrawCandidate::_sectionFirstVertex)
            uint32            _sectionVertexCount{ 0 };
            Material*         _pMaterial{ nullptr };
            MaterialInstance* _pInstance{ nullptr };
            /**
             * @brief (셰이더 경로 + define) 해시입니다. 배치가 쓸 PSO 를 정합니다.
             * @details **대표 머티리얼 포인터로는 대신할 수 없다.** 합치기가 켜지면 대표는 "이 퍼뮤테이션을
             *          처음 가져온 머티리얼" 인데, 퍼뮤테이션이 **인스턴스**에서 오면 부모가 같아 대표도
             *          같아진다. 서로 다른 셰이더로 그려야 할 것들이 한 배치로 합쳐진다. 해시를 키에 직접
             *          넣어야 갈라진다(그렇지 않으면 런타임에 인스턴스의 정적 스위치를 바꾸는 경로가 조용히 죽는다).
             */
            uint64 _permutationHash{ 0 };
            /** @brief 컬 모드를 뒤집어 그리는지(거울 변환) 나타냅니다. 퍼뮤테이션과 함께 PSO 를 정하므로 키입니다. */
            uint8 _bReverseCulling{ SW_FALSE };
            /** @brief 메시, 머티리얼, 인스턴스, 퍼뮤테이션, 컬 반전이 같은지 비교합니다. */
            bool operator==( const SortKey& other ) const
            {
                return _pMesh == other._pMesh && _sectionFirstVertex == other._sectionFirstVertex && _sectionVertexCount == other._sectionVertexCount &&
                       _pMaterial == other._pMaterial && _pInstance == other._pInstance && _permutationHash == other._permutationHash &&
                       _bReverseCulling == other._bReverseCulling;
            }
        };

        struct SortEntry
        {
            SortKey _key;
            uint32  _srcIdx{ 0 };
        };

        vector<SortEntry> _listScratchOpaqueEntry;
        /// @brief 정렬된 불투명 항목의 후보 인덱스만 뽑은 배열입니다. `emitBatch` 가 투명 쪽과 같은 형태로 받습니다.
        vector<uint32> _listScratchOpaqueIdx;
        vector<uint32> _listScratchTransparentIdx;
        /// @brief 입자 그리기의 정렬 키(`populateParticleBatches`)입니다. 후보 번호는 정렬 전 `_listParticleBatch` 의 자리입니다.
        vector<GPUTransparentSortKey> _listScratchParticleKey;
        /// @brief 입자 그리기를 정렬 순서로 옮겨 담는 작업 배열입니다(`_snapshot._listParticleBatch` 와 맞바꾼다).
        vector<GPUParticleBatch> _listScratchParticleBatch;
        /// @brief 주 투명 배치마다 첫 인스턴스의 키입니다(`fillMainBatchHeadKeys` — 입자 자리 정하기).
        vector<GPUTransparentSortKey> _listScratchBatchHeadKey;
        /// @brief 추가 뷰 하나의 입자 정렬 키입니다(`computeViewTransparentOrders` 가 뷰마다 다시 쓴다).
        vector<GPUTransparentSortKey> _listScratchViewParticleKey;
        /**
         * @brief 부분 업로드로 나눌 구간 수의 상한입니다. 넘으면 전체를 올립니다.
         *
         * @details 구간들은 **한 번의 호출**로 올라가므로(`updateStructuredBufferRegions`) 비용이 구간
         *          수에 비례하지 않는다. 그래도 상한을 두는 이유는 두 가지다. 구간 목록 자체가 스냅샷에
         *          실려 복사되고, 변경이 배열 전체에 고르게 흩어졌다면 통째로 올리는 것이 단순하고 싸다.
         */
        static constexpr size_t kMaxDirtyInstanceRun = 256;

        /**
         * @struct InstanceRefreshChunk
         * @brief 전체 제자리 갱신을 청크로 나눠 돌릴 때 청크 하나의 결과입니다.
         * @details 청크는 인스턴스 슬롯의 연속 구간이라 더티 구간(run)도 청크 안에서 만들고, 끝난 뒤
         *          경계가 맞닿는 구간만 이어 붙인다. 회전 인스턴스 수도 청크마다 세어 합친다.
         */
        struct InstanceRefreshChunk
        {
            uint32         _start{ 0 };
            uint32         _end{ 0 };
            uint32         _spinCount{ 0 };
            uint32         _runCount{ 0 };
            uint8          _bFailed{ SW_FALSE };
            uint8          _bTooManyRun{ SW_FALSE };
            GPUInstanceRun _arrRun[kMaxDirtyInstanceRun];
        };
        vector<InstanceRefreshChunk> _listRefreshChunk;

        /**
         * @brief 청크 하나의 슬롯 구간을 갱신합니다(워커에서 돌며 포인터만 만집니다).
         * @details 이전 값은 `pPrevious`(마지막 발행본)에서 읽고 결과는 `pInstance`(이번 쓰기 슬롯)에 통째로 쓴다.
         *          두 배열은 다른 메모리다(`GPUInstanceRing`).
         */
        static void refreshInstanceChunk( GPUInstance* pInstance, const GPUInstance* pPrevious, const GPUInstance* pRaw, const uint32* pSrcIndex,
                                          uint32 rawCount, InstanceRefreshChunk& chunk );

        /**
         * @brief 인스턴스 배열 링입니다. 되복사 없는 발행과 낡은 슬롯 따라잡기는 `GPUInstanceRing` 이 맡습니다.
         * @details 빌더는 주로 세 함수를 쓴다. `instanceWork()`(이번 프레임 쓰기 슬롯), `syncWriteSlotFromPublished()`(부분 갱신 전),
         *          `publishInstances()`(스냅샷에 포인터 넘기기)다. 이전 값은 `getPublished()` 로 읽는다. 규칙과 이유는 그 타입의 주석에 있다.
         */
        GPUInstanceRing _instanceRing;

        /** @brief 이번 프레임의 쓰기 슬롯을 반환합니다(`GPUInstanceRing::acquireWrite`). */
        vector<GPUInstance>& instanceWork() { return _instanceRing.acquireWrite(); }
        /** @brief 쓰기 슬롯을 발행합니다. 이번 빌드의 더티 구간을 이력으로 남깁니다. */
        void publishInstances();
        /** @brief 부분 갱신 전에 쓰기 슬롯을 마지막 발행본에 맞춥니다(`GPUInstanceRing::syncWriteFromPublished`). */
        void syncWriteSlotFromPublished() { _instanceRing.syncWriteFromPublished(); }

        /**
         * @brief `getInstances()[i]` 가 어느 후보에서 왔는지 나타냅니다(populateBatches 가 채웁니다).
         * @details 배치 구성이 그대로면 이 매핑도 그대로다. 그러면 배치를 다시 나눌 필요 없이 인스턴스
         *          값만 **제자리에서** 갱신하면 된다. 언리얼 GPUScene 이 프리미티브가 움직였을 때
         *          자료구조를 다시 만들지 않고 그 원소만 갱신하는 것과 같은 방식이다.
         */
        vector<uint32> _listInstanceSrcIndex;
        /// @brief 마지막 방출이 쓴 투명 정렬 순서입니다. 이 순서가 바뀌면 투명 꼬리를 다시 방출합니다(`rebuildTransparentTail`).
        vector<uint32> _listBuiltTransparentIdx;
        // 투명 꼬리를 자르는 위치(불투명 인스턴스 수 · 원소 항목 수)는 따로 들지 않는다. 경계는 스냅샷의 불투명 배치 수 하나이고
        // 나머지는 그 배치에서 끌어낸다(`GPUSceneSnapshot::getOpaqueInstanceCount`, `BatchElementTable::truncateToBatches`).
        /// @brief 마지막으로 본 `TextureCache::getReloadGeneration()` 입니다. 값이 다르면 머티리얼의 텍스처 인덱스를 새로 받게 합니다.
        uint32 _lastTextureReloadGeneration{ 0 };

        /**
         * @class BatchElementTable
         * @brief 배치마다 그 배치의 인스턴스가 쓰는 머티리얼 원소 인덱스(중복 없음)입니다. 배치 하나에 구간 하나이며 스냅샷의 배치 목록과 나란하다.
         * @details 회수 시계 스탬프는 **원소**에 찍는 것이라 인스턴스 8000개가 아니라 (배치, 원소) 쌍만 돌면 된다.
         *          합치기가 꺼져 있으면 배치당 원소 하나이고, 켜져 있어도 배치당 머티리얼 종류 수만큼이다.
         *          항목과 구간을 함께 고치는 길은 아래 함수뿐이다. 자르는 위치는 배치 수로만 받고 항목 수는 구간에서 끌어내므로
         *          두 목록이 어긋날 수 없다(전에는 불투명 항목 수를 따로 기록해 두고 그 값으로 잘랐다).
         */
        class BatchElementTable
        {
        public:
            /** @brief 원소 인덱스 목록입니다. 배치의 구간(`getBatchRanges`)으로 자릅니다. */
            const vector<uint32>& getElementIndices() const { return _listElementIndex; }
            /** @brief 배치마다 `getElementIndices` 의 구간입니다. 배치 번호와 같은 번호입니다. */
            const vector<GPUInstanceRun>& getBatchRanges() const { return _listBatchRange; }

            /** @brief 모두 지웁니다. */
            void clear()
            {
                _listElementIndex.clear();
                _listBatchRange.clear();
            }
            /** @brief 다음 배치의 빈 구간을 엽니다. 이후 `appendElement` 는 이 배치에 들어갑니다. */
            void beginBatch() { _listBatchRange.push_back( GPUInstanceRun{ static_cast<uint32>( _listElementIndex.size() ), 0 } ); }
            /** @brief 지금 배치에 원소를 붙입니다. 정렬되어 같은 원소가 연속으로 오므로 직전 것과 같으면 넣지 않습니다. */
            void appendElement( uint32 elementIndex )
            {
                SW_ASSERT( _listBatchRange.empty() == false );
                GPUInstanceRun& range = _listBatchRange.back();
                if ( range._count != 0 && _listElementIndex.back() == elementIndex )
                    return;
                _listElementIndex.push_back( elementIndex );
                ++range._count;
            }
            /** @brief 앞쪽 `batchCount` 개 배치만 남깁니다(투명 꼬리를 다시 붙이기 전). 항목은 남은 마지막 구간의 끝에서 자릅니다. 용량은 남습니다. */
            void truncateToBatches( uint32 batchCount )
            {
                SW_ASSERT( batchCount <= _listBatchRange.size() );
                const uint32 entryCount = ( batchCount == 0 ) ? 0u : _listBatchRange[batchCount - 1]._start + _listBatchRange[batchCount - 1]._count;
                _listElementIndex.resize( entryCount );
                _listBatchRange.resize( batchCount );
            }

        private:
            vector<uint32>         _listElementIndex;
            vector<GPUInstanceRun> _listBatchRange;
        };
        BatchElementTable _batchElementTable;

        /** @brief 마지막으로 반영한 프리미티브 집합 세대입니다. 달라졌으면 레지스트리가 바뀐 것입니다. */
        uint64 _lastPrimitiveSetGeneration{ 0 };
        float3 _lastCameraPos{};
        /** @brief 마지막 빌드가 쓴 깊이 축입니다(`GPUTransparentSorter::getAxis`). 다르면 정렬을 다시 합니다. */
        float3 _lastTransparentSortAxis{};
        uint8  _bMergeAcrossMaterials{ SW_FALSE };
        /// @brief 꼬리를 재방출하는 중이면 true 입니다. `emitBatch` 가 원소를 테이블에서 다시 조회하지 않고 `_listCandidateMaterialElement` 를 읽습니다.
        uint8 _bReuseMaterialElement{ SW_FALSE };
    };
} // namespace sw

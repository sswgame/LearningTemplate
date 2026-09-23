/**
 * @file GpuSceneBuilder.h
 * @brief 게임 스레드가 씬(MeshComponent)을 훑어 `GpuSceneSnapshot` 을 만드는 쪽.
 * @details 렌더 스레드 쪽(`GpuScene`)과는 스냅샷 타입으로만 만난다. 여기에는 GPU 핸들이 하나도 없고, 저쪽에는 씬이 없다.
 *          프레임 간에 유지되는 것은 재구축 판단용 캐시(후보 집합 · 카메라 · 세대)와 **머티리얼 원소 등록부**
 *          (언리얼 GPUScene 의 영속 ID 자리)다 — 스냅샷은 매 프레임 복사해 내보내고 정본은 여기 남는다.
 */
#pragma once
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

#include "Engine/EngineMinimal.h"
#include "Engine/Graphics/Renderer/Scene/GpuInstanceRing.h"
#include "Engine/Graphics/Renderer/Scene/GpuSceneSnapshot.h"

namespace sw
{
    class GpuUploadQueue;
    class Material;
    class MaterialInstance;
    class Mesh;
    class MeshComponent;
    class Scene;

    /**
     * @class GpuSceneBuilder
     * @brief 씬 → 스냅샷. 게임 스레드 전용이며 GPU 를 모른다.
     */
    struct PrimitiveInstanceEntry;

    class SW_API GpuSceneBuilder
    {
    public:
        GpuSceneBuilder() noexcept = default;
        ~GpuSceneBuilder()         = default;

        GpuSceneBuilder( GpuSceneBuilder&& other ) noexcept            = default;
        GpuSceneBuilder& operator=( GpuSceneBuilder&& other ) noexcept = default;

        GpuSceneBuilder( const GpuSceneBuilder& )            = delete;
        GpuSceneBuilder& operator=( const GpuSceneBuilder& ) = delete;

        /**
         * @brief 프리미티브가 이 수 이상이면 전체 수집의 채우기를 잡에 나눕니다.
         * @details 채우기는 프리미티브당 ~12 ns 라 8000 개가 직렬 100 us 다 — 잡 디스패치 바닥(~50 us)과 같은
         *          자릿수라 지금은 병렬이 86 us 로 비기는 수준이다. 프리미티브당 일이 늘면(LOD 선택·스키닝 바운드)
         *          바로 남는 구조라 문턱만 두고 유지한다. 부분 수집(더티 < 1/4)은 직렬이다 — 그쪽은 일이 작다.
         */
        static constexpr uint32 kParallelCollectPrimitiveCount = 4096;

        /** @brief 스냅샷·캐시·머티리얼 등록부를 비웁니다 (스냅샷이 든 소유도 여기서 놓인다). */
        void clear();
        /**
         * @brief MeshComponent를 수집해 CPU 스냅샷을 만듭니다 (게임 스레드).
         * @details 내용·카메라가 이전과 같으면 재구축을 건너뜁니다. 카메라만 바뀌면
         *          transparent 재정렬 + 배치 재구성만 합니다.
         * @note 전부 이 스레드에서 합니다. 인스턴스 채우기를 워커로 나눠 봤지만 **모든 크기에서 졌다** —
         *       원소당 일이 필드 몇 개 복사라 디스패치·대기 비용이 일 자체보다 훨씬 크다
         *       (400개 358us → 1us, 20,000개 219us → 99us). 다시 나누자고 제안하기 전에 그 숫자를 볼 것.
         */
        void buildFromScene( Scene* pScene, const float3& cameraPos );
        /**
         * @brief 스냅샷(GT → RT 로 옮겨지는 전부)을 outSnapshot 으로 복사합니다. GPU 쪽은 타입상 실릴 수 없습니다.
         * @details GT 가 프레임마다 RenderFramePacket 에 담을 때 쓴다. 호출 후 dirty 플래그는 소비된 것으로 보고
         *          0 으로 되돌린다(GpuScene::upload 의 재업로드 생략과 대칭되는 GT 쪽 소비 시점).
         */
        void exportCpuSnapshot( GpuSceneSnapshot& outSnapshot );
        /**
         * @brief 이번 빌드가 그릴 메시 중 아직 안 올라간 것을 업로드 큐에 올립니다 (게임 스레드).
         * @details 렌더 스레드가 처음 그릴 때 만들던 것을 **그리기 전에** 만들어 두기 위한 것이다. 큐가 만들어
         *          두면 RT 의 `Mesh::initRhi` 호출은 핸들을 읽는 일이 되고, 큐가 못 다룬 것은 RT 가 예전처럼
         *          그 자리에서 만든다 — 앞당기는 장치이지 유일한 통로가 아니다.
         */
        void requestGpuUploads( GpuUploadQueue& queue ) const;
        /**
         * @brief 불투명 배치를 머티리얼이 아니라 **셰이더 타입**(머티리얼 셰이더 경로)으로 묶을지 정합니다 (언리얼 GPUScene).
         * @details 머티리얼 파라미터는 인스턴스의 materialIndex 로 버퍼에서 읽으므로, 텍스처를 인덱스로 고를 수 있는 백엔드
         *          (DX12/Vulkan 텍스처 배열)에서는 같은 메시·같은 셰이더면 머티리얼이 달라도 한 드로우다. DX11/GL 은 머티리얼
         *          텍스처를 t5..t8 슬롯에 걸어야 해서 배치가 머티리얼 단위로 남는다. FrameRenderer/EngineLoop 가 디바이스
         *          caps(supportsNativeBindlessSampling)로 정한다. 바꾸면 다음 buildFromScene 이 다시 묶는다.
         */
        void setMergeBatchesAcrossMaterials( bool bMerge );

        /** @brief 인스턴스 목록을 반환합니다. */
        const vector<GpuInstance>& getInstances() const { return _snapshot.getInstances(); }
        /** @brief 불투명 배치를 반환합니다. */
        const vector<GpuMeshBatch>& getOpaqueBatches() const { return _snapshot._listOpaqueBatch; }
        /** @brief 투명 배치를 반환합니다. */
        const vector<GpuMeshBatch>& getTransparentBatches() const { return _snapshot._listTransparentBatch; }
        /** @brief 셰이더 타입별 머티리얼 데이터 그룹 (CPU 스냅샷). */
        const vector<GpuMaterialGroup>& getMaterialGroups() const { return _snapshot._listMaterialGroup; }
        /** @brief 퍼뮤테이션 하나를 얻습니다. 인덱스가 없으면 nullptr 입니다. */
        const GpuShaderPermutation* findShaderPermutation( uint32 index ) const
        {
            const vector<GpuShaderPermutation>* pList = _snapshot._pListShaderPermutation.get();
            return ( pList != nullptr && index < pList->size() ) ? &( *pList )[index] : nullptr;
        }
        /** @brief 마지막 buildFromScene이 CPU 스냅샷을 바꿨으면 true. */
        bool isCpuSnapshotDirty() const { return _snapshot._bCpuDirty != SW_FALSE; }

    private:
        /** @brief 수집된 인스턴스를 배치로 묶습니다. */
        void buildBatches();
        /**
         * @brief 정렬된 투명 후보를 배치로 방출합니다 (buildBatches 의 뒷부분). 불투명 뒤에 이어 붙는다.
         * @details 전체 재구축과 **투명 꼬리만 다시 짓기**(`rebuildTransparentTail`)가 같은 이 함수를 쓴다.
         */
        void emitTransparentBatches();
        /**
         * @brief 투명 정렬 순서만 바뀐 프레임에 불투명 접두부는 두고 투명 꼬리만 다시 방출합니다.
         * @details 투명 인스턴스는 불투명 뒤에 연속으로 앉는다(`_opaqueInstanceCount`). 예전에는 투명 순서가
         *          바뀌면 `refreshInstancesInPlace` 가 실패해 **불투명 8000 개까지** 통째로 다시 지었다 —
         *          투명 큐브가 위아래로 흔들리는 벤치에서 거의 매 프레임 그랬다(배치 단계 최소 139 · p50 327 us).
         *          접두부는 제자리 갱신이 이미 끝났고, 여기서는 꼬리를 잘라 내고 새 순서로 다시 붙인다.
         * @note **접두부 갱신과 같은 병렬 구간에서 돈다**(`refreshInstancesInPlace` 의 블록 0) — 워커에서 돌 수도 있다.
         *       쓰기 슬롯의 [0, `_opaqueInstanceCount`) 는 워커들이 포인터로 쓰는 중이라 건드리지 않는다: 자르고 뒤에 붙일 뿐이고,
         *       붙일 자리는 부르는 쪽이 미리 잡아 둔다(재할당이 없다). 접두부 청크가 읽는 것(발행본 · raw · 원천 인덱스의 접두부)도
         *       쓰지 않는다. 여기에 접두부를 만지는 일을 더하면 레이스다.
         */
        void rebuildTransparentTail();
        /** @brief 오래 안 쓰인 머티리얼 원소를 회수해 자리를 프리리스트로 돌립니다 (인덱스는 옮기지 않는다). */
        void retireUnusedMaterialElements();
        /** @brief 머티리얼 원소 레지스트리를 통째로 비웁니다 (그룹 기준이 바뀌었을 때). */
        void resetMaterialRegistry();
        /** @brief 머티리얼의 셰이더 타입 그룹 인덱스를 찾거나 만듭니다 (buildBatches 안). 머티리얼이 없으면 kInvalidMaterialGroup. */
        uint32 materialGroupFor( const Material* pMaterial );
        /**
         * @brief (머티리얼, 인스턴스) 의 퍼뮤테이션 해시 — 셰이더 경로와 정적 define 을 함께 봅니다.
         * @details 인스턴스는 키워드를 덮어쓸 수 있으므로 인스턴스가 있으면 그 해시를 쓴다(부모 것을 이미 포함한다).
         */
        static uint64 permutationHashFor( const Material* pMaterial, const MaterialInstance* pInstance );
        /** @brief 퍼뮤테이션 인덱스를 찾거나 만듭니다 (buildBatches 안). 머티리얼이 없으면 kInvalidShaderPermutation. */
        uint32 shaderPermutationFor( const Material* pMaterial, const MaterialInstance* pInstance );
        /**
         * @brief (머티리얼, 인스턴스) 쌍을 그룹에 넣고 원소 인덱스(materialIndex)를 돌려줍니다 (buildBatches 안).
         * @details 같은 쌍은 같은 원소를 공유한다. 인스턴스마다 부른다 — 배치를 셰이더 타입으로 합치면 한 배치 안에 여러 원소가 산다.
         */
        uint32 assignMaterialElement( const shared_ptr<Material>& material, const shared_ptr<MaterialInstance>& instance, uint32 groupIndex );
        /**
         * @brief 배치 키에 쓸 머티리얼 — 합치기가 켜져 있으면 같은 퍼뮤테이션의 대표 머티리얼, 아니면 그 머티리얼 자신.
         * @details 대표는 재구축마다 처음 만난 머티리얼이다(_mapShaderRepresentative). 재구축 여부 판단은 후보 자체를 비교하므로 대표가 바뀌어도 무관하다.
         *          불투명 나누기만 부른다 — 투명 방출은 해시가 같다는 것까지 본 뒤라 맵 없이 같은 답을 낸다(`emitTransparentBatches`).
         *          대표는 **퍼뮤테이션 단위**다 — 같은 .hlsl 이라도 정적 스위치가 다르면 다른 셰이더이므로 합칠 수 없다.
         */
        Material* batchKeyMaterial( Material* pMaterial, uint64 permutationHash );
        /**
         * @brief 투명 인덱스를 카메라 거리순(먼→가까운)으로 정렬합니다.
         * @details 지난 프레임 순서에서 삽입 정렬로 출발하고, 옮김이 원소당 `kTransparentInsertionMovesPerElement` 를 넘으면
         *          std::sort 로 넘긴다. 순서는 전순서라 어느 쪽이든 결과가 같다.
         */
        void sortTransparent( const float3& cameraPos );
        /**
         * @brief 투명 정렬이 삽입 정렬로 버틸 옮김 예산 — 원소당 칸 수.
         * @details 한 프레임에 조금씩 움직이는 씬은 뒤집힌 쌍이 원소 수보다 훨씬 적다. 이 예산을 넘는 프레임(카메라가 크게
         *          돌았다 · 나누기를 다시 했다)은 삽입 정렬을 멈추고 std::sort 가 마저 한다 — 최악이 N log N 에 예산만큼 더한 값이다.
         */
        static constexpr size_t kTransparentInsertionMovesPerElement = 8;
        /** @brief opaque/transparent 인덱스 테이블을 후보에서 다시 만듭니다. */
        void rebuildPartitionTables();
        /**
         * @brief 배치를 다시 나누지 않고 인스턴스 값만 제자리에서 갱신합니다.
         * @details 배치 키가 그대로고 투명 정렬 순서도 그대로일 때만 쓸 수 있다. 그 두 조건이 맞으면
         *          `getInstances()` 의 자리 배치와 각 원소의 `_meshBatchIndex`/`_materialIndex` 가 그대로라,
         *          바뀐 것은 트랜스폼과 바운드뿐이다.
         * @return 갱신했으면 true, 조건이 안 맞아 전체 재구축이 필요하면 false.
         */
        bool refreshInstancesInPlace( bool bPartialCollect );
        /** @brief 캐시 무효화. */
        void invalidateBuildCache();

        /** @brief 정본 스냅샷 — 매 프레임 `exportCpuSnapshot` 이 복사해 내보낸다. 퍼뮤테이션 표·머티리얼 그룹은 여기서 계속 자란다. */
        GpuSceneSnapshot _snapshot;
        /// @brief 셰이더 경로 → `_snapshot._listMaterialGroup` 인덱스. 예전엔 배치마다 그룹 목록을 string 비교로 훑었다.
        unordered_map<string, uint32> _mapShaderPathToGroup;

        /**
         * @brief 머티리얼 그룹의 원소 인덱스 표 — `_snapshot._listMaterialGroup` 과 같은 인덱스로 나란히 간다.
         * @details 스냅샷의 그룹에는 RT 가 읽는 것(경로·원소)만 남기고 표는 여기 둔다. 프레임마다 패킷으로 복사되지 않는다.
         */
        struct MaterialGroupState
        {
            unordered_map<GpuMaterialElementKey, uint32, GpuMaterialElementKeyHash> _mapEntryToIndex;
            /// @brief 원소별 마지막으로 쓰인 빌드 번호 — 오래 안 쓰인 원소를 회수하는 기준.
            vector<uint64> _listEntryLastSeenBuild;
            /// @brief 회수된 원소 자리. **인덱스를 옮기지 않고** 재사용한다 — 옮기면 영속 ID 가 아니게 된다.
            vector<uint32> _listFreeEntry;
            /// @brief 직전 조회 결과 — 배치 안의 인스턴스는 정렬돼 있어 대부분 같은 원소를 연속으로 묻는다.
            GpuMaterialElementKey _lastKey{};
            uint32                _lastIndex{ 0 };
            uint8                 _bHasLast{ SW_FALSE };
        };
        vector<MaterialGroupState> _listMaterialGroupState;
        /// @brief 빌드 번호. 원소가 마지막으로 쓰인 시점을 재는 데만 쓴다(회수 판정).
        uint64 _buildCounter{ 0 };
        /**
         * @brief 마지막 수집 때 본 퍼뮤테이션 세대(`MaterialUtil::getPermutationGeneration`).
         * @details 정지한 씬에서 머티리얼의 정적 스위치·키워드를 바꾸면 프리미티브는 하나도 더러워지지
         *          않는다. 이 값이 다르면 "아무도 안 움직였다" 는 건너뛰기를 하지 않는다.
         */
        uint64 _lastPermutationGeneration{ 0 };
        /**
         * @brief 재구축 중 **퍼뮤테이션 해시** → 대표 머티리얼 (배치 키 합치기용).
         * @details 예전엔 셰이더 **경로**가 키였다. 그러면 forwardlit.hlsl 을 쓰는 유리 머티리얼과 불투명 머티리얼이
         *          한 대표로 접혀 같은 배치가 되고, 배치는 PSO 하나로 그리므로 한쪽 퍼뮤테이션이 통째로 사라졌다.
         */
        unordered_map<uint64, Material*> _mapShaderRepresentative;
        /// @brief 퍼뮤테이션 해시 → `_snapshot._listShaderPermutation` 인덱스.
        unordered_map<uint64, uint32> _mapPermutationToIndex;

        /// buildFromScene에서 재사용해 프레임당 힙 할당을 줄입니다.
        struct DrawCandidate
        {
            float4x4                     _world{};
            float3                       _boundsCenter{};
            float32                      _boundsRadius{ 1.0f };
            shared_ptr<Mesh>             _mesh;
            shared_ptr<Material>         _material;
            shared_ptr<MaterialInstance> _instance;
            uint32                       _blendMode{ 0 };
            /// @brief GPU 회전 애니메이션 시드 (0 = 없음). MeshComponent 가 준다 → GpuInstance::_spinSeed.
            uint32 _spinSeed{ 0 };
            /**
             * @brief (셰이더 경로 + define) 해시 — **어느 PSO 로 그릴지**를 정하는 값.
             * @details 배치 키에 들어가야 한다. 머티리얼·인스턴스 **포인터가 그대로여도** 인스턴스의
             *          키워드·멀티컴파일·품질을 바꾸면 이 값이 바뀌고, 그러면 다른 셰이더로 그려야 한다.
             *          예전에는 이 값이 키에 없어서 `hasSameBatchKey` 가 "그대로" 라고 답했고, 배치가
             *          다시 나뉘지 않아 **바뀐 퍼뮤테이션이 화면에 반영되지 않았다**(런타임에 정적 스위치를
             *          바꾸는 길이 조용히 죽어 있었다). 수집에서 한 번 구해 두면 나누기·정렬도 다시 구하지 않는다.
             */
            uint64 _permutationHash{ 0 };

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
            bool operator==( const DrawCandidate& other ) const
            {
                return _mesh == other._mesh && _material == other._material && _instance == other._instance &&
                       _blendMode == other._blendMode && _spinSeed == other._spinSeed &&
                       _permutationHash == other._permutationHash &&
                       Memory::compare( &_world, &other._world, sizeof( _world ) ) == 0 &&
                       Memory::compare( &_boundsCenter, &other._boundsCenter, sizeof( _boundsCenter ) ) == 0 &&
                       Memory::compare( &_boundsRadius, &other._boundsRadius, sizeof( _boundsRadius ) ) == 0;
            }
            /** @brief operator== 의 부정입니다. */
            bool operator!=( const DrawCandidate& other ) const { return ( *this == other ) == false; }

            /**
             * @brief 배치가 묶이는 기준(메시·머티리얼·인스턴스·블렌드)이 같은지. 트랜스폼은 보지 않습니다.
             * @details 불투명 배치는 이 네 가지로만 나뉘고 정렬된다. 물체가 **움직이기만** 했다면
             *          배치 구성은 한 글자도 바뀌지 않으므로 다시 나누고 다시 정렬할 이유가 없다.
             *          움직이는 씬에서 남는 유일한 O(N log N) 이 그 정렬이다.
             */
            bool hasSameBatchKey( const DrawCandidate& other ) const
            {
                return _mesh == other._mesh && _material == other._material && _instance == other._instance &&
                       _blendMode == other._blendMode && _permutationHash == other._permutationHash;
            }
        };

        /**
         * @brief 프리미티브 하나를 후보로 채웁니다. 그릴 수 없으면(안 보임·비활성·메시 없음) false.
         * @details **전체 수집과 부분 수집이 같은 이 함수를 쓴다** — 채우는 규칙이 두 곳으로 갈리면
         *          부분 갱신만 낡은 필드를 남기고, 그 화면은 대부분의 프레임에서 멀쩡해 보인다.
         */
        bool fillCandidateFromPrimitive( MeshComponent* pMeshComp, Scene* pScene, DrawCandidate& cand );
        /** @brief 인스턴스 배치의 항목 하나를 후보로 채웁니다 — 메시 컴포넌트 판과 같은 규칙, 소유는 배치의 것. */
        bool fillCandidateFromInstanceEntry( const PrimitiveInstanceEntry& entry, Scene* pScene, DrawCandidate& cand );
        /**
         * @brief 후보의 머티리얼과 블렌드 모드 — 메시 컴포넌트 판과 인스턴스 배치 판이 같은 규칙이다.
         * @details 머티리얼이 없으면 씬 기본 머티리얼(언리얼의 기본 머티리얼). **블렌드 모드는 머티리얼의 성질이다** — 언리얼도
         *          블렌드 모드가 머티리얼 에셋에 있고, 그 값이 셰이더 퍼뮤테이션(불투명/반투명)을 가른다. 메시가 뒤집을 수 있게 두면
         *          불투명으로 컴파일된 머티리얼을 블렌딩으로 그리는 어긋난 상태가 만들어진다. 인스턴스만 붙은 메시는 **인스턴스의
         *          부모 머티리얼**이 정본이고(인스턴스는 값만 덮어쓴다), 둘 다 없을 때만 폴백을 쓴다 — 머티리얼이 없는 디버그 ·
         *          픽스처 메시가 그 경우다. (예전에는 인스턴스를 이 판단 **뒤에** 채워서 이 폴백이 한 번도 걸리지 않았다.)
         */
        static void fillCandidateMaterial( DrawCandidate& cand, Material* pMaterial, Scene* pScene, uint32 fallbackBlendMode );
        /** @brief 후보의 퍼뮤테이션 해시를 찍습니다 — 게임 스레드 전용(머티리얼의 지연 캐시를 건드린다). */
        static void stampPermutationHash( DrawCandidate& cand );
        /** @brief 후보에서 GPU 인스턴스 페이로드(월드·바운드·블렌드·시드)를 채웁니다. 배치·머티리얼 인덱스는 손대지 않는다. */
        static void fillPayload( const DrawCandidate& cand, GpuInstance& outInstance );
        /**
         * @brief 후보 [begin,end) 를 배치 하나로 방출하고 인스턴스를 작업 배열에 붙입니다 (buildBatches 안).
         * @details 불투명과 투명이 **같은 함수**를 쓴다. 예전에는 둘이 같은 40여 줄을 따로 들고 있어 한쪽에 넣은
         *          고침(역매핑·회전 수·머티리얼 원소)이 다른 쪽에 안 가는 모양이었다. 다른 것은 인자로 준다 —
         *          배치가 실을 머티리얼·인스턴스(불투명은 합치기 대표, 투명은 머리 후보의 것)와 블렌드 모드.
         */
        void emitBatch( const uint32* pSrcIdx, uint32 begin, uint32 end, RHIBlendMode blendMode, const shared_ptr<Material>& material,
                        const shared_ptr<MaterialInstance>& instance );
        /**
         * @brief 후보 인덱스 -> 마지막 방출이 그 후보에 준 머티리얼 원소 인덱스.
         * @details 원소 인덱스는 (머티리얼, 인스턴스) 쌍에 영속이라 배치 키가 그대로인 프레임에는 다시 묻지 않아도 된다.
         *          투명 꼬리를 다시 지을 때 인스턴스 2000 개가 해시 표를 다시 묻던 것(깊이순이라 원소가 번갈아 와서
         *          직전 조회 캐시도 못 맞힌다)을 배열 읽기 하나로 바꾼다. 전체 재구축이 채우고, 꼬리 재방출이 읽는다.
         */
        vector<uint32> _listCandidateMaterialElement;
        /// @brief 꼬리 재방출 중이면 true — `emitBatch` 가 원소를 표에서 다시 묻지 않고 `_listCandidateMaterialElement` 를 읽는다.
        uint8 _bReuseMaterialElement{ SW_FALSE };

        /**
         * @brief 전체 수집이 프리미티브 칸마다 남기는 표시 (비트).
         * @details 워커가 자기 칸을 채우면서 **지난 후보와의 비교까지** 한 번에 끝낸다. 예전에는 직렬 패스 둘이
         *          같은 8000 칸을 따로 지나갔다 — 해시 찍기(전부, 머티리얼 게터 둘씩) · 배치 키 비교(전부).
         *          해시는 (머티리얼, 인스턴스, 퍼뮤테이션 세대) 의 함수라 셋이 같으면 지난 값이 그대로 맞다.
         *          Release · 큐브 8000 전부 이동 · 300 프레임 ×2: 빌드 p50 786/851 → 720/720 us, 배치 키 비교
         *          65~81 → 0, 수집은 늘지 않았다(245/262 → 229/245). raw 채우기는 여기 넣지 않는다 — `buildFromScene` 주석.
         */
        enum CollectFlag : uint8
        {
            kCollectIncluded    = 1u << 0, ///< 후보에 실린다
            kCollectNeedsStamp  = 1u << 1, ///< 해시를 직렬 구간에서 찍어야 한다 (머티리얼·인스턴스·세대 중 하나가 달라졌다)
            kCollectKeySame     = 1u << 2, ///< 지난 후보와 배치 키가 같다
            kCollectContentSame = 1u << 3, ///< 지난 후보와 내용이 전부 같다
        };

        /** @brief 후보 배열에 실리지 않은 프리미티브 표시. */
        static constexpr uint32 kInvalidCandidateIndex = 0xFFFFFFFFu;
        /** @brief 이번 프레임에 "바뀌었다"고 표시된 프리미티브의 등록부 인덱스. */
        vector<uint32> _listDirtyPrimitive;
        /** @brief 등록부 인덱스 -> 후보 인덱스 (`kInvalidCandidateIndex` = 후보에 안 실림). */
        vector<uint32> _listPrimitiveToCandidate;
        /// @brief 전체 수집이 프리미티브 번호 자리에 남긴 `CollectFlag` — 앞으로 당길 때 읽는다.
        vector<uint8> _listCollectFlag;
        /** @brief 후보 인덱스 -> 인스턴스 슬롯 (`kInvalidCandidateIndex` = 인스턴스 없음). `_listInstanceSrcIndex` 의 역이다. */
        vector<uint32> _listCandidateToInstance;
        /** @brief 마지막 수집이 만든 후보 수 — 부분 수집이 자리 수를 그대로 이어받는다. */
        size_t _lastCandidateCount{ 0 };
        /** @brief 부분 수집이 한 프리미티브를 채워 볼 임시 자리 (프레임마다 할당하지 않는다). */
        DrawCandidate _candidateProbe;

        vector<DrawCandidate> _listScratchCandidate;
        /** @brief 마지막으로 반영된 후보 집합. 다음 프레임의 변경 판단 기준이자 scratch 버퍼의 재활용처입니다. */
        vector<DrawCandidate> _listBuiltCandidate;
        vector<GpuInstance>   _listScratchRaw;

        struct SortKey
        {
            Mesh*             _pMesh{ nullptr };
            Material*         _pMaterial{ nullptr };
            MaterialInstance* _pInstance{ nullptr };
            /**
             * @brief (셰이더 경로 + define) 해시 — 배치가 쓸 PSO 를 정한다.
             * @details **대표 머티리얼 포인터로는 대신할 수 없다.** 합치기가 켜지면 대표는 "이 퍼뮤테이션을
             *          처음 들고 온 머티리얼" 인데, 퍼뮤테이션이 **인스턴스**에서 오면 부모가 같아 대표도
             *          같아진다 — 서로 다른 셰이더로 그려야 할 것들이 한 배치로 접힌다. 해시를 키에 직접
             *          넣어야 갈린다(런타임에 인스턴스의 정적 스위치를 바꾸는 길이 여기서 죽어 있었다).
             */
            uint64 _permutationHash{ 0 };
            /** @brief 메시·머티리얼·인스턴스·퍼뮤테이션이 같은지 비교합니다. */
            bool operator==( const SortKey& other ) const
            {
                return _pMesh == other._pMesh && _pMaterial == other._pMaterial && _pInstance == other._pInstance &&
                       _permutationHash == other._permutationHash;
            }
        };

        struct SortEntry
        {
            SortKey _key;
            uint32  _srcIdx{ 0 };
        };

        vector<SortEntry> _listScratchOpaqueEntry;
        /// @brief 정렬된 불투명 항목의 후보 인덱스만 뽑은 것 — `emitBatch` 가 투명 쪽과 같은 모양으로 받는다.
        vector<uint32> _listScratchOpaqueIdx;
        vector<uint32> _listScratchTransparentIdx;
        /**
         * @brief 부분 업로드로 나눌 구간 수 상한. 넘으면 전체를 올린다.
         *
         * @details 구간들은 **한 번의 호출**로 올라가므로(`updateStructuredBufferRegions`) 비용이 구간
         *          수에 비례하지 않는다. 그래도 상한을 두는 이유는 두 가지다 — 구간 목록 자체가 스냅샷에
         *          실려 복사되고, 변경이 배열 전체에 고르게 흩어졌다면 통째로 올리는 것이 단순하고 싸다.
         *
         *          (구간마다 따로 부르던 때는 이 값이 성능을 직접 좌우했다: DX12 · 8000 인스턴스에서
         *          구간 1/2/5/20/64 가 17/34/45/113/209 us 였고 통째로는 ~100 us 였다.)
         */
        static constexpr size_t kMaxDirtyInstanceRun = 256;

        /**
         * @struct InstanceRefreshChunk
         * @brief 전체 제자리 갱신을 청크로 나눠 돌릴 때 청크 하나의 결과.
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
            GpuInstanceRun _arrRun[kMaxDirtyInstanceRun];
        };
        vector<InstanceRefreshChunk> _listRefreshChunk;

        /**
         * @brief 청크 하나의 슬롯 구간을 갱신합니다 (워커에서 돈다 — 포인터만 만진다).
         * @details 이전 값은 `pPrevious`(마지막 발행본)에서 읽고 결과는 `pInstance`(이번 쓰기 슬롯)에 통째로 쓴다 —
         *          두 배열은 다른 메모리다(아래 링 주석).
         */
        static void refreshInstanceChunk( GpuInstance* pInstance, const GpuInstance* pPrevious, const GpuInstance* pRaw, const uint32* pSrcIndex,
                                          uint32 rawCount, InstanceRefreshChunk& chunk );

        /**
         * @brief 인스턴스 배열 링 — 되복사 없는 발행과 낡은 슬롯 따라잡기는 `GpuInstanceRing` 이 든다.
         * @details 빌더는 세 창구만 쓴다: `instanceWork()`(이번 프레임 쓰기 슬롯) · `syncWriteSlotFromPublished()`(부분 갱신 전)
         *          · `publishInstances()`(스냅샷에 포인터 넘기기). 규칙과 이유는 그 타입의 주석에 있다.
         */
        GpuInstanceRing _instanceRing;

        /** @brief 이번 프레임의 쓰기 슬롯 (`GpuInstanceRing::acquireWrite`). */
        vector<GpuInstance>& instanceWork() { return _instanceRing.acquireWrite(); }
        /** @brief 쓰기 슬롯을 발행합니다 — 이번 빌드의 더티 구간을 이력으로 남긴다. */
        void publishInstances();
        /** @brief 부분 갱신 전에 쓰기 슬롯을 마지막 발행본에 맞춥니다 (`GpuInstanceRing::syncWriteFromPublished`). */
        void syncWriteSlotFromPublished() { _instanceRing.syncWriteFromPublished(); }

        /**
         * @brief `getInstances()[i]` 가 어느 후보에서 왔는지 (buildBatches 가 채운다).
         * @details 배치 구성이 그대로면 이 매핑도 그대로다. 그러면 배치를 다시 나눌 필요 없이 인스턴스
         *          값만 **제자리에서** 갱신하면 된다 — 언리얼 GPUScene 이 프리미티브가 움직였을 때
         *          자료구조를 다시 만들지 않고 그 원소만 갱신하는 것과 같은 자리다.
         */
        vector<uint32> _listInstanceSrcIndex;
        /// @brief 마지막 방출이 쓴 투명 정렬 순서. 이게 바뀌면 투명 꼬리를 다시 방출한다(`rebuildTransparentTail`).
        vector<uint32> _listBuiltTransparentIdx;
        /// @brief 마지막 전체 빌드에서 불투명이 차지한 인스턴스 수 · 배치 수 · 원소 표 항목 수 — 투명 꼬리를 자르는 자리.
        uint32 _opaqueInstanceCount{ 0 };
        uint32 _opaqueBatchCount{ 0 };
        uint32 _opaqueElementEntryCount{ 0 };

        /** @brief 투명 정렬 키 — (카메라 거리², 후보 인덱스). 정렬 전에 한 번 계산해 둔다. */
        struct TransparentSortKey
        {
            float32 _distanceSquared{ 0.0f };
            uint32  _candidateIndex{ 0 };
        };
        /**
         * @brief 투명 정렬의 작업 배열.
         * @details 예전에는 비교 함수가 원소마다 raw 에서 바운드를 읽어 거리를 **다시** 구했다 — 투명 2000 개면
         *          비교 22000 번에 무작위 읽기 44000 번이고, 그 raw 는 다른 코어가 방금 쓴 것이라 원격 캐시에서 왔다.
         *          키를 한 번 계산해 두면 읽기는 2000 번이고 정렬은 12 바이트 연속 배열 위에서 돈다.
         */
        vector<TransparentSortKey> _listTransparentSortKey;

        /**
         * @brief 배치마다 그 배치의 인스턴스가 쓰는 머티리얼 원소 인덱스(중복 없이) — `_listBatchElementRange` 로 자른다.
         * @details 회수 시계 도장은 **원소**에 찍는 것이라 인스턴스 8000 개가 아니라 (배치, 원소) 쌍만 돌면 된다.
         *          합치기가 꺼져 있으면 배치당 원소 하나, 켜져 있어도 배치당 머티리얼 종류 수다.
         */
        vector<uint32>         _listBatchElementIndex;
        vector<GpuInstanceRun> _listBatchElementRange;

        float3 _lastCameraPos{};
        /** @brief 마지막으로 반영한 프리미티브 집합 세대. 달라졌으면 등록부가 바뀐 것. */
        uint64 _lastPrimitiveSetGeneration{ 0 };
        uint8  _bMergeAcrossMaterials{ SW_FALSE };
    };
} // namespace sw

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
#include "Engine/Graphics/Renderer/Scene/GpuSceneSnapshot.h"

namespace sw
{
    class GpuUploadQueue;
    class Material;
    class MaterialInstance;
    class Mesh;
    class Scene;

    /**
     * @class GpuSceneBuilder
     * @brief 씬 → 스냅샷. 게임 스레드 전용이며 GPU 를 모른다.
     */
    class SW_API GpuSceneBuilder
    {
    public:
        GpuSceneBuilder() noexcept = default;
        ~GpuSceneBuilder()         = default;

        GpuSceneBuilder( GpuSceneBuilder&& other ) noexcept            = default;
        GpuSceneBuilder& operator=( GpuSceneBuilder&& other ) noexcept = default;

        GpuSceneBuilder( const GpuSceneBuilder& )            = delete;
        GpuSceneBuilder& operator=( const GpuSceneBuilder& ) = delete;

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
            return ( index < _snapshot._listShaderPermutation.size() ) ? &_snapshot._listShaderPermutation[index] : nullptr;
        }
        /** @brief 마지막 buildFromScene이 CPU 스냅샷을 바꿨으면 true. */
        bool isCpuSnapshotDirty() const { return _snapshot._bCpuDirty != SW_FALSE; }

    private:
        /** @brief 수집된 인스턴스를 배치로 묶습니다. */
        void buildBatches();
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
         *          대표는 **퍼뮤테이션 단위**다 — 같은 .hlsl 이라도 정적 스위치가 다르면 다른 셰이더이므로 합칠 수 없다.
         */
        Material* batchKeyMaterial( Material* pMaterial, uint64 permutationHash );
        /** @brief 투명 인덱스를 카메라 거리순(먼→가까운)으로 정렬합니다. */
        void sortTransparent( const float3& cameraPos );
        /** @brief opaque/transparent 인덱스 테이블을 후보에서 다시 만듭니다. */
        void rebuildPartitionTables();
        /** @brief 직전 후보와 배치 키가 모두 같은지(= 트랜스폼만 달라졌는지) 확인합니다. */
        bool hasSameBatchKeysAsBuilt() const;
        /**
         * @brief 배치를 다시 나누지 않고 인스턴스 값만 제자리에서 갱신합니다.
         * @details 배치 키가 그대로고 투명 정렬 순서도 그대로일 때만 쓸 수 있다. 그 두 조건이 맞으면
         *          `getInstances()` 의 자리 배치와 각 원소의 `_meshBatchIndex`/`_materialIndex` 가 그대로라,
         *          바뀐 것은 트랜스폼과 바운드뿐이다.
         * @return 갱신했으면 true, 조건이 안 맞아 전체 재구축이 필요하면 false.
         */
        bool refreshInstancesInPlace();
        /** @brief 캐시 무효화. */
        void invalidateBuildCache();

        /** @brief 정본 스냅샷 — 매 프레임 `exportCpuSnapshot` 이 복사해 내보낸다. 퍼뮤테이션 표·머티리얼 그룹은 여기서 계속 자란다. */
        GpuSceneSnapshot _snapshot;
        /// @brief 셰이더 경로 → `_snapshot._listMaterialGroup` 인덱스. 예전엔 배치마다 그룹 목록을 string 비교로 훑었다.
        unordered_map<string, uint32> _mapShaderPathToGroup;
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
        vector<uint32>    _listScratchTransparentIdx;
        /**
         * @brief 인스턴스를 짓는 **작업 배열**. 스냅샷에는 다 지은 뒤 `shared_ptr` 로 발행한다.
         * @details 발행은 옮기기라서 그 뒤 이것은 비어 있다. 제자리 갱신(`refreshInstancesInPlace`)이
         *          이전 값을 필요로 할 때만 발행본에서 되돌려 받는다 — 그래서 내용이 그대로인
         *          프레임에는 인스턴스 배열이 **한 번도 복사되지 않는다.**
         */
        vector<GpuInstance> _listInstanceWork;

        /**
         * @brief `getInstances()[i]` 가 어느 후보에서 왔는지 (buildBatches 가 채운다).
         * @details 배치 구성이 그대로면 이 매핑도 그대로다. 그러면 배치를 다시 나눌 필요 없이 인스턴스
         *          값만 **제자리에서** 갱신하면 된다 — 언리얼 GPUScene 이 프리미티브가 움직였을 때
         *          자료구조를 다시 만들지 않고 그 원소만 갱신하는 것과 같은 자리다.
         */
        vector<uint32> _listInstanceSrcIndex;
        /// @brief 마지막 전체 빌드가 쓴 투명 정렬 순서. 이게 바뀌면 제자리 갱신을 쓸 수 없다.
        vector<uint32> _listBuiltTransparentIdx;

        float3 _lastCameraPos{};
        /** @brief 마지막으로 반영한 프리미티브 집합 세대. 달라졌으면 등록부가 바뀐 것. */
        uint64 _lastPrimitiveSetGeneration{ 0 };
        uint8  _bMergeAcrossMaterials{ SW_FALSE };
    };
} // namespace sw

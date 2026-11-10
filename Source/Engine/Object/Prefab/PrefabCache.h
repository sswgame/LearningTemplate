/**
 * @file PrefabCache.h
 * @brief 프리팹 로드 캐시와 스폰입니다 — 로드 작업 · 캐시 · 스폰 · 중첩 스폰 · 펼치기 · 쿠킹.
 * @details 에셋 형식은 `PrefabAsset.h` 다.
 */
#pragma once
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

#include "Engine/Asset/AssetHandle.h"
#include "Engine/Asset/Cache/AssetCacheBase.h"
#include "Engine/Content/AssetID.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Object/Prefab/PrefabOverrides.h"

namespace sw
{
    /**
     * @class PrefabLoadOperation
     * @brief 프리팹 로드 하나의 공유 상태입니다. 로드 단계에서 파일을 읽고 중첩 원본을 확인해 캐시에 넣습니다(완료 단계 없음).
     * @details 로드 작업은 캐시를 원시 포인터로 가리킨다 — 캐시 소멸자가 진행 중인 로드를 모두 기다리므로(`~PrefabCache`) 단계를 실행하는 동안 캐시는 살아 있다.
     */
    class SW_API PrefabLoadOperation final : public AssetLoadOperation
    {
    public:
        using AssetType = const PrefabAsset;

        /** @brief @p resolvedPath 는 해석한 경로, @p cacheKey 는 그 캐시 키입니다(`PrefabCache::request` 가 한 번 구한다). */
        PrefabLoadOperation( PrefabCache* pCache, string_view resolvedPath, string cacheKey );

        /** @brief 읽은 프리팹입니다. `Ready` 일 때만 값이 있습니다(`AssetHandle::get` 이 상태를 본다). */
        const shared_ptr<const PrefabAsset>& getAsset() const { return _pAsset; }

    protected:
        [[nodiscard]] expected<void> runLoad() override;

    private:
        friend class PrefabCache;

        PrefabCache*                  _pCache;
        string                        _cacheKey;
        shared_ptr<const PrefabAsset> _pAsset;
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 프리팹을 로드하고 스폰하는 캐시입니다.
     * @details 캐시는 프리팹을 `shared_ptr` 로 나눠 준다. 원시 포인터를 내주면 다시 읽기(`reload`)가 항목을 버리는 순간 쥔 쪽이 해제된 메모리를 본다.
     */
    class SW_API PrefabCache final : public AssetCacheBase
    {
    public:
        /** @brief 레지스트리의 종류 이름입니다. `AssetManager::getAssetCache<T>()` 가 이것으로 찾습니다. */
        static constexpr const utf8* kAssetKindName = "Prefab";
        /** @brief 빈 프리팹 캐시로 만듭니다. */
        PrefabCache();
        /** @brief 진행 중인 로드를 기다린 뒤 캐시된 프리팹을 정리합니다(워커의 로드가 사라진 캐시에 쓰지 않게). */
        ~PrefabCache() override;

        /** @brief 복사를 금지합니다. */
        PrefabCache( const PrefabCache& ) = delete;
        /** @brief 대입을 금지합니다. */
        PrefabCache& operator=( const PrefabCache& ) = delete;

        /**
         * @brief 프리팹 로드를 요청합니다. 캐시에 있으면 준비된 핸들, 누가 읽는 중이면 그 로드, 아니면 워커가 읽습니다(엔진 태스크가 없으면 이 자리에서).
         * @details Dev 는 XML 과 JSON 저작본을, Shipping 은 쿠킹된 .prefab.bin 만 읽습니다. 캐시 키는 확장자를 뺀 정규화 경로입니다.
         *          중첩 원본은 그 로드 안에서 같은 스레드가 읽는다 — 다른 스레드의 로드를 기다리지 않으므로 순환한 프리팹 둘을 두 워커가 동시에 읽어도 멈추지 않는다.
         */
        PrefabHandle request( string_view assetRelativePath );
        /** @brief 프리팹을 로드합니다. `request` 와 같은 로드 작업을 기다립니다(새 로드는 이 자리에서 읽는다). 읽지 못하면 nullptr 입니다. */
        shared_ptr<const PrefabAsset> loadPrefab( string_view assetRelativePath );
        /**
         * @brief 프리팹을 스폰합니다.
         * @details 컴포넌트 틱 중이면 오브젝트는 바로 반환하고 프리팹 상태는 틱 직후(구조 변경 큐)에 채운다. 그때까지 오브젝트는 비어 있다.
         *          상태를 쓰지 못하면 그 오브젝트를 지운다. 틱 안에서 스폰하고 바로 초기화하려면 `executeOrDeferPostTick` 으로 감싼다.
         *          씬에 놓인 인스턴스는 이 함수를 거치지 않는다. 씬은 프리팹 원형에 덮어쓴 값만 얹어 만든다(`Scene::instantiate`, `PrefabOverrides`).
         */
        GameObject* spawn( GameObjectManager* pGameObjectManager, string_view assetRelativePath, const utf8* pInstanceName = nullptr );
        /**
         * @brief @p asset 의 중첩 인스턴스를 @p pRoot(그 프리팹의 인스턴스 루트) 아래에 스폰합니다. 원본의 중첩 인스턴스까지 재귀로 스폰합니다.
         * @details `spawn` 과 씬 로드(`Scene::instantiate`)가 루트 상태를 읽은 뒤 부른다. 노드마다 원본 프리팹에 오버라이드를 얹은 상태로 오브젝트를 만들고
         *          부모 오브젝트의 `_attachKey` 컴포넌트(없으면 primary)에 붙이며, 만든 오브젝트를 `PrefabInstanceRegistry` 에 부품으로 기록한다.
         *          원본을 읽지 못한 노드와 그 아래 노드는 건너뛰고 경고한다(유니티의 Missing Prefab 처럼 바깥 인스턴스는 남는다).
         *          스폰이 같은 프리팹을 다시 펼치면(로드 뒤 원본을 고쳐 생긴 순환) 그 노드에서 멈추고 오류를 알린다.
         *          구조 변경이 막힌 틱 안에서는 부르지 않는다(`spawn` 은 틱 뒤로 미룬 자리에서 부른다).
         *          노드 오버라이드 안의 `<Part>` 는 그 노드 아래 부품에, @p listPartOverride 는 @p pRoot 아래 부품에 얹는다 — 바깥 것이 나중에 얹혀 이긴다.
         * @param listPartOverride 인스턴스가 부품에 덮어쓴 것(씬 엔티티의 `<Part>`, `PrefabOverrides::collectPartOverrides`)입니다. 스폰한 부품에 닿지 않은 항목은 경고합니다.
         * @return 만든 오브젝트 수입니다.
         */
        uint32 spawnNestedInstances( GameObject* pRoot, const PrefabAsset& asset, const vector<PrefabPartOverride>& listPartOverride );
        /**
         * @brief @p root(@p asset 의 인스턴스 루트) 아래 부품 가운데 스폰한 그대로가 아닌 것의 덮어쓴 것을 모읍니다(비우고 채웁니다, 경로 순서는 스폰 순서).
         * @details 기준은 같은 프리팹을 씬 밖 임시 매니저에 덮어쓴 것 없이 스폰한 부품이다 — 노드 오버라이드 · 안쪽 `<Part>` 까지 스폰과 같은 식이라
         *          고치지 않은 부품은 적히지 않는다. 씬 저장(인스턴스 엔티티의 `<Part>`)과 Apply to Prefab(바깥 프리팹 노드의 `<Part>`)이 쓴다.
         *          옮긴 부품(`isAttachedNestedPart` 가 false)과 부품 키가 없는 부품(에디터가 프리팹을 만들며 들인 것)은 적지 않는다.
         *          구조 변경이 막힌 순회(`forEachGameObject`) 안에서는 부르지 않는다 — 임시 매니저에 오브젝트를 만든다.
         * @return 기준 스폰이나 비교에 실패하면 false 입니다.
         */
        [[nodiscard]] bool collectPartOverrides( const GameObject& root, const PrefabAsset& asset, vector<PrefabPartOverride>& outListPartOverride );
        /**
         * @brief @p inoutAsset 의 중첩 인스턴스를 쿠킹본 형식으로 펼칩니다 — 노드마다 원본에 오버라이드를 얹은 최종 상태를 싣고, 원본의 중첩 인스턴스를 뒤 노드로 덧붙입니다.
         * @details 쿠킹은 펼친다. 로드 시간 기준이다 — 참조를 남기면 배포본이 스폰할 때마다 원본 프리팹을 하나씩 열고 원형 상태를 만들어 오버라이드를
         *          얹어야 하지만, 펼친 쿠킹본은 파일 하나를 읽고 상태를 그대로 쓴다(언리얼 Packed Level Actor 가 쿡에서 레벨 인스턴스를 굳히는 것과 같다).
         *          원본 수정이 퍼지는 것은 다시 쿠킹할 때다(쿠킹은 늘 저작본 전체를 다시 만든다). 원본을 읽지 못하면(없음 · 순환) false 이고 에셋은 그대로다.
         */
        [[nodiscard]] bool flattenNestedInstances( PrefabAsset& inoutAsset );
        /**
         * @brief 캐시에서 프리팹 하나를 버립니다. 다음 `loadPrefab` 이 디스크를 다시 읽습니다(에디터 핫 리로드).
         * @details **지금 살아 있는 오브젝트는 바뀌지 않는다.** 보장하는 것은 "다음에 만드는 인스턴스에 고친 내용이 나온다"이다. 스폰할 때와
         *          씬을 다시 읽을 때가 그렇다. 씬은 프리팹 인스턴스의 덮어쓴 값만 저장하므로(`PrefabOverrides`) 다시 읽은 씬의 인스턴스에는
         *          고친 프리팹의 값이 퍼지고, 인스턴스가 덮어쓴 값은 그대로 남는다. 중첩 인스턴스를 가진 프리팹도 함께 버린다 — 고친 원본이 순환을
         *          만들었는지 다음 로드가 다시 본다(상위 프리팹은 원본을 경로로만 가지므로 버리지 않아도 고친 원본이 퍼진다).
         *          변형도 함께 버린다 — 읽을 때 부모를 합쳐 들고 있으므로 다음 로드가 다시 합쳐야 고친 부모가 퍼진다.
         */
        void reload( const AssetID& assetID );
        /**
         * @brief 프리팹 @p assetRelativePath 의 칸이 어디서 왔는지 변형 사슬을 따라 찾습니다. "이 칸이 어디서 왔나" 의 답이고 인스펙터 같은 도구가 묻습니다.
         * @param componentKey 컴포넌트 키(`이름#n`)입니다. 비면 오브젝트 자기 칸입니다.
         * @return 그 칸을 마지막으로 적은 프리팹 경로입니다(변형이 아니거나 어느 변형도 적지 않았으면 사슬 맨 위 프리팹). 읽지 못하면 빈 글입니다.
         */
        [[nodiscard]] string findFieldOrigin( string_view assetRelativePath, string_view componentKey, string_view fieldName );
        /** @brief 이 캐시가 다루는 에셋 종류의 이름입니다. */
        const utf8* getAssetKindName() const override { return kAssetKindName; }
        /** @brief 그 프리팹을 캐시가 지금 가지고 있으면 true 입니다. */
        bool isCached( string_view assetRelativePath ) const override;
        /** @brief 지금 보관 중인 항목 수입니다. */
        size_t getCachedCount() const override;
        /** @brief 캐시를 통째로 비웁니다. 다음 `loadPrefab` 이 디스크를 다시 읽습니다. */
        void clear() override;
        /** @brief 프리팹 경로(`kAssetType` 의 접미사)면 `request` 의 로드 작업(곧 프리팹)을 붙드는 토큰입니다. 아니면 nullptr 입니다. */
        shared_ptr<const void> pinAsset( string_view relativePath ) override;

        /**
         * @brief @p sourceRoot 아래(하위 폴더 포함)의 저작 프리팹(`*.prefab.xml`, `*.prefab.json`)을 `<cookedDir>/<상대 경로>/<이름>.prefab.bin` 으로 쿠킹합니다.
         * @return 기록한 쿠킹본 수입니다. 읽지 못한 것과, 앞의 소스와 같은 쿠킹본을 쓰게 되는 소스(`x.prefab.xml` 과 `x.prefab.json`)는
         *         @p outFailedCount 로 세고 경고합니다. Shipping 은 쿠킹본 하나만 읽으므로 어느 것이 이길지 정해 두면 안 됩니다.
         * @details 씬처럼 엔진이 쿠킹한다(`App --cook-scenes` 가 리소스 루트로 함께 부른다. 언리얼의 쿡 커맨드렛에 해당한다). 형식을 쓰는 곳은
         *          `PrefabAsset::saveToBinaryFile` 하나다. 중첩 인스턴스는 펼쳐서 싣는다(`flattenNestedInstances`). 펼치지 못한 프리팹(원본 없음 · 순환)은
         *          쓰지 않고 실패로 센다 — 배포본이 부품 없이 스폰하는 것을 쿠킹에서 막는다.
         *          프리팹마다 작업(`CookJob`) 하나로 베이크하고 @p store 의 기록이 새것이면 DDC 의 출력을 다시 쓴다(`CookStore::cookFile`). 중첩 원본도 그 작업의 입력이다.
         */
        static uint32 cookAllPrefabs( string_view sourceRoot, string_view cookedDir, const CookStore& store, uint32& outFailedCount );
        /** @brief 프리팹 쿠킹 작업의 키 값입니다. */
        static CookJobDesc makeCookJobDesc();
        /**
         * @brief 경로의 캐시 키입니다 — 정규화하고 확장자(`.bin` · `.xml` · `.json`)를 뗀 경로입니다.
         * @details 캐시 · 핫 리로드 · 순환 검사 스택(`PrefabPathStack::makeKey`) · 에셋의 참조 수집과 변형 읽기가 같은 키를 쓴다.
         */
        static string makeCacheKey( string_view assetRelativePath );

    private:
        friend class PrefabLoadOperation;

        /** @brief `request` 와 `loadPrefab` 의 본문입니다. 새 로드를 @p start 대로 시작합니다(동기 API 는 이 자리에서 읽는다 — `AssetLoadStart`). */
        PrefabHandle requestWith( string_view assetRelativePath, AssetLoadStart start );

        /**
         * @brief 캐시를 보고 없으면 이 스레드에서 읽어 넣습니다(로드 단계 본문 · 중첩 원본). 읽지 못하면 nullptr 입니다.
         * @details 읽는 사이 캐시를 버렸으면(`reload` · `clear` — 세대가 바뀜) 결과를 캐시에 넣지 않고 요청한 쪽에만 준다(옛 파일 내용이 다시 살아나지 않게).
         */
        shared_ptr<const PrefabAsset> loadAndCache( string_view assetRelativePath );
        /** @brief 스폰한 오브젝트에 프리팹 상태와 이름을 쓰고 중첩 인스턴스를 스폰합니다. 바로 하는 스폰과 틱 뒤로 미룬 스폰, 부품 기준 스폰이 같이 씁니다. 실패하면 false 입니다. */
        [[nodiscard]] bool applySpawnState( GameObject* pGameObject, const PrefabAsset& asset, string_view instanceName );
        /**
         * @brief 막 읽은 @p asset 의 중첩 인스턴스가 순환하지 않는지 봅니다(원본을 재귀로 로드한다). 이 프리팹이 순환에 들면 오류를 알리고 false 입니다.
         * @details 순환은 로드 오류다 — 순환에 든 프리팹은 캐시에 들지 않고 `loadPrefab` 이 nullptr 를 돌려준다. 순환 밖에서 그 프리팹을 놓은 프리팹은 로드되고
         *          그 노드만 스폰할 때 건너뛴다. 원본이 없는 노드는 경고만 한다.
         */
        [[nodiscard]] bool validateNestedInstances( const PrefabAsset& asset, const string& cacheKey );
        /** @brief 노드 하나의 오브젝트 상태(원본 + 오버라이드, 또는 펼친 상태)를 만듭니다. 원본 노드면 @p outSource 에 원본 프리팹을 줍니다. 실패하면 false 입니다. */
        [[nodiscard]] bool makeNestedInstanceState( const PrefabInstanceNode& node, string& outStateXML, shared_ptr<const PrefabAsset>& outSource );
        /**
         * @brief `flattenNestedInstances` 의 재귀 단계입니다 — @p asset 의 노드를 펼쳐 @p inoutListNode 끝에 더합니다. 루트 노드의 부모는 @p parentIndex 입니다.
         * @param parentPath 부모 노드의 부품 경로입니다(맨 바깥은 빈 글). 펼친 노드의 `_partPath` 가 이 아래에 붙습니다.
         * @param listPartOverride 바깥 노드들의 `<Part>` 입니다(바깥 프리팹 루트에서의 경로). 펼친 상태에 베이크해 넣습니다.
         */
        [[nodiscard]] bool appendFlattenedNodes( const PrefabAsset& asset, int32 parentIndex, string_view parentPath, const vector<PrefabPartOverride>& listPartOverride,
                                                 vector<PrefabInstanceNode>& inoutListNode );
        /**
         * @brief `spawnNestedInstances` 의 재귀 단계입니다. @p listPartOverride 의 경로는 맨 바깥 인스턴스 루트에서 측정한 것이고, @p pRoot 는 그 경로 @p rootPath 의 부품입니다.
         * @param inoutListSpawnedPath 스폰한 부품의 경로를 끝에 더합니다(닿지 않은 `<Part>` 를 알리는 데 씁니다).
         */
        uint32 spawnNestedInstancesUnder( GameObject* pRoot, const PrefabAsset& asset, string_view rootPath, const vector<PrefabPartOverride>& listPartOverride,
                                          vector<string>& inoutListSpawnedPath );

    private:
        mutable shared_mutex                                             _mapCacheMutex;
        unordered_map<string, shared_ptr<const PrefabAsset>> _mapCache   SW_GUARDED_BY( _mapCacheMutex );
        unordered_map<string, weak_ptr<PrefabLoadOperation>> _mapLoading SW_GUARDED_BY( _mapCacheMutex ); ///< 읽는 중인 로드(같은 경로의 동시 요청은 같은 로드 작업). `_mapCacheMutex` 로 지킨다
        atomic<uint32>                                                   _cacheGeneration;                ///< `reload` · `clear` 가 올린다. 읽는 사이 바뀌면 결과를 캐시에 넣지 않는다
    };
} // namespace sw

/**
 * @file SharedAssetCache.h
 * @brief 경로로 읽는 에셋 종류 하나를 나눠 주고 에셋 캐시 레지스트리에 보이는 캐시 템플릿입니다(스켈레톤 · 클립 · 본 LOD · 리그 · 스프라이트 클립 ·
 *        파쇄 · 소켓 · 알림 표 · 페어드 애니메이션 · 물리 에셋).
 * @details 표(`SharedAssetTable`)는 종류마다 캐시 객체가 들고, 캐시는 에셋 매니저의 레지스트리에 종류 이름으로 오릅니다(정적 창구 `request` · `acquire` 는
 *          그 이름으로 찾은 캐시의 표를 쓴다 — `SharedAssetCacheLookup`). Engine 종류는 매니저가 내장 캐시로 만들어 소유하고(`SW_SHARED_ASSET_CACHE`), GameFramework · 키트 · 게임 종류는
 *          그 모듈이 인스턴스화해 모듈 서브시스템이 소유하며 모듈 수명으로 오르내립니다(`SW_MODULE_SHARED_ASSET_CACHE`) — 예전 `GameDataCache<T>` 의 자리다. 멤버 정의는 이 헤더에 두지 않고
 *          `SharedAssetCacheDefinition.h` 에 두며, 에셋 종류의 .cpp 하나가 그것을 포함해 명시적으로 인스턴스화합니다. 쓰는 쪽 헤더는
 *          `SW_DECLARE_SHARED_ASSET_CACHE( <그 이미지의 _EXTERN_TEMPLATE 매크로>, 에셋 );` 만 둡니다 — 한 종류의 표는 한 이미지에만 있습니다.
 *          옛 객체를 살려 두는 동작은 `SharedAssetReloadMode::Detach` 와 쥔 쪽의 `shared_ptr` 가 맡습니다(세대 수로 옛 표를 들고 있지 않는다). 별칭(`using SkeletonCache = …`)은 두지 않고 쓰는 쪽이 `SharedAssetCache<Skeleton>` 으로 부릅니다(이름이 둘이면 찾기가 갈라집니다).
 *
 *          종류마다 다른 것(종류 이름 · 읽는 함수 · 메모리 태그 · 다시 읽기 방식)은 그 종류의 정의(`AssetTypeDef` — 이름과 `AssetTypeLoader`) 한 벌에만 적습니다.
 *          인스턴스화 · 캐시 등록 · 종류 등록은 `SW_SHARED_ASSET_CACHE( id, 에셋, 정의 )`(Engine) 또는 `SW_MODULE_SHARED_ASSET_CACHE( id, 에셋, 내보내기, 정의 )`(모듈) 한 줄이고,
 *          그 줄이 `SW_ASSET_TYPE` 를 대신합니다 — 캐시의 종류 이름이 곧 정의의 이름이라 둘이 갈라질 수 없습니다. 로더가 빠지면 인스턴스화가 컴파일 오류로 멈춥니다.
 *
 *          이 템플릿으로 합치지 않은 캐시와 그 이유입니다(모양이 "경로 → 공유 `const` 에셋, 디바이스 없음, 자체 상태 없음" 이 아닙니다).
 *          - `MeshCache`: 쓰는 쪽이 고치는 `shared_ptr<Mesh>`(const 아님)를 `WeakInternTable` 에 두고, `reloadShared( path, pDevice )` 가 정점을 바꾸기 전에
 *            `waitIdle` 하며, 경고한 경로 집합과 물리 셰이프 표 비우기(`IMeshDerivedCache`)를 함께 가집니다(MeshCache.cpp).
 *          - `TextureCache`: `acquire( path, pDevice )` 가 GPU 텍스처를 만들어 `Texture2D*` 를 참조 횟수로 빌려 주고(`release` 짝), 렌더 타깃 크기 바꾸기 ·
 *            영역 갱신 큐를 매니저마다의 `Impl` 에 둡니다(TextureCache.h).
 *          - `MaterialCache`: `acquire( path, pDevice )` 가 `Material*` 를 돌려주고 디바이스 초기화를 미뤄 두는 큐(`initializePending`)를 매니저마다 가집니다
 *            (MaterialCache.h).
 *          - `PrefabCache`: `loadPrefab` 이 매니저 소유 `PrefabAsset*` 를 돌려주고 스폰 · 되돌리기 · 중첩 펼치기를 함께 가진 매니저마다의 객체입니다
 *            (PrefabAsset.h).
 *          - `UITextAssetCache`: 문서 · 스타일 시트 서브시스템이 값 멤버로 가지는 캐시이고 메모리 글(`registerMemoryText`), 다시 읽기 알림 델리게이트, 파싱 횟수를
 *            인스턴스마다 가집니다. 실패를 남기지 않는 정책도 다릅니다(UITextAssetCache.h).
 *          - `AudioReloadCache` · `LocalizationReloadCache`(개발 도구 `DevTools/AssetReload` · `DevTools/Client/AssetReload`): 표가 없는 어댑터입니다. 데이터는 `IAudioService` · `LocalizationManager` 가 가지고 이 캐시는
 *            바뀐 파일을 넘기기만 합니다(각 .cpp).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Delegate/Subscription.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

#include "Engine/Asset/AssetHandle.h"
#include "Engine/Asset/Cache/AssetCacheBase.h"
#include "Engine/Content/AssetID.h"

namespace sw
{
    /** @brief 에셋 종류의 정의(`static constexpr AssetTypeDef kType`)입니다. 인스턴스화하는 .cpp 의 캐시 등록 줄이 특수화합니다(`SharedAssetCacheDefinition.h` 참고). */
    template <typename AssetType>
    struct SharedAssetCacheTraits;

    /** @brief 종류의 헤더가 `SW_DECLARE_SHARED_ASSET_CACHE` 로 캐시를 선언했으면 참입니다. 정의 줄(`SW_SHARED_ASSET_CACHE` · `SW_MODULE_SHARED_ASSET_CACHE`)이 단언한다. */
    template <typename AssetType>
    struct SharedAssetCacheDecl
    {
        static constexpr bool kValue = false;
    };

    template <typename AssetType>
    class SharedAssetLoadOperation;
    template <typename AssetType>
    class SharedAssetTable;
    template <typename TAsset>
    class AssetReloadWatch;
} // namespace sw

namespace sw
{
    /**
     * @struct SharedAssetCacheLookup
     * @brief 공유 캐시를 에셋 매니저에서 종류 이름으로 찾고 올리는 한 곳입니다. 템플릿 밖에 두어 모듈 이미지가 엔진 서비스 헤더를 끌지 않게 합니다.
     */
    struct SW_API SharedAssetCacheLookup
    {
        /** @brief 종류 이름 @p pKindName 의 캐시입니다. 없으면(모듈이 서기 전 · 내려간 뒤의 접근) 오류를 남기고 멈춥니다. */
        [[nodiscard]] static AssetCacheBase& getCache( const utf8* pKindName );
        /** @brief 모듈 캐시 @p cache 를 레지스트리에 올립니다(@p pReload 는 핫 리로드 함수 — 배포는 nullptr). 매니저가 없거나 이름이 겹치면 오류를 남기고 빈 구독입니다. */
        [[nodiscard]] static Subscription registerModuleCache( AssetCacheBase& cache, AssetCacheReloadFunction pReload );
    };

    /** @brief 공유 에셋 로드 핸들입니다(`SharedAssetCache<에셋>::request`). 핸들이 살아 있는 동안 준비된 에셋도 살아 있습니다. */
    template <typename AssetType>
    using SharedAssetHandle = AssetHandle<SharedAssetLoadOperation<AssetType>>;
} // namespace sw

namespace sw
{
    /**
     * @class SharedAssetCache
     * @brief 에셋 종류 하나의 공유 표를 들고 나눠 주며(`acquire`), 레지스트리에는 `AssetCacheBase` 로 보입니다. 에셋 매니저가 소유합니다.
     * @tparam AssetType 기본 생성 · 이동 대입이 되는 에셋 타입입니다.
     */
    template <typename AssetType>
    class SharedAssetCache final : public AssetCacheBase
    {
    public:
        /** @brief 레지스트리의 종류 이름입니다(`SharedAssetCacheLookup` · `AssetManager::getAssetCache<T>()` 가 찾는다). 정의는 `SharedAssetCacheDefinition.h` 입니다(종류의 특수화 값). */
        static const utf8* const kAssetKindName;

        /** @brief 빈 표를 만들고 종류 이름의 계측 칸을 올립니다(`CacheTracking::counted`). 정의는 `SharedAssetCacheDefinition.h` 라 인스턴스화하는 .cpp 에만 있습니다. */
        SharedAssetCache();
        ~SharedAssetCache() override;
        SharedAssetCache( const SharedAssetCache& )            = delete;
        SharedAssetCache& operator=( const SharedAssetCache& ) = delete;

        /**
         * @brief 경로의 에셋 로드를 요청합니다. 표에 있으면 준비된 핸들, 없으면 워커가 읽습니다(엔진 태스크가 없으면 이 자리에서). 워커에서 불러도 됩니다.
         * @details 실패는 기억하지 않으므로 파일이 생기면 다음 요청이 읽습니다(오류는 읽는 함수가 남깁니다).
         */
        static SharedAssetHandle<AssetType> request( string_view path );
        /** @brief 경로의 에셋을 나눠 받습니다. `request` 와 같은 로드 작업을 기다립니다(새 로드는 이 자리에서 읽는다). 읽을 수 없으면 nullptr 입니다. 워커에서 불러도 됩니다. */
        static shared_ptr<const AssetType> acquire( string_view path );
        /**
         * @brief 사용 중이면 다시 읽고 다시 읽은 횟수를 올립니다(틱 밖, 게임 스레드). 읽지 못하면 옛 내용 그대로이고 false 입니다.
         * @details 제자리로 바꾸는지 표에서 떼어 내는지는 종류의 `kReloadMode` 가 정합니다.
         */
        [[nodiscard]] static bool reloadShared( string_view path );

        const utf8* getAssetKindName() const override;
        /** @brief 그 경로의 에셋을 누가 쥐고 있는지 반환합니다. */
        bool isCached( string_view relativePath ) const override;
        /** @brief 사용 중이면 다시 읽습니다(`reloadShared`). */
        void reload( const AssetID& assetID );
        /** @brief 쥔 경로면 종류의 다시 읽기 방식대로 다시 읽습니다(합성 문맥이 바뀌었다 — `AssetCacheBase::reloadComposed`). */
        bool reloadComposed( string_view relativePath ) override;
        /** @brief 지금 누가 쥔 에셋 수입니다. */
        size_t getCachedCount() const override;
        /** @brief 표를 비웁니다. 쥔 쪽의 에셋은 그대로이고, 다음 요청은 파일을 새로 읽습니다. */
        void clear() override;

    private:
        friend class SharedAssetLoadOperation<AssetType>;
        friend class AssetReloadWatch<AssetType>; // 횟수는 감시만 읽는다 — 쓰는 쪽이 "본 횟수" 칸과 대조를 손으로 적지 않게 한다

        /** @brief 이 표에서 다시 읽은(또는 떼어 낸) 횟수입니다. 쓰는 쪽은 `AssetReloadWatch` 로 보고 값이 바뀌면 자기가 지은 것을 다시 짓습니다. */
        static uint32 getReloadCount();

        /** @brief 레지스트리에 오른 이 종류 캐시의 표입니다(정적 창구 · 로드 작업이 쓴다). 없으면 멈춘다. */
        static SharedAssetTable<AssetType>& getTable();
        /** @brief @p table 의 @p path 를 종류의 `kReloadMode` 대로 다시 읽습니다(`reloadShared` · `reload` 의 본문). */
        [[nodiscard]] static bool reloadIn( SharedAssetTable<AssetType>& table, string_view path );
        /** @brief `request` 와 `acquire` 의 본문입니다. 새 로드를 @p start 대로 시작합니다(동기 API 는 이 자리에서 읽는다 — `AssetLoadStart`). */
        static SharedAssetHandle<AssetType> requestWith( string_view path, AssetLoadStart start );
        /** @brief 로드 단계 본문입니다. 종류의 읽는 함수로 읽어 표에 넣습니다(메모리 태그를 겁니다). */
        [[nodiscard]] static bool loadIntoTable( string_view path, shared_ptr<const AssetType>& outAsset );

        unique_ptr<SharedAssetTable<AssetType>> _pTable; ///< 이 종류의 공유 표(정의가 `SharedAssetCacheDefinition.h` 에만 있어 포인터로 든다)
    };
} // namespace sw

namespace sw
{
    /**
     * @class SharedAssetLoadOperation
     * @brief 공유 에셋 로드 하나의 공유 상태입니다. 로드 단계에서 파일을 읽어 표에 넣습니다(완료 단계 없음 — 디바이스를 쓰지 않습니다).
     */
    template <typename TAsset>
    class SharedAssetLoadOperation final : public AssetLoadOperation
    {
    public:
        using AssetType = const TAsset;

        SharedAssetLoadOperation( const utf8* pKindName, string_view path, MemoryTag memoryTag )
            : AssetLoadOperation{ pKindName, path, memoryTag }
            , _pAsset{}
        {
        }

        /** @brief 읽은 에셋입니다. `Ready` 일 때만 값이 있습니다(`AssetHandle::get` 이 상태를 본다). */
        const shared_ptr<const TAsset>& getAsset() const { return _pAsset; }

    protected:
        [[nodiscard]] expected<void> runLoad() override
        {
            if ( SharedAssetCache<TAsset>::loadIntoTable( getPath(), _pAsset ) )
                return {};
            return unexpected( Error{ ErrorKind::InvalidArgument, "the asset could not be read (the loader logged why)", getPath() } );
        }

    private:
        friend class SharedAssetTable<TAsset>;

        shared_ptr<const TAsset> _pAsset;
    };
} // namespace sw

/**
 * @brief 종류 @p AssetType 의 캐시를 그 종류의 헤더에서 선언합니다(`namespace sw` 안). 인스턴스화는 한 이미지의 .cpp 정의 줄이 한다 — 쓰는 TU 가 몸체 없이 부르면
 *        `-Werror=undefined-func-template` 로 지므로(플랫폼마다 include 닫힘이 달라 한쪽에서만 나온다) 정의 줄이 이 선언을 단언한다.
 * @param ExternMacro 그 이미지의 `_EXTERN_TEMPLATE` 매크로(게임처럼 내보내지 않으면 빈 칸)
 */
#define SW_DECLARE_SHARED_ASSET_CACHE( ExternMacro, AssetType ) \
    template <>                                                 \
    struct SharedAssetCacheDecl<AssetType>                      \
    {                                                           \
        static constexpr bool kValue = true;                    \
    };                                                          \
    extern template class ExternMacro SharedAssetCache<AssetType>

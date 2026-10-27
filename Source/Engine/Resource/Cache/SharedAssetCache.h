/**
 * @file SharedAssetCache.h
 * @brief 경로로 읽는 에셋 종류 하나를 나눠 주고 에셋 캐시 레지스트리에 보이는 캐시 템플릿입니다(스켈레톤 · 클립 · 본 LOD · 리그 · 스프라이트 클립 ·
 *        파쇄 · 소켓 · 알림 표 · 페어드 애니메이션 · 물리 에셋).
 * @details 표(`SharedAssetTable`)는 종류마다 프로세스에 하나이고 Engine.dll 안에 있으며 엔진 수명 범위(`ScopeOwned`)가 소유합니다. 그래서 멤버 정의는 이 헤더에 두지 않고
 *          `SharedAssetCacheDefinition.h` 에 두며, 에셋 종류의 .cpp 하나가 그것을 포함해 명시적으로 인스턴스화합니다. 쓰는 쪽 헤더는
 *          `extern template class SW_API_EXTERN_TEMPLATE SharedAssetCache<에셋>;` 만 둡니다 — 모듈(게임 · 키트 · 에디터)이 표를 자기 이미지에 따로 만들 수
 *          없습니다. 별칭(`using SkeletonCache = …`)은 두지 않고 쓰는 쪽이 `SharedAssetCache<Skeleton>` 으로 부릅니다(이름이 둘이면 찾기가 갈라집니다).
 *
 *          종류마다 다른 것(종류 이름 · 읽는 함수 · 메모리 태그 · 다시 읽기 방식)은 `SharedAssetCacheTraits<에셋>` 특수화 하나에 모읍니다. 특수화는 인스턴스화하는
 *          .cpp 에만 있고, 값이 빠지면 인스턴스화가 컴파일 오류로 멈춥니다. 인스턴스화와 등록은 `SW_SHARED_ASSET_CACHE( 에셋 )` 한 줄입니다.
 *
 *          이 템플릿으로 합치지 않은 캐시와 그 이유입니다(모양이 "경로 → 공유 `const` 에셋, 디바이스 없음, 자체 상태 없음" 이 아닙니다).
 *          - `MeshCache`: 쓰는 쪽이 고치는 `shared_ptr<Mesh>`(const 아님)를 `WeakInternTable` 에 두고, `reloadShared( path, pDevice )` 가 정점을 바꾸기 전에
 *            `waitIdle` 하며, 경고한 경로 집합과 물리 셰이프 캐시(`PhysicsMeshShapeCache`) 읽기 함수 등록 · 비우기를 함께 가집니다(MeshCache.cpp).
 *          - `TextureCache`: `acquire( path, pDevice )` 가 GPU 텍스처를 만들어 `Texture2D*` 를 참조 횟수로 빌려 주고(`release` 짝), 렌더 타깃 크기 바꾸기 ·
 *            영역 갱신 큐를 매니저마다의 `Impl` 에 둡니다(TextureCache.h).
 *          - `MaterialCache`: `acquire( path, pDevice )` 가 `Material*` 를 돌려주고 디바이스 초기화를 미뤄 두는 큐(`initializePending`)를 매니저마다 가집니다
 *            (MaterialCache.h).
 *          - `PrefabCache`: `loadPrefab` 이 매니저 소유 `PrefabAsset*` 를 돌려주고 스폰 · 되돌리기 · 중첩 펼치기를 함께 가진 매니저마다의 객체입니다
 *            (PrefabAsset.h).
 *          - `UITextAssetCache`: 문서 · 스타일 시트 캐시의 기반 클래스(final 아님)이고 메모리 글(`registerMemoryText`), 다시 읽기 알림 델리게이트, 파싱 횟수를
 *            인스턴스마다 가집니다. 실패를 남기지 않는 정책도 다릅니다(UITextAssetCache.h).
 *          - `AudioReloadCache` · `LocalizationReloadCache`: 표가 없는 어댑터입니다. 데이터는 `IAudioService` · `LocalizationManager` 가 가지고 이 캐시는
 *            바뀐 파일을 넘기기만 합니다(각 .cpp).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Memory/Memory.h"

#include "Engine/Resource/AssetHandle.h"
#include "Engine/Resource/Cache/IAssetCache.h"
#include "Engine/Resource/ResourceID.h"

namespace sw
{
    /** @brief 에셋 종류마다 다른 값입니다. 인스턴스화하는 .cpp 가 특수화합니다(`SharedAssetCacheDefinition.h` 참고). */
    template <typename AssetType>
    struct SharedAssetCacheTraits;

    template <typename AssetType>
    class SharedAssetLoadBlock;
    template <typename AssetType>
    class SharedAssetTable;
} // namespace sw

namespace sw
{
    /** @brief 공유 에셋 로드 핸들입니다(`SharedAssetCache<에셋>::request`). 핸들이 살아 있는 동안 준비된 에셋도 살아 있습니다. */
    template <typename AssetType>
    using SharedAssetHandle = AssetHandle<SharedAssetLoadBlock<AssetType>>;
} // namespace sw

namespace sw
{
    /**
     * @class SharedAssetCache
     * @brief 에셋 종류 하나의 공유 표를 나눠 주고(`acquire`), 레지스트리에는 `IAssetCache` 로 보입니다. 객체는 상태가 없고 표를 보이는 창구일 뿐입니다.
     * @tparam AssetType 기본 생성 · 이동 대입이 되는 에셋 타입입니다.
     */
    template <typename AssetType>
    class SharedAssetCache final : public IAssetCache
    {
    public:
        /** @brief 종류 이름의 계측 칸을 올립니다(`CacheTracking::counted`). 정의는 `SharedAssetCacheDefinition.h` 라 인스턴스화하는 .cpp 에만 있습니다. */
        SharedAssetCache();

        /**
         * @brief 경로의 에셋 로드를 요청합니다. 표에 있으면 준비된 핸들, 없으면 워커가 읽습니다(엔진 태스크가 없으면 이 자리에서). 워커에서 불러도 됩니다.
         * @details 실패는 기억하지 않으므로 파일이 생기면 다음 요청이 읽습니다(오류는 읽는 함수가 남깁니다).
         */
        static SharedAssetHandle<AssetType> request( string_view path );
        /** @brief 경로의 에셋을 나눠 받습니다. `request` 와 같은 로드 블록을 기다립니다(새 로드는 이 자리에서 읽는다). 읽을 수 없으면 nullptr 입니다. 워커에서 불러도 됩니다. */
        static shared_ptr<const AssetType> acquire( string_view path );
        /**
         * @brief 사용 중이면 다시 읽고 다시 읽은 횟수를 올립니다(틱 밖, 게임 스레드). 읽지 못하면 옛 내용 그대로이고 false 입니다.
         * @details 제자리로 바꾸는지 표에서 떼어 내는지는 종류의 `kReloadMode` 가 정합니다.
         */
        [[nodiscard]] static bool reloadShared( string_view path );
        /** @brief 이 표에서 다시 읽은(또는 떼어 낸) 횟수입니다. 쓰는 쪽은 값이 바뀌면 자기가 지은 것을 다시 짓습니다. */
        static uint32 getReloadCount();

        const utf8* getAssetKindName() const override;
        /** @brief 그 경로의 에셋을 누가 쥐고 있는지 반환합니다. */
        bool isCached( string_view relativePath ) const override;
        /** @brief 사용 중이면 다시 읽습니다(`reloadShared`). 디바이스는 쓰지 않습니다. */
        void reload( const ResourceID& resourceID, IRHIDevice* pDevice ) override;
        /** @brief 지금 누가 쥔 에셋 수입니다. */
        size_t getCachedCount() const override;
        /** @brief 표를 비웁니다. 쥔 쪽의 에셋은 그대로이고, 다음 요청은 파일을 새로 읽습니다. */
        void clear() override;

    private:
        friend class SharedAssetLoadBlock<AssetType>;

        /** @brief 프로세스에 하나인 표입니다(Engine.dll 안에 있어 모듈 핫 리로드에도 사라지지 않고, 엔진 수명 범위가 소유해 엔진 종료 때 사라집니다). */
        static SharedAssetTable<AssetType>& getTable();
        /** @brief `request` 와 `acquire` 의 본문입니다. 새 로드를 @p start 대로 시작합니다(동기 API 는 이 자리에서 읽는다 — `AssetLoadStart`). */
        static SharedAssetHandle<AssetType> requestWith( string_view path, AssetLoadStart start );
        /** @brief 로드 단계 본문입니다. 종류의 읽는 함수로 읽어 표에 넣습니다(메모리 태그를 겁니다). */
        [[nodiscard]] static bool loadIntoTable( string_view path, shared_ptr<const AssetType>& outAsset );
    };
} // namespace sw

namespace sw
{
    /**
     * @class SharedAssetLoadBlock
     * @brief 공유 에셋 로드 하나의 공유 상태입니다. 로드 단계에서 파일을 읽어 표에 넣습니다(완료 단계 없음 — 디바이스를 쓰지 않습니다).
     */
    template <typename TAsset>
    class SharedAssetLoadBlock final : public AssetLoadBlock
    {
    public:
        using AssetType = const TAsset;

        SharedAssetLoadBlock( const utf8* pKindName, string_view path )
            : AssetLoadBlock{ pKindName, path }
            , _pAsset{}
        {
        }

        /** @brief 읽은 에셋입니다. `Ready` 일 때만 값이 있습니다(`AssetHandle::get` 이 상태를 본다). */
        const shared_ptr<const TAsset>& getAsset() const { return _pAsset; }

    protected:
        [[nodiscard]] bool runLoad( string& outError ) override
        {
            if ( SharedAssetCache<TAsset>::loadIntoTable( getPath(), _pAsset ) )
                return true;
            outError = "the asset could not be read (the loader logged why)";
            return false;
        }

    private:
        friend class SharedAssetTable<TAsset>;

        shared_ptr<const TAsset> _pAsset;
    };
} // namespace sw

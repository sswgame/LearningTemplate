/**
 * @file SharedAssetCacheDefinition.h
 * @brief `SharedAssetCache` 의 멤버 정의입니다. 에셋 종류를 명시적으로 인스턴스화하는 .cpp 만 포함합니다(Engine · GameFramework · 키트 · 게임).
 * @details 그 .cpp 는 이 헤더를 포함하고 `SW_SHARED_ASSET_CACHE( id, 에셋, 정의 );`(Engine) 또는
 *          `SW_MODULE_SHARED_ASSET_CACHE( id, 에셋, 내보내기 매크로, 정의 );`(모듈, `Engine/Module/ModuleSharedAssetCache.h`) 한 줄로(`namespace sw` 안) 종류 정의 · 명시적 인스턴스화 ·
 *          캐시 레지스트리 등록 · 종류 등록(`SW_ASSET_TYPE`)을 함께 합니다. 정의는 `AssetTypeDef` 값이고 로더(`AssetTypeLoader`)가 읽는 함수 · 메모리 태그 · 다시 읽기 방식을 든다 —
 *          캐시가 따로 적는 특성 표는 없다. 정의의 이름이 레지스트리의 종류 이름이다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Delegate/Subscription.h"
#include "Core/Diagnostics/CacheStats.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/Subsystem/ISubsystem.h"

#include "Engine/Asset/Cache/BuiltInAssetCache.h"
#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Asset/Cache/SharedAssetTable.h"
#include "Engine/Content/AssetTypeRegistry.h"

namespace sw
{
    /**
     * @struct SharedAssetCacheType
     * @brief 캐시 등록 줄이 특수화한 정의(`SharedAssetCacheTraits<에셋>::kType`)에서 캐시가 쓰는 값을 꺼냅니다. 로더 없는 정의는 컴파일 오류다.
     */
    template <typename AssetType>
    struct SharedAssetCacheType
    {
        static constexpr const AssetTypeDef& kDef = SharedAssetCacheTraits<AssetType>::kType;
        static_assert( kDef._loader._pLoadInto != nullptr, "a shared asset cache needs a loader in its asset type definition (AssetTypeLoader::of / with)" );

        static constexpr MemoryTag             kMemoryTag  = kDef._loader._memoryTag;
        static constexpr SharedAssetReloadMode kReloadMode = kDef._loader._reloadMode;

        /** @brief 레지스트리의 종류 이름입니다(정의의 이름 — 등록 줄의 글자라 끝이 0 이다). */
        static const utf8* getKindName() { return kDef._name.data(); }
        /** @brief 정의의 로더로 @p outAsset 에 읽습니다. 읽지 못하면 로더가 오류를 남기고 false 입니다. */
        [[nodiscard]] static bool load( const AssetID& assetID, AssetType& outAsset ) { return kDef._loader._pLoadInto( assetID, &outAsset ); }
    };

    /**
     * @brief 에셋 종류 하나의 계측 칸입니다(`CacheStats`, 종류 이름으로 한 번 올린다). 센 값은 이 칸에 있고 `SharedAssetCache` 의 생성자가 같은 이름으로 칸을 이어 받는다(`CacheTracking::counted`).
     * @details 표는 캐시(에셋 매니저 소유)가 들어 엔진을 다시 세우면 새로 서지만, 칸은 프로세스 끝까지 살아 함수 정적 핸들이 그대로 유효하다.
     */
    template <typename AssetType>
    const CacheStatsHandle& getSharedAssetCacheStats()
    {
        static const CacheStatsHandle s_stats = CacheStats::getOrCreate( SharedAssetCacheType<AssetType>::getKindName() );
        return s_stats;
    }

    template <typename AssetType>
    SharedAssetHandle<AssetType> SharedAssetCache<AssetType>::request( string_view path )
    {
        return requestWith( path, AssetLoadStart::Async );
    }

    template <typename AssetType>
    shared_ptr<const AssetType> SharedAssetCache<AssetType>::acquire( string_view path )
    {
        return requestWith( path, AssetLoadStart::OnCallingThread ).wait();
    }

    template <typename AssetType>
    SharedAssetHandle<AssetType> SharedAssetCache<AssetType>::requestWith( string_view path, AssetLoadStart start )
    {
        bool                                            bStartLoad = false;
        shared_ptr<SharedAssetLoadOperation<AssetType>> pOperation = getTable().request( path, SharedAssetCacheType<AssetType>::getKindName(), SharedAssetCacheType<AssetType>::kMemoryTag, bStartLoad );
        // 새 로드를 시작해야 하면 실패, 준비된 로드 작업이나 읽는 중인 로드 작업을 받았으면 적중이다. 빈 경로는 조회가 아니다.
        if ( path.empty() == false )
        {
            if ( bStartLoad )
                getSharedAssetCacheStats<AssetType>().recordMiss();
            else
                getSharedAssetCacheStats<AssetType>().recordHit();
        }
        if ( bStartLoad )
            AssetLoadOperation::startLoad( pOperation, start );
        return SharedAssetHandle<AssetType>{ std::move( pOperation ) };
    }

    template <typename AssetType>
    bool SharedAssetCache<AssetType>::loadIntoTable( string_view path, shared_ptr<const AssetType>& outAsset )
    {
        const ScopedMemoryTag memoryScope{ SharedAssetCacheType<AssetType>::kMemoryTag };
        return getTable().loadAndIntern( path, &SharedAssetCacheType<AssetType>::load, outAsset );
    }

    template <typename AssetType>
    bool SharedAssetCache<AssetType>::reloadShared( string_view path )
    {
        return reloadIn( getTable(), path );
    }

    template <typename AssetType>
    bool SharedAssetCache<AssetType>::reloadIn( SharedAssetTable<AssetType>& table, string_view path )
    {
        if constexpr ( SharedAssetCacheType<AssetType>::kReloadMode == SharedAssetReloadMode::InPlace )
            return table.reloadShared( path, &SharedAssetCacheType<AssetType>::load );
        else
            return table.detachShared( path, &SharedAssetCacheType<AssetType>::load );
    }

    template <typename AssetType>
    uint32 SharedAssetCache<AssetType>::getReloadCount()
    {
        return getTable().getReloadCount();
    }

    template <typename AssetType>
    const utf8* const SharedAssetCache<AssetType>::kAssetKindName = SharedAssetCacheTraits<AssetType>::kType._name.data();

    template <typename AssetType>
    SharedAssetCache<AssetType>::SharedAssetCache()
        : AssetCacheBase{ CacheTracking::counted( SharedAssetCacheType<AssetType>::getKindName() ) }
        , _pTable{ make_unique<SharedAssetTable<AssetType>>() }
    {
    }

    template <typename AssetType>
    SharedAssetCache<AssetType>::~SharedAssetCache() = default;

    template <typename AssetType>
    const utf8* SharedAssetCache<AssetType>::getAssetKindName() const
    {
        return SharedAssetCacheType<AssetType>::getKindName();
    }

    template <typename AssetType>
    bool SharedAssetCache<AssetType>::isCached( string_view relativePath ) const
    {
        return _pTable->findLive( relativePath ) != nullptr;
    }

    template <typename AssetType>
    void SharedAssetCache<AssetType>::reload( const AssetID& assetID )
    {
        (void)reloadIn( *_pTable, assetID.view() ); // 아무도 쥐지 않으면 다음 acquire 가 새로 읽고, 읽지 못하면 읽는 함수가 오류를 남기고 옛 내용을 지킨다
    }

    template <typename AssetType>
    bool SharedAssetCache<AssetType>::reloadComposed( string_view relativePath )
    {
        return isCached( relativePath ) && reloadIn( *_pTable, relativePath );
    }

    template <typename AssetType>
    size_t SharedAssetCache<AssetType>::getCachedCount() const
    {
        return _pTable->countLive();
    }

    template <typename AssetType>
    void SharedAssetCache<AssetType>::clear()
    {
        _pTable->clear(); // 자기 표를 비운다 — 매니저 종료 때도 불려 서비스 칸을 거치지 않는다
        getSharedAssetCacheStats<AssetType>().recordClear();
    }

    template <typename AssetType>
    SharedAssetTable<AssetType>& SharedAssetCache<AssetType>::getTable()
    {
        // 종류 이름이 레지스트리에서 겹치지 않으므로(목록이 거절한다) 그 이름의 캐시는 이 형식이다.
        return *static_cast<SharedAssetCache<AssetType>&>( SharedAssetCacheLookup::getCache( SharedAssetCacheType<AssetType>::getKindName() ) )._pTable;
    }
} // namespace sw

/**
 * @brief 에셋 종류의 정의를 적고(@p ... 는 `AssetTypeDef` 값) `SharedAssetCache<AssetType>` 을 Engine.dll 에서 내보내도록 인스턴스화해 내장 캐시로 등록하며, 같은 정의로 종류를 등록합니다(`SW_ASSET_TYPE`).
 * @details 인스턴스화하는 .cpp, `namespace sw` 안에 둔다. @p id 는 정적 이름을 만드는 식별자다. 예:
 * @code
 *     SW_SHARED_ASSET_CACHE( Skeleton, Skeleton, Skeleton::kAssetType.withLoader( AssetTypeLoader::of<Skeleton>( MemoryTag::Animation ) ) );
 * @endcode
 */
#define SW_SHARED_ASSET_CACHE( id, AssetType, ... )                                                                                                              \
    static_assert( SharedAssetCacheDecl<AssetType>::kValue, "declare the cache in the asset header: SW_DECLARE_SHARED_ASSET_CACHE( <extern macro>, <asset> )" ); \
    template <>                                                                                                                                                  \
    struct SharedAssetCacheTraits<AssetType>                                                                                                                     \
    {                                                                                                                                                            \
        static constexpr AssetTypeDef kType = __VA_ARGS__;                                                                                                       \
    };                                                                                                                                                           \
    template class SW_API SharedAssetCache<AssetType>;                                                                                                           \
    SW_BUILT_IN_ASSET_CACHE_WITH_ID( id, ::sw::SharedAssetCache<AssetType> );                                                                                    \
    SW_ASSET_TYPE( id, SharedAssetCacheTraits<AssetType>::kType )

namespace sw
{
    /**
     * @class SharedAssetCacheSubsystem
     * @brief 모듈 종류의 공유 캐시 하나를 가진 모듈 서브시스템입니다. 기동이 레지스트리에 올리고 종료가 내립니다 — 모듈이 내려가면 캐시도 함께 사라진다.
     * @details 직접 쓰지 말고 `SW_MODULE_SHARED_ASSET_CACHE` 를 쓴다. 언리얼 `UEngineSubsystem` 이 데이터 레지스트리를 갖는 자리다(기억에 의존).
     */
    template <typename AssetType>
    class SharedAssetCacheSubsystem : public ISubsystem
    {
    public:
        SharedAssetCacheSubsystem()
            : _cache{}
            , _registration{}
        {
        }

        SubsystemInitResult initializeSubsystem() override
        {
            _registration = SharedAssetCacheLookup::registerModuleCache( _cache, SW_ASSET_CACHE_RELOAD_FUNCTION( SharedAssetCache<AssetType> ) );
            return _registration.isActive() ? SubsystemInitResult::Succeeded : SubsystemInitResult::Failed;
        }
        /** @brief 레지스트리에서 내립니다. 쥔 쪽의 에셋은 그 `shared_ptr` 가 놓을 때까지 산다. */
        void shutdownSubsystem() override { _registration.reset(); }

    private:
        SharedAssetCache<AssetType> _cache;
        Subscription                _registration; ///< 레지스트리 등록(캐시보다 먼저 풀린다 — 나중에 선언)
    };
} // namespace sw

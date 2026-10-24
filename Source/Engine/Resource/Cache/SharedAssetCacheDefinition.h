/**
 * @file SharedAssetCacheDefinition.h
 * @brief `SharedAssetCache` 의 멤버 정의입니다. 에셋 종류를 명시적으로 인스턴스화하는 Engine 의 .cpp 만 포함합니다.
 * @details 그 .cpp 는 이 헤더를 포함하고 `SharedAssetCacheTraits<에셋>` 을 특수화한 뒤 `SW_SHARED_ASSET_CACHE( 에셋 );` 한 줄로(`namespace sw`
 *          안) 명시적 인스턴스화와 레지스트리 등록을 함께 합니다. 둘 중 하나만 빠질 수 없습니다. 특수화가 갖출 것은 아래와 같습니다.
 *
 *          - `kKindName`   : 레지스트리의 종류 이름(정적 문자열)
 *          - `kMemoryTag`  : 읽는 동안 거는 할당 태그
 *          - `kReloadMode` : 다시 읽기 방식(`SharedAssetReloadMode`)
 *          - `load( path, outAsset )` : `[[nodiscard]] static bool` — 읽지 못하면 오류를 남기고 false
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Diagnostics/CacheStats.h"
#include "Core/Memory/LifetimeScope.h"
#include "Core/Memory/Memory.h"

#include "Engine/Resource/Cache/BuiltInAssetCache.h"
#include "Engine/Resource/Cache/SharedAssetCache.h"
#include "Engine/Resource/Cache/SharedAssetTable.h"

namespace sw
{
    /** @brief 파일이 바뀐 에셋을 쥔 쪽에 어떻게 보이는가입니다. */
    enum class SharedAssetReloadMode : uint8
    {
        InPlace, ///< 쥔 객체를 제자리에서 바꾼다(`SharedAssetTable::reloadShared`). 모두가 다음 읽기부터 새 내용을 본다
        Detach,  ///< 표에서 떼어 낸다(`SharedAssetTable::detachShared`). 쥔 쪽은 옛 객체를 끝까지 쥐고, 다시 `acquire` 하면 새 객체를 받는다
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 에셋 종류 하나의 계측 칸입니다(`CacheStats`, 종류 이름으로 한 번 올린다). 센 값은 이 칸에 있고 `IAssetCache` 는 고치지 않는다.
     * @details 표는 엔진 수명 범위가 소유해 엔진을 다시 세우면 새로 서지만, 칸은 프로세스 끝까지 살아 함수 정적 핸들이 그대로 유효하다.
     */
    template <typename AssetType>
    const CacheStatsHandle& getSharedAssetCacheStats()
    {
        static const CacheStatsHandle s_stats = CacheStats::registerCache( SharedAssetCacheTraits<AssetType>::kKindName );
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
        bool                                        bStartLoad = false;
        shared_ptr<SharedAssetLoadBlock<AssetType>> pBlock     = getTable().request( path, SharedAssetCacheTraits<AssetType>::kKindName, bStartLoad );
        // 새 로드를 시작해야 하면 실패, 준비된 블록이나 읽는 중인 블록을 받았으면 적중이다. 빈 경로는 조회가 아니다.
        if ( path.empty() == false )
        {
            if ( bStartLoad )
                getSharedAssetCacheStats<AssetType>().recordMiss();
            else
                getSharedAssetCacheStats<AssetType>().recordHit();
        }
        if ( bStartLoad )
            AssetLoadBlock::startLoad( pBlock, start );
        return SharedAssetHandle<AssetType>{ std::move( pBlock ) };
    }

    template <typename AssetType>
    bool SharedAssetCache<AssetType>::loadIntoTable( string_view path, shared_ptr<const AssetType>& outAsset )
    {
        const ScopedMemoryTag memoryScope{ SharedAssetCacheTraits<AssetType>::kMemoryTag };
        return getTable().loadAndIntern( path, &SharedAssetCacheTraits<AssetType>::load, outAsset );
    }

    template <typename AssetType>
    bool SharedAssetCache<AssetType>::reloadShared( string_view path )
    {
        if constexpr ( SharedAssetCacheTraits<AssetType>::kReloadMode == SharedAssetReloadMode::InPlace )
            return getTable().reloadShared( path, &SharedAssetCacheTraits<AssetType>::load );
        else
            return getTable().detachShared( path, &SharedAssetCacheTraits<AssetType>::load );
    }

    template <typename AssetType>
    uint32 SharedAssetCache<AssetType>::getReloadCount()
    {
        return getTable().getReloadCount();
    }

    template <typename AssetType>
    const utf8* SharedAssetCache<AssetType>::getAssetKindName() const
    {
        return SharedAssetCacheTraits<AssetType>::kKindName;
    }

    template <typename AssetType>
    bool SharedAssetCache<AssetType>::isCached( string_view relativePath ) const
    {
        return getTable().findLive( relativePath ) != nullptr;
    }

    template <typename AssetType>
    void SharedAssetCache<AssetType>::reload( string_view relativePath, IRHIDevice* pDevice )
    {
        (void)pDevice;
        (void)reloadShared( relativePath ); // 아무도 쥐지 않으면 다음 acquire 가 새로 읽고, 읽지 못하면 읽는 함수가 오류를 남기고 옛 내용을 지킨다
    }

    template <typename AssetType>
    size_t SharedAssetCache<AssetType>::getCachedCount() const
    {
        return getTable().countLive();
    }

    template <typename AssetType>
    void SharedAssetCache<AssetType>::clear()
    {
        getTable().clear();
        getSharedAssetCacheStats<AssetType>().recordClear();
    }

    /**
     * @brief 에셋 종류마다 하나인 표의 소유자입니다 — 엔진 수명 범위가 소유해 엔진 종료 때 표째 사라집니다(처음 쓸 때 만들어진다).
     * @details 이 헤더는 Engine 의 .cpp 만 포함하므로 모든 사본이 Engine.dll 안에 있어 모듈 핫 리로드에 사라지지 않습니다. 함수 안 정적 지역으로 두면
     *          종료 누수 보고 뒤까지 남고 엔진을 다시 세워도 비지 않는다.
     */
    template <typename AssetType>
    inline ScopeOwned<SharedAssetTable<AssetType>> s_sharedAssetTableOwner{};

    template <typename AssetType>
    SharedAssetTable<AssetType>& SharedAssetCache<AssetType>::getTable()
    {
        SharedAssetTable<AssetType>* const pTable = s_sharedAssetTableOwner<AssetType>.getOrCreate();
        SW_ASSERT( pTable != nullptr ); // nullptr 은 엔진 수명 범위가 닫힌 뒤의 접근이다 — 캐시를 쓸 엔진이 없다
        return *pTable;
    }
} // namespace sw

/**
 * @brief `SharedAssetCache<AssetType>` 을 Engine.dll 에서 내보내도록 인스턴스화하고 내장 캐시로 등록합니다(인스턴스화하는 .cpp, `namespace sw` 안, 특수화 뒤).
 */
#define SW_SHARED_ASSET_CACHE( AssetType )             \
    template class SW_API SharedAssetCache<AssetType>; \
    SW_BUILT_IN_ASSET_CACHE_WITH_ID( AssetType, ::sw::SharedAssetCache<AssetType> )

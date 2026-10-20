/**
 * @file SharedAssetCacheDefinition.h
 * @brief `SharedAssetCache` 의 멤버 정의입니다. 에셋 종류를 명시적으로 인스턴스화하는 Engine 의 .cpp 만 포함합니다.
 * @details 그 .cpp 는 이 헤더를 포함하고 `SharedAssetCacheTraits<에셋>` 을 특수화한 뒤 `template class SW_API SharedAssetCache<에셋>;` 으로
 *          인스턴스화하고, 같은 파일 끝에서 `SW_BUILT_IN_ASSET_CACHE` 로 레지스트리에 올립니다. 특수화가 갖출 것은 아래와 같습니다.
 *
 *          - `kKindName`   : 레지스트리의 종류 이름(정적 문자열)
 *          - `kMemoryTag`  : 읽는 동안 거는 할당 태그
 *          - `kReloadMode` : 다시 읽기 방식(`SharedAssetReloadMode`)
 *          - `load( path, outAsset )` : `[[nodiscard]] static bool` — 읽지 못하면 오류를 남기고 false
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Memory/Memory.h"

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
    template <typename AssetType>
    shared_ptr<const AssetType> SharedAssetCache<AssetType>::acquire( string_view path )
    {
        const ScopedMemoryTag memoryScope{ SharedAssetCacheTraits<AssetType>::kMemoryTag };
        return getTable().acquire( path, &SharedAssetCacheTraits<AssetType>::load );
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
    }

    template <typename AssetType>
    SharedAssetTable<AssetType>& SharedAssetCache<AssetType>::getTable()
    {
        static SharedAssetTable<AssetType> s_table;
        return s_table;
    }
} // namespace sw

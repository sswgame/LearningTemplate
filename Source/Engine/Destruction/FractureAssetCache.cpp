#include "pch.h"

#include "Engine/Destruction/FractureAssetCache.h"

#include "Engine/Destruction/FractureAsset.h"
#include "Engine/Resource/Cache/BuiltInAssetCache.h"
#include "Engine/Resource/Cache/SharedAssetTable.h"

namespace sw
{
    namespace
    {
        struct FractureAssetCacheInternal
        {
            [[nodiscard]] static bool loadFracture( string_view path, FractureAsset& outAsset ) { return outAsset.loadFromResource( path ); }

            static SharedAssetTable<FractureAsset>& getTable()
            {
                static SharedAssetTable<FractureAsset> s_table;
                return s_table;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    shared_ptr<const FractureAsset> FractureAssetCache::acquire( string_view path )
    {
        return FractureAssetCacheInternal::getTable().acquire( path, &FractureAssetCacheInternal::loadFracture );
    }

    bool FractureAssetCache::reloadShared( string_view path )
    {
        // 제자리로 바꾸지 않고 분리한다. 쪼갠 조각의 바디와 렌더링이 이전 형상을 가리킨 채 내용만 바뀌면 안 된다.
        return FractureAssetCacheInternal::getTable().detachShared( path, &FractureAssetCacheInternal::loadFracture );
    }

    uint64 FractureAssetCache::getReloadGeneration()
    {
        return FractureAssetCacheInternal::getTable().getReloadCount();
    }

    bool FractureAssetCache::isCached( string_view relativePath ) const
    {
        return FractureAssetCacheInternal::getTable().findLive( relativePath ) != nullptr;
    }

    void FractureAssetCache::reload( string_view relativePath, IRHIDevice* pDevice )
    {
        (void)pDevice;
        (void)reloadShared( relativePath ); // 쓰는 곳이 없으면 할 일이 없고, 못 읽으면 옛 에셋을 지킨다
    }

    size_t FractureAssetCache::getCachedCount() const
    {
        return FractureAssetCacheInternal::getTable().countLive();
    }

    void FractureAssetCache::clear()
    {
        FractureAssetCacheInternal::getTable().clear();
    }
} // namespace sw

namespace sw
{
    SW_BUILT_IN_ASSET_CACHE( FractureAssetCache );
} // namespace sw

/**
 * @file PrefabCacheReload.cpp
 * @brief 프리팹 캐시의 핫 리로드입니다(개발 전용 — 배포 구성은 `*Reload.cpp` 를 짓지 않고, 등록은 `SW_ASSET_CACHE_RELOAD_FUNCTION` 이 nullptr 이다).
 */
#include "pch.h"

#include "Engine/Object/Prefab/PrefabAsset.h"

namespace sw
{
    void PrefabCache::reload( const AssetID& assetID, IRHIDevice* )
    {
        const string_view assetRelativePath = assetID.view();
        if ( assetRelativePath.empty() )
            return;

        const string cacheKey = makeCacheKey( assetRelativePath );

        UniqueLock<shared_mutex> writeLock{ _mapCacheMutex };
        // 읽는 중인 로드는 옛 파일을 읽었을 수 있다 — 세대를 올려 그 결과가 캐시에 들지 않게 하고, 다음 요청은 새로 읽게 진행 중 표에서 뗀다.
        _cacheGeneration.fetch_add( 1, std::memory_order_acq_rel );
        _mapLoading.clear();
        _mapCache.erase( cacheKey );
        // 중첩 인스턴스(원본 경로)를 가진 프리팹은 다음 로드가 순환을 다시 보게 버린다. 고친 원본이 상위를 놓으면 순환이 된다.
        for ( auto cacheIt = _mapCache.begin(); cacheIt != _mapCache.end(); )
        {
            const vector<PrefabInstanceNode>& listNode       = cacheIt->second->getNestedInstances();
            bool                              bHasSourceNode = false;
            for ( const PrefabInstanceNode& node : listNode )
            {
                bHasSourceNode = bHasSourceNode || node.isResolved() == false;
            }
            if ( bHasSourceNode )
                cacheIt = _mapCache.erase( cacheIt );
            else
                ++cacheIt;
        }
    }
} // namespace sw

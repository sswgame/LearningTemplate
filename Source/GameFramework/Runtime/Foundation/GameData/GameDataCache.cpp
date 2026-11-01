#include "pch.h"

#include "GameFramework/Runtime/Foundation/GameData/GameDataCache.h"

#include "Engine/Asset/AssetManager.h"

#include "GameFramework/Runtime/Foundation/Framework/GameService.h"

namespace sw
{
    bool GameDataCacheRegistration::registerWithAssetManager( AssetCacheBase& cache, AssetCacheReloadFunction pReload, Subscription& outRegistration )
    {
        // 모듈 서브시스템은 엔진 서비스가 묶인 뒤에 선다(`engine::registerModuleTypes`) — 게임 서비스가 묶이기 전이면 엔진 범위를 본다(`game::internal::findHostServices`).
        AssetManager* const pAssetManager = game::findSubsystem<AssetManager>();
        if ( pAssetManager == nullptr )
        {
            SW_LOG_ERROR_ID( "GameDataCache.DataCacheNoAsset", "Data cache '%#' has no asset manager to register with", cache.getAssetKindName() );
            return false;
        }
        outRegistration = pAssetManager->registerAssetCache( &cache, pReload );
        return true;
    }
} // namespace sw

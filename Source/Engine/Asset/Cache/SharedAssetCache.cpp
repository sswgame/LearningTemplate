#include "pch.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"

#include "Core/Log/Logger.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Foundation/EngineServices.h"

namespace sw
{
    AssetCacheBase& SharedAssetCacheLookup::getCache( const utf8* pKindName )
    {
        AssetManager* const   pAssetManager = engine::findSubsystem<AssetManager>();
        AssetCacheBase* const pCache        = pAssetManager != nullptr ? pAssetManager->findAssetCache( pKindName ) : nullptr;
        if ( pCache == nullptr )
        {
            SW_LOG_ERROR_ID( "SharedAssetCache.NotRegistered", "Shared asset cache '%#' is not registered - its .cpp needs SW_SHARED_ASSET_CACHE or SW_MODULE_SHARED_ASSET_CACHE, and a module cache lives only while its module is up", pKindName );
            ::sw::internal::reportMissingRequiredService( SW_FUNCTION_SIGNATURE ); // 위 오류가 어느 종류인지 남겼다 — Release 에서도 멈춘다
        }
        return *pCache;
    }

    Subscription SharedAssetCacheLookup::registerModuleCache( AssetCacheBase& cache, AssetCacheReloadFunction pReload )
    {
        // 모듈 서브시스템은 엔진 서비스가 선 뒤에 선다(`engine::registerModuleTypes`).
        AssetManager* const pAssetManager = engine::findSubsystem<AssetManager>();
        if ( pAssetManager == nullptr )
        {
            SW_LOG_ERROR_ID( "SharedAssetCache.NoAssetManager", "Shared asset cache '%#' has no asset manager to register with", cache.getAssetKindName() );
            return Subscription{};
        }
        return pAssetManager->registerAssetCache( &cache, pReload );
    }
} // namespace sw

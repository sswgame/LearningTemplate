#include "pch.h"

#include "Engine/Localization/LocalizationReloadCache.h"

#include "Engine/Asset/Cache/BuiltInAssetCache.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Localization/LocalizationManager.h"

namespace sw
{
    bool LocalizationReloadCache::isCached( string_view relativePath ) const
    {
        return engine::areEngineServicesBound() && engine::getSubsystem<LocalizationManager>().isProjectFile( relativePath );
    }

    void LocalizationReloadCache::reload( const AssetID& assetID, IRHIDevice* /*pDevice*/ )
    {
        const string_view relativePath = assetID.view();
        if ( engine::areEngineServicesBound() )
            (void)engine::getSubsystem<LocalizationManager>().reloadChangedFile( relativePath ); // Failed 는 관리자가 오류로 남겼다 — 캐시 `reload` 는 결과를 돌려주지 않는다
    }

    size_t LocalizationReloadCache::getCachedCount() const
    {
        return engine::areEngineServicesBound() ? engine::getSubsystem<LocalizationManager>().getMountedProjectNames().size() : 0u;
    }
} // namespace sw

namespace sw
{
    SW_BUILT_IN_ASSET_CACHE( LocalizationReloadCache );
} // namespace sw

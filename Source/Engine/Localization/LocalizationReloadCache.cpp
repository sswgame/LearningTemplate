#include "pch.h"

#include "Engine/Localization/LocalizationReloadCache.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Localization/LocalizationManager.h"
#include "Engine/Resource/Cache/BuiltInAssetCache.h"

namespace sw
{
    bool LocalizationReloadCache::isCached( string_view relativePath ) const
    {
        return engine::areEngineServicesBound() && engine::getSubsystem<LocalizationManager>().isProjectFile( relativePath );
    }

    void LocalizationReloadCache::reload( const ResourceID& resourceID, IRHIDevice* /*pDevice*/ )
    {
        const string_view relativePath = resourceID.view();
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

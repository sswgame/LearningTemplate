#include "pch.h"

#include "Engine/Localization/LocalizationReloadCache.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Localization/LocalizationManager.h"

namespace sw
{
    bool LocalizationReloadCache::isCached( string_view relativePath ) const
    {
        return engine::areEngineServicesBound() && engine::getLocalizationManager().isProjectFile( relativePath );
    }

    void LocalizationReloadCache::reload( string_view relativePath, IRHIDevice* /*pDevice*/ )
    {
        if ( engine::areEngineServicesBound() )
            (void)engine::getLocalizationManager().reloadChangedFile( relativePath );
    }

    size_t LocalizationReloadCache::getCachedCount() const
    {
        return engine::areEngineServicesBound() ? engine::getLocalizationManager().getMountedProjectNames().size() : 0u;
    }
} // namespace sw

#include "pch.h"

#include "Engine/Audio/AudioReloadCache.h"

#include "Engine/Audio/IAudioService.h"
#include "Engine/Common/EngineServices.h"
#include "Engine/Resource/Cache/BuiltInAssetCache.h"

namespace sw
{
    bool AudioReloadCache::isCached( string_view relativePath ) const
    {
        return engine::areEngineServicesBound() && engine::getSubsystem<IAudioService>().isDataFile( relativePath );
    }

    void AudioReloadCache::reload( const ResourceID& resourceID, IRHIDevice* /*pDevice*/ )
    {
        if ( engine::areEngineServicesBound() )
            (void)engine::getSubsystem<IAudioService>().reloadDataFile( resourceID ); // 실패는 서비스가 오류로 남기고 지금 것을 유지한다
    }

    size_t AudioReloadCache::getCachedCount() const
    {
        return engine::areEngineServicesBound() && engine::getSubsystem<IAudioService>().getMixerPath().empty() == false ? 1u : 0u;
    }
} // namespace sw

namespace sw
{
    SW_BUILT_IN_ASSET_CACHE( AudioReloadCache );
} // namespace sw

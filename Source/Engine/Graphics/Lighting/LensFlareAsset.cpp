#include "pch.h"

#include "Engine/Graphics/Lighting/LensFlareAsset.h"

#include "Engine/Asset/Cache/SharedAssetCacheDefinition.h"
#include "Engine/Serialization/Format/ReflectedXMLFile.h"

namespace sw
{
    namespace
    {
        struct LensFlareAssetInternal
        {
            /** @brief 공유 캐시의 로더 — 못 읽은 까닭을 로그로 남기고 false 다(그 플레어를 단 빛은 플레어가 없다). */
            [[nodiscard]] static bool load( const AssetID& assetID, LensFlareAsset& outAsset )
            {
                const expected<void> loaded = outAsset.loadFromAsset( assetID );
                if ( loaded.has_value() == false )
                    SW_LOG_ERROR_ID( "LensFlareAsset.Unreadable", "Lens flare '%#' could not be loaded (%#) - lights using it draw no flare", assetID.str(), loaded.error().message() );
                return loaded.has_value();
            }
        };
    } // namespace

    expected<void> LensFlareAsset::loadFromAsset( const AssetID& assetID )
    {
        LensFlareAsset loaded;
        if ( ReflectedXMLFile::loadDesc( assetID, loaded ) == false )
            return unexpected( Error{ ErrorKind::NotFound, "the lens flare file could not be read" } );
        if ( loaded._listElement.empty() )
            return unexpected( Error{ ErrorKind::InvalidArgument, "the lens flare has no elements" } );
        *this = std::move( loaded );
        return {};
    }
} // namespace sw

namespace sw
{
    SW_SHARED_ASSET_CACHE( LensFlareAsset, LensFlareAsset, LensFlareAsset::kAssetType.withLoader( AssetTypeLoader::with<LensFlareAsset, &LensFlareAssetInternal::load>( MemoryTag::RenderCPU ) ) );
} // namespace sw

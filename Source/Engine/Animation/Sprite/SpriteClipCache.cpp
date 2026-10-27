#include "pch.h"

#include "Engine/Animation/Sprite/SpriteClipCache.h"

#include "Core/Memory/Memory.h"

#include "Engine/Animation/Sprite/SpriteClipAsset.h"
#include "Engine/Resource/Cache/SharedAssetCacheDefinition.h"

namespace sw
{
    template <>
    struct SharedAssetCacheTraits<SpriteClipAsset>
    {
        static constexpr const utf8*           kKindName   = "SpriteClip";
        static constexpr MemoryTag             kMemoryTag  = MemoryTag::Animation;
        static constexpr SharedAssetReloadMode kReloadMode = SharedAssetReloadMode::InPlace;

        [[nodiscard]] static bool load( const ResourceID& resourceID, SpriteClipAsset& outClip ) { return outClip.loadFromFile( resourceID ); }
    };
} // namespace sw

namespace sw
{
    SW_SHARED_ASSET_CACHE( SpriteClipAsset );
} // namespace sw

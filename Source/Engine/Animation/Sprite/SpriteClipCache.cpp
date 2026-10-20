#include "pch.h"

#include "Engine/Animation/Sprite/SpriteClipCache.h"

#include "Core/Memory/Memory.h"

#include "Engine/Animation/Sprite/SpriteClipAsset.h"
#include "Engine/Resource/Cache/BuiltInAssetCache.h"
#include "Engine/Resource/Cache/SharedAssetCacheDefinition.h"

namespace sw
{
    template <>
    struct SharedAssetCacheTraits<SpriteClipAsset>
    {
        static constexpr const utf8*           kKindName   = "SpriteClip";
        static constexpr MemoryTag             kMemoryTag  = MemoryTag::Animation;
        static constexpr SharedAssetReloadMode kReloadMode = SharedAssetReloadMode::InPlace;

        [[nodiscard]] static bool load( string_view path, SpriteClipAsset& outClip ) { return outClip.loadFromFile( path ); }
    };
} // namespace sw

namespace sw
{
    template class SW_API SharedAssetCache<SpriteClipAsset>;

    SW_BUILT_IN_ASSET_CACHE( SpriteClipCache );
} // namespace sw

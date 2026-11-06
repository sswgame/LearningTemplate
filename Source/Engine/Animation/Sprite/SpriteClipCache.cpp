#include "pch.h"

#include "Engine/Animation/Sprite/SpriteClipCache.h"

#include "Core/Memory/Memory.h"

#include "Engine/Animation/Sprite/SpriteClipAsset.h"
#include "Engine/Animation/Sprite/SpriteLibrary.h"
#include "Engine/Asset/Cache/SharedAssetCacheDefinition.h"

namespace sw
{
    template <>
    struct SharedAssetCacheTraits<SpriteClipAsset>
    {
        static constexpr const utf8*           kKindName   = "SpriteClip";
        static constexpr MemoryTag             kMemoryTag  = MemoryTag::Animation;
        static constexpr SharedAssetReloadMode kReloadMode = SharedAssetReloadMode::InPlace;

        [[nodiscard]] static bool load( const AssetID& assetID, SpriteClipAsset& outClip ) { return outClip.loadFromFile( assetID ); }
    };
} // namespace sw

namespace sw
{
    template <>
    struct SharedAssetCacheTraits<SpriteLibrary>
    {
        static constexpr const utf8*           kKindName   = "SpriteLibrary";
        static constexpr MemoryTag             kMemoryTag  = MemoryTag::Animation;
        static constexpr SharedAssetReloadMode kReloadMode = SharedAssetReloadMode::InPlace;

        [[nodiscard]] static bool load( const AssetID& assetID, SpriteLibrary& outLibrary ) { return outLibrary.loadFromAsset( assetID ); }
    };
} // namespace sw

namespace sw
{
    SW_SHARED_ASSET_CACHE( SpriteClipAsset );
    SW_SHARED_ASSET_CACHE( SpriteLibrary );
} // namespace sw

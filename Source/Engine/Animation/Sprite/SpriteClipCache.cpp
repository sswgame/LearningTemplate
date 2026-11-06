#include "pch.h"

#include "Engine/Animation/Sprite/SpriteClipCache.h"

#include "Core/Memory/Memory.h"

#include "Engine/Animation/Sprite/SpriteClipAsset.h"
#include "Engine/Animation/Sprite/SpriteLibrary.h"
#include "Engine/Asset/Cache/SharedAssetCacheDefinition.h"

namespace sw
{
    namespace
    {
        struct SpriteClipCacheInternal
        {
            [[nodiscard]] static bool load( const AssetID& assetID, SpriteClipAsset& outClip ) { return outClip.loadFromFile( assetID ); }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_SHARED_ASSET_CACHE( SpriteClipAsset, SpriteClipAsset, SpriteClipAsset::kAssetType.withLoader( AssetTypeLoader::with<SpriteClipAsset, &SpriteClipCacheInternal::load>( MemoryTag::Animation ) ) );
    SW_SHARED_ASSET_CACHE( SpriteLibrary, SpriteLibrary, SpriteLibrary::kAssetType.withLoader( AssetTypeLoader::of<SpriteLibrary>( MemoryTag::Animation ) ) );
} // namespace sw

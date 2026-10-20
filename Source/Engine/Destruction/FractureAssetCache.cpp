#include "pch.h"

#include "Engine/Destruction/FractureAssetCache.h"

#include "Engine/Destruction/FractureAsset.h"
#include "Engine/Resource/Cache/BuiltInAssetCache.h"
#include "Engine/Resource/Cache/SharedAssetCacheDefinition.h"

namespace sw
{
    template <>
    struct SharedAssetCacheTraits<FractureAsset>
    {
        static constexpr const utf8* kKindName  = "Fracture";
        static constexpr MemoryTag   kMemoryTag = MemoryTag::Physics;
        /** @brief 쪼갠 조각의 바디와 렌더링이 이전 형상을 가리킨 채 내용만 바뀌면 안 되므로 제자리로 바꾸지 않고 떼어 낸다. */
        static constexpr SharedAssetReloadMode kReloadMode = SharedAssetReloadMode::Detach;

        [[nodiscard]] static bool load( string_view path, FractureAsset& outAsset ) { return outAsset.loadFromResource( path ); }
    };
} // namespace sw

namespace sw
{
    template class SW_API SharedAssetCache<FractureAsset>;

    SW_BUILT_IN_ASSET_CACHE( FractureAssetCache );
} // namespace sw

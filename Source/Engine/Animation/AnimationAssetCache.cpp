#include "pch.h"

#include "Engine/Animation/AnimationAssetCache.h"

#include "Core/Memory/Memory.h"

#include "Engine/Animation/AnimationClip.h"
#include "Engine/Animation/Rig/RigAsset.h"
#include "Engine/Animation/Skeletal/Skeleton.h"
#include "Engine/Animation/Skeletal/SkeletonBoneLOD.h"
#include "Engine/Resource/Cache/BuiltInAssetCache.h"
#include "Engine/Resource/Cache/SharedAssetCacheDefinition.h"

namespace sw
{
    template <>
    struct SharedAssetCacheTraits<Skeleton>
    {
        static constexpr const utf8*           kKindName   = "Skeleton";
        static constexpr MemoryTag             kMemoryTag  = MemoryTag::Animation;
        static constexpr SharedAssetReloadMode kReloadMode = SharedAssetReloadMode::InPlace;

        [[nodiscard]] static bool load( string_view path, Skeleton& outSkeleton ) { return outSkeleton.loadFromResource( path ); }
    };
} // namespace sw

namespace sw
{
    template <>
    struct SharedAssetCacheTraits<AnimationClip>
    {
        static constexpr const utf8*           kKindName   = "AnimationClip";
        static constexpr MemoryTag             kMemoryTag  = MemoryTag::Animation;
        static constexpr SharedAssetReloadMode kReloadMode = SharedAssetReloadMode::InPlace;

        [[nodiscard]] static bool load( string_view path, AnimationClip& outClip ) { return outClip.loadFromResource( path ); }
    };
} // namespace sw

namespace sw
{
    template <>
    struct SharedAssetCacheTraits<SkeletonBoneLOD>
    {
        static constexpr const utf8*           kKindName   = "SkeletonBoneLOD";
        static constexpr MemoryTag             kMemoryTag  = MemoryTag::Animation;
        static constexpr SharedAssetReloadMode kReloadMode = SharedAssetReloadMode::InPlace;

        [[nodiscard]] static bool load( string_view path, SkeletonBoneLOD& outBoneLOD ) { return outBoneLOD.loadFromResource( path ); }
    };
} // namespace sw

namespace sw
{
    template <>
    struct SharedAssetCacheTraits<RigAsset>
    {
        static constexpr const utf8*           kKindName   = "Rig";
        static constexpr MemoryTag             kMemoryTag  = MemoryTag::Animation;
        static constexpr SharedAssetReloadMode kReloadMode = SharedAssetReloadMode::InPlace;

        [[nodiscard]] static bool load( string_view path, RigAsset& outRig ) { return outRig.loadFromResource( path ); }
    };
} // namespace sw

namespace sw
{
    template class SW_API SharedAssetCache<Skeleton>;
    template class SW_API SharedAssetCache<AnimationClip>;
    template class SW_API SharedAssetCache<SkeletonBoneLOD>;
    template class SW_API SharedAssetCache<RigAsset>;

    SW_BUILT_IN_ASSET_CACHE( SkeletonCache );
    SW_BUILT_IN_ASSET_CACHE( AnimationClipCache );
    SW_BUILT_IN_ASSET_CACHE( SkeletonBoneLODCache );
    SW_BUILT_IN_ASSET_CACHE( RigAssetCache );
} // namespace sw

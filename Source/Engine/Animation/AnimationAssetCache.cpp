#include "pch.h"

#include "Engine/Animation/AnimationAssetCache.h"

#include "Core/Memory/Memory.h"

#include "Engine/Animation/AnimationClip.h"
#include "Engine/Animation/Rig/RigAsset.h"
#include "Engine/Animation/Skeletal/Skeleton.h"
#include "Engine/Animation/Skeletal/SkeletonBoneLOD.h"
#include "Engine/Resource/Cache/SharedAssetCacheDefinition.h"

namespace sw
{
    template <>
    struct SharedAssetCacheTraits<Skeleton>
    {
        static constexpr const utf8*           kKindName   = "Skeleton";
        static constexpr MemoryTag             kMemoryTag  = MemoryTag::Animation;
        static constexpr SharedAssetReloadMode kReloadMode = SharedAssetReloadMode::InPlace;

        [[nodiscard]] static bool load( const ResourceID& resourceID, Skeleton& outSkeleton ) { return outSkeleton.loadFromResource( resourceID ); }
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

        [[nodiscard]] static bool load( const ResourceID& resourceID, AnimationClip& outClip ) { return outClip.loadFromResource( resourceID ); }
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

        [[nodiscard]] static bool load( const ResourceID& resourceID, SkeletonBoneLOD& outBoneLOD ) { return outBoneLOD.loadFromResource( resourceID ); }
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

        [[nodiscard]] static bool load( const ResourceID& resourceID, RigAsset& outRig ) { return outRig.loadFromResource( resourceID ); }
    };
} // namespace sw

namespace sw
{
    SW_SHARED_ASSET_CACHE( Skeleton );
    SW_SHARED_ASSET_CACHE( AnimationClip );
    SW_SHARED_ASSET_CACHE( SkeletonBoneLOD );
    SW_SHARED_ASSET_CACHE( RigAsset );
} // namespace sw

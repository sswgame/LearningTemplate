#include "pch.h"

#include "Engine/Animation/AnimationAssetCache.h"

#include "Core/Memory/Memory.h"

#include "Engine/Animation/AnimationClip.h"
#include "Engine/Animation/Rig/RigAsset.h"
#include "Engine/Animation/Skeletal/Skeleton.h"
#include "Engine/Animation/Skeletal/SkeletonBoneLOD.h"
#include "Engine/Asset/Cache/SharedAssetCacheDefinition.h"

namespace sw
{
    template <>
    struct SharedAssetCacheTraits<Skeleton>
    {
        static constexpr const utf8*           kKindName   = "Skeleton";
        static constexpr MemoryTag             kMemoryTag  = MemoryTag::Animation;
        static constexpr SharedAssetReloadMode kReloadMode = SharedAssetReloadMode::InPlace;

        [[nodiscard]] static bool load( const AssetID& assetID, Skeleton& outSkeleton ) { return outSkeleton.loadFromAsset( assetID ); }
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

        [[nodiscard]] static bool load( const AssetID& assetID, AnimationClip& outClip ) { return outClip.loadFromAsset( assetID ); }
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

        [[nodiscard]] static bool load( const AssetID& assetID, SkeletonBoneLOD& outBoneLOD ) { return outBoneLOD.loadFromAsset( assetID ); }
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

        [[nodiscard]] static bool load( const AssetID& assetID, RigAsset& outRig ) { return outRig.loadFromAsset( assetID ); }
    };
} // namespace sw

namespace sw
{
    SW_SHARED_ASSET_CACHE( Skeleton );
    SW_SHARED_ASSET_CACHE( AnimationClip );
    SW_SHARED_ASSET_CACHE( SkeletonBoneLOD );
    SW_SHARED_ASSET_CACHE( RigAsset );
} // namespace sw

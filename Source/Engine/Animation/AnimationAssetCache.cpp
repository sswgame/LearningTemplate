#include "pch.h"

#include "Engine/Animation/AnimationAssetCache.h"

#include "Core/Memory/Memory.h"

#include "Engine/Animation/AnimationClip.h"
#include "Engine/Animation/AnimationSet.h"
#include "Engine/Animation/Rig/RigAsset.h"
#include "Engine/Animation/Skeletal/Skeleton.h"
#include "Engine/Animation/Skeletal/SkeletonBoneLOD.h"
#include "Engine/Asset/Cache/SharedAssetCacheDefinition.h"

namespace sw
{
    SW_SHARED_ASSET_CACHE( Skeleton, Skeleton, Skeleton::kAssetType.withLoader( AssetTypeLoader::of<Skeleton>( MemoryTag::Animation ) ) );
    SW_SHARED_ASSET_CACHE( AnimationClip, AnimationClip, AnimationClip::kAssetType.withLoader( AssetTypeLoader::of<AnimationClip>( MemoryTag::Animation ) ) );
    SW_SHARED_ASSET_CACHE( AnimationSet, AnimationSet, AnimationSet::kAssetType.withLoader( AssetTypeLoader::of<AnimationSet>( MemoryTag::Animation ) ) );
    SW_SHARED_ASSET_CACHE( SkeletonBoneLOD, SkeletonBoneLOD, SkeletonBoneLOD::kAssetType.withLoader( AssetTypeLoader::of<SkeletonBoneLOD>( MemoryTag::Animation ) ) );
    SW_SHARED_ASSET_CACHE( RigAsset, RigAsset, RigAsset::kAssetType.withLoader( AssetTypeLoader::of<RigAsset>( MemoryTag::Animation ) ) );
} // namespace sw

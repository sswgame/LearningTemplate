#include "pch.h"

#include "Engine/Resource/AnimationAssetCache.h"

#include "Core/Memory/Memory.h"

#include "Engine/Animation/AnimClip.h"
#include "Engine/Animation/Rig/RigAsset.h"
#include "Engine/Animation/Skeleton.h"
#include "Engine/Animation/SkeletonBoneLod.h"
#include "Engine/Resource/SharedAssetTable.h"

namespace sw
{
    namespace
    {
        struct AnimationAssetCacheInternal
        {
            [[nodiscard]] static bool loadSkeleton( string_view path, Skeleton& outSkeleton ) { return outSkeleton.loadFromResource( path ); }
            [[nodiscard]] static bool loadClip( string_view path, AnimClip& outClip ) { return outClip.loadFromResource( path ); }
            [[nodiscard]] static bool loadRig( string_view path, RigAsset& outRig ) { return outRig.loadFromResource( path ); }
            [[nodiscard]] static bool loadBoneLod( string_view path, SkeletonBoneLod& outBoneLod ) { return outBoneLod.loadFromResource( path ); }

            static SharedAssetTable<Skeleton>& getSkeletonTable()
            {
                static SharedAssetTable<Skeleton> s_table;
                return s_table;
            }

            static SharedAssetTable<AnimClip>& getClipTable()
            {
                static SharedAssetTable<AnimClip> s_table;
                return s_table;
            }

            static SharedAssetTable<RigAsset>& getRigTable()
            {
                static SharedAssetTable<RigAsset> s_table;
                return s_table;
            }

            static SharedAssetTable<SkeletonBoneLod>& getBoneLodTable()
            {
                static SharedAssetTable<SkeletonBoneLod> s_table;
                return s_table;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    shared_ptr<const Skeleton> SkeletonCache::acquire( string_view path )
    {
        SW_MEMORY_SCOPE( Animation );
        return AnimationAssetCacheInternal::getSkeletonTable().acquire( path, &AnimationAssetCacheInternal::loadSkeleton );
    }

    bool SkeletonCache::reloadShared( string_view path )
    {
        return AnimationAssetCacheInternal::getSkeletonTable().reloadShared( path, &AnimationAssetCacheInternal::loadSkeleton );
    }

    bool SkeletonCache::isCached( string_view relativePath ) const
    {
        return AnimationAssetCacheInternal::getSkeletonTable().findLive( relativePath ) != nullptr;
    }

    void SkeletonCache::reload( string_view relativePath, IRHIDevice* pDevice )
    {
        (void)pDevice;
        (void)reloadShared( relativePath ); // 읽지 못하면 옛 내용을 그대로 쓴다 — 읽기 오류는 loadFromResource 가 남긴다
    }

    size_t SkeletonCache::getCachedCount() const
    {
        return AnimationAssetCacheInternal::getSkeletonTable().countLive();
    }

    void SkeletonCache::clear()
    {
        AnimationAssetCacheInternal::getSkeletonTable().clear();
    }

    shared_ptr<const AnimClip> AnimClipCache::acquire( string_view path )
    {
        SW_MEMORY_SCOPE( Animation );
        return AnimationAssetCacheInternal::getClipTable().acquire( path, &AnimationAssetCacheInternal::loadClip );
    }

    bool AnimClipCache::reloadShared( string_view path )
    {
        return AnimationAssetCacheInternal::getClipTable().reloadShared( path, &AnimationAssetCacheInternal::loadClip );
    }

    bool AnimClipCache::isCached( string_view relativePath ) const
    {
        return AnimationAssetCacheInternal::getClipTable().findLive( relativePath ) != nullptr;
    }

    void AnimClipCache::reload( string_view relativePath, IRHIDevice* pDevice )
    {
        (void)pDevice;
        (void)reloadShared( relativePath ); // 읽지 못하면 옛 내용을 그대로 쓴다 — 읽기 오류는 loadFromResource 가 남긴다
    }

    size_t AnimClipCache::getCachedCount() const
    {
        return AnimationAssetCacheInternal::getClipTable().countLive();
    }

    void AnimClipCache::clear()
    {
        AnimationAssetCacheInternal::getClipTable().clear();
    }

    shared_ptr<const SkeletonBoneLod> SkeletonBoneLodCache::acquire( string_view path )
    {
        return AnimationAssetCacheInternal::getBoneLodTable().acquire( path, &AnimationAssetCacheInternal::loadBoneLod );
    }

    bool SkeletonBoneLodCache::reloadShared( string_view path )
    {
        return AnimationAssetCacheInternal::getBoneLodTable().reloadShared( path, &AnimationAssetCacheInternal::loadBoneLod );
    }

    bool SkeletonBoneLodCache::isCached( string_view relativePath ) const
    {
        return AnimationAssetCacheInternal::getBoneLodTable().findLive( relativePath ) != nullptr;
    }

    void SkeletonBoneLodCache::reload( string_view relativePath, IRHIDevice* pDevice )
    {
        (void)pDevice;
        (void)reloadShared( relativePath ); // 읽지 못하면 옛 내용을 그대로 쓴다 — 읽기 오류는 loadFromResource 가 남긴다
    }

    shared_ptr<const RigAsset> RigAssetCache::acquire( string_view path )
    {
        return AnimationAssetCacheInternal::getRigTable().acquire( path, &AnimationAssetCacheInternal::loadRig );
    }

    bool RigAssetCache::reloadShared( string_view path )
    {
        return AnimationAssetCacheInternal::getRigTable().reloadShared( path, &AnimationAssetCacheInternal::loadRig );
    }

    bool RigAssetCache::isCached( string_view relativePath ) const
    {
        return AnimationAssetCacheInternal::getRigTable().findLive( relativePath ) != nullptr;
    }

    void RigAssetCache::reload( string_view relativePath, IRHIDevice* pDevice )
    {
        (void)pDevice;
        (void)reloadShared( relativePath ); // 읽지 못하면 옛 내용을 그대로 쓴다 — 읽기 오류는 loadFromResource 가 남긴다
    }

    size_t SkeletonBoneLodCache::getCachedCount() const
    {
        return AnimationAssetCacheInternal::getBoneLodTable().countLive();
    }

    void SkeletonBoneLodCache::clear()
    {
        AnimationAssetCacheInternal::getBoneLodTable().clear();
    }

    size_t RigAssetCache::getCachedCount() const
    {
        return AnimationAssetCacheInternal::getRigTable().countLive();
    }

    void RigAssetCache::clear()
    {
        AnimationAssetCacheInternal::getRigTable().clear();
    }
} // namespace sw

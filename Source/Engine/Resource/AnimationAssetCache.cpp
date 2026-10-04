#include "pch.h"

#include "Engine/Resource/AnimationAssetCache.h"

#include "Core/Concurrency/mutex.h"
#include "Core/Container/unordered_map.h"
#include "Core/File/FileUtil.h"
#include "Core/Memory/MemoryProfiler.h"

#include "Engine/Animation/AnimClip.h"
#include "Engine/Animation/Rig/RigAsset.h"
#include "Engine/Animation/Skeleton.h"

namespace sw
{
    namespace
    {
        struct AnimationAssetCacheInternal
        {
            /**
             * @brief 경로 → 약한 참조 표 하나와 잠금입니다. 종류마다 하나(프로세스 전역)입니다.
             * @details 읽기는 잠금 밖에서 합니다(파일 IO). 둘이 같은 경로를 동시에 읽으면 먼저 넣은 쪽이 남고 다른 쪽은 그것을 받습니다.
             */
            template <typename AssetType>
            struct SharedTable
            {
                mutex                                      _mutex;
                unordered_map<string, weak_ptr<AssetType>> _mapAsset;

                static string makeKey( string_view path ) { return FileUtil::normalizeSeparators( path ); }

                shared_ptr<AssetType> findLive( string_view path )
                {
                    std::scoped_lock<mutex> lock{ _mutex };
                    const auto              it = _mapAsset.find( makeKey( path ) );
                    return ( it != _mapAsset.end() ) ? it->second.lock() : nullptr;
                }

                shared_ptr<const AssetType> acquire( string_view path )
                {
                    SW_MEMORY_SCOPE( Animation );
                    if ( path.empty() )
                        return nullptr;
                    shared_ptr<const AssetType> live = findLive( path );
                    if ( live != nullptr )
                        return live;

                    shared_ptr<AssetType> loaded = make_shared<AssetType>();
                    if ( loaded->loadFromResource( path ) == false )
                        return nullptr;

                    std::scoped_lock<mutex> lock{ _mutex };
                    for ( auto iter = _mapAsset.begin(); iter != _mapAsset.end(); )
                    {
                        if ( iter->second.expired() )
                            iter = _mapAsset.erase( iter );
                        else
                            ++iter;
                    }
                    weak_ptr<AssetType>&        slot   = _mapAsset[makeKey( path )];
                    shared_ptr<const AssetType> winner = slot.lock();
                    if ( winner != nullptr )
                        return winner;
                    slot = loaded;
                    return loaded;
                }

                [[nodiscard]] bool reloadShared( string_view path )
                {
                    shared_ptr<AssetType> live = findLive( path );
                    if ( live == nullptr )
                        return false;
                    AssetType fresh;
                    if ( fresh.loadFromResource( path ) == false )
                        return false;
                    *live = std::move( fresh );
                    return true;
                }

                size_t countLive()
                {
                    std::scoped_lock<mutex> lock{ _mutex };
                    size_t                  liveCount{ 0 };
                    for ( const auto& [key, asset] : _mapAsset )
                    {
                        if ( asset.expired() == false )
                            ++liveCount;
                    }
                    return liveCount;
                }

                void clear()
                {
                    std::scoped_lock<mutex> lock{ _mutex };
                    _mapAsset.clear();
                }
            };

            static SharedTable<Skeleton>& getSkeletonTable()
            {
                static SharedTable<Skeleton> s_table;
                return s_table;
            }

            static SharedTable<AnimClip>& getClipTable()
            {
                static SharedTable<AnimClip> s_table;
                return s_table;
            }

            static SharedTable<RigAsset>& getRigTable()
            {
                static SharedTable<RigAsset> s_table;
                return s_table;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    shared_ptr<const Skeleton> SkeletonCache::acquire( string_view path )
    {
        return AnimationAssetCacheInternal::getSkeletonTable().acquire( path );
    }

    bool SkeletonCache::reloadShared( string_view path )
    {
        return AnimationAssetCacheInternal::getSkeletonTable().reloadShared( path );
    }

    bool SkeletonCache::isCached( string_view relativePath ) const
    {
        return AnimationAssetCacheInternal::getSkeletonTable().findLive( relativePath ) != nullptr;
    }

    void SkeletonCache::reload( string_view relativePath, IRHIDevice* pDevice )
    {
        (void)pDevice;
        (void)reloadShared( relativePath );
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
        return AnimationAssetCacheInternal::getClipTable().acquire( path );
    }

    bool AnimClipCache::reloadShared( string_view path )
    {
        return AnimationAssetCacheInternal::getClipTable().reloadShared( path );
    }

    bool AnimClipCache::isCached( string_view relativePath ) const
    {
        return AnimationAssetCacheInternal::getClipTable().findLive( relativePath ) != nullptr;
    }

    void AnimClipCache::reload( string_view relativePath, IRHIDevice* pDevice )
    {
        (void)pDevice;
        (void)reloadShared( relativePath );
    }

    size_t AnimClipCache::getCachedCount() const
    {
        return AnimationAssetCacheInternal::getClipTable().countLive();
    }

    void AnimClipCache::clear()
    {
        AnimationAssetCacheInternal::getClipTable().clear();
    }

    shared_ptr<const RigAsset> RigAssetCache::acquire( string_view path )
    {
        return AnimationAssetCacheInternal::getRigTable().acquire( path );
    }

    bool RigAssetCache::reloadShared( string_view path )
    {
        return AnimationAssetCacheInternal::getRigTable().reloadShared( path );
    }

    bool RigAssetCache::isCached( string_view relativePath ) const
    {
        return AnimationAssetCacheInternal::getRigTable().findLive( relativePath ) != nullptr;
    }

    void RigAssetCache::reload( string_view relativePath, IRHIDevice* pDevice )
    {
        (void)pDevice;
        (void)reloadShared( relativePath );
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

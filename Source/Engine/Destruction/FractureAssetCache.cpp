#include "pch.h"

#include "Engine/Destruction/FractureAssetCache.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/unordered_map.h"
#include "Core/File/FileUtil.h"

#include "Engine/Destruction/FractureAsset.h"

namespace sw
{
    namespace
    {
        struct FractureAssetCacheInternal
        {
            struct Table
            {
                mutex                                          _mutex;
                unordered_map<string, weak_ptr<FractureAsset>> _mapAsset;
                atomic<uint64>                                 _generation{ 0 };
            };

            static Table& getTable()
            {
                static Table s_table;
                return s_table;
            }

            static string makeKey( string_view path ) { return FileUtil::normalizeSeparators( path ); }

            static shared_ptr<FractureAsset> findLive( string_view path )
            {
                Table&                  table = getTable();
                std::scoped_lock<mutex> lock{ table._mutex };
                const auto              iter = table._mapAsset.find( makeKey( path ) );
                return iter != table._mapAsset.end() ? iter->second.lock() : nullptr;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    shared_ptr<const FractureAsset> FractureAssetCache::acquire( string_view path )
    {
        if ( path.empty() )
            return nullptr;
        shared_ptr<const FractureAsset> live = FractureAssetCacheInternal::findLive( path );
        if ( live != nullptr )
            return live;
        shared_ptr<FractureAsset> loaded = make_shared<FractureAsset>();
        if ( loaded->loadFromResource( path ) == false )
            return nullptr;
        FractureAssetCacheInternal::Table& table = FractureAssetCacheInternal::getTable();
        std::scoped_lock<mutex>            lock{ table._mutex };
        for ( auto iter = table._mapAsset.begin(); iter != table._mapAsset.end(); )
        {
            if ( iter->second.expired() )
                iter = table._mapAsset.erase( iter );
            else
                ++iter;
        }
        weak_ptr<FractureAsset>&        slot   = table._mapAsset[FractureAssetCacheInternal::makeKey( path )];
        shared_ptr<const FractureAsset> winner = slot.lock();
        if ( winner != nullptr )
            return winner;
        slot = loaded;
        return loaded;
    }

    bool FractureAssetCache::reloadShared( string_view path )
    {
        if ( FractureAssetCacheInternal::findLive( path ) == nullptr )
            return false;
        shared_ptr<FractureAsset> fresh = make_shared<FractureAsset>();
        if ( fresh->loadFromResource( path ) == false )
            return false;
        FractureAssetCacheInternal::Table& table = FractureAssetCacheInternal::getTable();
        {
            std::scoped_lock<mutex> lock{ table._mutex };
            table._mapAsset[FractureAssetCacheInternal::makeKey( path )] = fresh;
        }
        // 새 객체는 쓰는 쪽이 다시 받을 때까지 표만 들고 있다 — 약한 참조라 아무도 받지 않으면 사라지므로 한 번 더 붙들어 둔다.
        static mutex                             s_keepMutex;
        static vector<shared_ptr<FractureAsset>> s_listKeep;
        {
            std::scoped_lock<mutex> lock{ s_keepMutex };
            s_listKeep.push_back( fresh );
            if ( s_listKeep.size() > 16 )
                s_listKeep.erase( s_listKeep.begin() );
        }
        table._generation.fetch_add( 1, std::memory_order_acq_rel );
        return true;
    }

    uint64 FractureAssetCache::getReloadGeneration()
    {
        return FractureAssetCacheInternal::getTable()._generation.load( std::memory_order_acquire );
    }

    bool FractureAssetCache::isCached( string_view relativePath ) const
    {
        return FractureAssetCacheInternal::findLive( relativePath ) != nullptr;
    }

    void FractureAssetCache::reload( string_view relativePath, IRHIDevice* pDevice )
    {
        (void)pDevice;
        (void)reloadShared( relativePath );
    }

    size_t FractureAssetCache::getCachedCount() const
    {
        FractureAssetCacheInternal::Table& table = FractureAssetCacheInternal::getTable();
        std::scoped_lock<mutex>            lock{ table._mutex };
        size_t                             liveCount = 0;
        for ( const auto& [key, asset] : table._mapAsset )
        {
            if ( asset.expired() == false )
                ++liveCount;
        }
        return liveCount;
    }

    void FractureAssetCache::clear()
    {
        FractureAssetCacheInternal::Table& table = FractureAssetCacheInternal::getTable();
        std::scoped_lock<mutex>            lock{ table._mutex };
        table._mapAsset.clear();
    }
} // namespace sw

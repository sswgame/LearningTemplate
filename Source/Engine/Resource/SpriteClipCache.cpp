#include "pch.h"

#include "Engine/Resource/SpriteClipCache.h"

#include "Core/Concurrency/mutex.h"
#include "Core/Container/unordered_map.h"
#include "Core/File/FileUtil.h"
#include "Core/Memory/MemoryProfiler.h"

#include "Engine/Animation/SpriteClipAsset.h"

namespace sw
{
    namespace
    {
        struct SpriteClipCacheInternal
        {
            /** @brief 공유 표 하나입니다. 경로 → 약한 참조. 잠금과 함께 둡니다. */
            struct SharedTable
            {
                mutex                                            _mutex;
                unordered_map<string, weak_ptr<SpriteClipAsset>> _mapClip; ///< 쥔 쪽에는 const 로 준다 — 고치는 것은 `reloadShared` 뿐이다
            };

            /** @brief 프로세스에 하나인 공유 표입니다(Engine.dll 안 — 모듈 핫 리로드에 사라지지 않습니다). */
            static SharedTable& getSharedTable()
            {
                static SharedTable s_table;
                return s_table;
            }

            /** @brief 표의 키입니다 — 핫 리로드가 같은 칸을 찾도록 구분자를 맞춥니다. */
            static string makeKey( string_view path ) { return FileUtil::normalizeSeparators( path ); }

            /** @brief 그 경로를 지금 쥔 클립입니다. 없으면 nullptr 입니다. */
            static shared_ptr<SpriteClipAsset> findLive( string_view path )
            {
                SharedTable&            table = getSharedTable();
                const string            key   = makeKey( path );
                std::scoped_lock<mutex> lock{ table._mutex };
                const auto              it = table._mapClip.find( key );
                return ( it != table._mapClip.end() ) ? it->second.lock() : nullptr;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    shared_ptr<const SpriteClipAsset> SpriteClipCache::acquire( string_view path )
    {
        SW_MEMORY_SCOPE( Animation );
        if ( path.empty() )
            return nullptr;

        shared_ptr<const SpriteClipAsset> live = SpriteClipCacheInternal::findLive( path );
        if ( live != nullptr )
            return live;

        // 읽기는 잠금 밖에서 한다(파일 IO). 둘이 같은 경로를 동시에 읽으면 먼저 넣은 쪽이 남고 다른 쪽은 그것을 받는다.
        shared_ptr<SpriteClipAsset> loaded = make_shared<SpriteClipAsset>();
        if ( loaded->loadFromFile( path ) == false )
            return nullptr;

        SpriteClipCacheInternal::SharedTable& table = SpriteClipCacheInternal::getSharedTable();
        std::scoped_lock<mutex>               lock{ table._mutex };
        for ( auto iter = table._mapClip.begin(); iter != table._mapClip.end(); )
        {
            if ( iter->second.expired() )
                iter = table._mapClip.erase( iter );
            else
                ++iter;
        }
        weak_ptr<SpriteClipAsset>&        slot   = table._mapClip[SpriteClipCacheInternal::makeKey( path )];
        shared_ptr<const SpriteClipAsset> winner = slot.lock();
        if ( winner != nullptr )
            return winner;
        slot = loaded;
        return loaded;
    }

    bool SpriteClipCache::reloadShared( string_view path )
    {
        if ( path.empty() )
            return false;
        shared_ptr<SpriteClipAsset> live = SpriteClipCacheInternal::findLive( path );
        if ( live == nullptr )
            return false;

        // 읽기에 실패하면 옛 내용을 지킨다(반쯤 쓴 파일을 저장 중에 본 경우 — 다음 이벤트가 다시 읽는다).
        SpriteClipAsset fresh;
        if ( fresh.loadFromFile( path ) == false )
            return false;
        *live = std::move( fresh );
        return true;
    }

    bool SpriteClipCache::isCached( string_view relativePath ) const
    {
        return SpriteClipCacheInternal::findLive( relativePath ) != nullptr;
    }

    void SpriteClipCache::reload( string_view relativePath, IRHIDevice* pDevice )
    {
        (void)pDevice;
        (void)reloadShared( relativePath ); // 쥔 쪽이 없으면 다시 읽을 것이 없다 — 다음에 읽는 쪽이 새 내용을 읽는다
    }

    size_t SpriteClipCache::getCachedCount() const
    {
        SpriteClipCacheInternal::SharedTable& table = SpriteClipCacheInternal::getSharedTable();
        std::scoped_lock<mutex>               lock{ table._mutex };
        size_t                                liveCount{ 0 };
        for ( const auto& [key, clip] : table._mapClip )
        {
            if ( clip.expired() == false )
                ++liveCount;
        }
        return liveCount;
    }

    void SpriteClipCache::clear()
    {
        SpriteClipCacheInternal::SharedTable& table = SpriteClipCacheInternal::getSharedTable();
        std::scoped_lock<mutex>               lock{ table._mutex };
        table._mapClip.clear();
    }
} // namespace sw

#include "pch.h"

#include "Core/Diagnostics/CacheStats.h"

#include "Core/Concurrency/mutex.h"
#include "Core/Log/Logger.h"

namespace sw
{
    SW_LOG_CALLER( "CacheStats" );

    namespace
    {
        struct CacheStatsInternal
        {
            /** @brief 프로세스에 하나인 칸 배열입니다. 힙이 아니라 정적 저장소라 핸들이 쥔 포인터가 엔진을 다시 세워도 살아 있다(파일 머리말). */
            struct Registry
            {
                mutex              _mutex{};
                uint32             _entryCount{ 0 };
                CacheStatsCounters _arrEntry[CacheStats::kMaxCacheCount]{};
            };

            /** @brief 레지스트리입니다. 함수 정적 변수라 첫 사용 때 서고, 소멸자가 하는 일이 없어 정적 소멸 뒤의 기록도 안전하다. */
            static Registry& getRegistry()
            {
                static Registry s_registry;
                return s_registry;
            }

            /** @brief 이름의 칸 번호입니다(잠금 아래). 없으면 `_entryCount` 입니다. */
            static uint32 findIndex( const Registry& registry, string_view name )
            {
                for ( uint32 index = 0; index < registry._entryCount; ++index )
                {
                    if ( registry._arrEntry[index].getName() == name )
                        return index;
                }
                return registry._entryCount;
            }

            /** @brief 새 칸을 올리지 못한 까닭입니다. 잠금은 잎이라 로그는 잠금을 놓은 뒤에 쓴다(`reportAddFailure`). */
            enum class AddFailure : uint8
            {
                None,
                Full,
                BadName,
            };

            /** @brief 새 칸을 올립니다(잠금 아래). 이름이 비었거나 길거나 칸이 없으면 nullptr 이고 까닭을 @p outFailure 에 적습니다. */
            static CacheStatsCounters* addEntry( Registry& registry, string_view name, AddFailure& outFailure )
            {
                outFailure = AddFailure::None;
                if ( registry._entryCount >= CacheStats::kMaxCacheCount )
                {
                    outFailure = AddFailure::Full;
                    return nullptr;
                }
                CacheStatsCounters& entry = registry._arrEntry[registry._entryCount];
                if ( entry.trySetName( name ) == false )
                {
                    outFailure = AddFailure::BadName;
                    return nullptr;
                }
                ++registry._entryCount;
                return &entry;
            }

            /** @brief `addEntry` 가 올리지 못한 까닭을 오류 로그로 남깁니다(잠금 밖). */
            static void reportAddFailure( AddFailure failure, string_view name )
            {
                if ( failure == AddFailure::Full )
                    SW_LOG_ERROR( "Cache stats registry is full (%# entries) - '%#' is not counted. Raise CacheStats::kMaxCacheCount", CacheStats::kMaxCacheCount, name );
                else if ( failure == AddFailure::BadName )
                    SW_LOG_ERROR( "Cache stats name '%#' is empty or longer than %# characters - the cache is not counted", name, CacheStatsCounters::kMaxNameLength );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    CacheStatsHandle CacheStats::registerCache( string_view name )
    {
        CacheStatsInternal::Registry&  registry = CacheStatsInternal::getRegistry();
        bool                           bDuplicate{ false };
        CacheStatsInternal::AddFailure failure{ CacheStatsInternal::AddFailure::None };
        CacheStatsHandle               handle{};
        {
            ScopedLock<mutex> lock{ registry._mutex };
            bDuplicate = CacheStatsInternal::findIndex( registry, name ) != registry._entryCount;
            if ( bDuplicate == false )
                handle = CacheStatsHandle{ CacheStatsInternal::addEntry( registry, name, failure ) };
        }
        if ( bDuplicate ) // 잠금은 잎이다 — 로그는 놓은 뒤에 쓴다
            SW_LOG_ERROR( "Cache stats name '%#' is already registered - keeping the first", name );
        CacheStatsInternal::reportAddFailure( failure, name );
        return handle;
    }

    CacheStatsHandle CacheStats::getOrCreate( string_view name )
    {
        CacheStatsInternal::Registry&  registry = CacheStatsInternal::getRegistry();
        CacheStatsInternal::AddFailure failure{ CacheStatsInternal::AddFailure::None };
        CacheStatsHandle               handle{};
        {
            ScopedLock<mutex> lock{ registry._mutex };
            const uint32      index = CacheStatsInternal::findIndex( registry, name );
            handle                  = index != registry._entryCount ? CacheStatsHandle{ &registry._arrEntry[index] } : CacheStatsHandle{ CacheStatsInternal::addEntry( registry, name, failure ) };
        }
        CacheStatsInternal::reportAddFailure( failure, name );
        return handle;
    }

    void CacheStats::markWindowStart()
    {
        CacheStatsInternal::Registry& registry = CacheStatsInternal::getRegistry();
        ScopedLock<mutex>             lock{ registry._mutex };
        for ( uint32 index = 0; index < registry._entryCount; ++index )
        {
            CacheStatsCounters& entry = registry._arrEntry[index];
            entry._windowHitBase      = entry.getHitCount();
            entry._windowMissBase     = entry.getMissCount();
            entry._windowClearBase    = entry.getClearCount();
        }
    }

    void CacheStats::collect( vector<CacheStatsSnapshot>& outListSnapshot )
    {
        outListSnapshot.clear();
        CacheStatsInternal::Registry& registry = CacheStatsInternal::getRegistry();
        ScopedLock<mutex>             lock{ registry._mutex };
        outListSnapshot.reserve( registry._entryCount );
        for ( uint32 index = 0; index < registry._entryCount; ++index )
        {
            const CacheStatsCounters& entry = registry._arrEntry[index];
            CacheStatsSnapshot        snapshot{};
            snapshot._name            = string( entry.getName() );
            snapshot._hitCount        = entry.getHitCount() - entry._windowHitBase;
            snapshot._missCount       = entry.getMissCount() - entry._windowMissBase;
            snapshot._clearCount      = entry.getClearCount() - entry._windowClearBase;
            snapshot._itemCount       = entry._itemCount.load( std::memory_order_relaxed );
            snapshot._bItemCountKnown = entry._bItemCountReported.load( std::memory_order_relaxed ) == SW_TRUE;
            outListSnapshot.push_back( std::move( snapshot ) );
        }
    }

    uint32 CacheStats::getCount()
    {
        CacheStatsInternal::Registry& registry = CacheStatsInternal::getRegistry();
        ScopedLock<mutex>             lock{ registry._mutex };
        return registry._entryCount;
    }
} // namespace sw

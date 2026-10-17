#include "pch.h"

#include "Core/Log/LogOnce.h"

#include "Core/Concurrency/mutex.h"
#include "Core/Container/unordered_set.h"

namespace sw
{
    namespace
    {
        struct LogOnceInternal
        {
            static constexpr size_t kKindCount = static_cast<size_t>( LogOnceKind::Count );

            /** @brief 종류마다 이번 범위에서 알린 키입니다. 범위는 프로세스 하나 또는 테스트 케이스 하나입니다. */
            struct State
            {
                mutex                 _mutex;
                unordered_set<uint64> _arrClaimed[kKindCount];
            };

            static State& getState()
            {
                static State s_state;
                return s_state;
            }
        };
    } // namespace

    bool LogOnce::tryClaim( LogOnceKind kind, uint64 key )
    {
        const size_t kindIndex = static_cast<size_t>( kind );
        SW_ASSERT( kindIndex < LogOnceInternal::kKindCount );
        LogOnceInternal::State& state = LogOnceInternal::getState();
        std::lock_guard<mutex>  lock( state._mutex );
        return state._arrClaimed[kindIndex].insert( key ).second;
    }

    void LogOnce::beginScope()
    {
        LogOnceInternal::State& state = LogOnceInternal::getState();
        std::lock_guard<mutex>  lock( state._mutex );
        for ( unordered_set<uint64>& claimed : state._arrClaimed )
        {
            claimed.clear();
        }
    }
} // namespace sw

#include "pch.h"

#include "Core/Log/LogOnce.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/unordered_set.h"
#include "Core/Memory/LifetimeScope.h"

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
                atomic<uint64>        _arrAttemptCount[kKindCount]{}; ///< `tryClaim` 을 부른 횟수 — `beginScope` 가 지우지 않는다
            };

            /**
             * @brief 알린 키의 저장소입니다. 엔진 수명 범위가 소유해 엔진 종료 때 저장소째 사라집니다(처음 알릴 때 만들어진다).
             * @details Core 의 아래 층이라 서비스 범위를 쓸 수 없다. 소유자는 `ScopeOwned` 가 등록하는 엔진 수명 범위이고, 함수 안 정적 지역을 두지 않는다.
             */
            static inline ScopeOwned<State> s_state{};
        };
    } // namespace

    bool LogOnce::tryClaim( LogOnceKind kind, uint64 key )
    {
        const size_t kindIndex = static_cast<size_t>( kind );
        SW_ASSERT( kindIndex < LogOnceInternal::kKindCount );
        LogOnceInternal::State* const pState = LogOnceInternal::s_state.getOrCreate();
        // nullptr 은 엔진 수명 범위가 닫힌 뒤다(Debug 는 `getOrCreate` 가 단언한다) — 기억할 곳이 없으니 알림을 막지 않는다.
        if ( pState == nullptr )
            return true;
        pState->_arrAttemptCount[kindIndex].fetch_add( 1, std::memory_order_relaxed );
        std::lock_guard<mutex> lock( pState->_mutex );
        return pState->_arrClaimed[kindIndex].insert( key ).second;
    }

    uint64 LogOnce::getAttemptCount( LogOnceKind kind )
    {
        const size_t kindIndex = static_cast<size_t>( kind );
        SW_ASSERT( kindIndex < LogOnceInternal::kKindCount );
        const LogOnceInternal::State* const pState = LogOnceInternal::s_state.find();
        return pState != nullptr ? pState->_arrAttemptCount[kindIndex].load( std::memory_order_relaxed ) : 0u; // 상태가 없으면 아무도 부르지 않았다
    }

    void LogOnce::beginScope()
    {
        LogOnceInternal::State* const pState = LogOnceInternal::s_state.find();
        if ( pState == nullptr )
            return; // 아직 아무것도 알리지 않았다 — 잊을 키가 없다
        std::lock_guard<mutex> lock( pState->_mutex );
        for ( unordered_set<uint64>& claimed : pState->_arrClaimed )
        {
            claimed.clear();
        }
    }
} // namespace sw

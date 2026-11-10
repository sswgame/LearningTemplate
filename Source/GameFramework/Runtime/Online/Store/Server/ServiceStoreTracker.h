/**
 * @file ServiceStoreTracker.h
 * @brief 저장 왕복 하나를 람다 둘(저장소 스레드의 일 · 맡긴 스레드의 완료)로 맡기고, 맡긴 일 수를 세는 서비스 쪽 앞입니다.
 * @details 저장 왕복마다 `IServiceStoreJob` 을 상속한 클래스(입력 멤버 복사 · `run` · `complete`, 약 30 줄)를 적던 것을 람다 둘로 줄인다.
 *          UE 의 `AsyncTask` + 게임 스레드 완료, C# `Task.ContinueWith` 와 같은 모양이다.
 *          - 일 람다는 저장소 스레드에서 연결(`IServiceStoreConnection&`)을 받아 돌고, 값으로 잡은 입력만 쓴다(서비스 멤버를 만지지 않는다). 돌려준 값이 완료로 간다.
 *          - 완료 람다는 맡긴 스레드의 `pollCompletions` 에서 `( 결과&, ServiceStoreCompletion )` 또는 `( 결과& )` 로 불린다. 일이 값을 돌려주지 않으면 `( ServiceStoreCompletion )` 또는 `()` 다.
 *          - 맡긴 일을 여기서 세므로 서비스의 `isIdle` · `getOpenWorkCount` 와 종료 대기가 이 수를 본다. 일마다 `_bXPending` 깃발을 손으로 두면 빠뜨리거나 겹쳐 돌았다.
 *          @code
 *              _store.submit( [accountID]( IServiceStoreConnection& connection ) { return ServiceLedger::readBalances( connection, accountID ); },
 *                             [this]( LedgerReadResult& result, ServiceStoreCompletion completion ) { applyBalances( result, completion ); } );
 *          @endcode
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

#include "GameFramework/Runtime/Online/Store/Server/ServiceStore.h"
#include "GameFramework/Runtime/Online/Store/Server/ServiceStoreUtil.h"

#include <type_traits>

namespace sw
{
    class ServiceStoreTracker;

    /** @brief 값을 돌려주지 않는 일 람다의 자리 값입니다. */
    struct ServiceStoreNoResult
    {
    };

    /** @brief 저장소에서 읽은 값과 그 결과입니다. 일 람다가 "결과 + 목록" 을 함께 돌려줄 때 씁니다(읽기 함수가 결과를 돌려주고 값을 출력 인자로 채우는 모양). */
    template <typename TValue>
    struct ServiceStoreRead
    {
        TValue             _value{};
        ServiceStoreResult _result{ ServiceStoreResult::Unavailable };

        bool isOk() const { return _result == ServiceStoreResult::Ok; }
    };

    /** @brief 요청 하나의 저장 함수가 낸 결과(키트 `OnlineOutcome`)와 채운 응답입니다. 요청 일 람다가 돌려주고 완료가 `finish` 로 넘깁니다. */
    template <typename TOutcome, typename TReply>
    struct ServiceStoreCallResult
    {
        TOutcome _outcome{};
        TReply   _reply{};
    };

    /**
     * @class ServiceStoreFunctionJob
     * @brief 람다 둘로 선 저장 왕복입니다. `ServiceStoreTracker::submit` 만 만듭니다.
     */
    template <typename TRun, typename TComplete>
    class ServiceStoreFunctionJob final : public IServiceStoreJob
    {
    public:
        using RunResult = std::invoke_result_t<TRun&, IServiceStoreConnection&>;
        using Result    = std::conditional_t<std::is_void_v<RunResult>, ServiceStoreNoResult, RunResult>;

        ServiceStoreFunctionJob( ServiceStoreTracker& tracker, TRun run, TComplete complete )
            : _run{ std::move( run ) }
            , _complete{ std::move( complete ) }
            , _result{}
            , _pTracker{ &tracker }
        {
        }

        void run( IServiceStoreConnection& connection ) override
        {
            if constexpr ( std::is_void_v<RunResult> )
                _run( connection );
            else
                _result = _run( connection );
        }

        void complete() override;

    private:
        TRun                 _run;
        TComplete            _complete;
        Result               _result;
        ServiceStoreTracker* _pTracker;
    };
} // namespace sw

namespace sw
{
    /**
     * @class ServiceStoreTracker
     * @brief 서비스 하나가 저장소에 맡긴 일의 앞입니다. 서비스의 틱 스레드에서만 씁니다.
     * @details 완료 람다가 불리기 직전에 수를 줄인다 — 완료가 새 일을 맡기거나 `isIdle` 을 물어도 맞는다.
     *          서비스는 이것이 0 이 될 때까지 `flushUntilIdle` 로 기다린 뒤 내려간다(완료 람다가 잡은 `this` 가 살아 있어야 하므로).
     */
    class ServiceStoreTracker
    {
    public:
        ServiceStoreTracker()
            : _pStore{ nullptr }
            , _openCount{ 0 }
        {
        }

        ServiceStoreTracker( const ServiceStoreTracker& )            = delete;
        ServiceStoreTracker& operator=( const ServiceStoreTracker& ) = delete;

        /** @brief 맡길 저장소를 정합니다. 내릴 때는 null 을 줍니다. */
        void           bindStore( IServiceStore* pStore ) { _pStore = pStore; }
        IServiceStore* getStore() const { return _pStore; }
        bool           hasStore() const { return _pStore != nullptr; }

        /**
         * @brief @p run 을 저장소 스레드에 맡기고, 끝나면 @p complete 를 이 스레드에서 부릅니다. 저장소가 없으면 조립 실수이므로 어서션을 냅니다(호출부가 `hasStore` 로 먼저 가른다).
         */
        template <typename TRun, typename TComplete>
        void submit( TRun&& run, TComplete&& complete )
        {
            SW_ASSERT( _pStore != nullptr );
            if ( _pStore == nullptr )
                return;
            ++_openCount;
            _pStore->submit( make_unique<ServiceStoreFunctionJob<std::decay_t<TRun>, std::decay_t<TComplete>>>( *this, std::forward<TRun>( run ), std::forward<TComplete>( complete ) ) );
        }

        /** @brief 맡겼지만 완료가 아직 불리지 않은 일 수입니다. */
        int32 getOpenCount() const { return _openCount; }
        bool  isIdle() const { return _openCount == 0; }

        /** @brief @p isIdle() 이 true 가 되거나 시간 제한(`ServiceStoreUtil::kShutdownWaitMilliseconds`)이 지날 때까지 완료를 거둡니다. 서비스 종료가 부릅니다. */
        template <typename TIsIdle>
        void flushUntilIdle( TIsIdle&& isIdle )
        {
            ServiceStoreUtil::flushUntilIdle( _pStore, std::forward<TIsIdle>( isIdle ) );
        }

        /** @brief 걸음마다 @p onPoll() 도 부르는 `flushUntilIdle` 입니다. 저장소 완료 말고도 거둘 것이 있는 서비스(영수증 검증)가 씁니다. */
        template <typename TIsIdle, typename TPoll>
        void flushUntilIdle( TIsIdle&& isIdle, TPoll&& onPoll )
        {
            ServiceStoreUtil::flushUntilIdle( _pStore, std::forward<TIsIdle>( isIdle ), std::forward<TPoll>( onPoll ) );
        }

        /** @brief 맡긴 일이 모두 끝나거나 시간 제한이 지날 때까지 완료를 거둡니다. */
        void flushUntilIdle()
        {
            flushUntilIdle( [this]()
            { return isIdle(); } );
        }

        /** @brief 맡긴 일 하나의 완료가 불리기 직전입니다. `ServiceStoreFunctionJob::complete` 만 부릅니다. */
        void recordFinished()
        {
            SW_ASSERT( _openCount > 0 );
            --_openCount;
        }

    private:
        IServiceStore* _pStore;
        int32          _openCount;
    };
} // namespace sw

namespace sw
{
    template <typename TRun, typename TComplete>
    void ServiceStoreFunctionJob<TRun, TComplete>::complete()
    {
        _pTracker->recordFinished();
        if constexpr ( std::is_void_v<RunResult> )
        {
            if constexpr ( std::is_invocable_v<TComplete&, ServiceStoreCompletion> )
                _complete( getCompletion() );
            else
                _complete();
        }
        else
        {
            if constexpr ( std::is_invocable_v<TComplete&, Result&, ServiceStoreCompletion> )
                _complete( _result, getCompletion() );
            else
                _complete( _result );
        }
    }
} // namespace sw

/**
 * @file TaskFuture.h
 * @brief C++17 호환 Fluent 비동기 TaskFuture<T> 및 TaskPromise<T> 파이프라인
 */
#pragma once
#include "Core/Common/StdHeaders.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Task/TaskTypes.h"

namespace sw
{
    template <typename T>
    class TaskFuture;
    template <typename T>
    class TaskPromise;

    namespace internal
    {
        template <typename T>
        struct SharedFutureState
        {
            mutable mutex                                     _mutex;
            mutable std::condition_variable_any               _cv;
            atomic<bool>                                      _bReady{ false };
            atomic<bool>                                      _bHasValue{ false };
            std::aligned_storage_t<sizeof( T ), alignof( T )> _storage;
            Delegate<void( const T& )>                        _continuation;

            SharedFutureState()
                : _mutex{}
                , _cv{}
                , _bReady{ false }
                , _bHasValue{ false }
                , _storage{}
                , _continuation{}
            {
            }

            ~SharedFutureState()
            {
                reset();
            }

            void reset()
            {
                if ( _bHasValue.load( std::memory_order_acquire ) )
                {
                    reinterpret_cast<T*>( &_storage )->~T();
                    _bHasValue.store( false, std::memory_order_release );
                }
                _bReady.store( false, std::memory_order_release );
            }

            void setValue( const T& value )
            {
                Delegate<void( const T& )> cont;
                {
                    std::scoped_lock<mutex> lock{ _mutex };
                    if ( _bReady.load( std::memory_order_relaxed ) )
                        return;

                    new ( &_storage ) T( value );
                    _bHasValue.store( true, std::memory_order_release );
                    _bReady.store( true, std::memory_order_release );
                    cont = std::move( _continuation );
                }
                _cv.notify_all();
                if ( cont.isBound() )
                    cont( *reinterpret_cast<const T*>( &_storage ) );
            }

            void setValue( T&& value )
            {
                Delegate<void( const T& )> cont;
                {
                    std::scoped_lock<mutex> lock{ _mutex };
                    if ( _bReady.load( std::memory_order_relaxed ) )
                        return;

                    new ( &_storage ) T( std::move( value ) );
                    _bHasValue.store( true, std::memory_order_release );
                    _bReady.store( true, std::memory_order_release );
                    cont = std::move( _continuation );
                }
                _cv.notify_all();
                if ( cont.isBound() )
                    cont( *reinterpret_cast<const T*>( &_storage ) );
            }

            const T& get() const
            {
                wait();
                return *reinterpret_cast<const T*>( &_storage );
            }

            void wait() const
            {
                if ( _bReady.load( std::memory_order_acquire ) )
                    return;

                std::unique_lock<mutex> lock{ _mutex };
                _cv.wait( lock, [this]()
                {
                    return _bReady.load( std::memory_order_acquire );
                } );
            }

            bool waitFor( uint32 timeoutMs ) const
            {
                if ( _bReady.load( std::memory_order_acquire ) )
                    return true;

                std::unique_lock<mutex> lock{ _mutex };
                return _cv.wait_for( lock, std::chrono::milliseconds( timeoutMs ), [this]()
                {
                    return _bReady.load( std::memory_order_acquire );
                } );
            }

            void setContinuation( Delegate<void( const T& )> cont )
            {
                // 이미 끝났으면 지금 부르고, 아니면 보관한다. **락 밖에서 부른다** — 콜백이 다시
                // 이 future 를 건드릴 수 있다. 갈 곳을 하나씩만 정해 옮긴 값을 되살려 읽지 않는다
                // (`void` 특수화는 이미 이 모양인데 여기만 bool 플래그와 `std::move` 가 서로를
                // 배제한다는 사실에 기대고 있었다 — 그래서 use-after-move 억제 주석이 붙어 있었다).
                Delegate<void( const T& )> immediate;
                {
                    std::scoped_lock<mutex> lock{ _mutex };
                    if ( _bReady.load( std::memory_order_acquire ) )
                        immediate = std::move( cont );
                    else
                        _continuation = std::move( cont );
                }
                if ( immediate.isBound() )
                    immediate( *reinterpret_cast<const T*>( &_storage ) );
            }
        };

        template <>
        struct SharedFutureState<void>
        {
            mutable mutex                       _mutex;
            mutable std::condition_variable_any _cv;
            atomic<bool>                        _bReady{ false };
            Delegate<void()>                    _continuation;

            SharedFutureState()
                : _mutex{}
                , _cv{}
                , _bReady{ false }
                , _continuation{}
            {
            }

            void setValue()
            {
                Delegate<void()> cont;
                {
                    std::scoped_lock<mutex> lock{ _mutex };
                    if ( _bReady.load( std::memory_order_relaxed ) )
                        return;

                    _bReady.store( true, std::memory_order_release );
                    cont = std::move( _continuation );
                }
                _cv.notify_all();
                if ( cont.isBound() )
                    cont();
            }

            void get() const
            {
                wait();
            }

            void wait() const
            {
                if ( _bReady.load( std::memory_order_acquire ) )
                    return;

                std::unique_lock<mutex> lock{ _mutex };
                _cv.wait( lock, [this]()
                {
                    return _bReady.load( std::memory_order_acquire );
                } );
            }

            bool waitFor( uint32 timeoutMs ) const
            {
                if ( _bReady.load( std::memory_order_acquire ) )
                    return true;

                std::unique_lock<mutex> lock{ _mutex };
                return _cv.wait_for( lock, std::chrono::milliseconds( timeoutMs ), [this]()
                {
                    return _bReady.load( std::memory_order_acquire );
                } );
            }

            void setContinuation( Delegate<void()> cont )
            {
                // 이미 끝났으면 지금 부르고, 아니면 보관한다. **락 밖에서 부른다** — 콜백이 다시
                // 이 future 를 건드릴 수 있다. 옮긴 값을 조건으로 되살려 쓰지 않도록 갈 곳을
                // 하나씩만 정한다(예전에는 bool 플래그와 std::move 가 서로를 배제한다는 사실에
                // 기대고 있어서, 읽는 사람도 분석기도 use-after-move 로 볼 수밖에 없었다).
                Delegate<void()> immediate;
                {
                    std::scoped_lock<mutex> lock{ _mutex };
                    if ( _bReady.load( std::memory_order_acquire ) )
                        immediate = std::move( cont );
                    else
                        _continuation = std::move( cont );
                }
                if ( immediate.isBound() )
                    immediate();
            }
        };
    } // namespace internal

    /**
     * @class TaskFuture
     * @brief C++17 기반의 비동기 결과 수신 및 Fluent 후속 작업(.then) 체이닝 래퍼
     */
    template <typename T>
    class TaskFuture
    {
    public:
        TaskFuture()
            : _pState{ nullptr }
        {
        }

        explicit TaskFuture( sw::shared_ptr<internal::SharedFutureState<T>> pState )
            : _pState{ std::move( pState ) }
        {
        }

        bool isValid() const { return _pState != nullptr; }
        bool isReady() const { return _pState != nullptr && _pState->_bReady.load( std::memory_order_acquire ); }

        const T& get() const
        {
            SW_ASSERT( _pState != nullptr );
            return _pState->get();
        }

        void wait() const
        {
            if ( _pState != nullptr )
                _pState->wait();
        }

        bool waitFor( uint32 timeoutMs ) const
        {
            return _pState != nullptr && _pState->waitFor( timeoutMs );
        }

        template <typename F>
        auto then( F&& continuationFunc ) const -> TaskFuture<std::invoke_result_t<F, const T&>>
        {
            using ReturnType = std::invoke_result_t<F, const T&>;

            // **원본이 무효하면 결과도 무효다.** 예전에는 여기서 유효한(그러나 아무도 값을 넣어 주지
            // 않는) future 를 돌려줬고, 그것을 `wait()` 하면 **영원히 멈췄다**. 그리고 그 함정을
            // `whenAllFutures` · `whenAnyFuture` 가 각자 우회하고 있었다 — 유효한 것만 세고, 후보가
            // 하나도 없으면 무효한 future 를 돌려주도록. 우회가 두 벌이면 세 번째 호출부가 같은 함정에
            // 빠진다. 뿌리를 여기서 막는다: 무효한 future 는 `wait()` 가 곧장 돌아오고 `waitFor` 가
            // false 이며 `isValid()` 로 물어볼 수 있다 — "무효가 들어오면 무효가 나간다" 가 사슬 전체에
            // 전해진다. (`fallback()` 은 처음부터 이 자리를 바르게 다뤘다 — 값을 채워 끝낸다.)
            if ( _pState == nullptr )
                return TaskFuture<ReturnType>{};

            auto                   pNextState = sw::make_shared<internal::SharedFutureState<ReturnType>>();
            TaskFuture<ReturnType> nextFuture( pNextState );

            _pState->setContinuation( SW_DELEGATE_LAMBDA( Delegate<void( const T& )>, [pNextState, contFunc = std::forward<F>( continuationFunc )]( const T& val )
            {
                if constexpr ( std::is_void_v<ReturnType> )
                {
                    contFunc( val );
                    pNextState->setValue();
                }
                else
                {
                    pNextState->setValue( contFunc( val ) );
                }
            } ) );

            return nextFuture;
        }

        TaskFuture<T> fallback( const T& fallbackValue ) const
        {
            auto          pNextState = sw::make_shared<internal::SharedFutureState<T>>();
            TaskFuture<T> nextFuture( pNextState );

            if ( _pState == nullptr )
            {
                nextFuture._pState->setValue( fallbackValue );
                return nextFuture;
            }

            _pState->setContinuation( SW_DELEGATE_LAMBDA( Delegate<void( const T& )>, [pNextState]( const T& val )
            {
                pNextState->setValue( val );
            } ) );

            return nextFuture;
        }

    private:
        sw::shared_ptr<internal::SharedFutureState<T>> _pState;
    };

    template <>
    class TaskFuture<void>
    {
    public:
        TaskFuture()
            : _pState{ nullptr }
        {
        }

        explicit TaskFuture( sw::shared_ptr<internal::SharedFutureState<void>> pState )
            : _pState{ std::move( pState ) }
        {
        }

        bool isValid() const { return _pState != nullptr; }
        bool isReady() const { return _pState != nullptr && _pState->_bReady.load( std::memory_order_acquire ); }

        void get() const
        {
            SW_ASSERT( _pState != nullptr );
            _pState->get();
        }

        void wait() const
        {
            if ( _pState != nullptr )
                _pState->wait();
        }

        bool waitFor( uint32 timeoutMs ) const
        {
            return _pState != nullptr && _pState->waitFor( timeoutMs );
        }

        template <typename F>
        auto then( F&& continuationFunc ) const -> TaskFuture<std::invoke_result_t<F>>
        {
            using ReturnType = std::invoke_result_t<F>;

            // **원본이 무효하면 결과도 무효다.** 예전에는 여기서 유효한(그러나 아무도 값을 넣어 주지
            // 않는) future 를 돌려줬고, 그것을 `wait()` 하면 **영원히 멈췄다**. 그리고 그 함정을
            // `whenAllFutures` · `whenAnyFuture` 가 각자 우회하고 있었다 — 유효한 것만 세고, 후보가
            // 하나도 없으면 무효한 future 를 돌려주도록. 우회가 두 벌이면 세 번째 호출부가 같은 함정에
            // 빠진다. 뿌리를 여기서 막는다: 무효한 future 는 `wait()` 가 곧장 돌아오고 `waitFor` 가
            // false 이며 `isValid()` 로 물어볼 수 있다 — "무효가 들어오면 무효가 나간다" 가 사슬 전체에
            // 전해진다. (`fallback()` 은 처음부터 이 자리를 바르게 다뤘다 — 값을 채워 끝낸다.)
            if ( _pState == nullptr )
                return TaskFuture<ReturnType>{};

            auto                   pNextState = sw::make_shared<internal::SharedFutureState<ReturnType>>();
            TaskFuture<ReturnType> nextFuture( pNextState );

            _pState->setContinuation( SW_DELEGATE_LAMBDA( Delegate<void()>, [pNextState, contFunc = std::forward<F>( continuationFunc )]()
            {
                if constexpr ( std::is_void_v<ReturnType> )
                {
                    contFunc();
                    pNextState->setValue();
                }
                else
                {
                    pNextState->setValue( contFunc() );
                }
            } ) );

            return nextFuture;
        }

    private:
        sw::shared_ptr<internal::SharedFutureState<void>> _pState;
    };

    /**
     * @class TaskPromise
     * @brief 비동기 연산의 생산자(Producer) 측면에서 값을 설정하는 프라미스 클래스
     */
    template <typename T>
    class TaskPromise
    {
    public:
        TaskPromise()
            : _pState{ sw::make_shared<internal::SharedFutureState<T>>() }
        {
        }

        TaskFuture<T> getFuture() const
        {
            return TaskFuture<T>( _pState );
        }

        void setValue( const T& value )
        {
            if ( _pState != nullptr )
                _pState->setValue( value );
        }

        void setValue( T&& value )
        {
            if ( _pState != nullptr )
                _pState->setValue( std::move( value ) );
        }

    private:
        sw::shared_ptr<internal::SharedFutureState<T>> _pState;
    };

    template <>
    class TaskPromise<void>
    {
    public:
        TaskPromise()
            : _pState{ sw::make_shared<internal::SharedFutureState<void>>() }
        {
        }

        TaskFuture<void> getFuture() const
        {
            return TaskFuture<void>( _pState );
        }

        void setValue()
        {
            if ( _pState != nullptr )
                _pState->setValue();
        }

    private:
        sw::shared_ptr<internal::SharedFutureState<void>> _pState;
    };

    /**
     * @brief 여러 TaskFuture가 모두 완료될 때까지 비동기 대기하여 결과 벡터를 모아 반환합니다 (Promise.all / WhenAll).
     * @details 결과 벡터는 입력과 **같은 길이**이고 자리도 그대로다. 유효하지 않은 future 자리는
     *          기본값으로 남는다 — 그런 future 는 `then` 이 콜백을 걸어 주지 않으므로 기다릴 수가 없다.
     *          목록이 비었거나 전부 유효하지 않으면 **곧바로 끝난 future** 를 돌려준다.
     */
    template <typename T>
    inline TaskFuture<vector<T>> whenAllFutures( const vector<TaskFuture<T>>& listFuture )
    {
        // **유효하지 않은 future 는 세지 않는다.** `then` 은 상태가 없으면 콜백을 걸지 않고 그냥
        // 돌아간다 — 그런 것을 카운트다운에 넣으면 0 에 닿지 못해 결과 future 가 **영원히 끝나지
        // 않는다**. 기본 생성된 future 하나가 섞이는 것만으로 대기가 멈춘다.
        size_t validCount = 0;
        for ( const TaskFuture<T>& future : listFuture )
        {
            if ( future.isValid() )
                ++validCount;
        }

        // 빈 목록도, 전부 유효하지 않은 목록도 여기로 온다. 자리는 남기고 기본값으로 채운다.
        if ( validCount == 0 )
        {
            TaskPromise<vector<T>> promise;
            promise.setValue( vector<T>( listFuture.size() ) );
            return promise.getFuture();
        }

        struct WhenAllContext
        {
            mutex                  _mutex{};
            TaskPromise<vector<T>> _promise{};
            vector<T>              _listResult{};
            size_t                 _remaining{ 0 };
        };

        auto pCtx = sw::make_shared<WhenAllContext>();
        pCtx->_listResult.resize( listFuture.size() );
        pCtx->_remaining = validCount;

        for ( size_t futureIndex = 0; futureIndex < listFuture.size(); ++futureIndex )
        {
            if ( listFuture[futureIndex].isValid() == false )
                continue;

            listFuture[futureIndex].then( [pCtx, futureIndex]( const T& val )
            {
                bool bDone = false;
                {
                    std::scoped_lock<mutex> lock{ pCtx->_mutex };
                    pCtx->_listResult[futureIndex] = val;
                    --pCtx->_remaining;
                    if ( pCtx->_remaining == 0 )
                        bDone = true;
                }
                if ( bDone )
                    pCtx->_promise.setValue( pCtx->_listResult );
            } );
        }

        return pCtx->_promise.getFuture();
    }

    /**
     * @brief 여러 TaskFuture 중 가장 먼저 완료된 Future의 결과를 즉시 반환합니다 (Promise.race / WhenAny).
     * @details 유효하지 않은 future 는 후보에서 뺀다. 후보가 하나도 없으면 **유효하지 않은 future**
     *          (`isValid() == false`)를 돌려준다 — 값을 만들 길이 없으므로 기다리게 두면 안 된다.
     */
    template <typename T>
    inline TaskFuture<T> whenAnyFuture( const vector<TaskFuture<T>>& listFuture )
    {
        // 이길 후보가 하나도 없으면 값을 만들 길이 없다. 예전에는 **유효한** future 를 돌려줬는데
        // 아무도 값을 넣어 주지 않아 `wait()` 가 영원히 멈췄다(형제인 `whenAllFutures` 는 빈 목록을
        // 제대로 끝냈다). `isValid()` 가 false 인 future 를 돌려주면 `wait()` 는 곧장 돌아오고
        // 호출부가 "결과가 없다" 를 물어볼 수 있다.
        bool bHasValid = false;
        for ( const TaskFuture<T>& future : listFuture )
        {
            if ( future.isValid() )
            {
                bHasValid = true;
                break;
            }
        }

        if ( bHasValid == false )
            return TaskFuture<T>{};

        struct WhenAnyContext
        {
            atomic<bool>   _bTriggered{ false };
            TaskPromise<T> _promise{};
        };

        auto pCtx = sw::make_shared<WhenAnyContext>();

        for ( size_t futureIndex = 0; futureIndex < listFuture.size(); ++futureIndex )
        {
            if ( listFuture[futureIndex].isValid() == false )
                continue;

            listFuture[futureIndex].then( [pCtx]( const T& val )
            {
                bool expected = false;
                if ( pCtx->_bTriggered.compare_exchange_strong( expected, true ) )
                    pCtx->_promise.setValue( val );
            } );
        }

        return pCtx->_promise.getFuture();
    }

    /**
     * @class ITaskStateMachine
     * @brief C++17 환경에서 코루틴 대체용으로 단계별(Step-by-Step) 비동기 실행을 지원하는 상태 머신 인터페이스
     */
    class SW_API ITaskStateMachine
    {
    public:
        virtual ~ITaskStateMachine() = default;

        /**
         * @brief 상태 머신의 다음 단계를 실행합니다.
         * @return 모든 단계 완료 시 true, 다음 프레임/비동기 대기 후 계속해야 하면 false
         */
        virtual bool step() = 0;
    };

} // namespace sw

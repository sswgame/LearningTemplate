/**
 * @file TaskFuture.h
 * @brief C++17 호환 Fluent 비동기 TaskFuture<T> 및 TaskPromise<T> 파이프라인
 */
#pragma once
#include "Core/Concurrency/mutex.h"

#include "Engine/EngineMinimal.h"
#include "Engine/Utility/Task/TaskTypes.h"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <type_traits>

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
			mutable mutex									  _mutex;
			mutable std::condition_variable_any				  _cv;
			std::atomic<bool>								  _bReady{ false };
			std::atomic<bool>								  _bHasValue{ false };
			std::aligned_storage_t<sizeof( T ), alignof( T )> _storage;
			Delegate<void( const T& )>						  _continuation;

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
				bool bExecuteImmediately = false;
				{
					std::scoped_lock<mutex> lock{ _mutex };
					if ( _bReady.load( std::memory_order_acquire ) )
						bExecuteImmediately = true;
					else
						_continuation = std::move( cont );
				}
				if ( bExecuteImmediately && cont.isBound() )
					cont( *reinterpret_cast<const T*>( &_storage ) );
			}
		};

		template <>
		struct SharedFutureState<void>
		{
			mutable mutex						_mutex;
			mutable std::condition_variable_any _cv;
			std::atomic<bool>					_bReady{ false };
			Delegate<void()>					_continuation;

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
				bool bExecuteImmediately = false;
				{
					std::scoped_lock<mutex> lock{ _mutex };
					if ( _bReady.load( std::memory_order_acquire ) )
						bExecuteImmediately = true;
					else
						_continuation = std::move( cont );
				}
				if ( bExecuteImmediately && cont.isBound() )
					cont();
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
			using ReturnType				  = std::invoke_result_t<F, const T&>;
			auto				   pNextState = sw::make_shared<internal::SharedFutureState<ReturnType>>();
			TaskFuture<ReturnType> nextFuture( pNextState );

			if ( _pState == nullptr )
				return nextFuture;

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
			auto		  pNextState = sw::make_shared<internal::SharedFutureState<T>>();
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
			using ReturnType				  = std::invoke_result_t<F>;
			auto				   pNextState = sw::make_shared<internal::SharedFutureState<ReturnType>>();
			TaskFuture<ReturnType> nextFuture( pNextState );

			if ( _pState == nullptr )
				return nextFuture;

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
	 */
	template <typename T>
	inline TaskFuture<vector<T>> whenAllFutures( const vector<TaskFuture<T>>& listFutures )
	{
		if ( listFutures.empty() )
		{
			TaskPromise<vector<T>> promise;
			promise.setValue( vector<T>{} );
			return promise.getFuture();
		}

		struct WhenAllContext
		{
			mutex				   _mutex{};
			TaskPromise<vector<T>> _promise{};
			vector<T>			   _listResults{};
			size_t				   _remaining{ 0 };
		};

		auto pCtx = sw::make_shared<WhenAllContext>();
		pCtx->_listResults.resize( listFutures.size() );
		pCtx->_remaining = listFutures.size();

		for ( size_t futureIndex = 0; futureIndex < listFutures.size(); ++futureIndex )
		{
			listFutures[futureIndex].then( [pCtx, futureIndex]( const T& val )
			{
				bool bDone = false;
				{
					std::scoped_lock<mutex> lock{ pCtx->_mutex };
					pCtx->_listResults[futureIndex] = val;
					--pCtx->_remaining;
					if ( pCtx->_remaining == 0 )
						bDone = true;
				}
				if ( bDone )
					pCtx->_promise.setValue( pCtx->_listResults );
			} );
		}

		return pCtx->_promise.getFuture();
	}

	/**
	 * @brief 여러 TaskFuture 중 가장 먼저 완료된 Future의 결과를 즉시 반환합니다 (Promise.race / WhenAny).
	 */
	template <typename T>
	inline TaskFuture<T> whenAnyFuture( const vector<TaskFuture<T>>& listFutures )
	{
		if ( listFutures.empty() )
		{
			TaskPromise<T> promise;
			return promise.getFuture();
		}

		struct WhenAnyContext
		{
			std::atomic<bool> _bTriggered{ false };
			TaskPromise<T>	  _promise{};
		};

		auto pCtx = sw::make_shared<WhenAnyContext>();

		for ( size_t futureIndex = 0; futureIndex < listFutures.size(); ++futureIndex )
		{
			listFutures[futureIndex].then( [pCtx]( const T& val )
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

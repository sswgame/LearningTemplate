#include "pch.h"

#include "Core/Concurrency/DeadlockDetector.h"

#include "Core/Memory/CallStackCapture.h"
#include "Core/String/StringBuilder.h"

SW_LOG_CALLER( "DeadlockDetector" );
namespace sw
{
	namespace
	{
		/** @brief 프로세스 전역 활성 데드락 감지기 인스턴스 포인터 */
		atomic<DeadlockDetector*> s_activeDetector{ nullptr };

	} // namespace

	DeadlockDetector::DeadlockDetector()
		: _bInitialized{ false }
		, _mutex{}
		, _mapThreadState{}
		, _mapLockOwner{}
	{
	}

	DeadlockDetector::~DeadlockDetector()
	{
		shutdown();
	}

	/**
	 * @brief 데드락 감지기를 초기화하고 활성 감지기로 등록합니다.
	 */
	void DeadlockDetector::initialize()
	{
		if ( _bInitialized.exchange( true ) )
			return;

		CallStackCapture::initialize();

		DeadlockDetector* pExpected{ nullptr };
		s_activeDetector.compare_exchange_strong( pExpected, this, std::memory_order_acq_rel, std::memory_order_relaxed );
	}

	/**
	 * @brief 데드락 감지기를 종료하고 전역 등록을 해제합니다.
	 */
	void DeadlockDetector::shutdown()
	{
		if ( _bInitialized.exchange( false ) == false )
			return;

		auto pExpected = this;
		s_activeDetector.compare_exchange_strong( pExpected, nullptr, std::memory_order_acq_rel, std::memory_order_relaxed );

		CallStackCapture::shutdown();
	}

	DeadlockDetector* DeadlockDetector::getActive()
	{
		return s_activeDetector.load( std::memory_order_acquire );
	}

	/**
	 * @brief 스레드가 특정 락을 획득하기 직전 호출되어 대기 상태를 기록하고 사이클을 검사합니다.
	 */
	void DeadlockDetector::recordLockIntended( void* pLock )
	{
		if ( _bInitialized.load() == false )
			return;

		std::thread::id tid = std::this_thread::get_id();
		CallStack		currentStack;
		CallStackCapture::capture( currentStack, 2 );

		std::scoped_lock<mutex> lock{ _mutex };

		ThreadState& state		= _mapThreadState[tid];
		state._threadId			= tid;
		state._pWaitingLock		= pLock;
		state._waitingCallStack = currentStack;

		// 순환 대기 체인 발생 여부 실시간 검사
		if ( checkForCycle( tid, pLock ) )
		{
			// checkForCycle 내부에서 dumpDeadlock()이 호출되고 중단됩니다.
		}
	}

	/**
	 * @brief 스레드가 락 획득에 성공했을 때 호출되어 소유권을 기록합니다.
	 */
	void DeadlockDetector::recordLockAcquired( void* pLock )
	{
		if ( _bInitialized.load() == false )
			return;

		std::thread::id tid = std::this_thread::get_id();

		std::scoped_lock<sw::mutex> lock{ _mutex };
		ThreadState&				state = _mapThreadState[tid];
		state._pWaitingLock				  = nullptr;
		state._listHeldLock.push_back( pLock );
		state._mapAcquiredCallStack[pLock] = state._waitingCallStack; // 획득 시점 스택은 대기 시작 시점의 스택과 동일

		_mapLockOwner[pLock] = tid;
	}

	/**
	 * @brief 스레드가 락을 해제했을 때 호출되어 소유권을 반환합니다.
	 */
	void DeadlockDetector::recordLockReleased( void* pLock )
	{
		if ( _bInitialized.load() == false )
			return;

		std::thread::id tid = std::this_thread::get_id();

		std::scoped_lock<mutex> lock{ _mutex };
		ThreadState&			state = _mapThreadState[tid];

		auto it = std::find( state._listHeldLock.begin(), state._listHeldLock.end(), pLock );
		if ( it != state._listHeldLock.end() )
			state._listHeldLock.erase( it );
		state._mapAcquiredCallStack.erase( pLock );

		_mapLockOwner.erase( pLock );
	}

	/**
	 * @brief Wait-For Graph에서 시작 스레드부터 대기 체인을 따라가며 순환(Cycle) 여부를 판정합니다.
	 */
	bool DeadlockDetector::checkForCycle( std::thread::id startThreadId, void* pLockRequested )
	{
		vector<std::thread::id> listPath;
		std::thread::id			currentThread = startThreadId;
		void*					pCurrentLock  = pLockRequested;

		while ( true )
		{
			listPath.push_back( currentThread );

			auto ownerIt = _mapLockOwner.find( pCurrentLock );
			if ( ownerIt == _mapLockOwner.end() )
			{
				// 현재 락의 소유자가 없으면 데드락이 아님
				return false;
			}

			std::thread::id ownerThread = ownerIt->second;
			if ( ownerThread == startThreadId )
			{
				// 대기 체인이 시작 스레드로 되돌아왔으므로 교착상태 확정!
				dumpDeadlock( listPath );
				return true;
			}

			auto stateIt = _mapThreadState.find( ownerThread );
			if ( stateIt == _mapThreadState.end() || stateIt->second._pWaitingLock == nullptr )
			{
				// 소유자 스레드가 다른 락을 기다리고 있지 않으므로 체인 종료
				return false;
			}

			// 소유자 스레드가 대기 중인 다음 락으로 추적 진행
			currentThread = ownerThread;
			pCurrentLock  = stateIt->second._pWaitingLock;
		}
	}

	/**
	 * @brief 교착상태에 연루된 모든 스레드와 락의 호출 스택을 상세히 출력하고 디버거를 중단합니다.
	 */
	void DeadlockDetector::dumpDeadlock( const vector<std::thread::id>& cycle )
	{
		StringBuilder<constant::kMaxBuffer8192> builder;
		builder.append( "\n=======================================================\n" );
		builder.append( "[FATAL ERROR] DEADLOCK DETECTED!\n" );
		builder.append( "=======================================================\n" );
		builder.appendFormat( "Cycle size: %# threads involved.\n\n", static_cast<int32>( cycle.size() ) );

		for ( size_t cycleIndex = 0; cycleIndex < cycle.size(); ++cycleIndex )
		{
			std::thread::id tid	  = cycle[cycleIndex];
			ThreadState&	state = _mapThreadState[tid];

			uint32 tidHash = static_cast<uint32>( std::hash<std::thread::id>{}( tid ) );
			builder.appendFormat( "Thread [%#] is waiting for lock at address: %#\n", tidHash,
								  reinterpret_cast<uintptr_t>( state._pWaitingLock ) );
			builder.append( "  Waiting Call Stack:\n" );
			builder.append( CallStackCapture::symbolize( state._waitingCallStack ) );
			builder.append( "\n" );

			builder.appendFormat( "  But it currently holds %# locks:\n", static_cast<int32>( state._listHeldLock.size() ) );
			for ( void* pHeld : state._listHeldLock )
			{
				builder.appendFormat( "    - Lock at address: %#\n", reinterpret_cast<uintptr_t>( pHeld ) );
				builder.append( "    - Acquired at Call Stack:\n" );
				builder.append( CallStackCapture::symbolize( state._mapAcquiredCallStack[pHeld] ) );
				builder.append( "\n" );
			}
			builder.append( "-------------------------------------------------------\n" );
		}

		SW_LOG_ERROR( "%#", builder.c_str() );
		SW_DEBUG_BREAK();
	}

} // namespace sw

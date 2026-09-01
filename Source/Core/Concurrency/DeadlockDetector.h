/**
 * @file DeadlockDetector.h
 * @brief 런타임 콜 스택 기반 데드락 사이클 탐지기
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Process/CallStackCapture.h"

namespace sw
{
	// ------------------------------------------------------------------------------
	// 1) DeadlockDetector — intended → acquired → released
	//    대기 그래프에 사이클이 있으면 dumpDeadlock
	// ------------------------------------------------------------------------------
	/** @brief 스레드별 대기/보유 락으로 데드락 사이클을 찾습니다. */
	class SW_API DeadlockDetector
	{
	public:
		/** @brief 탐지 맵을 비운 상태로 둡니다. */
		DeadlockDetector();
		/** @brief 스레드 상태 맵을 비웁니다. */
		~DeadlockDetector();

		/** @brief 복사를 금지합니다. */
		DeadlockDetector( const DeadlockDetector& ) = delete;
		/** @brief 복사 대입을 금지합니다. */
		DeadlockDetector& operator=( const DeadlockDetector& ) = delete;

		/** @brief 탐지를 켜고 자신을 전역 활성 탐지기로 등록합니다. */
		void initialize();
		/** @brief 탐지를 끄고 맵을 비웁니다. */
		void shutdown();

		/**
		 * @brief mutex 훅이 사용하는 활성 탐지기입니다.
		 * @return 등록된 탐지기 (없으면 nullptr)
		 */
		static DeadlockDetector* getActive();

		/**
		 * @brief 락 획득을 시도하기 직전에 대기 락을 기록합니다.
		 */
		void recordLockIntended( void* pLock );

		/**
		 * @brief 락을 얻은 뒤 보유 목록에 넣고 대기를 지웁니다.
		 */
		void recordLockAcquired( void* pLock );

		/**
		 * @brief 락을 풀 때 보유 목록과 소유자 맵에서 제거합니다.
		 */
		void recordLockReleased( void* pLock );

	private:
		/** @brief 한 스레드가 기다리는 락·보유 락·콜스택입니다. */
		struct ThreadState
		{
			std::thread::id					_threadId;
			void*							_pWaitingLock{ nullptr };
			vector<void*>					_listHeldLock;
			CallStack						_waitingCallStack;
			unordered_map<void*, CallStack> _mapAcquiredCallStack;
		};

		/** @brief startThreadId 가 pLockRequested 를 기다릴 때 사이클이 있는지 봅니다. */
		bool checkForCycle( std::thread::id startThreadId, void* pLockRequested );
		/** @brief 사이클에 참여한 스레드의 대기/보유 락을 로그로 남깁니다. */
		void dumpDeadlock( const vector<std::thread::id>& listCycle );

		atomic<bool>								_bInitialized;
		mutex										_mutex;
		unordered_map<std::thread::id, ThreadState> _mapThreadState;
		unordered_map<void*, std::thread::id>		_mapLockOwner;
	};

} // namespace sw

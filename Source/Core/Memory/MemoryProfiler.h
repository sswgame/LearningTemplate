/**
 * @file MemoryProfiler.h
 * @brief 할당 추적·콜스택 프로파일, CRT/LSan 누수 검사, (옵션) global new 훅.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Memory/CallStackCapture.h"

namespace sw
{
	// ------------------------------------------------------------------------------
	// 1) MemoryTag — 할당 카테고리 (스레드 로컬)
	//    ScopedMemoryTag / SW_MEMORY_SCOPE 가 TLS를 바꿨다가 되돌림
	// ------------------------------------------------------------------------------
	enum class MemoryTag : uint32
	{
		Unknown = 0,
		Core,
		Engine,
		Graphics,
		Physics,
		Audio,
		Game,
		Editor,
		MaxTags
	};

	/** @brief 태그별 할당·해제 누적 통계입니다. */
	struct MemoryProfileStats
	{
		atomic<uint64> _totalAllocatedBytes{ 0 };
		atomic<uint64> _totalFreedBytes{ 0 };
		atomic<uint64> _currentAllocatedBytes{ 0 };
		atomic<uint64> _currentAllocationCount{ 0 };
	};

	/** @brief 콜스택별 현재 할당량입니다. */
	struct CallStackAllocInfo
	{
		CallStack _stack;
		uint64	  _currentBytes{ 0 };
		uint64	  _currentCount{ 0 };
	};

	// ------------------------------------------------------------------------------
	// 2) MemoryProfiler — 할당 추적 · 콜스택 집계 (엔진 인스턴스)
	//    누수 검사(아래 3)와 별개. CRT/LSan은 프로세스 전역
	// ------------------------------------------------------------------------------
	class SW_API MemoryProfiler
	{
	public:
		/** @brief 태그 표시 이름을 반환합니다. */
		static const utf8* getMemoryTagName( MemoryTag tag );
		/** @brief 현재 스레드의 할당 태그를 설정합니다. */
		static void setCurrentMemoryTag( MemoryTag tag );
		/** @brief 현재 스레드의 할당 태그를 반환합니다. */
		static MemoryTag getCurrentMemoryTag();
		// ------------------------------------------------------------------------------
		// 3) 플랫폼 누수 검사 — CRT(Windows) / LSan(그 외)
		//    enable → (수명 할당) → captureBaseline → shutdown 후 report
		// ------------------------------------------------------------------------------
		/** @brief 프로세스 시작 초기에 플랫폼 누수 추적을 켭니다. */
		static void enableMemoryLeakChecks();
		/** @brief 의도적 수명 할당 이후 힙 스냅샷을 저장합니다. */
		static void captureMemoryLeakBaseline();
		/** @brief 종료 후 플랫폼 누수를 보고합니다. 힙이 늘면 0이 아닙니다. */
		static int32 reportMemoryLeaks( const utf8* pPhaseTag = "shutdown" );

	public:
		/** @brief 추적 플래그와 태그 통계를 0으로 둡니다. */
		MemoryProfiler();
		/** @brief 추적 맵을 비웁니다. */
		~MemoryProfiler();

		/** @brief 복사를 금지합니다. */
		MemoryProfiler( const MemoryProfiler& ) = delete;
		/** @brief 복사 대입을 금지합니다. */
		MemoryProfiler& operator=( const MemoryProfiler& ) = delete;

		/** @brief 추적을 켜고 자신을 전역 활성 프로파일러로 등록합니다. */
		void initialize();
		/** @brief 추적을 끄고 콜스택 맵을 비웁니다. */
		void shutdown();

		/**
		 * @brief 전역 operator new/delete 훅이 기록 대상으로 삼는 프로파일러입니다.
		 * @return 등록된 프로파일러 (없으면 nullptr)
		 */
		static MemoryProfiler* getActive();

		/** @brief 할당 추적을 켜거나 끕니다. */
		void setTrackingEnabled( bool bEnabled );
		/** @brief 할당 추적 사용 여부를 반환합니다. */
		bool isTrackingEnabled() const { return _bTrackingEnabled.load( std::memory_order_relaxed ); }

		/** @brief 콜스택 세부 추적을 켜거나 끕니다. */
		void setDetailedTrackingEnabled( bool bEnabled );
		/** @brief 콜스택 세부 추적 사용 여부를 반환합니다. */
		bool isDetailedTrackingEnabled() const { return _bDetailedTrackingEnabled.load( std::memory_order_relaxed ); }

		/**
		 * @brief 메모리 할당을 기록합니다.
		 * @return 콜 스택 해시 반환
		 */
		uint64 recordAllocation( void* pPtr, size_t size, MemoryTag tag );

		/**
		 * @brief 메모리 해제를 기록합니다.
		 */
		void recordFree( void* pPtr, size_t size, MemoryTag tag, uint64 callStackHash = 0 );

		/** @brief 태그별 할당 통계를 반환합니다. */
		const MemoryProfileStats& getStats( MemoryTag tag ) const;

		/** @brief 콜스택별 현재 할당량을 복사해 돌려줍니다. */
		vector<CallStackAllocInfo> getTopCallStacks() const;

	private:
		atomic<bool> _bInitialized;
		atomic<bool> _bTrackingEnabled;
		atomic<bool> _bDetailedTrackingEnabled;

		MemoryProfileStats _arrStats[static_cast<uint32>( MemoryTag::MaxTags )];

		// 콜스택 세부 추적용 (_bDetailedTrackingEnabled가 true일 때만 사용)
		mutable mutex							  _stackMapMutex;
		unordered_map<uint64, CallStackAllocInfo> _mapCallStackAllocInfo;
	};

	/**
	 * @brief 스코프 동안 TLS 할당 태그를 바꿨다가 되돌립니다.
	 */
	struct SW_API ScopedMemoryTag
	{
		/** @brief 현재 태그를 저장하고 tag 로 바꿉니다. */
		ScopedMemoryTag( MemoryTag tag )
		{
			_prevTag = MemoryProfiler::getCurrentMemoryTag();
			MemoryProfiler::setCurrentMemoryTag( tag );
		}

		/** @brief 복사를 금지합니다. */
		ScopedMemoryTag( const ScopedMemoryTag& ) = delete;
		/** @brief 복사 대입을 금지합니다. */
		ScopedMemoryTag& operator=( const ScopedMemoryTag& ) = delete;

		/** @brief 진입 전 태그로 되돌립니다. */
		~ScopedMemoryTag() { MemoryProfiler::setCurrentMemoryTag( _prevTag ); }

	private:
		MemoryTag _prevTag;
	};
} // namespace sw

/** @brief 스코프 동안 MemoryTag::tag 로 할당을 분류합니다. Release 에서는 no-op. */
#if defined( SW_DEBUG )
	#define SW_MEMORY_SCOPE( tag ) sw::ScopedMemoryTag _scopedMemoryTag_##__LINE__( sw::MemoryTag::tag )
#else
	#define SW_MEMORY_SCOPE( tag ) ( (void)0 )
#endif

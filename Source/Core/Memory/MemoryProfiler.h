/**
 * @file MemoryProfiler.h
 * @brief 할당 추적·콜스택 프로파일, CRT/LSan 누수 검사, (옵션) global new 훅.
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
        /**
         * @brief 추적을 켠 뒤 세어진 할당 **횟수** 누계 — 해제해도 줄지 않는다.
         * @details "살아 있는 양"(위 셋)은 프레임 안에서 잡았다 놓은 것을 못 본다 — 그것이 churn 이고, 상용 엔진이
         *          프레임당 할당 수로 감시하는 것이다. 두 시점의 차를 프레임 수로 나눠 읽는다(`FrameProfileSession`).
         */
        atomic<uint64> _totalAllocationCount{ 0 };
    };

    /** @brief 콜스택별 현재 할당량입니다. */
    struct CallStackAllocInfo
    {
        CallStack _stack;
        uint64    _currentBytes{ 0 };
        uint64    _currentCount{ 0 };
        uint64    _totalBytes{ 0 }; ///< 이 자리에서 지금까지 할당한 바이트 누계 (churn)
        uint64    _totalCount{ 0 }; ///< 이 자리에서 지금까지 할당한 횟수 누계 (churn)
    };

    /** @brief `getTopCallStacks` 의 정렬 기준. */
    enum class TopCallStackOrder : uint8
    {
        LiveBytes,  ///< 지금 살아 있는 바이트 — 누수·상주 메모리를 볼 때
        TotalCount, ///< 할당 횟수 누계 — 프레임마다 잡았다 놓는 자리(churn)를 볼 때
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
        /** @brief 모든 태그의 할당 횟수 누계 합. 프레임당 할당 수는 두 시점의 차다. */
        uint64 getTotalAllocationCount() const;

        /**
         * @brief 지금 **살아 있는** 할당 수의 합 — 해제하면 줄어든다. 두 시점의 차가 0 이 아니면 그만큼 남은 것이다.
         * @details 위 누계(churn)와 쓰임이 다르다: 이쪽은 "돌려놨는가" 를 본다. 소유자를 만들었다 부순 전후로 재면
         *          그 소유자가 흘린 것을 세고, LeakSanitizer 가 없는 구성(윈도우 Debug)에서도 같은 결함을 잡는다.
         */
        uint64 getLiveAllocationCount() const;

        /** @brief 콜스택별 집계를 @p order 기준으로 내림차순 정렬해 돌려줍니다 (세부 추적이 켜져 있을 때만 채워진다). */
        vector<CallStackAllocInfo> getTopCallStacks( TopCallStackOrder order = TopCallStackOrder::LiveBytes ) const;

    private:
        atomic<bool> _bInitialized;
        atomic<bool> _bTrackingEnabled;
        atomic<bool> _bDetailedTrackingEnabled;

        MemoryProfileStats _arrStat[static_cast<uint32>( MemoryTag::MaxTags )];

        // 콜스택 세부 추적용 (_bDetailedTrackingEnabled가 true일 때만 사용)
        mutable mutex                             _stackMapMutex;
        unordered_map<uint64, CallStackAllocInfo> _mapCallStackAllocInfo;
        unordered_map<void*, uint64>              _mapPtrToCallStackHash;
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

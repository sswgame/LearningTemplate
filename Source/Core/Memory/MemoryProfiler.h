/**
 * @file MemoryProfiler.h
 * @brief 할당 추적 · 콜 스택 프로파일, CRT/LSan 누수 검사, (선택) 전역 new 훅입니다.
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
    // 1) MemoryTag — 할당 분류(스레드 로컬)
    //    ScopedMemoryTag / SW_MEMORY_SCOPE 가 TLS 값을 바꿨다가 되돌린다
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

    /** @brief 태그별 할당 · 해제 누적 통계입니다. */
    struct MemoryProfileStats
    {
        atomic<uint64> _totalAllocatedBytes{ 0 };
        atomic<uint64> _totalFreedBytes{ 0 };
        atomic<uint64> _currentAllocatedBytes{ 0 };
        atomic<uint64> _currentAllocationCount{ 0 };
        /**
         * @brief 추적을 켠 뒤로 센 할당 **횟수**의 누계입니다. 해제해도 줄지 않습니다.
         * @details "살아 있는 양"(위의 세 값)으로는 프레임 안에서 할당했다가 바로 해제한 것을 볼 수 없습니다. 그것이 churn 이고,
         *          상용 엔진이 프레임당 할당 수로 감시하는 것입니다. 두 시점의 차이를 프레임 수로 나눠 읽습니다(`FrameProfileSession`).
         */
        atomic<uint64> _totalAllocationCount{ 0 };
    };

    /** @brief 콜 스택별 현재 할당량입니다. */
    struct CallStackAllocInfo
    {
        CallStack _stack;
        uint64    _currentBytes{ 0 };
        uint64    _currentCount{ 0 };
        uint64    _totalBytes{ 0 }; ///< 이 위치에서 지금까지 할당한 바이트 누계(churn)
        uint64    _totalCount{ 0 }; ///< 이 위치에서 지금까지 할당한 횟수 누계(churn)
    };

    /** @brief `getTopCallStacks` 의 정렬 기준입니다. */
    enum class TopCallStackOrder : uint8
    {
        LiveBytes,  ///< 지금 살아 있는 바이트. 누수 · 상주 메모리를 볼 때
        TotalCount, ///< 할당 횟수 누계. 프레임마다 할당했다 해제하는 곳(churn)을 볼 때
    };

    // ------------------------------------------------------------------------------
    // 2) MemoryProfiler — 할당 추적 · 콜 스택 집계(엔진 인스턴스)
    //    누수 검사(아래 3)와는 별개다. CRT/LSan 은 프로세스 전역이다
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
        // 3) 플랫폼 누수 검사 — CRT(Windows) / LSan(그 밖)
        //    enable → (수명 할당) → captureBaseline → shutdown 뒤 report
        // ------------------------------------------------------------------------------
        /** @brief 프로세스 시작 직후에 플랫폼 누수 추적을 켭니다. */
        static void enableMemoryLeakChecks();
        /** @brief 의도적인 수명 할당을 마친 뒤 힙 스냅샷을 저장합니다. */
        static void captureMemoryLeakBaseline();
        /** @brief 종료 후 플랫폼 누수를 보고합니다. 힙이 늘었으면 0 이 아닌 값을 반환합니다. */
        static int32 reportMemoryLeaks( const utf8* pPhaseTag = "shutdown" );

    public:
        /** @brief 추적 플래그와 태그 통계를 0 으로 둡니다. */
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
         * @brief 전역 operator new/delete 훅이 기록하는 대상 프로파일러입니다.
         * @return 등록된 프로파일러(없으면 nullptr)
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
         * @return 콜 스택 해시
         */
        uint64 recordAllocation( void* pPtr, size_t size, MemoryTag tag );

        /**
         * @brief 메모리 해제를 기록합니다.
         */
        void recordFree( void* pPtr, size_t size, MemoryTag tag, uint64 callStackHash = 0 );

        /** @brief 태그별 할당 통계를 반환합니다. */
        const MemoryProfileStats& getStats( MemoryTag tag ) const;
        /** @brief 모든 태그의 할당 횟수 누계를 합한 값입니다. 프레임당 할당 수는 두 시점의 차이입니다. */
        uint64 getTotalAllocationCount() const;

        /**
         * @brief 지금 **살아 있는** 할당 수의 합입니다. 해제하면 줄어듭니다. 두 시점의 차이가 0 이 아니면 그만큼 남은 것입니다.
         * @details 위의 누계(churn)와는 쓰임이 다릅니다. 이쪽은 "되돌려 놓았는가" 를 봅니다. 어떤 소유자를 만들었다 부순 전후로
         *          재면 그 소유자가 흘린 할당을 셀 수 있고, LeakSanitizer 가 없는 구성(윈도우 Debug)에서도 같은 결함을 잡습니다.
         */
        uint64 getLiveAllocationCount() const;

        /** @brief 콜 스택별 집계를 @p order 기준 내림차순으로 정렬해 반환합니다(세부 추적이 켜져 있을 때만 채워집니다). */
        vector<CallStackAllocInfo> getTopCallStacks( TopCallStackOrder order = TopCallStackOrder::LiveBytes ) const;

    private:
        atomic<bool> _bInitialized;
        atomic<bool> _bTrackingEnabled;
        atomic<bool> _bDetailedTrackingEnabled;

        MemoryProfileStats _arrStat[static_cast<uint32>( MemoryTag::MaxTags )];

        // 콜 스택 세부 추적용(_bDetailedTrackingEnabled 가 true 일 때만 쓴다)
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

        /** @brief 들어오기 전의 태그로 되돌립니다. */
        ~ScopedMemoryTag() { MemoryProfiler::setCurrentMemoryTag( _prevTag ); }

    private:
        MemoryTag _prevTag;
    };
} // namespace sw

/** @brief 스코프 동안 할당을 MemoryTag::tag 로 분류합니다. Release 에서는 아무 일도 하지 않습니다. */
#if defined( SW_DEBUG )
    #define SW_MEMORY_SCOPE( tag ) sw::ScopedMemoryTag _scopedMemoryTag_##__LINE__( sw::MemoryTag::tag )
#else
    #define SW_MEMORY_SCOPE( tag ) ( (void)0 )
#endif

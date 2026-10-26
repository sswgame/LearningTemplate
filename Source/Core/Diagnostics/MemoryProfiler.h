/**
 * @file MemoryProfiler.h
 * @brief 할당 추적 · 콜 스택 프로파일, CRT/LSan 누수 검사, (선택) 전역 new 훅입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/ShardedCounter.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/array.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Diagnostics/CallStackCapture.h"
#include "Core/Memory/Memory.h"
#include "Core/Memory/MemoryTag.h"

namespace sw
{
    /**
     * @brief 태그 하나의 할당 · 해제 통계를 한 시점에 읽은 값입니다(`MemoryProfiler::getStats` 가 스레드 슬롯을 합쳐 만든다).
     * @details 값이라 읽은 뒤에는 바뀌지 않습니다. 새 값이 필요하면 `getStats` 를 다시 부릅니다. 칸을 차례로 읽는 동안 다른 스레드가 더한 것은
     *          들어갈 수도 빠질 수도 있습니다(한 순간의 값이 아니다).
     */
    struct MemoryProfileStats
    {
        uint64 _totalAllocatedBytes{ 0 };
        uint64 _totalFreedBytes{ 0 };
        /** @brief 지금 살아 있는 바이트입니다. 해제는 센 블록만 빼므로 합은 0 아래로 가지 않습니다(칸을 차례로 읽는 사이의 어긋남은 0 으로 읽는다). */
        uint64 _currentAllocatedBytes{ 0 };
        /** @brief 지금 살아 있는 할당 수입니다(0 아래는 0). */
        uint64 _currentAllocationCount{ 0 };
        /**
         * @brief 추적을 켠 뒤로 센 할당 **횟수**의 누계입니다. 해제해도 줄지 않습니다.
         * @details "살아 있는 양"(위의 두 값)으로는 프레임 안에서 할당했다가 바로 해제한 것을 볼 수 없습니다. 그것이 churn 이고,
         *          상용 엔진이 프레임당 할당 수로 감시하는 것입니다. 두 시점의 차이를 프레임 수로 나눠 읽습니다(`FrameProfileSession`).
         */
        uint64 _totalAllocationCount{ 0 };
        /**
         * @brief 추적을 켠 뒤(또는 `resetPeaks` 뒤) **읽을 때마다 본** 살아 있는 바이트의 최고치입니다.
         * @details 할당 경로는 칸에만 더하므로 최고치는 합을 읽는 쪽(`getStats` · 프레임마다의 `reportExceededBudgets` · 패널)이 올립니다 — 언리얼 LLM 이
         *          스레드별 상태를 프레임마다 합칠 때 최고치를 보는 것과 같습니다. 두 번 읽는 사이에 잡았다 놓은 순간 최고치는 잡히지 않습니다.
         */
        uint64 _peakAllocatedBytes{ 0 };
        /** @brief 읽을 때마다 본 살아 있는 할당 수의 최고치입니다. */
        uint64 _peakAllocationCount{ 0 };
        /** @brief 예산(바이트)입니다. 0 이면 예산이 없다. 데이터(`Config/Engine/MemoryBudget.json`)가 정한다. */
        uint64 _budgetBytes{ 0 };
        /** @brief 지금 예산을 넘은 상태로 알렸으면 true 입니다(경고는 넘을 때 한 번, 90 % 아래로 내려가면 다시 켠다). */
        bool _bOverBudget{ false };
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 스레드 슬롯 하나 안의 태그 하나의 값입니다. 할당 · 해제 경로는 지금 스레드 슬롯의 이 값만 고칩니다.
     * @details 살아 있는 양은 부호 있는 값입니다 — 한 스레드가 할당하고 다른 스레드가 해제하면 슬롯 하나는 음수가 되지만 합은 맞습니다.
     *          슬롯 안에서 태그별 값(56 B)이 이어 있어 한 스레드의 할당은 자기 슬롯의 줄 하나만 건드립니다.
     */
    struct MemoryTagShardCounter
    {
        atomic<int64>  _liveBytes{ 0 };
        atomic<int64>  _liveCount{ 0 };
        atomic<uint64> _totalAllocatedBytes{ 0 };
        atomic<uint64> _totalAllocationCount{ 0 };
        atomic<uint64> _totalFreedBytes{ 0 };
        /** @brief 태그 합계 창(`LeakWindowDetail::TagTotals`) 안에서 태어나 아직 살아 있는 바이트입니다. 창 밖 블록의 해제는 빼지 않으므로 오래된 블록이 상쇄하지 못한다. */
        atomic<int64> _windowLiveBytes{ 0 };
        /** @brief 같은 블록의 개수입니다. */
        atomic<int64> _windowLiveCount{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 스레드 슬롯 하나 — 모든 태그의 값입니다(`ShardArray` 가 슬롯마다 캐시 라인 경계에 둔다). */
    struct MemoryProfilerShard
    {
        MemoryTagShardCounter _arrTag[kMemoryTagCount]{};
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 태그마다 하나뿐인 값(최고치 · 예산)입니다. 할당 경로는 건드리지 않고, 합을 읽는 쪽이 고칩니다.
     * @details 태그마다 캐시 라인 경계에서 시작해, 합을 읽는 스레드(프레임 예산 확인 · 패널)끼리 이웃 태그 줄을 나눠 쓰지 않습니다.
     */
    struct alignas( 64 ) MemoryTagSummary
    {
        atomic<uint64> _peakAllocatedBytes{ 0 };
        atomic<uint64> _peakAllocationCount{ 0 };
        atomic<uint64> _budgetBytes{ 0 };
        atomic<bool>   _bOverBudget{ false };
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 누수 측정 창이 블록을 얼마나 자세히 기록하는지입니다(`MemoryProfiler::beginLeakWindow`).
     * @details 스택 캡처는 할당 하나에 수 마이크로초라 창이 길수록 비쌉니다. 그래서 창을 연 쪽이 값을 고릅니다 — 기본값은 없습니다.
     */
    enum class LeakWindowDetail : uint8
    {
        TagTotals,  ///< 태그별 살아 있는 바이트 · 개수만 센다(할당마다 원자 덧셈 둘). 기동 뒤부터 종료까지의 긴 창에 쓴다
        FullStacks, ///< 창 안의 모든 할당의 호출 스택을 잡고 블록마다 기록한다. 씬 로드 한 번처럼 짧은 창에 쓴다
    };

    /** @brief 누수 창 보고서의 한 줄 — 같은 태그 · 같은 스택 · 같은 회차에서 태어나 창이 닫힐 때 아직 살아 있던 블록의 합입니다. */
    struct LeakWindowEntry
    {
        uint64    _bytes{ 0 };      ///< 늘어난 바이트
        uint64    _blockCount{ 0 }; ///< 늘어난 블록 수
        uint64    _stackHash{ 0 };  ///< 할당 스택의 해시. `TagTotals` 창은 0
        string    _firstFrame;      ///< 스택의 첫 프레임(Core/Memory · Core/Container 프레임은 거른다). `TagTotals` 창은 비어 있다
        uint32    _bornRound{ 0 };  ///< 태어난 회차(1부터, `beginLeakRound` 로 넘어간다). `TagTotals` 창은 늘 1
        MemoryTag _tag{ MemoryTag::Unknown };
    };
} // namespace sw

namespace sw
{
    /**
     * @brief `MemoryProfiler::endLeakWindow` 가 돌려주는 보고서입니다. 줄은 늘어난 바이트가 큰 순서입니다.
     * @details 창 안에서 태어난 블록 가운데 창이 닫힐 때 살아 있는 것만 들어 있습니다. 창 밖에서 태어난 블록의 해제는 뺄 대상이 아니므로
     *          어떤 줄도 줄이지 않습니다. 반대로 **창을 연 스레드가 아닌 스레드의 할당도 섞여 듭니다** — `_foreignThreadBlockCount` 가 그 개수라
     *          0 이 아니면 줄의 일부는 측정 대상 밖의 일(로더 · 로그 워커)일 수 있다.
     */
    struct LeakWindowReport
    {
        bool                    _bValid{ false }; ///< 창이 열려 있었으면 true. 창 없이 `endLeakWindow` 를 부른 보고서는 false 이고 아래 값이 모두 비어 있다
        LeakWindowDetail        _detail{ LeakWindowDetail::TagTotals };
        uint32                  _roundCount{ 0 };              ///< 창 안에서 지난 회차 수(`beginLeakWindow` 가 첫 회차)
        uint64                  _totalBytes{ 0 };              ///< 모든 줄의 바이트 합
        uint64                  _totalBlockCount{ 0 };         ///< 모든 줄의 블록 수 합
        uint64                  _foreignThreadBlockCount{ 0 }; ///< 창을 연 스레드가 아닌 스레드가 낳은 블록 수(`FullStacks` 창만 안다, `TagTotals` 는 0)
        uint64                  _foreignThreadBytes{ 0 };      ///< 같은 블록의 바이트
        vector<LeakWindowEntry> _listEntry;
    };
} // namespace sw

namespace sw
{
    /** @brief `FullStacks` 창이 블록 하나에 대해 남기는 기록입니다. 해제하면 지웁니다. */
    struct LeakWindowBlock
    {
        uint64    _size{ 0 };
        uint64    _stackHash{ 0 };
        uint64    _threadKey{ 0 }; ///< 낳은 스레드의 식별 값(`MemoryProfilerInternal::getCurrentThreadKey`)
        uint32    _stamp{ 0 };     ///< 낳은 회차의 스탬프
        MemoryTag _tag{ MemoryTag::Unknown };
    };
} // namespace sw

namespace sw
{
    /**
     * @brief `FullStacks` 창의 슬롯 하나 — 블록 테이블과 스택 테이블입니다. 블록의 **주소**로 슬롯을 고릅니다.
     * @details 해제하는 스레드는 낳은 스레드와 다를 수 있어 스레드 슬롯으로는 지울 슬롯을 알 수 없습니다. 주소로 고르면 같은 블록은 늘 같은 슬롯에서 만나고,
     *          스레드들이 슬롯 열여섯에 흩어져 하나의 잠금에 몰리지 않습니다. 스택 테이블은 슬롯마다 따로 둡니다(같은 스택이 슬롯마다 한 벌씩 들어갈 수 있습니다).
     */
    struct LeakWindowShard
    {
        mutex                                 _mutex{};
        unordered_map<void*, LeakWindowBlock> _mapBlock{};
        unordered_map<uint64, CallStack>      _mapStack{};
    };
} // namespace sw

namespace sw
{
    /** @brief 콜 스택별 현재 할당량입니다. */
    struct CallStackAllocInfo
    {
        CallStack _stack;
        uint64    _currentBytes{ 0 };
        uint64    _currentCount{ 0 };
        uint64    _totalBytes{ 0 }; ///< 이 위치에서 지금까지 할당한 바이트 누계(churn)
        uint64    _totalCount{ 0 }; ///< 이 위치에서 지금까지 할당한 횟수 누계(churn)
    };
} // namespace sw

namespace sw
{
    /** @brief `getTopCallStacks` 의 정렬 기준입니다. */
    enum class TopCallStackOrder : uint8
    {
        LiveBytes,  ///< 지금 살아 있는 바이트. 누수 · 상주 메모리를 볼 때
        TotalCount, ///< 할당 횟수 누계. 프레임마다 할당했다 해제하는 곳(churn)을 볼 때
    };

    // ------------------------------------------------------------------------------
    // 1) MemoryProfiler — 할당 추적 · 콜 스택 집계(엔진 인스턴스)
    //    누수 검사(아래 2)와는 별개다. CRT/LSan 은 프로세스 전역이다
    // ------------------------------------------------------------------------------
    class SW_API MemoryProfiler final : public IAllocationTracker
    {
    public:
        /** @brief 태그 표시 이름을 반환합니다. */
        static const utf8* getMemoryTagName( MemoryTag tag );
        /** @brief 표시 이름(대소문자 무시)으로 태그를 찾습니다. 데이터(예산 표)가 태그를 이름으로 적습니다. 모르는 이름이면 false 입니다. */
        [[nodiscard]] static bool findMemoryTagByName( string_view name, MemoryTag& outTag );
        /**
         * @brief 플랫폼 힙(CRT)이 지금 들고 있는 바이트입니다. sw 할당자를 거치지 않은 할당(외부 라이브러리 · `std::allocator`)까지 포함합니다.
         * @details 태그 합과 비교하면 태그가 볼 수 없는 양이 나옵니다. sw 블록도 CRT 에서 오므로 그 헤더(블록당 48 바이트)까지 이 값에 들어 있습니다.
         *          Windows Debug CRT 에서만 측정할 수 있고, 그 밖에서는 0 입니다.
         */
        static uint64 getPlatformHeapBytes();
        /**
         * @brief 프로세스가 시작한 뒤 플랫폼 힙(CRT)이 받은 할당의 **누계**입니다(바이트 · 횟수). 해제해도 줄지 않습니다.
         * @details sw 할당자의 누계(`getTotalAllocatedBytes` · `getTotalAllocationCount`)와 비교하면 sw 할당자 밖(`std::allocator` · 외부 라이브러리 ·
         *          CRT 직접 호출)의 churn 이 나옵니다. 횟수는 CRT 의 할당 요청 번호입니다(블록 하나를 잡아 그 번호를 읽고 바로 놓는다).
         *          Windows Debug CRT 에서만 측정할 수 있습니다.
         * @return 측정할 수 없는 구성이면 false 이고 두 값은 0 입니다.
         */
        [[nodiscard]] static bool getPlatformHeapTotals( uint64& outTotalBytes, uint64& outRequestCount );
        // ------------------------------------------------------------------------------
        // 2) 플랫폼 누수 검사 — CRT(Windows) / LSan(그 밖)
        //    enable → (수명 할당) → captureMemoryLeakBaseline(= 태그 합계 누수 창 열기, 아래 3-1) → shutdown 뒤 report
        // ------------------------------------------------------------------------------
        /** @brief 프로세스 시작 직후에 플랫폼 누수 추적을 켭니다. */
        static void enableMemoryLeakChecks();
        /** @brief 의도적인 수명 할당을 마친 뒤 힙 스냅샷을 저장하고, 활성 프로파일러의 태그 합계 누수 창을 엽니다(`beginLeakWindow`). */
        static void captureMemoryLeakBaseline();
        /** @brief 종료 후 플랫폼 누수를 보고합니다. 힙이 늘었으면 0 이 아닌 값을 반환합니다. */
        static int32 reportMemoryLeaks( const utf8* pPhaseTag = "shutdown" );

    public:
        /** @brief 추적 플래그와 태그 통계를 0 으로 둡니다. */
        MemoryProfiler();
        /** @brief 추적 맵을 비웁니다. */
        ~MemoryProfiler() override;

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
         * @brief 메모리 할당을 기록합니다. 추적이 꺼져 있거나 프로파일러 안에서 잡힌 블록은 세지 않습니다(`_bCounted` 가 false).
         * @return 센 여부와 콜 스택 해시
         */
        [[nodiscard]] AllocationRecord recordAllocation( void* pPtr, size_t size, MemoryTag tag ) override;

        /**
         * @brief 메모리 해제를 기록합니다. **이 프로파일러가 센 블록만** 넘깁니다 — 할당기는 블록 헤더의 기록기 번호로 거른다.
         * @details 센 블록이면 추적을 지금 꺼 두었어도 뺍니다. 할당을 셀 때와 해제를 셀 때가 늘 짝이라 살아 있는 양이 어긋나지 않는다.
         */
        void recordFree( void* pPtr, size_t size, MemoryTag tag, const AllocationRecord& born = AllocationRecord{} ) override;

        /** @brief 태그별 할당 통계를 지금 스레드 슬롯에서 합쳐 읽은 값입니다(슬롯 수만큼 읽는다 — 할당마다가 아니라 프레임 · 패널에서 부른다). 최고치도 이때 올린다. */
        MemoryProfileStats getStats( MemoryTag tag ) const;
        /** @brief 모든 태그의 지금 살아 있는 바이트 합입니다. */
        uint64 getLiveAllocatedBytes() const;
        /** @brief 태그를 지금 살아 있는 바이트가 큰 순서로 늘어놓습니다(같으면 enum 순서). 보고와 에디터 패널이 같은 순서를 씁니다. */
        array<MemoryTag, kMemoryTagCount> makeTagOrderByLiveBytes() const;
        /** @brief 모든 태그의 할당 바이트 누계를 합한 값입니다(블록 헤더는 빼고). 해제해도 줄지 않습니다. */
        uint64 getTotalAllocatedBytes() const;
        /** @brief 모든 태그의 할당 횟수 누계를 합한 값입니다. 프레임당 할당 수는 두 시점의 차이입니다. */
        uint64 getTotalAllocationCount() const;

        /**
         * @brief 지금 **살아 있는** 할당 수의 합입니다. 해제하면 줄어듭니다. 두 시점의 차이가 0 이 아니면 그만큼 남은 것입니다.
         * @details 위의 누계(churn)와는 쓰임이 다릅니다. 이쪽은 "되돌려 놓았는가" 를 봅니다. 어떤 소유자를 만들었다 부순 전후로
         *          측정하면 그 소유자가 흘린 할당을 셀 수 있고, LeakSanitizer 가 없는 구성(윈도우 Debug)에서도 같은 결함을 잡습니다.
         */
        uint64 getLiveAllocationCount() const;

        // ------------------------------------------------------------------------------
        // 3) 최고치 · 예산 · 기준선(누수) — UE LLM 의 태그별 최고치 · 예산(`LLM_TRACKER`)과 같은 역할
        // ------------------------------------------------------------------------------
        /** @brief 모든 태그의 최고치를 지금 값으로 되돌립니다(측정 창을 새로 시작할 때). */
        void resetPeaks();
        /** @brief 태그 @p tag 의 예산을 @p budgetBytes 로 둡니다. 0 이면 예산을 없앱니다. */
        void setBudget( MemoryTag tag, uint64 budgetBytes );
        /** @brief 태그의 예산(바이트)입니다. 없으면 0 입니다. */
        uint64 getBudget( MemoryTag tag ) const;
        /** @brief 모든 예산을 없앱니다. */
        void clearBudgets();
        /**
         * @brief 살아 있는 바이트를 예산과 비교해, **이번에 새로** 넘은 태그마다 경고 한 줄을 남깁니다. 프레임마다 한 번 부릅니다(태그 수만큼 원자 읽기).
         * @details 넘은 상태는 기억합니다 — 넘어 있는 동안 다시 경고하지 않고, 예산의 90 % 아래로 내려가면 다시 켭니다(경계에서 오락가락할 때 로그가 넘치지 않게).
         * @param pOutListNewlyExceeded 있으면 이번에 새로 넘은 태그를 담습니다.
         * @return 이번에 새로 넘은 태그 수입니다.
         */
        uint32 reportExceededBudgets( vector<MemoryTag>* pOutListNewlyExceeded = nullptr );

        // ------------------------------------------------------------------------------
        // 3-1) 누수 측정 창 — beginLeakWindow → (beginLeakRound → …) → endLeakWindow
        //    창 안에서 태어나 닫힐 때 아직 살아 있는 블록만 센다. 지금까지 쓰던 "기준선 뒤로 태그가 늘어난 양" 도 이 창의 한 인스턴스다
        //    (`captureMemoryLeakBaseline` = `TagTotals` 창을 열고, 종료 끝에 `endLeakWindow` 로 닫는다)
        // ------------------------------------------------------------------------------
        /**
         * @brief 누수 측정 창을 엽니다. 이 호출이 첫 회차를 시작합니다.
         * @details 창 안의 할당에는 스탬프(`AllocationRecord::_windowStamp`)이 찍힙니다. 창 밖에서 태어난 블록의 해제는 창의 값을 줄이지 않습니다 —
         *          "기준선 뒤의 살아 있는 양 차이" 로 재면 오래된 블록을 풀 때 새 누수가 상쇄됩니다. 창은 하나만 열립니다(열린 채 다시 열면 단언).
         *          열고 닫는 스레드는 하나여야 합니다(제어 스레드). 다른 스레드의 할당도 창에 섞여 들고, 보고서가 그 개수를 따로 알려 줍니다.
         * @param detail 기본값이 없습니다 — 스택 전수 캡처는 비싸서 창마다 고릅니다(`LeakWindowDetail`).
         */
        void beginLeakWindow( LeakWindowDetail detail );
        /** @brief 열린 창 안에서 다음 회차를 시작합니다. 이 뒤에 태어난 블록은 새 회차로 보고됩니다. `FullStacks` 창만 회차를 가른다(`TagTotals` 에서는 단언). */
        void beginLeakRound();
        /** @brief 창이 열려 있으면 true 입니다. */
        bool isLeakWindowOpen() const { return _leakFirstStamp.load( std::memory_order_acquire ) != 0; }
        /**
         * @brief 창을 닫고 창 안에서 태어나 아직 살아 있는 블록을 보고합니다. 닫은 뒤의 할당에는 스탬프가 찍히지 않습니다.
         * @details **열린 창이 없으면 오류**입니다 — 단언하고 `_bValid` 가 false 인 빈 보고서를 돌려줍니다(받는 쪽이 `_bValid` 를 봐야 한다).
         */
        [[nodiscard]] LeakWindowReport endLeakWindow();
        /**
         * @brief 보고서를 stderr 로 한 줄씩 남깁니다(로거가 종료된 뒤에도 보이게 — CRT 누수 보고와 같은 경로). 엔진 종료 끝에 Debug 에서 부릅니다.
         * @return 남긴 줄 수입니다. 보고서가 `_bValid` 가 아니면 오류 한 줄을 남기고 0 입니다.
         */
        static uint32 printLeakWindowReport( const LeakWindowReport& report, const utf8* pPhaseTag );

        /** @brief 콜 스택별 집계를 @p order 기준 내림차순으로 정렬해 반환합니다(세부 추적이 켜져 있을 때만 채워집니다). */
        vector<CallStackAllocInfo> getTopCallStacks( TopCallStackOrder order = TopCallStackOrder::LiveBytes ) const;

    private:
        /**
         * @brief 태그 @p tagIndex 의 살아 있는 바이트 · 수를 칸에서 합쳐 읽고(0 아래는 0), 그 값으로 최고치를 올립니다.
         * @details 최고치는 할당 경로가 아니라 합을 읽는 곳이 올립니다(`MemoryProfileStats::_peakAllocatedBytes`).
         */
        void computeTagLive( uint32 tagIndex, uint64& outLiveBytes, uint64& outLiveCount ) const;

        /** @brief `FullStacks` 창의 블록 테이블에 @p pPtr 를 넣습니다(스택 테이블에도 없으면 넣는다). 테이블 안의 할당은 세지 않는다. */
        void addLeakWindowBlock( void* pPtr, size_t size, MemoryTag tag, uint32 stamp, const CallStack& stack );
        /** @brief 창 안에서 태어난 블록(@p stamp)이 풀릴 때 창의 값에서 뺍니다. 창 밖 · 닫힌 창의 스탬프이면 아무것도 하지 않는다. */
        void releaseLeakWindowBlock( void* pPtr, size_t size, MemoryTagShardCounter& counter, uint32 stamp );
        /** @brief 창의 블록 테이블 · 스택 테이블 · 태그 합계를 비웁니다(창을 열 때). */
        void clearLeakWindowData();
        /** @brief `FullStacks` 창을 닫을 때 테이블을 모아 줄로 만듭니다(테이블은 비운다). */
        void collectLeakWindowStacks( uint32 firstStamp, LeakWindowReport& outReport );
        /** @brief `TagTotals` 창을 닫을 때 태그 합계를 줄로 만듭니다(합계는 비운다). */
        void collectLeakWindowTagTotals( LeakWindowReport& outReport );

        atomic<bool> _bInitialized;
        atomic<bool> _bTrackingEnabled;
        atomic<bool> _bDetailedTrackingEnabled;

        ShardArray<MemoryProfilerShard> _shard;                          ///< 스레드 슬롯마다 모든 태그의 값. 할당 · 해제는 지금 스레드 슬롯만 고친다
        mutable MemoryTagSummary        _arrTagSummary[kMemoryTagCount]; ///< 태그마다 최고치 · 예산. 합을 읽는 const 함수도 최고치를 올리므로 mutable

        // 누수 측정 창. 스탬프는 1 부터 오르는 회차 번호이고 0 은 "창 밖" 이다. 32비트라 회차를 수십억 번 넘기면 돌아가지만 창 하나가 쓰는 회차는 수십 개다
        atomic<uint32>              _leakStampCounter;   ///< 마지막으로 나눠 준 스탬프
        atomic<uint32>              _leakFirstStamp;     ///< 열린 창의 첫 스탬프. 0 이면 창이 닫혀 있다
        atomic<uint32>              _leakCurrentStamp;   ///< 지금 회차의 스탬프. 새 할당이 찍는다. 0 이면 찍지 않는다
        atomic<bool>                _bLeakWindowStacks;  ///< 열린 창이 `FullStacks` 이면 true
        atomic<uint64>              _leakOwnerThreadKey; ///< 창을 연 스레드의 식별 값
        ShardArray<LeakWindowShard> _leakShard;          ///< `FullStacks` 창의 블록 · 스택 테이블(블록 주소로 슬롯을 고른다)

        // 콜 스택 세부 추적용 — 할당 때 `_bDetailedTrackingEnabled` 가 true 였던 블록만 올라간다(해제는 블록 헤더의 해시로 내린다)
        mutable mutex                                                    _stackMapMutex;
        unordered_map<uint64, CallStackAllocInfo> _mapCallStackAllocInfo SW_GUARDED_BY( _stackMapMutex );
    };
} // namespace sw

/**
 * @file ProfilerTimeline.h
 * @brief 계측 구간(`SW_PROFILE_SCOPE`)을 스레드마다 링 버퍼에 남기는 미니 타임라인입니다 — 켤 때만 기록합니다.
 * @details 에디터 프로파일러 패널의 Timeline 탭이 읽습니다. 빠른 확인(스레드 사이 겹침 · 기다림)은 이것으로, 긴 분석은 Tracy 로 합니다.
 *          유니티 Profiler 의 Timeline 보기가 에디터 안에 있는 것에 해당합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

/**
 * @brief 트레이스 내보내기(긴 캡처 · Chrome Trace Event 저장)가 컴파일되는 구성이면 1 입니다(Dev). Shipping 은 0 이고 긴 캡처 기록 경로 · 변환기 · 개발 명령이 모두 빠집니다.
 */
#if defined( SW_SHIPPING )
    #define SW_PROFILER_TRACE_ENABLED 0
#else
    #define SW_PROFILER_TRACE_ENABLED 1
#endif

namespace sw
{
    /** @brief 타임라인 사건 하나 — 계측 구간 하나의 시작 · 끝(단조 시계 나노초), `FrameProfiler` 슬롯, 깊이(바깥 구간 0)입니다. */
    struct ProfilerTimelineEvent
    {
        uint64 _beginNanos{ 0 };
        uint64 _endNanos{ 0 };
        uint32 _slot{ 0 };
        uint16 _depth{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 한 스레드의 사건 목록입니다(시작 시각 순). 이름은 OS 스레드 이름, 없으면 "Thread <번호>" 입니다. */
    struct ProfilerTimelineThread
    {
        string                        _name;
        vector<ProfilerTimelineEvent> _listEvent;
    };
} // namespace sw

namespace sw
{
    /**
     * @class ProfilerTimeline
     * @brief 계측 구간을 스레드마다 링 버퍼(스레드당 `kEventCapacity` 사건)에 남깁니다. 꺼져 있으면 스코프의 비용은 bool 하나 읽기입니다.
     * @details 쓰는 쪽은 자기 스레드 버퍼 하나뿐이라 잠금이 없습니다. 버퍼는 그 스레드가 처음 기록할 때 만들고 타임라인이 종료될 때까지 둡니다
     *          (스레드가 끝나도 남은 사건을 읽을 수 있게). 읽는 쪽(에디터 패널)은 버퍼마다 쓰기 수를 읽고 그 앞을 복사한 뒤, 복사하는 동안 덮였을 수
     *          있는 사건을 쓰기 수로 다시 골라 버립니다. 사건 칸은 relaxed 원자라 덮이는 중에 읽어도 데이터 레이스가 아닙니다.
     *          프레임 경계는 게임 스레드의 `FrameProfiler::beginFrame` 이 시작 시각 링에 남깁니다.
     * @note `FrameProfiler` 가 소유합니다(`FrameProfiler::getTimeline`). 기록은 `FrameProfiler` 계측이 켜져 있어야 합니다(스코프가 시계를 읽어야 한다).
     */
    class SW_API ProfilerTimeline
    {
    public:
        /** @brief 스레드 하나의 링 크기입니다. 넘치면 오래된 사건부터 버립니다. */
        static constexpr uint32 kEventCapacity = 8192;
        /** @brief 기록하는 스레드의 최대 수입니다. 넘는 스레드의 사건은 버립니다. */
        static constexpr uint32 kMaxThread = 64;
        /** @brief 프레임 시작 시각 링 크기입니다(최근 프레임 수의 상한). */
        static constexpr uint32 kFrameCapacity = 64;

        ProfilerTimeline();
        ~ProfilerTimeline();

        ProfilerTimeline( const ProfilerTimeline& )            = delete;
        ProfilerTimeline& operator=( const ProfilerTimeline& ) = delete;

        /** @brief 기록을 켜거나 끕니다. 끄면 새 사건만 멈추고 남은 사건은 그대로 읽힙니다. 기록 중인 타임라인이 하나라도 있으면 네트워크 흐름(`NetFlowLog`)도 남깁니다. */
        void setRecording( bool bRecording );
        /** @brief 기록 중이면 true 입니다. */
        bool isRecording() const { return _bRecording.load( std::memory_order_relaxed ); }

        /** @brief 이 스레드의 사건 하나를 남깁니다(`ScopedFrameProfile` 소멸자가 부른다). 기록 중이 아니면 아무것도 하지 않습니다. */
        void recordEvent( uint32 slot, uint64 beginNanos, uint64 endNanos, uint16 depth );
        /** @brief 프레임 시작 시각을 남깁니다(게임 스레드 `FrameProfiler::beginFrame`). 기록 중이 아니면 아무것도 하지 않습니다. */
        void recordFrameBegin( uint64 nanos );

        /**
         * @brief 최근 @p frameCount 프레임 구간(끝난 프레임만)의 사건을 스레드마다 모읍니다.
         * @details 구간과 겹치는 사건만 담고, 사건이 없는 스레드는 빼며, 스레드 안은 시작 시각 순입니다. 끝난 프레임이 모자라면 남은 가장 오래된 프레임부터입니다.
         * @return 구간이 있으면(프레임 시작이 둘 이상 기록됐으면) true. @p outBeginNanos · @p outEndNanos 는 그 구간입니다.
         */
        [[nodiscard]] bool collectRecentFrames( uint32 frameCount, vector<ProfilerTimelineThread>& outListThread, uint64& outBeginNanos,
                                                uint64& outEndNanos ) const;
        /** @brief [@p beginNanos, @p endNanos] 안의 프레임 시작 시각을 오름차순으로 담습니다(타임라인의 프레임 경계선). */
        void collectFrameBegins( uint64 beginNanos, uint64 endNanos, vector<uint64>& outListFrameBegin ) const;
        /** @brief 최근 @p frameCount 프레임 구간에 사건을 남긴 스레드 수입니다(감시 값 · 테스트용). */
        uint32 countActiveThreads( uint32 frameCount ) const;
#if SW_PROFILER_TRACE_ENABLED
        /**
         * @brief 긴 캡처를 시작합니다 — 링과 따로, 스레드마다 @p maxEventsPerThread 사건까지 **처음부터** 모읍니다(오래된 것을 버리지 않는다).
         * @details 기록이 꺼져 있으면 켭니다(끝날 때 원래대로). 예산을 넘은 스레드는 더 담지 않고 잘림 표지만 남깁니다. 프레임 경계도 같은 수까지 담습니다.
         *          켜 두는 동안 사건마다 벡터 덧붙이기 한 번이 더 듭니다(링 기록과 같은 위치에서). 이미 진행 중이면 처음부터 다시 시작합니다.
         */
        void beginLongCapture( uint32 maxEventsPerThread );
        /** @brief 긴 캡처를 멈춥니다. 모은 사건은 `collectLongCapture` 로 읽습니다. 기록 상태는 시작 전으로 되돌립니다. */
        void endLongCapture();
        /** @brief 긴 캡처 중이면 true 입니다. */
        bool isLongCaptureActive() const { return _longLimit.load( std::memory_order_relaxed ) != 0; }
        /**
         * @brief 멈춘 긴 캡처의 사건을 스레드마다(시작 시각 순) 모읍니다. 진행 중이면 먼저 멈추십시오.
         * @param outTruncated 예산을 넘어 담지 못한 사건이 있으면 true
         * @return 사건이 하나라도 있으면 true
         */
        [[nodiscard]] bool collectLongCapture( vector<ProfilerTimelineThread>& outListThread, vector<uint64>& outListFrameBegin,
                                               bool& outTruncated ) const;
#endif

        /** @brief 모든 사건과 프레임 경계를 비웁니다(스레드 버퍼는 남긴다). 기록 중에 부르면 그 순간 쓰던 사건 하나가 남을 수 있습니다. */
        void clear();

    private:
        struct ThreadBuffer;

        /** @brief 이 스레드의 버퍼입니다. 처음이면 만들어 등록합니다. 슬롯이 없으면 nullptr 입니다. */
        ThreadBuffer* ensureThreadBuffer();

    private:
        atomic<ThreadBuffer*> _arrThreadBuffer[kMaxThread];
        atomic<uint64>        _arrFrameBeginNanos[kFrameCapacity];
        atomic<uint64>        _frameWriteCount;
        atomic<uint32>        _threadCount;
        uint32                _generation; ///< 이 타임라인의 고유 번호 — 스레드 지역 캐시가 다른(이미 종료된) 타임라인의 버퍼를 쓰지 않게 한다
        atomic<bool>          _bRecording;
#if SW_PROFILER_TRACE_ENABLED
        atomic<uint32> _longLimit; ///< 긴 캡처의 스레드당 사건 수 상한(0 = 꺼짐)
        atomic<uint32> _longEpoch; ///< 긴 캡처를 시작할 때마다 올린다 — 스레드 버퍼가 옛 캡처의 사건을 비운다
        atomic<bool>   _bLongTruncated;
        atomic<bool>   _bLongPrevRecording;
        atomic<bool>   _bLongFrameBusy; ///< 게임 스레드가 `_listLongFrameBegin` 에 덧붙이는 중
        vector<uint64> _listLongFrameBegin;
#endif
    };
} // namespace sw

/**
 * @file FrameProfileSession.cpp
 * @brief FrameProfileSession 구현입니다(워밍업 판정과 보고 시점).
 */
#include "pch.h"

#include "Engine/Profiling/FrameProfileSession.h"

#include "Core/Diagnostics/CallStackCapture.h"
#include "Core/Diagnostics/MemoryProfiler.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Profiling/MemoryBudgetMonitor.h"
#include "Engine/Profiling/ProfilerTraceExport.h"

namespace sw
{
    /**
     * @brief `-gv_profileFrames=N`: 워밍업 뒤 N 프레임을 재고 보고한 다음 종료합니다.
     * @details 선언이 여기 있는 이유: 이 스위치를 해석하고 판정하는 코드가 모두 이 파일에 있습니다.
     */
    SW_TEST_GLOBAL_VARIABLE_SHIPPED( int32, gv_profileFrames, 0, "프레임 프로파일 측정 프레임 수 (0=사용 안 함)" );
    /**
     * @brief `-gv_profileSeconds=S`: 워밍업 뒤 S 초를 재고 보고한 다음 종료합니다. `-gv_profileFrames` 와 함께 주면 먼저 닿는 쪽이 끝냅니다.
     * @details 장시간 실행(soak · `Scripts/qa/Soak.py`)이 쓴다. 프레임 상한이 없으면 같은 프레임 수가 장면마다 몇 초인지 모른다.
     */
    SW_TEST_GLOBAL_VARIABLE( int32, gv_profileSeconds, 0, "프레임 프로파일 측정 시간(초, 0=사용 안 함)" );
    /**
     * @brief `-gv_profileAllocSites=N`: 측정 구간의 할당을 콜스택별로 세어 상위 N 곳을 보고합니다 (0=끄기).
     * @details 프레임당 할당 **횟수**는 시간 표에 보이지 않는 비용입니다. 잡았다 놓는 것은 살아 있는 양에 남지 않습니다.
     *          할당마다 콜스택을 잡으므로 느립니다. 숫자를 읽는 용도이지 프레임 시간을 같이 측정하는 용도가 아닙니다.
     */
    SW_TEST_GLOBAL_VARIABLE( int32, gv_profileAllocSites, 0, "측정 구간의 할당을 콜스택별로 세어 상위 N 곳을 보고 (0=끄기)" );
#if SW_PROFILER_TRACE_ENABLED
    /**
     * @brief `-gv_profileTracePath=<경로>`: 측정 창(워밍업 뒤 ~ 보고) 전체를 트레이스로 저장합니다(비면 끄기).
     * @details 성능 회귀 스크립트가 진 게임을 한 번 더 돌려 "어느 스레드의 어느 구간이 늘었나" 를 볼 파일을 남길 때 씁니다(`Scripts/qa/PerfRegression.py`).
     *          긴 캡처가 사건마다 24 B 를 쓰므로 숫자를 측정하는 판에는 켜지 않습니다.
     */
    SW_TEST_GLOBAL_VARIABLE( sw::string, gv_profileTracePath, "", "측정 창 전체를 이 경로에 트레이스로 저장 (비면 끄기)" );
#endif

    bool FrameProfileSession::isMeasureWindowDone( uint64 frames, uint64 frameTarget, int64 elapsedMicro, int64 secondsTarget )
    {
        const bool bFramesDone  = frameTarget > 0 && frames >= frameTarget;
        const bool bSecondsDone = secondsTarget > 0 && elapsedMicro >= secondsTarget * constant::kMicrosecondsPerSecond;
        return bFramesDone || bSecondsDone;
    }

    void FrameProfileSession::begin()
    {
        if ( gv_profileFrames <= 0 && gv_profileSeconds <= 0 )
            return;

        _frameTarget   = gv_profileFrames > 0 ? static_cast<uint64>( gv_profileFrames ) : 0;
        _secondsTarget = gv_profileSeconds > 0 ? static_cast<int64>( gv_profileSeconds ) : 0;
        _bActive       = SW_TRUE;
        engine::getSubsystem<FrameProfiler>().setEnabled( true );
        SW_LOG_INFO( "[Profile] Profiling enabled - warmup %# + measure %# frames / %# seconds (0 = no such criterion)", kWarmupFrames, _frameTarget, _secondsTarget );
    }

    void FrameProfileSession::onFrameEnd()
    {
        if ( _bReported == SW_TRUE )
            return;
        if ( _bActive == SW_FALSE )
        {
            // 기동 뒤에 목표를 걸 수 있다 — 부하 시나리오(`<Load durationSeconds>`)가 씬 플레이를 시작한 프레임에 `gv_profileSeconds` 를 넣어 측정 창을 연다.
            const bool bLateStart = gv_profileFrames > 0 || gv_profileSeconds > 0;
            if ( bLateStart )
            {
                begin();
                engine::getSubsystem<FrameProfiler>().reset(); // 워밍업을 지금부터 센다
            }
            return;
        }

        FrameProfiler& profiler = engine::getSubsystem<FrameProfiler>();
        const uint64   frames   = profiler.getFrameCount();

        // 워밍업(셰이더 컴파일·PSO 생성·트랜지언트 할당)이 첫 프레임들을 크게 부풀린다.
        // 그 구간을 통계에 섞으면 평균이 의미를 잃으므로 버리고 다시 센다.
        if ( _bWarmedUp == SW_FALSE )
        {
            if ( frames < kWarmupFrames )
                return;

            // reset() 은 프레임 카운터도 0 으로 되돌린다. 플래그가 없으면 이 조건이 60
            // 프레임마다 다시 참이 되어 영원히 워밍업만 한다.
            _bWarmedUp         = SW_TRUE;
            _measureStartMicro = MonotonicClock::nowMicroseconds();
            _lastFrameEndMicro = _measureStartMicro;
            profiler.reset();
            SW_LOG_INFO( "[Profile] Discarded %# warmup frames. Measuring %# frames from now.", kWarmupFrames,
                         _frameTarget );
#if SW_PROFILER_TRACE_ENABLED
            if ( gv_profileTracePath.empty() == false )
            {
                string reply;
                if ( ProfilerTraceExport::startRecording( profiler, 0.0, gv_profileTracePath, "Game", 0, reply ) == false )
                    SW_LOG_WARNING( "[Profile] trace capture not started: %#", reply );
            }
#endif
            // 할당 횟수도 같은 측정 구간에서 센다. 시간 표에는 보이지 않는 비용이다. 배포본에는 프로파일러가 없다.
            if ( MemoryProfiler* pMemory = MemoryProfiler::getActive(); pMemory != nullptr )
            {
                pMemory->setTrackingEnabled( true );
                pMemory->resetPeaks();
                if ( gv_profileAllocSites > 0 )
                    pMemory->setDetailedTrackingEnabled( true );
                _allocationCountAtStart     = pMemory->getTotalAllocationCount();
                _bAllocationTrackingStarted = SW_TRUE;
            }
            return;
        }

        const int64 frameEndMicro = MonotonicClock::nowMicroseconds();
        recordFrameInterval( frameEndMicro - _lastFrameEndMicro );
        _lastFrameEndMicro = frameEndMicro;
        if ( isMeasureWindowDone( frames, _frameTarget, frameEndMicro - _measureStartMicro, _secondsTarget ) == false )
            return;

        _bReported = SW_TRUE;
        profiler.report( "frame breakdown" );
        // 측정 구간의 벽시계 시간이다. 프레임이 실제로 몇 us 마다 나왔는지(처리량)를 본다. 구간 표만으로는 병목이 어디서 기다리는지 알 수 없다.
        [[maybe_unused]] const int64 elapsedMicro = frameEndMicro - _measureStartMicro;
        SW_LOG_INFO( "[Profile] wall  %# frames in %# ms  = %# us/frame", frames, elapsedMicro / constant::kMicrosecondsPerMillisecond, elapsedMicro / static_cast<int64>( frames == 0 ? 1 : frames ) );
        reportHitches();
        reportAllocations( frames );
#if SW_PROFILER_TRACE_ENABLED
        if ( gv_profileTracePath.empty() == false )
            (void)ProfilerTraceExport::finishRecording( profiler ); // 저장 경로 · 실패 이유는 finishRecording 이 로그로 남긴다
#endif
        profiler.setEnabled( false );
        _bQuitRequested = SW_TRUE;
    }

    void FrameProfileSession::recordFrameInterval( int64 intervalMicro )
    {
        for ( uint32 index = 0; index < static_cast<uint32>( std::size( kArrHitchThresholdMicro ) ); ++index )
        {
            if ( intervalMicro > kArrHitchThresholdMicro[index] )
                ++_arrHitchCount[index];
        }
        // 큰 순으로 다섯 칸 — 들어갈 자리부터 뒤로 민다.
        for ( uint32 index = 0; index < kWorstFrameCount; ++index )
        {
            if ( intervalMicro <= _arrWorstFrameMicro[index] )
                continue;
            for ( uint32 moveIndex = kWorstFrameCount - 1; moveIndex > index; --moveIndex )
            {
                _arrWorstFrameMicro[moveIndex] = _arrWorstFrameMicro[moveIndex - 1];
            }
            _arrWorstFrameMicro[index] = intervalMicro;
            return;
        }
    }

    void FrameProfileSession::reportHitches() const
    {
        // 줄 모양은 `Scripts/qa/Load.py` 가 읽는다 — 바꾸면 그 정규식도 고친다.
        SW_LOG_INFO( "[Profile] hitch  worst %# %# %# %# %# us  over16ms %#  over33ms %#  over100ms %#", _arrWorstFrameMicro[0], _arrWorstFrameMicro[1], _arrWorstFrameMicro[2],
                     _arrWorstFrameMicro[3], _arrWorstFrameMicro[4], _arrHitchCount[0], _arrHitchCount[1], _arrHitchCount[2] );
    }

    void FrameProfileSession::reportAllocations( [[maybe_unused]] uint64 frames )
    {
        // 보고는 Info 로그로만 나간다. 그것이 사라지는 빌드(Shipping)에는 프로파일러도 없으니 본문을 통째로 뺀다.
#if SW_LOG_LEVEL_COMPILED( SW_LOG_VERBOSITY_INFO )
        if ( _bAllocationTrackingStarted == SW_FALSE || frames == 0 )
            return;
        MemoryProfiler* pMemory = MemoryProfiler::getActive();
        if ( pMemory == nullptr )
            return;

        MemoryBudgetMonitor::logMemoryReport( *pMemory, "profile window" );

        const uint64 allocationCount = pMemory->getTotalAllocationCount() - _allocationCountAtStart;
        const uint64 perFrameX10     = ( allocationCount * 10 ) / frames;
        SW_LOG_INFO( "[Profile] alloc/frame  %#.%#   (%# sw allocations / %# frames - std allocator and direct CRT calls excluded)",
                     perFrameX10 / 10, perFrameX10 % 10, allocationCount, frames );

        if ( gv_profileAllocSites > 0 )
        {
            const vector<CallStackAllocInfo> listSite = pMemory->getTopCallStacks( TopCallStackOrder::TotalCount );
            const size_t                     shown    = MathUtil::min<size_t>( listSite.size(), static_cast<size_t>( gv_profileAllocSites ) );
            SW_LOG_INFO( "[Profile] Top %# allocation sites (by count in the measure window, of %# sites):", shown, listSite.size() );
            for ( size_t index = 0; index < shown; ++index )
            {
                const CallStackAllocInfo& site       = listSite[index];
                const uint64              countX10   = ( site._totalCount * 10 ) / frames;
                const uint64              bytesFrame = site._totalBytes / frames;
                SW_LOG_INFO( "[Profile]  #%#  %#.%# /frame  %# B/frame  (total %#)", index + 1, countX10 / 10, countX10 % 10,
                             bytesFrame, site._totalCount );
                SW_LOG_INFO( "%#", CallStackCapture::symbolize( site._stack ).c_str() );
            }
            pMemory->setDetailedTrackingEnabled( false );
        }
#endif
    }
} // namespace sw

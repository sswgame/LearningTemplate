/**
 * @file FrameProfileSession.cpp
 * @brief FrameProfileSession 구현입니다(워밍업 판정과 보고 시점).
 */
#include "pch.h"

#include "DevTools/Profiling/FrameProfileSession.h"

#include "Core/Diagnostics/CacheStats.h"
#include "Core/Diagnostics/CallStackCapture.h"
#include "Core/Diagnostics/MemoryProfiler.h"
#include "Core/Foundation/BuildInfo.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/Time/MonotonicClock.h"

#include "DevTools/Profiling/ProfilerTraceExport.h"

#include "Engine/Asset/Cache/CacheStatsUtil.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/RHI/Diagnostics/RHIMemoryTracker.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/RHI.h"
#include "Engine/Presentation/IEngineRenderDriver.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Profiling/MemoryBudgetMonitor.h"

namespace sw
{
    /**
     * @brief `-gv_profileFrames=N`: 워밍업 뒤 N 프레임을 재고 보고한 다음 종료합니다.
     * @details 선언이 여기 있는 이유: 이 스위치를 해석하고 판정하는 코드가 모두 이 파일에 있습니다.
     */
    SW_TEST_GLOBAL_VARIABLE( int32, gv_profileFrames, 0, "프레임 프로파일 측정 프레임 수 (0=사용 안 함)" );
    /**
     * @brief `-gv_profileSeconds=S`: 워밍업 뒤 S 초를 재고 보고한 다음 종료합니다. `-gv_profileFrames` 와 함께 주면 먼저 닿는 쪽이 끝냅니다.
     * @details 장시간 실행(soak)이 쓴다. 프레임 상한이 없으면 같은 프레임 수가 장면마다 몇 초인지 모른다. 부하 시나리오(`<Load>`)는 이름으로 넣는다(`AutomationRunner`).
     */
    SW_TEST_GLOBAL_VARIABLE( int32, gv_profileSeconds, 0, "프레임 프로파일 측정 시간(초, 0=사용 안 함)" );
    /**
     * @brief `-gv_profileAllocSites=N`: 측정 구간의 할당을 콜스택별로 세어 상위 N 곳을 보고합니다 (0=끄기).
     * @details 프레임당 할당 **횟수**는 시간 표에 보이지 않는 비용입니다. 잡았다 놓는 것은 살아 있는 양에 남지 않습니다.
     *          할당마다 콜스택을 잡으므로 느립니다. 숫자를 읽는 용도이지 프레임 시간을 같이 측정하는 용도가 아닙니다.
     */
    SW_TEST_GLOBAL_VARIABLE( int32, gv_profileAllocSites, 0, "측정 구간의 할당을 콜스택별로 세어 상위 N 곳을 보고 (0=끄기)" );
    /**
     * @brief `-gv_profileTracePath=<경로>`: 측정 창(워밍업 뒤 ~ 보고) 전체를 트레이스로 저장합니다(비면 끄기).
     * @details 성능 회귀 스크립트가 진 게임을 한 번 더 돌려 "어느 스레드의 어느 구간이 늘었나" 를 볼 파일을 남길 때 씁니다.
     *          긴 캡처가 사건마다 24 B 를 쓰므로 숫자를 측정하는 판에는 켜지 않습니다.
     */
    SW_TEST_GLOBAL_VARIABLE( sw::GlobalDiskPath, gv_profileTracePath, "", "측정 창 전체를 이 경로에 트레이스로 저장 (비면 끄기)" );
    /**
     * @brief `-gv_profileReportPath=<경로>`: 측정 창의 보고를 JSON 으로도 씁니다(비면 끄기). 회귀 스크립트가 로그 글을 긁지 않고 칸으로 읽는다.
     */
    SW_TEST_GLOBAL_VARIABLE( sw::GlobalDiskPath, gv_profileReportPath, "", "측정 창의 보고를 이 경로에 JSON 으로 저장 (비면 끄기)" );

    FrameProfileSession::FrameProfileSession( IEngineRenderDriver* pRenderDriver )
        : _pRenderDriver{ pRenderDriver }
        , _frameTarget{ 0 }
        , _secondsTarget{ 0 }
        , _allocationCountAtStart{ 0 }
        , _measureStartMicro{ 0 }
        , _lastFrameEndMicro{ 0 }
        , _arrWorstFrameMicro{}
        , _arrHitchCount{}
        , _arrPacingBin{}
        , _report{}
        , _bActive{ SW_FALSE }
        , _bReported{ SW_FALSE }
        , _bWarmedUp{ SW_FALSE }
        , _bAllocationTrackingStarted{ SW_FALSE }
    {
    }

    bool FrameProfileSession::isMeasureWindowDone( uint64 frames, uint64 frameTarget, int64 elapsedMicro, int64 secondsTarget )
    {
        const bool bFramesDone  = frameTarget > 0 && frames >= frameTarget;
        const bool bSecondsDone = secondsTarget > 0 && elapsedMicro >= secondsTarget * constant::time::kMicrosecondsPerSecond;
        return bFramesDone || bSecondsDone;
    }

    void FrameProfileSession::begin()
    {
        _frameTarget            = gv_profileFrames > 0 ? static_cast<uint64>( gv_profileFrames ) : 0;
        _secondsTarget          = gv_profileSeconds > 0 ? static_cast<int64>( gv_profileSeconds ) : 0;
        _bActive                = SW_TRUE;
        FrameProfiler& profiler = engine::getSubsystem<FrameProfiler>();
        profiler.setEnabled( true );
        profiler.reset(); // 워밍업을 지금부터 센다
        // 배경 창의 상한(`gv_backgroundFrameRate`)이 측정을 흐리지 않게 한다 — 예전에는 엔진이 "프로파일 실행 중" 을 앱 루프에 따로 알렸다. 실행 인자가 고른 페이싱은 그대로 둔다.
        GlobalVariableManager&    variables = engine::getSubsystem<GlobalVariableManager>();
        const GlobalVariableInfo* pPacing   = variables.findVariable( "gv_framePacing" );
        if ( pPacing != nullptr && pPacing->getValueAsString() == "Auto" )
            (void)variables.setValueFromString( "gv_framePacing", "FullSpeed" );
        SW_LOG_INFO_ID( "FrameProfileSession.ProfilingEnabledWarmupMeasure", "[Profile] Profiling enabled - warmup %# + measure %# frames / %# seconds (0 = no such criterion)", kWarmupFrames, _frameTarget, _secondsTarget );
    }

    EngineFrameExit FrameProfileSession::onFrameEnd( string_view activeSceneID )
    {
        if ( _bReported == SW_TRUE )
            return kEngineFrameRunning;
        if ( _bActive == SW_FALSE )
        {
            // 실행 인자로 준 목표는 첫 프레임 끝에, 부하 시나리오(`<Load durationSeconds>`)가 씬 플레이를 시작한 프레임에 넣은 목표는 그 프레임 끝에 연다.
            if ( gv_profileFrames > 0 || gv_profileSeconds > 0 )
                begin();
            return kEngineFrameRunning;
        }
        if ( advance() == false )
            return kEngineFrameRunning;
        reportEngineTables( activeSceneID );
        return ProcessExitCode::Passed;
    }

    bool FrameProfileSession::advance()
    {
        FrameProfiler& profiler = engine::getSubsystem<FrameProfiler>();
        const uint64   frames   = profiler.getFrameCount();

        // 워밍업(셰이더 컴파일·PSO 생성·트랜지언트 할당)이 첫 프레임들을 크게 부풀린다.
        // 그 구간을 통계에 섞으면 평균이 의미를 잃으므로 버리고 다시 센다.
        if ( _bWarmedUp == SW_FALSE )
        {
            if ( frames < kWarmupFrames )
                return false;

            // reset() 은 프레임 카운터도 0 으로 되돌린다. 플래그가 없으면 이 조건이 60
            // 프레임마다 다시 참이 되어 영원히 워밍업만 한다.
            _bWarmedUp         = SW_TRUE;
            _measureStartMicro = MonotonicClock::nowMicroseconds();
            _lastFrameEndMicro = _measureStartMicro;
            profiler.reset();
            CacheStats::markWindowStart(); // 캐시 적중 · 실패는 워밍업(첫 로드가 모두 실패)을 빼고 센다
            SW_LOG_INFO_ID( "FrameProfileSession.DiscardedWarmupFramesMeasuring", "[Profile] Discarded %# warmup frames. Measuring %# frames from now.", kWarmupFrames,
                            _frameTarget );
            if ( gv_profileTracePath.empty() == false )
            {
                string reply;
                if ( ProfilerTraceExport::startRecording( profiler, 0.0, gv_profileTracePath, "Game", 0, reply ) == false )
                    SW_LOG_WARNING_ID( "FrameProfileSession.TraceCaptureNotStarted", "[Profile] trace capture not started: %#", reply );
            }
            // 할당 횟수도 같은 측정 구간에서 센다. 시간 표에는 보이지 않는 비용이다. 프로파일러가 없는 구성도 있다.
            if ( MemoryProfiler* pMemory = MemoryProfiler::getActive(); pMemory != nullptr )
            {
                pMemory->setTrackingEnabled( true );
                pMemory->resetPeaks();
                if ( gv_profileAllocSites > 0 )
                    pMemory->setDetailedTrackingEnabled( true );
                _allocationCountAtStart     = pMemory->getTotalAllocationCount();
                _bAllocationTrackingStarted = SW_TRUE;
            }
            return false;
        }

        const int64 frameEndMicro = MonotonicClock::nowMicroseconds();
        recordFrameInterval( frameEndMicro - _lastFrameEndMicro );
        _lastFrameEndMicro = frameEndMicro;
        if ( isMeasureWindowDone( frames, _frameTarget, frameEndMicro - _measureStartMicro, _secondsTarget ) == false )
            return false;

        _bReported = SW_TRUE;
        profiler.report( "frame breakdown" );
        // 측정 구간의 벽시계 시간이다. 프레임이 실제로 몇 us 마다 나왔는지(처리량)를 본다. 구간 표만으로는 병목이 어디서 기다리는지 알 수 없다.
        [[maybe_unused]] const int64 elapsedMicro = frameEndMicro - _measureStartMicro;
        SW_LOG_INFO_ID( "FrameProfileSession.WallFramesMsUs", "[Profile] wall  %# frames in %# ms  = %# us/frame", frames, elapsedMicro / constant::time::kMicrosecondsPerMillisecond, elapsedMicro / static_cast<int64>( frames == 0 ? 1 : frames ) );
        reportHitches();
        fillReport( profiler, frames, elapsedMicro );
        reportAllocations( frames );
        if ( gv_profileTracePath.empty() == false )
            (void)ProfilerTraceExport::finishRecording( profiler ); // 저장 경로 · 실패 이유는 finishRecording 이 로그로 남긴다
        profiler.setEnabled( false );
        return true;
    }

    void FrameProfileSession::reportEngineTables( string_view activeSceneID )
    {
        // CPU 메모리 태그 표 옆에 GPU 메모리 표를 둔다. 렌더러가 없는 호스트(전용 서버)는 표가 없다.
        RHI* const        pRHI       = _pRenderDriver != nullptr ? _pRenderDriver->findRHI() : nullptr;
        const bool        bHasDevice = pRHI != nullptr && pRHI->hasDevice();
        const string_view rhiBackend = bHasDevice ? string_view( pRHI->getDevice().getBackendName() ) : string_view{};
        if ( bHasDevice )
            pRHI->getDevice().getMemoryTracker().report( pRHI->getDevice().getBackendName() );
        // 캐시 적중 · 실패 표도 같은 자리에서 남긴다.
        CacheStatsUtil::logReport( "profile window" );
        // 기계 판독 보고(`-gv_profileReportPath`). 실패는 `saveToFile` 이 오류로 남긴다.
        if ( gv_profileReportPath.empty() )
            return;
        _report._rhiBackend = string( rhiBackend );
        _report._scene      = string( activeSceneID );
        (void)_report.saveToFile( gv_profileReportPath.c_str() );
    }

    void FrameProfileSession::fillReport( const FrameProfiler& profiler, uint64 frames, int64 elapsedMicro )
    {
        FrameProfileReport& report = _report;
        report                     = FrameProfileReport{};
        report._build              = build::kConfigName;
        report._platform           = build::kPlatformName;
        report._target             = build::kTargetName;
        report._warmupFrames       = static_cast<int64>( kWarmupFrames );
        report._frames             = static_cast<int64>( frames );
        report._elapsedMicro       = elapsedMicro;
        report._microPerFrame      = elapsedMicro / static_cast<int64>( frames == 0 ? 1 : frames );
        report._listWorstFrameMicro.assign( std::begin( _arrWorstFrameMicro ), std::end( _arrWorstFrameMicro ) );
        report._over16ms                    = _arrHitchCount[0];
        report._over33ms                    = _arrHitchCount[1];
        report._over100ms                   = _arrHitchCount[2];
        const FramePacing pacing            = computePacing( _arrPacingBin, kPacingBinCount );
        report._pacingMedianMicro           = pacing._medianMicro;
        report._pacingOverOneAndHalf        = pacing._overOneAndHalfMedian;
        report._pacingOverTwoAndHalf        = pacing._overTwoAndHalfMedian;
        const MemoryProfiler* const pMemory = MemoryProfiler::getActive();
        if ( _bAllocationTrackingStarted == SW_TRUE && pMemory != nullptr && frames > 0 )
        {
            const uint64 allocationCount   = pMemory->getTotalAllocationCount() - _allocationCountAtStart;
            report._allocationCount        = static_cast<int64>( allocationCount );
            report._allocationsPerFrameX10 = static_cast<int64>( ( allocationCount * 10 ) / frames );
            report._allocationTracked      = 1;
        }
        profiler.collectScopeSummaries( report._listScope );
    }

    void FrameProfileSession::recordFrameInterval( int64 intervalMicro )
    {
        for ( uint32 index = 0; index < static_cast<uint32>( std::size( kArrHitchThresholdMicro ) ); ++index )
        {
            if ( intervalMicro > kArrHitchThresholdMicro[index] )
                ++_arrHitchCount[index];
        }
        const int64 binIndex = MathUtil::clamp<int64>( intervalMicro / kPacingBinMicro, 0, static_cast<int64>( kPacingBinCount ) - 1 );
        ++_arrPacingBin[binIndex];
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
        SW_LOG_INFO_ID( "FrameProfileSession.HitchWorstUsOver16ms", "[Profile] hitch  worst %# %# %# %# %# us  over16ms %#  over33ms %#  over100ms %#", _arrWorstFrameMicro[0], _arrWorstFrameMicro[1], _arrWorstFrameMicro[2],
                        _arrWorstFrameMicro[3], _arrWorstFrameMicro[4], _arrHitchCount[0], _arrHitchCount[1], _arrHitchCount[2] );
        // VSync 를 켠 실행이면 중앙 간격이 화면 주기라 over1.5x 가 주기를 하나 이상 놓친 프레임 수다.
        [[maybe_unused]] const FramePacing pacing = computePacing( _arrPacingBin, kPacingBinCount );
        SW_LOG_INFO_ID( "FrameProfileSession.PacingMedianUsOver1", "[Profile] pacing  median %# us  over1.5x %#  over2.5x %#", pacing._medianMicro, pacing._overOneAndHalfMedian, pacing._overTwoAndHalfMedian );
    }

    FrameProfileSession::FramePacing FrameProfileSession::computePacing( const uint32* pArrBin, uint32 binCount )
    {
        FramePacing pacing{};
        uint64      total = 0;
        for ( uint32 index = 0; index < binCount; ++index )
        {
            total += pArrBin[index];
        }
        if ( total == 0 )
            return pacing;

        // 중앙값이 든 칸 — 누적이 (n + 1) / 2 에 처음 닿는 칸
        const uint64 medianRank = ( total + 1 ) / 2;
        uint64       seen       = 0;
        uint32       medianBin  = 0;
        for ( ; medianBin < binCount; ++medianBin )
        {
            seen += pArrBin[medianBin];
            if ( seen >= medianRank )
                break;
        }
        pacing._medianMicro = static_cast<int64>( medianBin ) * kPacingBinMicro + kPacingBinMicro / 2;

        // 칸 아래 끝이 문턱 이상인 칸만 센다 — 문턱이 걸친 칸을 빼서 놓침을 부풀리지 않는다.
        const int64 oneAndHalf = pacing._medianMicro * 3 / 2;
        const int64 twoAndHalf = pacing._medianMicro * 5 / 2;
        for ( uint32 index = 0; index < binCount; ++index )
        {
            const int64 lowerMicro = static_cast<int64>( index ) * kPacingBinMicro;
            if ( lowerMicro >= oneAndHalf )
                pacing._overOneAndHalfMedian += pArrBin[index];
            if ( lowerMicro >= twoAndHalf )
                pacing._overTwoAndHalfMedian += pArrBin[index];
        }
        return pacing;
    }

    void FrameProfileSession::reportAllocations( [[maybe_unused]] uint64 frames )
    {
        // 보고는 Info 로그로만 나간다. 그것이 사라지는 구성에는 본문을 통째로 뺀다.
#if SW_LOG_LEVEL_COMPILED( SW_LOG_VERBOSITY_INFO )
        if ( _bAllocationTrackingStarted == SW_FALSE || frames == 0 )
            return;
        MemoryProfiler* pMemory = MemoryProfiler::getActive();
        if ( pMemory == nullptr )
            return;

        MemoryBudgetMonitor::logMemoryReport( *pMemory, "profile window" );

        const uint64 allocationCount = pMemory->getTotalAllocationCount() - _allocationCountAtStart;
        const uint64 perFrameX10     = ( allocationCount * 10 ) / frames;
        SW_LOG_INFO_ID( "FrameProfileSession.AllocFrameSwAllocations", "[Profile] alloc/frame  %#.%#   (%# sw allocations / %# frames - std allocator and direct CRT calls excluded)",
                        perFrameX10 / 10, perFrameX10 % 10, allocationCount, frames );

        if ( gv_profileAllocSites > 0 )
        {
            const vector<CallStackAllocInfo> listSite = pMemory->getTopCallStacks( TopCallStackOrder::TotalCount );
            const size_t                     shown    = MathUtil::min<size_t>( listSite.size(), static_cast<size_t>( gv_profileAllocSites ) );
            SW_LOG_INFO_ID( "FrameProfileSession.TopAllocationSitesCount", "[Profile] Top %# allocation sites (by count in the measure window, of %# sites):", shown, listSite.size() );
            for ( size_t index = 0; index < shown; ++index )
            {
                const CallStackAllocInfo& site       = listSite[index];
                const uint64              countX10   = ( site._totalCount * 10 ) / frames;
                const uint64              bytesFrame = site._totalBytes / frames;
                SW_LOG_INFO_ID( "FrameProfileSession.FrameBFrameTotal", "[Profile]  #%#  %#.%# /frame  %# B/frame  (total %#)", index + 1, countX10 / 10, countX10 % 10,
                                bytesFrame, site._totalCount );
                SW_LOG_INFO_ID( "FrameProfileSession.CallSite", "%#", CallStackCapture::symbolize( site._stack ).c_str() );
            }
            pMemory->setDetailedTrackingEnabled( false );
        }
#endif
    }
} // namespace sw

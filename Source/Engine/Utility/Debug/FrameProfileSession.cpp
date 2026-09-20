/**
 * @file FrameProfileSession.cpp
 * @brief FrameProfileSession 구현 — 워밍업 판정과 보고 시점
 */
#include "pch.h"

#include "Engine/Utility/Debug/FrameProfileSession.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/MemoryProfiler.h"
#include "Core/Process/CallStackCapture.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Utility/Debug/FrameProfiler.h"

namespace sw
{
    /**
     * @brief `-gv_profileFrames=N` — 워밍업 뒤 N 프레임을 재고 보고한 다음 종료합니다.
     * @details 선언이 여기 있는 이유: 이 스위치를 해석하고 판정하는 코드가 전부 이 파일이다. 예전에는
     *          `EngineLoop.cpp` 의 다른 gv_ 들 사이에 선언만 놓여 있어서, 값을 읽는 곳과 규칙이 도는
     *          곳이 갈라져 있었다.
     */
    SW_GLOBAL_VARIABLE_INT( gv_profileFrames, 0, "프레임 프로파일 측정 프레임 수 (0=사용 안 함)" );
    /**
     * @brief `-gv_profileAllocSites=N` — 측정 구간의 할당을 콜스택별로 세어 상위 N 곳을 보고합니다 (0=끄기).
     * @details 프레임당 할당 **횟수**는 시간 표에 안 보이는 비용이다 — 잡았다 놓는 것은 살아 있는 양에 남지 않는다.
     *          할당마다 콜스택을 잡으므로 느리다: 숫자를 읽는 용도이지 프레임 시간을 같이 재는 용도가 아니다.
     */
    SW_GLOBAL_VARIABLE_INT( gv_profileAllocSites, 0, "측정 구간의 할당을 콜스택별로 세어 상위 N 곳을 보고 (0=끄기)" );

    void FrameProfileSession::begin()
    {
        if ( gv_profileFrames <= 0 )
            return;

        _frameTarget = static_cast<uint64>( gv_profileFrames );
        engine::getFrameProfiler().setEnabled( true );
        SW_LOG_INFO( "[Profile] 계측 활성화 — 워밍업 %# + 측정 %# 프레임", kWarmupFrames, _frameTarget );
    }

    void FrameProfileSession::onFrameEnd()
    {
        if ( _frameTarget == 0 || _bReported == SW_TRUE )
            return;

        FrameProfiler& profiler = engine::getFrameProfiler();
        const uint64   frames   = profiler.getFrameCount();

        // 워밍업(셰이더 컴파일·PSO 생성·트랜지언트 할당)이 첫 프레임들을 크게 부풀린다.
        // 그 구간을 통계에 섞으면 평균이 의미를 잃으므로 버리고 다시 센다.
        if ( _bWarmedUp == SW_FALSE )
        {
            if ( frames < kWarmupFrames )
                return;

            // reset() 은 프레임 카운터도 0 으로 되돌린다 — 플래그가 없으면 이 조건이 매 60
            // 프레임마다 다시 참이 되어 영원히 워밍업만 한다.
            _bWarmedUp = SW_TRUE;
            profiler.reset();
            SW_LOG_INFO( "[Profile] 워밍업 %# 프레임을 버렸습니다. 지금부터 %# 프레임을 잽니다.", kWarmupFrames,
                         _frameTarget );
            // 할당 횟수도 같은 창에서 센다 — 시간 표에는 안 보이는 비용이다. 배포본에는 프로파일러가 없다.
            if ( MemoryProfiler* pMemory = MemoryProfiler::getActive(); pMemory != nullptr )
            {
                pMemory->setTrackingEnabled( true );
                if ( gv_profileAllocSites > 0 )
                    pMemory->setDetailedTrackingEnabled( true );
                _allocationCountAtStart     = pMemory->getTotalAllocationCount();
                _bAllocationTrackingStarted = SW_TRUE;
            }
            return;
        }

        if ( frames < _frameTarget )
            return;

        _bReported = SW_TRUE;
        profiler.report( "frame breakdown" );
        reportAllocations( frames );
        profiler.setEnabled( false );
        _bWantsQuit = SW_TRUE;
    }

    void FrameProfileSession::reportAllocations( [[maybe_unused]] uint64 frames )
    {
        // 보고는 Info 로그로만 나간다 — 그것이 사라지는 빌드(Shipping)에는 프로파일러도 없으니 본문을 통째로 뺀다.
#if SW_LOG_LEVEL_COMPILED( SW_LOG_VERBOSITY_INFO )
        if ( _bAllocationTrackingStarted == SW_FALSE || frames == 0 )
            return;
        MemoryProfiler* pMemory = MemoryProfiler::getActive();
        if ( pMemory == nullptr )
            return;

        const uint64 allocationCount = pMemory->getTotalAllocationCount() - _allocationCountAtStart;
        const uint64 perFrameX10     = ( allocationCount * 10 ) / frames;
        SW_LOG_INFO( "[Profile] alloc/frame  %#.%#   (sw 할당 %# 회 / %# 프레임 — std 할당자·CRT 직접 호출은 제외)",
                     perFrameX10 / 10, perFrameX10 % 10, allocationCount, frames );

        if ( gv_profileAllocSites > 0 )
        {
            const vector<CallStackAllocInfo> listSite = pMemory->getTopCallStacks( TopCallStackOrder::TotalCount );
            const size_t                     shown    = MathUtil::min<size_t>( listSite.size(), static_cast<size_t>( gv_profileAllocSites ) );
            SW_LOG_INFO( "[Profile] 할당 자리 상위 %# 곳 (측정 창 안 횟수 순, %# 곳 중):", shown, listSite.size() );
            for ( size_t index = 0; index < shown; ++index )
            {
                const CallStackAllocInfo& site       = listSite[index];
                const uint64              countX10   = ( site._totalCount * 10 ) / frames;
                const uint64              bytesFrame = site._totalBytes / frames;
                SW_LOG_INFO( "[Profile]  #%#  %#.%# 회/프레임  %# B/프레임  (누적 %# 회)", index + 1, countX10 / 10, countX10 % 10,
                             bytesFrame, site._totalCount );
                SW_LOG_INFO( "%#", CallStackCapture::symbolize( site._stack ).c_str() );
            }
            pMemory->setDetailedTrackingEnabled( false );
        }
#endif
    }
} // namespace sw

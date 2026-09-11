/**
 * @file FrameProfileSession.cpp
 * @brief FrameProfileSession 구현 — 워밍업 판정과 보고 시점
 */
#include "pch.h"

#include "Engine/Utility/Debug/FrameProfileSession.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/Logger.h"

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
            return;
        }

        if ( frames < _frameTarget )
            return;

        _bReported = SW_TRUE;
        profiler.report( "frame breakdown" );
        profiler.setEnabled( false );
        _bWantsQuit = SW_TRUE;
    }
} // namespace sw

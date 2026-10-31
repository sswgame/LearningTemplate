#include "pch.h"

#include "Core/Log/Logger.h"

#include "Engine/HeadlessSubsystem.h"

// 자동화 런타임을 짓지 않는 구성(`SW_ENABLE_AUTOMATION=OFF` · 전용 서버)의 시나리오 행렬 작업이다. 짓는 구성은 `HeadlessScenarioMatrix.cpp` 다.
namespace sw
{
    SW_LOG_CALLER( "Headless" );

    bool HeadlessSubsystem::runScenarioMatrixTask( CommandLineManager& commandLine, HeadlessTaskState& inoutState )
    {
        string backends;
        if ( commandLine.getArgument( CommandLineArgument::SCENARIO_BACKENDS, backends ) == false || backends.empty() )
            return false;
        // 행렬을 시킨 실행은 실행기 없이 결과를 낼 수 없다 — 이유를 남기고 실패로 끝낸다(폴백 없음, `AppAutomationHostOff.cpp` 와 같다).
        inoutState.enterHeadless();
        SW_LOG_ERROR( "-scenario-backends needs a build with SW_ENABLE_AUTOMATION=ON (this build has no automation runtime)" );
        inoutState._bFailed = true;
        return true;
    }
} // namespace sw

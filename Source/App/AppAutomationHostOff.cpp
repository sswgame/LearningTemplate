#include "pch.h"

#include "App/AppAutomationHost.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Log/Logger.h"

// 자동화 런타임을 뺀 구성(`SW_ENABLE_AUTOMATION=OFF` — Engine 이 `Automation/Runner/` 를 짓지 않는다)의 구현이다. 구동기가 없다.
// 켠 구성은 `AppAutomationHost.cpp` 를 CMake 가 고른다(`Source/App/CMakeLists.txt`).
namespace sw
{
    SW_LOG_CALLER( "App" );

    AppAutomationHost::AppAutomationHost()
        : _pSession{ nullptr }
        , _frameSubscription{}
    {
    }

    void AppAutomationHost::addToHostScope( [[maybe_unused]] SubsystemScope& host )
    {
    }

    bool AppAutomationHost::start( [[maybe_unused]] Engine& engine, [[maybe_unused]] SubsystemScope& host, const CommandLineManager& commandLineManager )
    {
        // 시나리오를 시킨 실행은 시나리오 없이 돌면 결과를 낼 수 없다 — 이유를 남기고 기동을 멈춘다(폴백 없음).
        string     value;
        const bool bAsked = commandLineManager.getArgument( CommandLineArgument::SCENARIO, value ) || commandLineManager.getArgument( CommandLineArgument::SCENARIO_RECORD, value );
        if ( bAsked == false )
            return true;
        SW_LOG_ERROR( "-scenario / -scenario-record need a build with SW_ENABLE_AUTOMATION=ON (this build has no automation runtime)" );
        return false;
    }

    bool AppAutomationHost::isBusy() const
    {
        return false;
    }

    void AppAutomationHost::detach( [[maybe_unused]] SubsystemScope& host )
    {
        _frameSubscription.reset();
    }
} // namespace sw

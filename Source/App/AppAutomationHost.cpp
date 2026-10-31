#include "pch.h"

#include "App/AppAutomationHost.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"
#include "Core/Subsystem/SubsystemScope.h"

#ifndef SW_AUTOMATION_ENABLED
    #error "SW_AUTOMATION_ENABLED comes from CMake (cmake/Engine/BuildLayout.cmake, option SW_ENABLE_AUTOMATION)"
#endif
#if SW_AUTOMATION_ENABLED
    #include "Engine/Automation/Runner/AutomationSession.h"
    #include "Engine/Automation/Scenario/ScenarioPlayback.h"
#endif
#include "Engine/Engine.h"
#include "Engine/Renderer/RenderThread.h"

namespace sw
{
    SW_LOG_CALLER( "App" );

    AppAutomationHost::AppAutomationHost()
        : _pSession{ nullptr }
        , _frameSubscription{}
    {
    }

#if SW_AUTOMATION_ENABLED
    void AppAutomationHost::addToHostScope( SubsystemScope& host )
    {
        SW_MEMORY_SCOPE( EngineMisc );
        _pSession = &host.addSubsystem( make_unique<AutomationSession>() );
    }

    void AppAutomationHost::start( Engine& engine, SubsystemScope& host, const CommandLineManager& commandLineManager )
    {
        string recordPath;
        string recordProbes;
        if ( commandLineManager.getArgument( CommandLineArgument::SCENARIO_RECORD, recordPath ) && recordPath.empty() == false )
        {
            (void)commandLineManager.getArgument( CommandLineArgument::SCENARIO_RECORD_PROBES, recordProbes ); // 없으면 검사점 없이 입력만 녹화한다
            (void)_pSession->beginRecording( recordPath, recordProbes );                                       // 거절하면 이유를 로그로 남기고 녹화 없이 돈다
        }
        string scenarioPath;
        string reportPath;
        (void)commandLineManager.getArgument( CommandLineArgument::SCENARIO_REPORT, reportPath ); // 없으면 보고 파일을 쓰지 않는다
        RenderThread* pRenderThread = engine.getRenderThread();
        const bool    bKeepGoing    = commandLineManager.isArgumentProvided( CommandLineArgument::SCENARIO_KEEP_GOING );
        const bool    bScenario     = commandLineManager.getArgument( CommandLineArgument::SCENARIO, scenarioPath ) && scenarioPath.empty() == false;
        const bool    bStarted      = bScenario && _pSession->begin( scenarioPath, reportPath, bKeepGoing );
        if ( bStarted && pRenderThread != nullptr )
            pRenderThread->setScenarioCaptureEnabled( true );
        // 늘 붙인다 — 게임 · 콘솔의 재생(`ScenarioPlayback::play`)은 기동 뒤 아무 때나 시작한다. 쉬는 세션의 프레임 훅은 아무것도 하지 않는다.
        _frameSubscription = engine.getFrameHooks().add( _pSession->makeFrameHooks() );
        // 재생기는 호스트 범위에 인터페이스로 건다 — `ScenarioPlayback` 이 범위에서 찾는다(정적 칸을 두지 않는다).
        const bool bPlayerBound = host.bind<IScenarioPlayer>( &_pSession->getScenarioPlayer() );
        SW_ASSERT( bPlayerBound );
        (void)bPlayerBound;
    }

    bool AppAutomationHost::isBusy() const
    {
        // 재생(데모 · 벤치마크 경로)은 사람이 보는 화면이라 보통 프레임 속도로 돈다 — 무인 실행만 센다.
        return _pSession != nullptr && _pSession->isBusy() && _pSession->isPlaying() == false;
    }

    void AppAutomationHost::detach( SubsystemScope& host )
    {
        host.unbind<IScenarioPlayer>(); // 재생기(세션)를 내리기 전에 푼다
        if ( _pSession != nullptr )
            _pSession->onDetachedFromEngine();
        _frameSubscription.reset();
    }
#else
    void AppAutomationHost::addToHostScope( [[maybe_unused]] SubsystemScope& host )
    {
    }

    void AppAutomationHost::start( [[maybe_unused]] Engine& engine, [[maybe_unused]] SubsystemScope& host, const CommandLineManager& commandLineManager )
    {
        // 자동화 런타임을 뺀 구성(`SW_ENABLE_AUTOMATION=OFF`)은 시나리오 인자를 조용히 무시하지 않는다 — 폴백 없이 이유를 남기고 시나리오 없이 돈다.
        string     value;
        const bool bAsked = commandLineManager.getArgument( CommandLineArgument::SCENARIO, value ) || commandLineManager.getArgument( CommandLineArgument::SCENARIO_RECORD, value );
        if ( bAsked )
            SW_LOG_ERROR( "-scenario / -scenario-record need a build with SW_ENABLE_AUTOMATION=ON (this build has no automation runtime). Ignored" );
    }

    bool AppAutomationHost::isBusy() const
    {
        return false;
    }

    void AppAutomationHost::detach( [[maybe_unused]] SubsystemScope& host )
    {
        _frameSubscription.reset();
    }
#endif
} // namespace sw

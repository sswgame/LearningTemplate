#include "pch.h"

#include "Engine/Automation/AutomationSession.h"

#include "Core/File/FileUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/Logger.h"

#include "Engine/Automation/AutomationExitCode.h"
#include "Engine/Automation/AutomationRunner.h"
#include "Engine/Automation/AutomationScenarioChain.h"
#include "Engine/Automation/AutomationStepRegistry.h"
#include "Engine/Common/EngineServices.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Window/IWindow.h"
#include "Engine/Window/MainWindowSubsystem.h"

namespace sw
{
    SW_LOG_CALLER( "AutomationSession" );

    AutomationSession::AutomationSession()
        : _pRunner{ nullptr }
        , _pChain{ nullptr }
        , _recorder{}
        , _listRecordProbe{}
        , _recordPath{}
        , _reportPath{}
        , _workingFolder{}
        , _screenshotBaseline{ 0 }
    {
    }

    AutomationSession::~AutomationSession() = default;

    void AutomationSession::shutdownSubsystem()
    {
        // 실행기가 로그 리스너 · 입력을 빌려 쓴다 — 엔진 서비스가 사라지기 전에 비운다.
        _pRunner.reset();
        _pChain.reset();
        _recordPath.clear();
    }

    bool AutomationSession::beginRecording( string_view outputPath, string_view probeList )
    {
        if ( ScenarioRecorder::isAllowedOutputPath( outputPath ) == false )
        {
            SW_LOG_ERROR( "[ScenarioRecord] the output must be a relative path under Saved/Automation/ ending in .scenario.xml, got '%#'", string( outputPath ).c_str() );
            return false;
        }
        _recordPath      = string( outputPath );
        _listRecordProbe = AutomationScenarioChain::splitList( probeList );
        SW_LOG_INFO( "[ScenarioRecord] recording the play into %# (%# checkpoint(s))", _recordPath.c_str(), static_cast<uint32>( _listRecordProbe.size() ) );
        return true;
    }

    void AutomationSession::startRecordingWhenReady()
    {
        if ( _recordPath.empty() || _recorder.isRecording() )
            return;
        Scene*                   pScene   = engine::getSubsystem<SceneManager>().getActiveScene();
        const GameObjectManager* pManager = pScene != nullptr ? pScene->getObjectManager() : nullptr;
        const bool               bReady   = pManager != nullptr && pManager->hasBegunPlay() && AutomationStepRegistry::isStartBlocked() == false;
        if ( bReady )
            _recorder.start();
    }

    void AutomationSession::finishRecording()
    {
        if ( _recorder.isRecording() == false )
            return;
        ScenarioRecordSettings settings;
        settings._name                         = FileUtil::removeExtension( FileUtil::removeExtension( FileUtil::getFileNamePart( _recordPath ) ) ); // a.scenario.xml → a
        settings._fixedDelta                   = _pRunner != nullptr ? _pRunner->getScenario().getFixedDelta() : settings._fixedDelta;
        settings._firstFrame                   = 0;
        settings._bIncludeSynthetic            = true;
        settings._bStartWhenScenePlaying       = true;
        const MainWindowSubsystem* pWindowHost = engine::findSubsystem<MainWindowSubsystem>();
        const IWindow*             pWindow     = pWindowHost != nullptr ? pWindowHost->getWindow() : nullptr;
        settings._clientWidth                  = pWindow != nullptr ? pWindow->getWidth() : 0;
        settings._clientHeight                 = pWindow != nullptr ? pWindow->getHeight() : 0;
        Scene*                   pScene        = engine::getSubsystem<SceneManager>().getActiveScene();
        const GameObjectManager* pManager      = pScene != nullptr ? pScene->getObjectManager() : nullptr;
        for ( const string& spec : _listRecordProbe )
        {
            ScenarioRecordCheckpoint checkpoint;
            string                   error;
            if ( ScenarioRecorder::readCheckpoint( spec, pManager, checkpoint, error ) )
                settings._listCheckpoint.push_back( std::move( checkpoint ) );
            else
                SW_LOG_WARNING( "[ScenarioRecord] checkpoint %# skipped: %#", spec.c_str(), error.c_str() );
        }
        const ScenarioRecordOutput output = _recorder.stop( settings );
        if ( FileUtil::ensureParentDirectoryExists( _recordPath ) == false || FileUtil::writeTextFile( _recordPath, output._xmlText ) == false )
        {
            SW_LOG_ERROR( "[ScenarioRecord] could not write %#", _recordPath.c_str() );
            return;
        }
        SW_LOG_INFO( "[ScenarioRecord] wrote %# (%# step(s), %# checkpoint(s), %# event(s) skipped) - replay with -scenario=%#", _recordPath.c_str(),
                     output._writtenStepCount, static_cast<uint32>( settings._listCheckpoint.size() ), output._skippedEventCount,
                     ScenarioRecorder::makeReplayID( _recordPath ).c_str() );
    }

    bool AutomationSession::begin( string_view scenarioArgument, string_view reportPath, bool bKeepGoing, uint32 completedScreenshotCount )
    {
        _reportPath    = string( reportPath );
        _workingFolder = FileUtil::getCurrentPath();
        // 녹화(`Saved/Automation/…`)를 id(`saved/…`)로 다시 돌릴 수 있게 붙인다. 못 붙이면 오류가 남고 그 id 를 읽는 단계만 읽기 오류로 진다.
        (void)ScenarioRecorder::mountSavedDomain( _workingFolder );
        _pChain = make_unique<AutomationScenarioChain>();
        _pChain->begin( AutomationScenarioChain::splitList( scenarioArgument ), bKeepGoing );
        if ( _pChain->isActive() == false )
        {
            _pChain.reset();
            return false;
        }
        startStage( completedScreenshotCount );
        return true;
    }

    void AutomationSession::startStage( uint32 completedScreenshotCount )
    {
        // 앞 단계가 프로세스의 작업 폴더를 옮겼으면(파일 대화상자 같은 것) 상대 경로를 쓰는 다음 단계가 엉뚱한 폴더에 쓴다 — 알리고 처음 폴더로 되돌린다.
        if ( _pChain->getStageIndex() > 0 && _workingFolder.empty() == false )
        {
            const string currentFolder = FileUtil::getCurrentPath();
            if ( currentFolder != _workingFolder )
            {
                SW_LOG_WARNING( "[Scenario] the working folder changed to '%#' during stage %# - restoring '%#'", currentFolder.c_str(),
                                static_cast<uint32>( _pChain->getStageIndex() ), _workingFolder.c_str() );
                (void)FileUtil::setCurrentPath( _workingFolder );
            }
        }

        _pRunner = make_unique<AutomationRunner>();
        _pRunner->startFromPath( _pChain->getCurrentPath(), AutomationScenarioChain::makeStageReportPath( _reportPath, _pChain->getStageIndex(), _pChain->getStageCount() ) );
        _screenshotBaseline = completedScreenshotCount;
        if ( _pRunner->isActive() )
        {
            const string fixedDelta = to_string( _pRunner->getScenario().getFixedDelta() );
            if ( engine::getSubsystem<GlobalVariableManager>().setValueFromString( "gv_fixedFrameDelta", fixedDelta ) == false )
                SW_LOG_WARNING( "[Scenario] this host has no gv_fixedFrameDelta - the scenario runs on the wall clock" );
        }
    }

    void AutomationSession::onFrameBegin( const EngineFrameContext& context )
    {
        startRecordingWhenReady();
        if ( _pRunner != nullptr && context._pInput != nullptr )
            _pRunner->onFrameBegin( *context._pInput );
    }

    int32 AutomationSession::onFrameEnd( const EngineFrameContext& context )
    {
        // 녹화는 시나리오보다 먼저 이 프레임을 적는다 — 시나리오가 이 프레임에 끝나도 마지막 프레임이 녹화에 포함된다.
        if ( _recorder.isRecording() && context._pInput != nullptr )
            _recorder.update( *context._pInput, context._deltaSeconds );
        // `isActive` 가 아니라 `hasEnded` — 시작 프레임에서 진 시나리오(읽기 오류 · 시작 시한)도 여기서 끝맺고 그 코드로 끝난다.
        const bool bNoRunnerToUpdate = _pRunner == nullptr || _pRunner->hasEnded() || context._pInput == nullptr;
        if ( bNoRunnerToUpdate )
            return kRunning;
        const uint32 completedScreenshotCount = context._completedScreenshotCount;
        _pRunner->setCompletedScreenshotCount( completedScreenshotCount >= _screenshotBaseline ? completedScreenshotCount - _screenshotBaseline : 0u );
        const AutomationOutcome result = _pRunner->onFrameEnd( *context._pInput );
        if ( result == AutomationOutcome::Running )
            return kRunning;
        const int32 exitCode = onStageEnded( result, completedScreenshotCount );
        if ( exitCode != kRunning )
            finishRecording();
        return exitCode;
    }

    bool AutomationSession::tryGetRunStatus( string& outScenarioName, string& outStepKind, uint32& outStepNumber, uint32& outFrame ) const
    {
        const bool bRunning = _pRunner != nullptr && _pRunner->isActive();
        if ( bRunning == false )
            return false;
        outScenarioName = _pRunner->getScenario().getName();
        outStepKind     = _pRunner->getCurrentStepKind();
        outStepNumber   = _pRunner->getCurrentStepNumber();
        outFrame        = _pRunner->getFrameIndex();
        return true;
    }

    int32 AutomationSession::onStageEnded( AutomationOutcome result, uint32 completedScreenshotCount )
    {
        const size_t stageIndex = _pChain->getStageIndex();
        const string stageName  = _pRunner->getScenario().getName().empty() ? _pChain->getCurrentPath() : _pRunner->getScenario().getName();
        const bool   bNextStage = _pChain->onStageEnded( result );
        if ( _pChain->isChain() )
        {
            const string line = _pChain->describeStageEnd( stageIndex, result, stageName );
            if ( result == AutomationOutcome::Passed || result == AutomationOutcome::Unconfirmed )
                SW_LOG_INFO( "[Scenario] %#", line.c_str() );
            else
                SW_LOG_ERROR( "[Scenario] %#", line.c_str() );
        }
        if ( bNextStage )
        {
            startStage( completedScreenshotCount );
            return kRunning;
        }
        if ( _pChain->isChain() )
            SW_LOG_INFO( "[Scenario] chain %# (%# stage(s), %# failed, %# skipped, %# not run)", getAutomationOutcomeName( _pChain->getFinalResult() ),
                         static_cast<uint32>( _pChain->getStageCount() ), static_cast<uint32>( _pChain->getFailedStageCount() ),
                         static_cast<uint32>( _pChain->getSkippedResultCount() ), static_cast<uint32>( _pChain->getSkippedStageCount() ) );
        return getAutomationExitCode( _pChain->getFinalResult() );
    }

    int32 AutomationSession::onWindowClosed( const EngineFrameContext& context )
    {
        finishRecording();
        // 이미 끝난 세션은 정하지 않는다 — 종료 코드는 끝낸 프레임(`onStageEnded`)이 체인 전체의 결과로 정했다. 여기서 러너의 결과를 돌려주면
        // 마지막 단계의 결과(통과 0)가 앞 단계의 실패(10)를 덮는다(엔진 루프는 창 닫힘 관찰자의 코드를 앞선 종료 요청보다 앞세운다).
        if ( _pRunner == nullptr || _pRunner->hasEnded() )
            return kRunning;
        AutomationOutcome result = _pRunner->onWindowClosed( context._pInput );
        // 창이 닫히면 루프가 끝난다 — 돌릴 단계가 남아 있으면 체인은 끝까지 가지 못했다(`CloseWindow` 는 마지막 단계에만 둔다).
        if ( _pChain != nullptr && _pChain->onStageEnded( result ) )
        {
            SW_LOG_ERROR( "[Scenario] the window closed with %# stage(s) still to run (put <CloseWindow> in the last scenario of a chain)",
                          static_cast<uint32>( _pChain->getStageCount() - _pChain->getStageIndex() ) );
            result = AutomationOutcome::Failed;
        }
        return getAutomationExitCode( result );
    }

    void AutomationSession::onBuildRenderPacket( EngineRenderPacketContext& inoutContext )
    {
        // 패킷 하나에는 스크린샷이 하나다 — 이미 찼으면 꺼내지 않고 다음 패킷에 싣는다.
        const bool bCannotTakeScreenshot = _pRunner == nullptr || inoutContext._pScreenshotPath == nullptr || inoutContext._pScreenshotPath->empty() == false;
        if ( bCannotTakeScreenshot )
            return;
        (void)_pRunner->takePendingScreenshotPath( *inoutContext._pScreenshotPath ); // 요청이 없으면 빈 채로 둔다
    }
} // namespace sw

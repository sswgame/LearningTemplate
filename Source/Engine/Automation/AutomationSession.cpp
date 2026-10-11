#include "pch.h"

#include "Engine/Automation/AutomationSession.h"

#include "Core/File/FileUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/Logger.h"

#include "Engine/Automation/AutomationRunner.h"
#include "Engine/Automation/AutomationScenarioChain.h"
#include "Engine/Common/EngineServices.h"

namespace sw
{
    SW_LOG_CALLER( "AutomationSession" );

    AutomationSession::AutomationSession()
        : _pRunner{ nullptr }
        , _pChain{ nullptr }
        , _reportPath{}
        , _workingFolder{}
        , _screenshotBaseline{ 0 }
    {
    }

    AutomationSession::~AutomationSession() = default;

    bool AutomationSession::begin( string_view scenarioArgument, string_view reportPath, bool bKeepGoing, uint32 completedScreenshotCount )
    {
        _reportPath    = string( reportPath );
        _workingFolder = FileUtil::getCurrentPath();
        _pChain        = make_unique<AutomationScenarioChain>();
        _pChain->begin( AutomationScenarioChain::splitList( scenarioArgument ), bKeepGoing );
        if ( _pChain->isActive() == false )
            return false;
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
            if ( engine::getGlobalVariableManager().setValueFromString( "gv_fixedFrameDelta", fixedDelta ) == false )
                SW_LOG_WARNING( "[Scenario] this host has no gv_fixedFrameDelta - the scenario runs on the wall clock" );
        }
    }

    void AutomationSession::onFrameBegin( InputManager& input )
    {
        if ( _pRunner != nullptr )
            _pRunner->onFrameBegin( input );
    }

    int32 AutomationSession::onFrameEnd( InputManager& input, uint32 completedScreenshotCount )
    {
        // `isActive` 가 아니라 `hasEnded` — 시작 프레임에서 진 시나리오(읽기 오류 · 시작 시한)도 여기서 끝맺고 그 코드로 끝난다.
        if ( _pRunner == nullptr || _pRunner->hasEnded() )
            return kRunning;
        _pRunner->setCompletedScreenshotCount( completedScreenshotCount >= _screenshotBaseline ? completedScreenshotCount - _screenshotBaseline : 0u );
        const AutomationResult result = _pRunner->onFrameEnd( input );
        if ( result == AutomationResult::Running )
            return kRunning;
        return onStageEnded( static_cast<int32>( result ), completedScreenshotCount );
    }

    int32 AutomationSession::onStageEnded( int32 resultCode, uint32 completedScreenshotCount )
    {
        const AutomationResult result     = static_cast<AutomationResult>( resultCode );
        const size_t           stageIndex = _pChain->getStageIndex();
        const string           stageName  = _pRunner->getScenario().getName().empty() ? _pChain->getCurrentPath() : _pRunner->getScenario().getName();
        const bool             bNextStage = _pChain->onStageEnded( result );
        if ( _pChain->isChain() )
        {
            const string line = _pChain->describeStageEnd( stageIndex, result, stageName );
            if ( result == AutomationResult::Passed )
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
            SW_LOG_INFO( "[Scenario] chain %# (%# stage(s), %# failed, %# skipped, %# not run)", AutomationRunner::getResultName( _pChain->getFinalResult() ),
                         static_cast<uint32>( _pChain->getStageCount() ), static_cast<uint32>( _pChain->getFailedStageCount() ),
                         static_cast<uint32>( _pChain->getSkippedResultCount() ), static_cast<uint32>( _pChain->getSkippedStageCount() ) );
        return static_cast<int32>( _pChain->getFinalResult() );
    }

    int32 AutomationSession::onWindowClosed( InputManager* pInput )
    {
        if ( _pRunner == nullptr )
            return kRunning;
        AutomationResult result = _pRunner->onWindowClosed( pInput );
        // 창이 닫히면 루프가 끝난다 — 돌릴 단계가 남아 있으면 체인은 끝까지 가지 못했다(`CloseWindow` 는 마지막 단계에만 둔다).
        if ( _pChain != nullptr && _pChain->onStageEnded( result ) )
        {
            SW_LOG_ERROR( "[Scenario] the window closed with %# stage(s) still to run (put <CloseWindow> in the last scenario of a chain)",
                          static_cast<uint32>( _pChain->getStageCount() - _pChain->getStageIndex() ) );
            result = AutomationResult::Failed;
        }
        return static_cast<int32>( result );
    }

    bool AutomationSession::takePendingScreenshotPath( string& outPath )
    {
        return _pRunner != nullptr && _pRunner->takePendingScreenshotPath( outPath );
    }
} // namespace sw

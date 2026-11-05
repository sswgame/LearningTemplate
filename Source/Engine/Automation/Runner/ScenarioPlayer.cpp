#include "pch.h"

#include "Engine/Automation/Runner/ScenarioPlayer.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/Logger.h"

#include "Engine/Automation/AutomationRuntime.h"
#include "Engine/Automation/Runner/AutomationRunner.h"
#include "Engine/EngineFrameHookList.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Input/InputManager.h"

namespace sw
{
    ScenarioPlayer::ScenarioPlayer( EngineFrameHookList& frameHooks )
        : _pRunner{ nullptr }
    {
        EngineFrameHooks hooks{};
        hooks._onFrameBegin     = SW_DELEGATE_METHOD( EngineFrameDelegate, &ScenarioPlayer::onFrameBegin, this );
        hooks._onAfterSceneTick = SW_DELEGATE_METHOD( EngineFrameDelegate, &ScenarioPlayer::onAfterSceneTick, this );
        hooks._onFrameEnd       = SW_DELEGATE_METHOD( EngineFrameExitDelegate, &ScenarioPlayer::onFrameEnd, this );
        hooks._onWindowClosed   = SW_DELEGATE_METHOD( EngineFrameExitDelegate, &ScenarioPlayer::onWindowClosed, this );
        // 늘 붙인다 — 재생은 기동 뒤 아무 때나 시작하고, 훅 안에서 구독을 붙이고 떼지 않는다(쉬는 재생기의 훅은 아무것도 하지 않는다).
        _frameSubscription = frameHooks.add( hooks );
    }

    ScenarioPlayer::~ScenarioPlayer()
    {
        _frameSubscription.reset(); // 훅이 가리키는 이 객체보다 먼저 뗀다
    }

    bool ScenarioPlayer::play( const AssetID& scenarioPath, const ScenarioPlayOptions& options )
    {
        // 실행기는 입력 관리자의 가상 입력 칸 · 쓰기 입구를 하나씩 쓴다 — 테스트 실행 · 녹화 · 다른 재생과 겹쳐 돌지 않는다.
        const IAutomationRuntime* const pRuntime = engine::findSubsystem<IAutomationRuntime>();
        if ( isPlaying() || ( pRuntime != nullptr && pRuntime->isBusy() ) )
        {
            SW_LOG_WARNING_ID( "ScenarioPlayer.CannotPlayBusy", "[Scenario] cannot play '%#' - a scenario, playback or recording is already running", scenarioPath.c_str() );
            return false;
        }
        _options = options;
        _path    = scenarioPath;
        if ( options._bFixedDelta )
        {
            GlobalVariableInfo* pFixedDelta = engine::getSubsystem<GlobalVariableManager>().findVariable( "gv_fixedFrameDelta" );
            _previousFixedDelta             = pFixedDelta != nullptr ? pFixedDelta->getValueAsString() : string{};
        }
        startRunner();
        SW_LOG_INFO_ID( "ScenarioPlayer.Playing", "[Scenario] playing %#%#%#", scenarioPath.c_str(), options._bLoop ? " (loop)" : "", options._bStopOnHumanInput ? " (stops on input)" : "" );
        return true;
    }

    void ScenarioPlayer::startRunner()
    {
        _pRunner = make_unique<AutomationRunner>(); // 행 감시를 켜지 않는다 — 재생은 프로세스를 끝내지 않는다
        _pRunner->startFromPath( _path, {} );
        _humanInputSerial = engine::getSubsystem<InputManager>().getHumanInputSerial();
        if ( _options._bFixedDelta && _pRunner->isActive() )
            (void)engine::getSubsystem<GlobalVariableManager>().setValueFromString( "gv_fixedFrameDelta", to_string( _pRunner->getScenario().getFixedDelta() ) );
    }

    void ScenarioPlayer::stopPlayback()
    {
        if ( isPlaying() )
            endPlayback( ScenarioPlayEnd::Stopped, engine::findSubsystem<InputManager>() );
    }

    void ScenarioPlayer::endPlayback( ScenarioPlayEnd end, InputManager* pInput )
    {
        if ( _pRunner != nullptr )
            _pRunner->stop( pInput, "playback stopped" ); // 끝난 실행기면 아무것도 하지 않는다 — 가상 입력은 실행기가 뗀다
        _pRunner.reset();
        if ( _options._bFixedDelta && _previousFixedDelta.empty() == false )
            (void)engine::getSubsystem<GlobalVariableManager>().setValueFromString( "gv_fixedFrameDelta", _previousFixedDelta );
        const ScenarioPlayFinishedDelegate onFinished = _options._onFinished;
        _options                                      = ScenarioPlayOptions{};
        SW_LOG_INFO_ID( "ScenarioPlayer.PlaybackEnded", "[Scenario] playback of %# ended (%#)", _path.c_str(),
                        end == ScenarioPlayEnd::Completed        ? "completed"
                        : end == ScenarioPlayEnd::Failed         ? "failed"
                        : end == ScenarioPlayEnd::StoppedByHuman ? "a person took over"
                                                                 : "stopped" );
        if ( onFinished.isBound() )
            onFinished( end ); // 상태를 비운 뒤에 알린다 — 알림이 다음 재생을 시작해도 된다
    }

    void ScenarioPlayer::onFrameBegin( const EngineFrameContext& context )
    {
        if ( _pRunner != nullptr && context._pInput != nullptr )
            _pRunner->onFrameBegin( *context._pInput );
    }

    void ScenarioPlayer::onAfterSceneTick( [[maybe_unused]] const EngineFrameContext& context )
    {
        if ( _pRunner != nullptr )
            _pRunner->forwardPendingScreenshot();
    }

    int32 ScenarioPlayer::onFrameEnd( const EngineFrameContext& context )
    {
        if ( context._pInput == nullptr || _pRunner == nullptr )
            return kEngineFrameRunning;
        if ( _options._bStopOnHumanInput && context._pInput->getHumanInputSerial() != _humanInputSerial )
        {
            endPlayback( ScenarioPlayEnd::StoppedByHuman, context._pInput );
            return kEngineFrameRunning;
        }
        const AutomationOutcome result = _pRunner->hasEnded() ? _pRunner->getResult() : _pRunner->onFrameEnd( *context._pInput );
        if ( result == AutomationOutcome::Running )
            return kEngineFrameRunning;
        if ( result == AutomationOutcome::Passed && _options._bLoop )
        {
            startRunner(); // 어트랙트 루프 — 처음부터 다시
            return kEngineFrameRunning;
        }
        endPlayback( result == AutomationOutcome::Passed ? ScenarioPlayEnd::Completed : ScenarioPlayEnd::Failed, context._pInput );
        return kEngineFrameRunning;
    }

    int32 ScenarioPlayer::onWindowClosed( const EngineFrameContext& context )
    {
        if ( isPlaying() )
            endPlayback( ScenarioPlayEnd::Stopped, context._pInput ); // 재생은 종료 코드를 정하지 않는다
        return kEngineFrameRunning;
    }
} // namespace sw
